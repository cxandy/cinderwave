#include "storage.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include <string.h>

namespace cws {
namespace {

// "CW01" as a little-endian uint32.
constexpr uint32_t kSaveMagic = 0x31305743u;

// 1 MiB into the flash chip: unreachable by any realistic firmware image.
constexpr uint32_t kFlashOffset = 0x100000u;

// Flash is memory-mapped through XIP, so reading is a plain pointer read.
const SavedState* flashImage() {
    return reinterpret_cast<const SavedState*>(XIP_BASE + kFlashOffset);
}

} // namespace

bool load(SavedState& out) {
    const SavedState* p = flashImage();
    if (p->magic != kSaveMagic || p->version != kSaveVersion) return false;

    // Sanity-check the payload so a half-programmed sector cannot inject
    // nonsense into the sequencer.
    for (int i = 0; i < cw::kNumSteps; ++i) {
        if (p->steps[i].note > 127) return false;
    }
    if (p->wave > 3) return false;
    if (p->octave < -3 || p->octave > 3) return false;

    out = *p;
    return true;
}

void save(const SavedState& in) {
    SavedState s = in;
    s.magic = kSaveMagic;
    s.version = kSaveVersion;
    s.reserved0 = 0;
    s.reserved[0] = 0;
    s.reserved[1] = 0;

    // Skip the (audible, ~50 ms) erase/program cycle when the image on flash
    // is already byte-identical.
    if (memcmp(flashImage(), &s, sizeof(SavedState)) == 0) return;

    // Flash programming stalls the XIP bus, so no interrupt may fire while
    // it runs — the audio IRQ would execute from uncacheable flash. This is
    // the SDK's standard pattern (see the pico-examples flash_program demo).
    const uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(kFlashOffset, FLASH_SECTOR_SIZE);
    flash_range_program(kFlashOffset,
                        reinterpret_cast<const uint8_t*>(&s), sizeof(s));
    restore_interrupts(ints);
}

} // namespace cws
