// Pattern persistence in the Pico's on-board flash.
//
// The 16-step pattern plus octave and waveform live in one flash sector at a
// fixed offset, so a saved pattern survives power cycling and is loaded at
// boot. Saving erases and reprograms a 4 KiB sector with interrupts disabled,
// which briefly mutes the audio (~50 ms) — that is expected for an explicit
// user-initiated save.
//
// The offset sits 1 MiB into the flash, far beyond any realistic firmware
// image, on both the 2 MiB (Pico / Pico W) and 4 MiB (Pico 2) boards.
#ifndef CINDERWAVE_STORAGE_H
#define CINDERWAVE_STORAGE_H

#include "cinderwave/sequencer.h"

#include <stdint.h>

namespace cws {

// Bump when SavedState changes shape; load() rejects older layouts.
constexpr uint16_t kSaveVersion = 1;

struct SavedState {
    uint32_t magic;                      // kSaveMagic when written
    uint16_t version;
    uint16_t reserved0;
    cw::Step steps[cw::kNumSteps];       // note / gate / accent / slide
    int8_t   octave;                     // -3..3
    uint8_t  wave;                       // cw::Waveform as an integer
    uint8_t  reserved[2];
};
static_assert(sizeof(SavedState) < 256, "save fits in a single flash page");

// Read the saved state from flash. Returns false when nothing valid is
// stored (fresh flash, or a foreign/corrupt/older-version image).
bool load(SavedState& out);

// Erase + program the save sector. Blocks with interrupts disabled for the
// duration of the flash operation — call from the main loop, never from an
// interrupt.
void save(const SavedState& in);

} // namespace cws

#endif // CINDERWAVE_STORAGE_H
