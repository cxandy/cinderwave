// Tests for the pattern storage: the on-flash record format, its checksum,
// and the ring bookkeeping that has to survive a power cut in the middle of
// a write.
//
// The flash itself is not touched here. A plain byte buffer stands in for the
// storage area, and the firmware's own code paths are driven against it.
#include "check.h"
#include "storage_format.h"

#include <cstring>
#include <vector>

using cws::SavedState;
using cws::format::kRecordSize;

namespace {

constexpr uint32_t kSlotCount      = 64;
constexpr uint32_t kPageSize       = 256;
constexpr uint32_t kPagesPerSector = 16;

// Stand-in for the storage area: 64 pages of 256 bytes, all erased.
struct FakeFlash {
    std::vector<uint8_t> bytes;

    FakeFlash() : bytes(kSlotCount * kPageSize, 0xFFu) {}

    uint8_t* page(uint32_t slot) { return bytes.data() + slot * kPageSize; }
    const uint8_t* page(uint32_t slot) const { return bytes.data() + slot * kPageSize; }

    void eraseSector(uint32_t sector) {
        const uint32_t first = sector * kPagesPerSector;
        for (uint32_t i = 0; i < kPagesPerSector * kPageSize; ++i) {
            bytes[first * kPageSize + i] = 0xFFu;
        }
    }
};

SavedState makeState(int seed) {
    SavedState s;
    for (int i = 0; i < cw::kNumSteps; ++i) {
        s.steps[i].note   = static_cast<uint8_t>((seed + i * 3) % 128);
        s.steps[i].gate   = ((seed + i) % 3) != 0;
        s.steps[i].accent = ((seed + i) % 5) == 0;
        s.steps[i].slide  = ((seed + i) % 7) == 0;
    }
    s.octave      = static_cast<int8_t>((seed % 5) - 2);
    s.wave        = static_cast<uint8_t>(seed % 4);
    s.tempoOffset = static_cast<float>(seed) * 0.25f;
    return s;
}

bool sameState(const SavedState& a, const SavedState& b) {
    if (a.octave != b.octave) return false;
    if (a.wave != b.wave) return false;
    if (a.tempoOffset != b.tempoOffset) return false;
    for (int i = 0; i < cw::kNumSteps; ++i) {
        if (a.steps[i].note != b.steps[i].note) return false;
        if (a.steps[i].gate != b.steps[i].gate) return false;
        if (a.steps[i].accent != b.steps[i].accent) return false;
        if (a.steps[i].slide != b.steps[i].slide) return false;
    }
    return true;
}

} // namespace

CW_TEST(storage_record_round_trip) {
    const SavedState original = makeState(42);

    uint8_t record[kRecordSize];
    cws::format::encodeRecord(original, 7, record);

    SavedState decoded;
    CHECK(cws::format::decodeRecord(record, decoded));
    CHECK(sameState(original, decoded));
    CHECK(decoded.octave == original.octave);
}

CW_TEST(storage_record_detects_corruption) {
    const SavedState original = makeState(11);
    uint8_t record[kRecordSize];
    cws::format::encodeRecord(original, 3, record);

    SavedState decoded;

    // A flipped bit inside the payload must fail the checksum.
    uint8_t flipped[kRecordSize];
    memcpy(flipped, record, kRecordSize);
    flipped[cws::format::kHeaderSize + 2] ^= 0x40u;
    CHECK(!cws::format::decodeRecord(flipped, decoded));

    // So must a wrong magic, a wrong version and a wrong length.
    memcpy(flipped, record, kRecordSize);
    flipped[0] ^= 0xFFu;
    CHECK(!cws::format::decodeRecord(flipped, decoded));

    memcpy(flipped, record, kRecordSize);
    flipped[4] = 99;
    CHECK(!cws::format::decodeRecord(flipped, decoded));

    memcpy(flipped, record, kRecordSize);
    flipped[5] = 7;
    CHECK(!cws::format::decodeRecord(flipped, decoded));
}

