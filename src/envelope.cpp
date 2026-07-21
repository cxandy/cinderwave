#include "cinderwave/envelope.h"

#include <cmath>

namespace cw {
namespace {

Sample timeCoef(Sample seconds, int sampleRate) {
    if (seconds <= 0.0f) {
        return 0.0f;
    }
    return std::exp(-1.0f / (seconds * static_cast<Sample>(sampleRate)));
}

Sample coefTime(Sample coef, int sampleRate) {
    if (coef <= 0.0f || coef >= 1.0f) {
        return 0.0f;
    }
    return -1.0f / (std::log(coef) * static_cast<Sample>(sampleRate));
}

} // namespace

Envelope::Envelope(int sampleRate)
    : sampleRate_(sampleRate > 0 ? sampleRate : kSampleRate) {
    setAttack(0.01f);
    setDecay(0.1f);
    setRelease(0.1f);
}

void Envelope::setSampleRate(int sampleRate) {
    const int oldRate = sampleRate_;
    const Sample attackSeconds =
        attackInc_ >= 1.0f ? 0.0f : 1.0f / (attackInc_ * static_cast<Sample>(oldRate));
    const Sample decaySeconds = coefTime(decayCoef_, oldRate);
    const Sample releaseSeconds = coefTime(releaseCoef_, oldRate);
    sampleRate_ = sampleRate > 0 ? sampleRate : kSampleRate;
    setAttack(attackSeconds);
    setDecay(decaySeconds);
    setRelease(releaseSeconds);
}

void Envelope::setAttack(Sample seconds) {
    attackInc_ = seconds <= 0.0f ? 1.0f : 1.0f / (seconds * static_cast<Sample>(sampleRate_));
}

void Envelope::setDecay(Sample seconds) {
    decayCoef_ = timeCoef(seconds, sampleRate_);
}

void Envelope::setSustain(Sample level) {
    sustain_ = clampf(level, 0.0f, 1.0f);
}

void Envelope::setRelease(Sample seconds) {
    releaseCoef_ = timeCoef(seconds, sampleRate_);
}

void Envelope::gate(bool on) {
    stage_ = on ? Stage::Attack : Stage::Release;
}

void Envelope::reset() {
    stage_ = Stage::Idle;
    level_ = 0.0f;
}

Sample Envelope::process() {
    switch (stage_) {
    case Stage::Idle:
        level_ = 0.0f;
        break;
    case Stage::Attack:
        level_ += attackInc_;
        if (level_ >= 1.0f) {
            level_ = 1.0f;
            stage_ = Stage::Decay;
        }
        break;
    case Stage::Decay:
        level_ = sustain_ + (level_ - sustain_) * decayCoef_;
        if (std::fabs(level_ - sustain_) <= 0.001f) {
            level_ = sustain_;
            stage_ = Stage::Sustain;
        }
        break;
    case Stage::Sustain:
        level_ = sustain_;
        break;
    case Stage::Release:
        level_ *= releaseCoef_;
        if (level_ <= 0.001f || releaseCoef_ <= 0.0f) {
            level_ = 0.0f;
            stage_ = Stage::Idle;
        }
        break;
    }

    level_ = clampf(level_, 0.0f, 1.0f);
    return level_;
}

} // namespace cw
