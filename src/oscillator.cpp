#include "cinderwave/oscillator.h"

#include <cmath>

namespace cw {
namespace {

constexpr Sample kPi = 3.14159265358979323846f;

Sample polyBlep(Sample t, Sample dt) {
    if (dt <= 0.0f) {
        return 0.0f;
    }
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

Sample wrap01(Sample x) {
    while (x >= 1.0f) {
        x -= 1.0f;
    }
    while (x < 0.0f) {
        x += 1.0f;
    }
    return x;
}

} // namespace

Oscillator::Oscillator(int sampleRate)
    : sampleRate_(sampleRate > 0 ? sampleRate : kSampleRate) {
    setFrequency(freq_);
}

void Oscillator::setSampleRate(int sampleRate) {
    sampleRate_ = sampleRate > 0 ? sampleRate : kSampleRate;
    setFrequency(freq_);
}

void Oscillator::setFrequency(Sample hz) {
    freq_ = clampf(hz, 0.0f, static_cast<Sample>(sampleRate_) * 0.5f);
    phaseInc_ = freq_ / static_cast<Sample>(sampleRate_);
}

void Oscillator::setWaveform(Waveform w) {
    wave_ = w;
}

void Oscillator::setPulseWidth(Sample pw) {
    pulseWidth_ = clampf(pw, 0.05f, 0.95f);
}

void Oscillator::reset() {
    phase_ = 0.0f;
}

Sample Oscillator::process() {
    const Sample t = phase_;
    const Sample dt = phaseInc_;
    Sample out = 0.0f;

    switch (wave_) {
    case Waveform::Saw:
        out = 2.0f * t - 1.0f;
        out -= polyBlep(t, dt);
        break;
    case Waveform::Square: {
        out = t < pulseWidth_ ? 1.0f : -1.0f;
        out += polyBlep(t, dt);
        out -= polyBlep(wrap01(t - pulseWidth_), dt);
        break;
    }
    case Waveform::Triangle:
        out = 4.0f * std::fabs(t - 0.5f) - 1.0f;
        break;
    case Waveform::Sine:
        out = std::sin(2.0f * kPi * t);
        break;
    }

    phase_ += phaseInc_;
    phase_ = wrap01(phase_);
    return clampf(out, -1.1f, 1.1f);
}

} // namespace cw
