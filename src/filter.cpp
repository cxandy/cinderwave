#include "cinderwave/filter.h"

#include <cmath>

namespace cw {
namespace {

constexpr Sample kPi = 3.14159265358979323846f;

} // namespace

Filter::Filter(int sampleRate)
    : sampleRate_(sampleRate > 0 ? sampleRate : kSampleRate) {
    recompute();
}

void Filter::setSampleRate(int sampleRate) {
    sampleRate_ = sampleRate > 0 ? sampleRate : kSampleRate;
    setCutoff(cutoff_);
}

void Filter::setCutoff(Sample hz) {
    const Sample hi = static_cast<Sample>(sampleRate_) * 0.49f;
    cutoff_ = clampf(hz, 10.0f, hi);
    recompute();
}

void Filter::setResonance(Sample q) {
    reso_ = clampf(q, 0.5f, 20.0f);
    recompute();
}

void Filter::setMode(FilterMode m) {
    mode_ = m;
}

void Filter::reset() {
    ic1eq_ = 0.0f;
    ic2eq_ = 0.0f;
}

Sample Filter::process(Sample in) {
    const Sample v3 = in - ic2eq_;
    const Sample v1 = a1_ * ic1eq_ + a2_ * v3;
    const Sample v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3;
    ic1eq_ = 2.0f * v1 - ic1eq_;
    ic2eq_ = 2.0f * v2 - ic2eq_;

    switch (mode_) {
    case FilterMode::LowPass:
        return v2;
    case FilterMode::BandPass:
        return v1;
    case FilterMode::HighPass:
        return in - k_ * v1 - v2;
    }
    return v2;
}

void Filter::recompute() {
    g_ = std::tan(kPi * cutoff_ / static_cast<Sample>(sampleRate_));
    k_ = 1.0f / reso_;
    a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
    a2_ = g_ * a1_;
    a3_ = g_ * a2_;
}

} // namespace cw
