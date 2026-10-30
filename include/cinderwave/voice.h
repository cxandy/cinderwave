// A single synth voice: oscillator -> filter -> VCA(amp env), with a second
// envelope modulating the filter cutoff. This is the classic acid-bass signal
// path that gives Cinderwave its bite.
#ifndef CINDERWAVE_VOICE_H
#define CINDERWAVE_VOICE_H

#include "cinderwave/config.h"
#include "cinderwave/oscillator.h"
#include "cinderwave/envelope.h"
#include "cinderwave/filter.h"

namespace cw {

// Patch parameters a user (or the firmware's pots) can tweak live.
struct Patch {
    Waveform   wave = Waveform::Saw;
    Sample     cutoff = 800.0f;     // base filter cutoff (Hz)
    Sample     resonance = 6.0f;    // filter Q
    Sample     envMod = 3000.0f;    // how many Hz the filter env opens the cutoff
    Sample     ampAttack = 0.002f;
    Sample     ampDecay = 0.18f;
    Sample     ampSustain = 0.0f;   // 0 = plucky, classic for basslines
    Sample     ampRelease = 0.06f;
    Sample     filterDecay = 0.20f;
    Sample     accentAmount = 0.6f; // extra gain + env-mod on accented steps
    Sample     glideTime = 0.04f;   // seconds to slide between notes
};

class Voice {
public:
    explicit Voice(int sampleRate = kSampleRate);

    void setSampleRate(int sampleRate);
    void setPatch(const Patch& p);
    const Patch& patch() const { return patch_; }

    // midiNote: 0..127. accent/slide come from the sequencer step.
    void noteOn(int midiNote, bool accent, bool slide);
    void noteOff();

    bool active() const { return amp_.active(); }

    // One sample of audio in [-1, 1] (before master gain).
    Sample process();

    // The pre-filter signal of the most recent process() call: oscillator ->
    // amp envelope -> accent gain, with the resonant filter bypassed.
    //
    // The firmware routes this to the second (OUT-B) jack so that a hardware
    // potentiometer can blend the filtered voice with the raw oscillator.
    Sample raw() const { return raw_; }

    static Sample midiToHz(int note);

private:
    void applyPatch();

    int        sampleRate_;
    Patch      patch_;
    Oscillator osc_;
    Filter     filter_;
    Envelope   amp_;      // amplitude
    Envelope   fenv_;     // filter cutoff modulation

    Sample targetHz_ = 0.0f;   // note we are gliding toward
    Sample currentHz_ = 0.0f;  // portamento state
    Sample glideCoef_ = 0.0f;
    bool   gliding_ = false;
    bool   accent_ = false;
    Sample raw_ = 0.0f;        // last process() call, filter bypassed
};

} // namespace cw

#endif // CINDERWAVE_VOICE_H
