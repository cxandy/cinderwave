// Linear-attack, exponential decay/release ADSR. Gate-driven so the sequencer
// can retrigger it per step (with accent scaling the peak).
#ifndef CINDERWAVE_ENVELOPE_H
#define CINDERWAVE_ENVELOPE_H

#include "cinderwave/config.h"

namespace cw {

class Envelope {
public:
    enum class Stage : uint8_t { Idle, Attack, Decay, Sustain, Release };

    explicit Envelope(int sampleRate = kSampleRate);

    void setSampleRate(int sampleRate);
    void setAttack(Sample seconds);
    void setDecay(Sample seconds);
    void setSustain(Sample level);   // [0, 1]
    void setRelease(Sample seconds);

    // Rising edge starts Attack; falling edge starts Release.
    void gate(bool on);
    void reset();                    // silence, Idle

    Stage stage() const { return stage_; }
    bool active() const { return stage_ != Stage::Idle; }

    // Advance one sample; returns current level in [0, 1].
    Sample process();

private:
    int    sampleRate_;
    Stage  stage_ = Stage::Idle;
    Sample level_ = 0.0f;
    Sample sustain_ = 0.7f;
    // Per-stage increments/coefficients derived from the time setters.
    Sample attackInc_ = 1.0f;    // linear per-sample rise
    Sample decayCoef_ = 0.0f;    // exponential coefficient toward sustain
    Sample releaseCoef_ = 0.0f;  // exponential coefficient toward 0
};

} // namespace cw

#endif // CINDERWAVE_ENVELOPE_H
