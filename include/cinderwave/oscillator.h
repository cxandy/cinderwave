// Band-limited oscillator using PolyBLEP to tame aliasing on the hard-edged
// saw and square waves — the difference you actually hear on a cheap DAC.
#ifndef CINDERWAVE_OSCILLATOR_H
#define CINDERWAVE_OSCILLATOR_H

#include "cinderwave/config.h"

namespace cw {

enum class Waveform : uint8_t { Saw, Square, Triangle, Sine };

class Oscillator {
public:
    explicit Oscillator(int sampleRate = kSampleRate);

    void setSampleRate(int sampleRate);
    void setFrequency(Sample hz);        // clamped to [0, sampleRate/2]
    void setWaveform(Waveform w);
    void setPulseWidth(Sample pw);       // Square duty in [0.05, 0.95], default 0.5
    void reset();                        // phase back to 0

    Waveform waveform() const { return wave_; }
    Sample frequency() const { return freq_; }

    // Advance one sample. Output is nominally in [-1, 1].
    Sample process();

private:
    int   sampleRate_;
    Sample freq_ = 0.0f;
    Sample phase_ = 0.0f;      // normalized [0, 1)
    Sample phaseInc_ = 0.0f;   // freq / sampleRate
    Sample pulseWidth_ = 0.5f;
    Waveform wave_ = Waveform::Saw;
};

} // namespace cw

#endif // CINDERWAVE_OSCILLATOR_H
