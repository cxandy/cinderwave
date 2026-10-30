// Dual PWM audio output.
//
//   OUT-A : GP15 -> PWM slice 7 channel B -> 100 R + 10 nF RC -> 3.5 mm jack
//   OUT-B : GP2  -> PWM slice 1 channel A -> 470 R -> 3.5 mm jack
//
// The RP2040 has exactly ONE PWM interrupt (NUM_PWM_IRQS == 1), and the two
// outputs sit on different slices, so both duty values are written from a
// single wrap interrupt on the OUT-A slice. Slice 1 free-runs with an
// identical clkdiv and wrap, so it stays phase-locked to slice 7.
//
// A physical potentiometer cross-mixes OUT-A and OUT-B outside the
// microcontroller; the firmware just has to feed each output its own signal.
#ifndef CINDERWAVE_AUDIO_H
#define CINDERWAVE_AUDIO_H

#include <stdint.h>

#include "cinderwave/config.h"

namespace cwa {

// One stereo-ish frame: the value destined for each physical output.
struct AudioFrame {
    cw::Sample a = 0.0f;  // OUT-A (GP15)
    cw::Sample b = 0.0f;  // OUT-B (GP2)
};

// Called from the PWM wrap interrupt. Keep it fast and non-blocking.
using RenderFn = void (*)(AudioFrame&);

class AudioOut {
public:
    // Starts both PWM slices and installs the wrap interrupt.
    void init(RenderFn render);

    // The rate the PWM slices actually run at, in Hz (32002.6 with the
    // default wrap).
    uint32_t actualRate() const { return actualRate_; }

    // Nominal engine rate the PWM is matched to.
    static constexpr uint32_t kNominalRate = cw::kSampleRate;

private:
    uint32_t actualRate_ = 0;
};

} // namespace cwa

#endif // CINDERWAVE_AUDIO_H
