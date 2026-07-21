// Cinderwave — open hardware groovebox for the RP2040.
// Platform-independent DSP core. No dynamic allocation, no exceptions,
// no hardware headers here: everything below runs identically on a Pico
// and on a desktop host that renders to WAV.
#ifndef CINDERWAVE_CONFIG_H
#define CINDERWAVE_CONFIG_H

#include <cstdint>

namespace cw {

// Audio sample type. float on host and RP2040 (the M0+ has no FPU, but the
// pico SDK's software float is fast enough for one voice at 32 kHz; the
// firmware converts to 12-bit PWM / 16-bit I2S at the sink).
using Sample = float;

// Default engine sample rate. Chosen so a single PWM cycle on the RP2040
// (125 MHz / 32000 ≈ 3906 counts) still gives ~11.9 bits of amplitude range.
constexpr int kSampleRate = 32000;

// Sequencer geometry.
constexpr int kNumSteps = 16;   // one bar of 16th notes
constexpr int kNumTracks = 1;   // monosynth groovebox (one voice, one lane)

// Clamp helper shared across the DSP core.
inline Sample clampf(Sample x, Sample lo, Sample hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

} // namespace cw

#endif // CINDERWAVE_CONFIG_H
