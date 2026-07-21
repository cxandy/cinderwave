#include "check.h"

#include "cinderwave/oscillator.h"

#include <cmath>

CW_TEST(oscillator_saw_is_bounded_and_centered) {
    cw::Oscillator osc(cw::kSampleRate);
    osc.setWaveform(cw::Waveform::Saw);
    osc.setFrequency(1000.0f);

    constexpr int n = cw::kSampleRate;
    double sum = 0.0;
    bool sawNegative = false;
    bool sawPositive = false;
    for (int i = 0; i < n; ++i) {
        const cw::Sample s = osc.process();
        CHECK(s >= -1.1f);
        CHECK(s <= 1.1f);
        sawNegative = sawNegative || s < 0.0f;
        sawPositive = sawPositive || s > 0.0f;
        sum += s;
    }

    CHECK_NEAR(sum / static_cast<double>(n), 0.0, 0.02);
    CHECK(sawNegative);
    CHECK(sawPositive);
}

CW_TEST(oscillator_frequency_clamps_at_nyquist) {
    cw::Oscillator osc(cw::kSampleRate);
    osc.setFrequency(static_cast<cw::Sample>(cw::kSampleRate));
    CHECK_NEAR(osc.frequency(), static_cast<cw::Sample>(cw::kSampleRate) * 0.5f, 0.001f);
}
