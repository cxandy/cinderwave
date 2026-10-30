// Pattern persistence -- implementation.
//
// See storage.h for the design notes and storage_format.h for the on-flash
// record format. This file is the only part that talks to the flash: it maps
// the ring onto real flash pages and takes care of the erase/program dance.

#include "storage.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/platform.h"

#include "storage_format.h"

#include <cstring>

namespace cws {
namespace {

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

// The pattern lives in the last four sectors of the flash.
constexpr uint32_t kStorageSectors = 4;
constexpr uint32_t kStorageBytes   = kStorageSectors * FLASH_SECTOR_SIZE;    // 16 KB
constexpr uint32_t kStorageOffset  = PICO_FLASH_SIZE_BYTES - kStorageBytes;  // 0x1FC000

// One snapshot per flash page.
constexpr uint32_t kPagesPerSector = FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE;    // 16
constexpr uint32_t kSlotCount      = kStorageSectors * kPagesPerSector;      // 64

// ---------------------------------------------------------------------------
// Slot access
// ---------------------------------------------------------------------------

const uint8_t* storageBase() {
    return reinterpret_cast<const uint8_t*>(XIP_BASE + kStorageOffset);
}

const uint8_t* slotAddress(uint32_t slot) {
    return storageBase() + slot * FLASH_PAGE_SIZE;
}

// End of the firmware image, provided by the linker script. If the image ever
// grows into the storage area we must not touch it.
extern "C" uint8_t __flash_binary_end;

bool storageAreaUsable() {
    const uintptr_t binaryEnd =
        reinterpret_cast<uintptr_t>(&__flash_binary_end) - XIP_BASE;
    return binaryEnd <= kStorageOffset;
}

// ---------------------------------------------------------------------------
// Ring state
// ---------------------------------------------------------------------------

// Where the next snapshot goes, and which sequence number it gets.
uint32_t g_nextSlot = 0;
int32_t  g_nextSeq  = 1;

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool load(SavedState& out) {
    const format::ScanResult found =
        format::findLatest(storageBase(), kSlotCount, FLASH_PAGE_SIZE);

    if (!found.found) {
        g_nextSlot = 0;
        g_nextSeq  = 1;
        return false;
    }

    if (!format::decodeRecord(slotAddress(found.slot), out)) {
        // The scan already validated the record, so this cannot happen --
        // but returning false is safer than handing back half a pattern.
        g_nextSlot = 0;
        g_nextSeq  = 1;
        return false;
    }

    g_nextSlot = (found.slot + 1) % kSlotCount;
    g_nextSeq  = found.seq + 1;
    return true;
}

bool save(const SavedState& in) {
    if (!storageAreaUsable()) return false;

    // Pick the slot to write. The first page of a sector is always usable
    // because it is about to be erased; anywhere else the page has to be
    // erased already, which a torn write can prevent.
    const uint32_t slot = format::chooseSlot(storageBase(), kSlotCount,
                                              FLASH_PAGE_SIZE, kPagesPerSector,
                                              g_nextSlot);
    const uint32_t sector = slot / kPagesPerSector;

    // The buffer handed to the flash programmer must live in RAM: the flash
    // cannot be read while it is being programmed.
    alignas(4) uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    format::encodeRecord(in, g_nextSeq, page);

    // The flash leaves execute-in-place mode while it is erased or programmed,
    // and the interrupt handlers live in flash -- so no interrupt may run
    // until the flash is readable again.
    const uint32_t irq = save_and_disable_interrupts();

    if (slot % kPagesPerSector == 0) {
        // Starting a new sector: erase it first. It still holds the oldest
        // snapshots of the ring, never the live one, which is in the sector
        // we just finished.
        flash_range_erase(kStorageOffset + sector * FLASH_SECTOR_SIZE,
                          FLASH_SECTOR_SIZE);
    }
    flash_range_program(kStorageOffset + slot * FLASH_PAGE_SIZE, page,
                        FLASH_PAGE_SIZE);
    flash_flush_cache();

    restore_interrupts(irq);

    g_nextSlot = (slot + 1) % kSlotCount;
    g_nextSeq  = static_cast<int32_t>(static_cast<uint32_t>(g_nextSeq) + 1u);
    return true;
}

} // namespace cws
