// Platform-independent half of the pattern storage: the on-flash record
// format, its checksum, and the ring bookkeeping.
//
// Everything that talks to the flash itself lives in storage.cpp; this header
// has no SDK dependency, so the host test suite can exercise the tricky parts
// (record encoding, corruption detection, ring wrap-around, torn writes)
// against a plain byte buffer.
//
// On-flash layout, one record per 256-byte page:
//
//    0..3   magic "CWR1"
//    4      format version
//    5      payload length
//    6..7   reserved
//    8..11  sequence number (wraps around)
//   12..15  CRC32 of the payload
//   16..    payload
//
// Payload:
//
//    0..15  note per step
//   16..31  flags per step (gate / accent / slide)
//   32..35  tempo offset (float)
//   36      octave (signed byte)
//   37      waveform
//   38..39  reserved
#ifndef CINDERWAVE_STORAGE_FORMAT_H
#define CINDERWAVE_STORAGE_FORMAT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "storage.h"

namespace cws {
namespace format {

constexpr uint32_t kMagic       = 0x31525743u;  // 'C','W','R','1'
constexpr uint8_t  kVersion     = 1;
constexpr uint32_t kHeaderSize  = 16;
constexpr uint32_t kPayloadSize = 40;
constexpr uint32_t kRecordSize  = kHeaderSize + kPayloadSize;  // 56

constexpr uint32_t kFlagGate   = 0x01u;
constexpr uint32_t kFlagAccent = 0x02u;
constexpr uint32_t kFlagSlide  = 0x04u;

// ---------------------------------------------------------------------------
// Little-endian helpers (the flash layout is fixed, the CPU is not)
// ---------------------------------------------------------------------------

inline void putU32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

inline uint32_t getU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline void putI8(uint8_t* p, int8_t v) {
    *p = static_cast<uint8_t>(static_cast<int>(v) & 0xFF);
}

inline int8_t getI8(const uint8_t* p) {
    const uint8_t v = *p;
    return (v < 0x80u) ? static_cast<int8_t>(v)
                       : static_cast<int8_t>(static_cast<int>(v) - 256);
}

inline void putFloat(uint8_t* p, float f) {
    uint32_t bits = 0;
    memcpy(&bits, &f, sizeof(bits));
    putU32(p, bits);
}

inline float getFloat(const uint8_t* p) {
    const uint32_t bits = getU32(p);
    float f = 0.0f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

// CRC-32 (IEEE 802.3, the usual reflected polynomial).
inline uint32_t crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

// ---------------------------------------------------------------------------
// Records
// ---------------------------------------------------------------------------

// Serialises a snapshot. `out` must have room for kRecordSize bytes.
inline void encodeRecord(const SavedState& state, int32_t seq, uint8_t* out) {
    uint8_t* payload = out + kHeaderSize;

    for (int i = 0; i < cw::kNumSteps; ++i) {
        payload[i] = state.steps[i].note;
        uint8_t flags = 0;
        if (state.steps[i].gate)   flags |= kFlagGate;
        if (state.steps[i].accent) flags |= kFlagAccent;
        if (state.steps[i].slide)  flags |= kFlagSlide;
        payload[cw::kNumSteps + i] = flags;
    }
    putFloat(payload + 32, state.tempoOffset);
    putI8(payload + 36, state.octave);
    payload[37] = state.wave;
    payload[38] = 0;
    payload[39] = 0;

    putU32(out, kMagic);
    out[4] = kVersion;
    out[5] = static_cast<uint8_t>(kPayloadSize);
    out[6] = 0;
    out[7] = 0;
    putU32(out + 8, static_cast<uint32_t>(seq));
    putU32(out + 12, crc32(payload, kPayloadSize));
}

// Reads a record back. Returns false when the header or the checksum does not
// match, which is what a torn write looks like.
inline bool decodeRecord(const uint8_t* in, SavedState& out) {
    if (getU32(in) != kMagic) return false;
    if (in[4] != kVersion) return false;
    if (in[5] != kPayloadSize) return false;
    if (crc32(in + kHeaderSize, kPayloadSize) != getU32(in + 12)) return false;

    const uint8_t* p = in + kHeaderSize;
    for (int i = 0; i < cw::kNumSteps; ++i) {
        out.steps[i].note   = p[i];
        const uint8_t flags = p[cw::kNumSteps + i];
        out.steps[i].gate   = (flags & kFlagGate) != 0;
        out.steps[i].accent = (flags & kFlagAccent) != 0;
        out.steps[i].slide  = (flags & kFlagSlide) != 0;
    }
    out.tempoOffset = getFloat(p + 32);
    out.octave      = getI8(p + 36);
    out.wave        = p[37];
    return true;
}

// ---------------------------------------------------------------------------
// Ring bookkeeping
// ---------------------------------------------------------------------------

struct ScanResult {
    bool     found = false;
    uint32_t slot = 0;
    int32_t  seq  = 0;
};

// Finds the newest valid record in a ring of `slotCount` pages that are
// `pageSize` bytes apart.
//
// Sequence numbers are compared as signed differences, so the counter can
// wrap around without the comparison breaking.
inline ScanResult findLatest(const uint8_t* base, uint32_t slotCount,
                              uint32_t pageSize) {
    ScanResult result;
    for (uint32_t slot = 0; slot < slotCount; ++slot) {
        const uint8_t* rec = base + slot * pageSize;
        if (getU32(rec) != kMagic) continue;
        if (rec[4] != kVersion) continue;
        if (rec[5] != kPayloadSize) continue;
        if (crc32(rec + kHeaderSize, kPayloadSize) != getU32(rec + 12)) continue;

        const int32_t seq = static_cast<int32_t>(getU32(rec + 8));
        // The difference is computed in unsigned arithmetic: subtracting two
        // sequence numbers as signed values could overflow when the counter
        // wraps around.
        const int32_t delta = static_cast<int32_t>(
            static_cast<uint32_t>(seq) - static_cast<uint32_t>(result.seq));
        if (!result.found || delta > 0) {
            result.found = true;
            result.slot  = slot;
            result.seq   = seq;
        }
    }
    return result;
}

// A page that has never been programmed reads back as all ones.
inline bool pageIsErased(const uint8_t* base, uint32_t slot, uint32_t pageSize) {
    const uint8_t* p = base + slot * pageSize;
    for (uint32_t i = 0; i < pageSize; ++i) {
        if (p[i] != 0xFFu) return false;
    }
    return true;
}

// Picks the slot to write next.
//
//   - The first page of a sector is always usable: it is about to be erased.
//   - Anywhere else the page has to be erased already, which is only
//     guaranteed if the previous pass through that sector completed. A page
//     that was torn by a power cut is skipped; it becomes usable again when
//     its sector is erased.
inline uint32_t chooseSlot(const uint8_t* base, uint32_t slotCount,
                            uint32_t pageSize, uint32_t pagesPerSector,
                            uint32_t startSlot) {
    uint32_t slot = startSlot;
    for (uint32_t i = 0; i < slotCount; ++i) {
        if (slot % pagesPerSector == 0) break;
        if (pageIsErased(base, slot, pageSize)) break;
        slot = (slot + 1) % slotCount;
    }
    return slot;
}

} // namespace format
} // namespace cws

#endif // CINDERWAVE_STORAGE_FORMAT_H