CW_TEST(storage_finds_newest_by_sequence) {
    FakeFlash flash;
    uint8_t record[kRecordSize];

    cws::format::encodeRecord(makeState(1), 1, record);
    memcpy(flash.page(0), record, kRecordSize);

    cws::format::encodeRecord(makeState(2), 2, record);
    memcpy(flash.page(5), record, kRecordSize);

    cws::format::encodeRecord(makeState(3), 3, record);
    memcpy(flash.page(9), record, kRecordSize);

    cws::format::ScanResult found =
        cws::format::findLatest(flash.bytes.data(), kSlotCount, kPageSize);
    CHECK(found.found);
    CHECK(found.slot == 9);
    CHECK(found.seq == 3);

    // The sequence number wins over the position in the ring.
    cws::format::encodeRecord(makeState(4), 10, record);
    memcpy(flash.page(2), record, kRecordSize);

    found = cws::format::findLatest(flash.bytes.data(), kSlotCount, kPageSize);
    CHECK(found.found);
    CHECK(found.slot == 2);
    CHECK(found.seq == 10);
}

CW_TEST(storage_sequence_wraps_around) {
    FakeFlash flash;
    uint8_t record[kRecordSize];

    const int32_t kMax = 2147483647;
    cws::format::encodeRecord(makeState(1), kMax - 1, record);
    memcpy(flash.page(0), record, kRecordSize);

    cws::format::encodeRecord(makeState(2), kMax, record);
    memcpy(flash.page(1), record, kRecordSize);

    // The counter wraps around to the most negative value.
    cws::format::encodeRecord(makeState(3), static_cast<int32_t>(
                                                 static_cast<uint32_t>(kMax) + 1u),
                                record);
    memcpy(flash.page(2), record, kRecordSize);

    const cws::format::ScanResult found =
        cws::format::findLatest(flash.bytes.data(), kSlotCount, kPageSize);
    CHECK(found.found);
    CHECK(found.slot == 2);
}

CW_TEST(storage_skips_a_torn_page) {
    FakeFlash flash;

    // Slot 3 was torn by a power cut: it holds half a record, so it cannot be
    // programmed again.
    flash.page(3)[0] = 0x00u;

    const uint32_t slot = cws::format::chooseSlot(flash.bytes.data(), kSlotCount,
                                                   kPageSize, kPagesPerSector, 3);
    CHECK(slot == 4);
}

CW_TEST(storage_erases_the_sector_before_reusing_it) {
    FakeFlash flash;

    // The first page of a sector is usable even when it is dirty, because it
    // is about to be erased.
    for (uint32_t i = 0; i < kPageSize; ++i) {
        flash.page(16)[i] = 0x00u;
    }

    const uint32_t slot = cws::format::chooseSlot(flash.bytes.data(), kSlotCount,
                                                   kPageSize, kPagesPerSector, 16);
    CHECK(slot == 16);
}

CW_TEST(storage_ring_survives_a_power_cut) {
    // Save many patterns in a row, tearing every seventh write the way a
    // power cut would. After every write the ring is scanned again, the way
    // the firmware does it at boot, and the pattern that comes back must be
    // the newest one that was written completely.
    FakeFlash flash;
    uint32_t nextSlot = 0;
    int32_t  nextSeq  = 1;
    SavedState lastGood = makeState(0);
    bool haveGood = false;

    for (int round = 0; round < 400; ++round) {
        const SavedState state = makeState(round + 1);

        const uint32_t slot = cws::format::chooseSlot(
            flash.bytes.data(), kSlotCount, kPageSize, kPagesPerSector, nextSlot);
        if (slot % kPagesPerSector == 0) {
            flash.eraseSector(slot / kPagesPerSector);
        }

        uint8_t record[kRecordSize];
        cws::format::encodeRecord(state, nextSeq, record);
        memcpy(flash.page(slot), record, kRecordSize);

        const bool torn = (round % 7) == 3;
        if (torn) {
            // The write died half way through the page.
            memset(flash.page(slot) + kRecordSize / 2, 0x5Au,
                   kPageSize - kRecordSize / 2);
        } else {
            lastGood = state;
            haveGood = true;
        }

        nextSlot = (slot + 1) % kSlotCount;
        nextSeq = static_cast<int32_t>(static_cast<uint32_t>(nextSeq) + 1u);

        // Reboot: find the newest intact record.
        const cws::format::ScanResult found =
            cws::format::findLatest(flash.bytes.data(), kSlotCount, kPageSize);
        if (!haveGood) {
            CHECK(!found.found);
            continue;
        }
        CHECK(found.found);

        SavedState decoded;
        CHECK(cws::format::decodeRecord(flash.page(found.slot), decoded));
        CHECK(sameState(lastGood, decoded));
    }
}
