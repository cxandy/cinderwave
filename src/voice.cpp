#include "cinderwave/voice.h"

#include <cmath>

namespace cw {
namespace {

Sample glideCoef(Sample seconds, int sampleRate) {
    if (seconds <= 0.0f) {
        return 0.0f;
    }
    return std::exp(-1.0f / (seconds * static_cast<Sample>(sampleRate)));
}

} // namespace

Voice::Voice(int sampleRate)
    : sampleRate_(sampleRate > 0 ? sampleRate : kSampleRate),
      osc_(sampleRate_),
      filter_(sampleRate_),
      amp_(sampleRate_),
      fenv_(sampleRate_) {
    applyPatch();
}

void Voice::setSampleRate(int sampleRate) {
    sampleRate_ = sampleRate > 0 ? sampleRate : kSampleRate;
    osc_.setSampleRate(sampleRate_);
    filter_.setSampleRate(sampleRate_);
    amp_.setSampleRate(sampleRate_);
    fenv_.setSampleRate(sampleRate_);
    applyPatch();
}

void Voice::setPatch(const Patch& p) {
    patch_ = p;
    applyPatch();
}

void Voice::noteOn(int midiNote, bool accent, bool slide) {
    targetHz_ = midiToHz(midiNote);
    accent_ = accent;
    gliding_ = slide && currentHz_ > 0.0f;
    glideCoef_ = glideCoef(patch_.glideTime, sampleRate_);
    if (!gliding_) {
        currentHz_ = targetHz_;
    }
    osc_.setFrequency(currentHz_);
    amp_.gate(true);
    fenv_.gate(true);
}

void Voice::noteOff() {
    amp_.gate(false);
    fenv_.gate(false);
}

Sample Voice::process() {
    if (gliding_) {
        currentHz_ = targetHz_ + (currentHz_ - targetHz_) * glideCoef_;
        if (std::fabs(currentHz_ - targetHz_) <= 0.01f) {
            currentHz_ = targetHz_;
            gliding_ = false;
        }
        osc_.setFrequency(currentHz_);
    }

    const Sample ampLevel = amp_.process();
    const Sample fLevel = fenv_.process();
    const Sample accentMod = accent_ ? patch_.envMod * patch_.accentAmount : 0.0f;
    const Sample cutoff = patch_.cutoff + fLevel * (patch_.envMod + accentMod);
    filter_.setCutoff(cutoff);

    const Sample filtered = filter_.process(osc_.process());
    const Sample accentGain = accent_ ? 1.0f + patch_.accentAmount : 1.0f;
    return clampf(filtered * ampLevel * accentGain, -1.0f, 1.0f);
}

Sample Voice::midiToHz(int note) {
    return 440.0f * std::pow(2.0f, (static_cast<Sample>(note) - 69.0f) / 12.0f);
}

void Voice::applyPatch() {
    osc_.setWaveform(patch_.wave);
    filter_.setMode(FilterMode::LowPass);
    filter_.setCutoff(patch_.cutoff);
    filter_.setResonance(patch_.resonance);

    amp_.setAttack(patch_.ampAttack);
    amp_.setDecay(patch_.ampDecay);
    amp_.setSustain(patch_.ampSustain);
    amp_.setRelease(patch_.ampRelease);

    fenv_.setAttack(patch_.ampAttack);
    fenv_.setDecay(patch_.filterDecay);
    fenv_.setSustain(0.0f);
    fenv_.setRelease(patch_.ampRelease);
}

} // namespace cw
