// Andrew Simper's zero-delay-feedback state-variable filter (TPT form).
// Stable across the full cutoff range and cheap enough for the M0+.
// Reference: "Solving the continuous SVF equations using trapezoidal
// integration" (cytomic, 2013).
#ifndef CINDERWAVE_FILTER_H
#define CINDERWAVE_FILTER_H

#include "cinderwave/config.h"

namespace cw {

enum class FilterMode : uint8_t { LowPass, BandPass, HighPass };

class Filter {
public:
    explicit Filter(int sampleRate = kSampleRate);

    void setSampleRate(int sampleRate);
    void setCutoff(Sample hz);           // clamped to (0, sampleRate/2)
    void setResonance(Sample q);         // [0.5, ~20]; self-oscillates high
    void setMode(FilterMode m);
    void reset();                        // clear the two integrator states

    FilterMode mode() const { return mode_; }

    Sample process(Sample in);

private:
    void recompute();                    // refresh g/k coefficients

    int        sampleRate_;
    FilterMode mode_ = FilterMode::LowPass;
    Sample cutoff_ = 1000.0f;
    Sample reso_ = 0.707f;
    // Coefficients.
    Sample g_ = 0.0f;   // tan(pi * fc / fs)
    Sample k_ = 0.0f;   // 1 / Q
    Sample a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
    // Integrator state.
    Sample ic1eq_ = 0.0f, ic2eq_ = 0.0f;
};

} // namespace cw

#endif // CINDERWAVE_FILTER_H
