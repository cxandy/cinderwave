#include "cinderwave/synth.h"

namespace cw {

Synth::Synth(int sampleRate)
    : sampleRate_(sampleRate > 0 ? sampleRate : kSampleRate),
      voice_(sampleRate_),
      seq_(sampleRate_) {
}

void Synth::setSampleRate(int sampleRate) {
    sampleRate_ = sampleRate > 0 ? sampleRate : kSampleRate;
    voice_.setSampleRate(sampleRate_);
    seq_.setSampleRate(sampleRate_);
}

void Synth::loadDemoPattern() {
    Patch p{};
    p.wave = Waveform::Saw;
    p.cutoff = 520.0f;
    p.resonance = 9.0f;
    p.envMod = 3600.0f;
    p.ampAttack = 0.002f;
    p.ampDecay = 0.16f;
    p.ampSustain = 0.0f;
    p.ampRelease = 0.045f;
    p.filterDecay = 0.18f;
    p.accentAmount = 0.55f;
    p.glideTime = 0.055f;
    setPatch(p);
    setMasterGain(0.75f);

    seq_.clear();
    seq_.setTempo(130.0f);
    const Step pattern[kNumSteps] = {
        {33, true,  true,  false},
        {36, true,  false, true },
        {40, true,  false, false},
        {36, false, false, false},
        {45, true,  true,  true },
        {43, true,  false, false},
        {36, true,  false, false},
        {33, false, false, false},
        {36, true,  true,  false},
        {48, true,  false, false},
        {43, true,  false, true },
        {40, true,  true,  false},
        {36, false, false, false},
        {33, true,  false, false},
        {36, true,  true,  true },
        {45, true,  false, false},
    };
    for (int i = 0; i < kNumSteps; ++i) {
        seq_.setStep(i, pattern[i]);
    }
}

void Synth::renderBlock(Sample* out, int count) {
    if (!out || count <= 0) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        out[i] = renderSample();
    }
}

Sample Synth::renderSample(Sample* rawOut) {
    const StepEvent ev = seq_.tick();
    if (ev.release && !ev.slide) {
        voice_.noteOff();
    }
    if (ev.trigger) {
        voice_.noteOn(ev.note, ev.accent, ev.slide);
    }
    const Sample s = voice_.process();
    if (rawOut) {
        *rawOut = voice_.raw();
    }
    return s * masterGain_;
}

} // namespace cw
