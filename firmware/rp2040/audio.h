// Single PWM audio output.
//
//   OUT-A : GP15 -> PWM slice 7 channel B -> 100 R + 10 nF RC -> 3.5 mm jack
//
// OUT-B (GP2) is reserved for now: the firmware does not configure the pin and
// does not drive it. The board has a second jack and a potentiometer that
// cross-mixes the two outputs, so when OUT-B comes back it will be a second
// PWM slice (GP2 is PWM slice 1, channel A) fed from the same wrap interrupt.
//
// The RP2040 has exactly ONE PWM interrupt (NUM_PWM_IRQS == 1), so a second
// output has to be written from the same handler rather than from its own.
#ifndef CINDERWAVE_AUDIO_H
#define CINDERWAVE_AUDIO_H

#include <stdint.h>

#include "cinderwave/config.h"

namespace cwa {

// Called from the PWM wrap interrupt to produce one sample. Keep it fast and
// non-blocking.
using RenderFn = cw::Sample (*)();

class AudioOut {
public:
    // Starts the PWM slice and installs the wrap interrupt.
    void init(RenderFn render);

    // The rate the PWM slice actually runs at, in Hz (32002.6 with the
    // default wrap).
    uint32_t actualRate() const { return actualRate_; }

    // Nominal engine rate the PWM is matched to.
    static constexpr uint32_t kNominalRate = cw::kSampleRate;

private:
    uint32_t actualRate_ = 0;
};

} // namespace cwa

#endif // CINDERWAVE_AUDIO_H
