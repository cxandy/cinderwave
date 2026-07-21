#include "check.h"

#include "cinderwave/filter.h"

#include <cmath>

CW_TEST(filter_lowpass_attenuates_high_frequency_more_than_highpass) {
    cw::Filter low(cw::kSampleRate);
    cw::Filter high(cw::kSampleRate);
    low.setCutoff(400.0f);
    high.setCutoff(400.0f);
    low.setResonance(0.707f);
    high.setResonance(0.707f);
    low.setMode(cw::FilterMode::LowPass);
    high.setMode(cw::FilterMode::HighPass);

    double lowEnergy = 0.0;
    double highEnergy = 0.0;
    for (int i = 0; i < 4096; ++i) {
        const cw::Sample in = (i & 1) == 0 ? 1.0f : -1.0f;
        const cw::Sample lo = low.process(in);
        const cw::Sample hi = high.process(in);
        CHECK(std::isfinite(lo));
        CHECK(std::isfinite(hi));
        if (i > 512) {
            lowEnergy += static_cast<double>(lo) * lo;
            highEnergy += static_cast<double>(hi) * hi;
        }
    }
    CHECK(lowEnergy < highEnergy * 0.25);
}

CW_TEST(filter_is_finite_across_cutoff_sweep) {
    cw::Filter filter(cw::kSampleRate);
    filter.setResonance(12.0f);
    for (int i = 0; i < 4096; ++i) {
        const cw::Sample cutoff = 20.0f + static_cast<cw::Sample>(i) * 3.5f;
        filter.setCutoff(cutoff);
        const cw::Sample in = std::sin(static_cast<cw::Sample>(i) * 0.07f);
        const cw::Sample out = filter.process(in);
        CHECK(std::isfinite(out));
    }
}
