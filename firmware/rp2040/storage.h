// Pattern persistence in the Pico's on-board flash.
//
// The pattern survives a power cycle: the last few sectors of the flash hold
// a ring of snapshots, and the newest valid one is loaded at boot.
//
// The layout is deliberately simple:
//
//   - the storage area is the last four sectors (16 KB) of the flash
//   - it is divided into 256-byte pages, one snapshot per page
//   - pages are filled in order, sixteen per sector
//   - starting a new sector erases it first, which is safe because that
//     sector still holds the oldest snapshots
//
// So a save usually costs one 0.7 ms page program; the 45 ms sector erase
// only happens once every sixteen saves.
//
// Two properties matter for a musical instrument:
//
//   - Atomicity: a new snapshot is always written to a page that is not the
//     one holding the current snapshot, so a power cut during the write
//     leaves the previous pattern intact.
//   - Wear: each sector is erased once per full cycle through the ring, so
//     the flash endurance is multiplied by the number of pages per sector.
//
// Every snapshot carries a CRC32, so a half-written page is detected and
// ignored instead of loading as garbage.
//
// Writing to flash takes the chip out of execute-in-place mode, so the
// interrupt that renders audio cannot run during a write (the handler lives
// in flash). The caller is expected to save at a quiet moment.
//
// The record format itself lives in storage_format.h, which has no SDK
// dependency and is covered by the host test suite.
#ifndef CINDERWAVE_STORAGE_H
#define CINDERWAVE_STORAGE_H

#include "cinderwave/sequencer.h"

#include <stdint.h>

namespace cws {

// Everything that should come back after a power cycle.
struct SavedState {
    cw::Step steps[cw::kNumSteps];   // note / gate / accent / slide
    int8_t   octave = 0;             // -3..3
    uint8_t  wave = 0;               // cw::Waveform as an integer
    uint8_t  reserved[3] = {0, 0, 0};
    float    tempoOffset = 0.0f;     // added to the tempo pot reading
};

// Reads the most recent valid snapshot. Returns false when nothing valid is
// stored yet (fresh flash, or a corrupt/foreign image).
bool load(SavedState& out);

// Writes a snapshot. Returns false if the storage area is unusable, which
// should not happen unless the firmware grew into it.
//
// This blocks with the interrupts off for the duration of the flash
// operation: erasing a sector takes ~45 ms. Call it from the main loop,
// never from an interrupt, and preferably while nothing is sounding.
bool save(const SavedState& in);

} // namespace cws

#endif // CINDERWAVE_STORAGE_H
