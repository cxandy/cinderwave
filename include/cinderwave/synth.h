// Top-level engine: wires the Sequencer to the Voice and renders audio blocks.
// The firmware calls renderBlock() from its audio IRQ; the host renderer calls
// it in a loop to fill a WAV. One object, both worlds.
#ifndef CINDERWAVE_SYNTH_H
#define CINDERWAVE_SYNTH_H

#include "cinderwave/config.h"
#include "cinderwave/voice.h"
#include "cinderwave/sequencer.h"

namespace cw {

class Synth {
public:
    explicit Synth(int sampleRate = kSampleRate);

    void setSampleRate(int sampleRate);

    Voice&     voice() { return voice_; }
    Sequencer& sequencer() { return seq_; }

    void setPatch(const Patch& p) { voice_.setPatch(p); }
    void setMasterGain(Sample g) { masterGain_ = clampf(g, 0.0f, 1.0f); }

    // Load a demo acid pattern into the sequencer (handy for firmware boot and
    // the host smoke render).
    void loadDemoPattern();

    // Render `count` samples into `out` (mono). Drives the sequencer clock.
    void renderBlock(Sample* out, int count);

    // Convenience for tests / single-sample hosts.
    //
    // When `rawOut` is non-null it receives the pre-filter signal of the same
    // frame (see Voice::raw()), so a host that drives two outputs can take
    // the filtered voice on one and the raw oscillator on the other without
    // rendering the frame twice.
    Sample renderSample(Sample* rawOut = nullptr);

private:
    int       sampleRate_;
    Voice     voice_;
    Sequencer seq_;
    Sample    masterGain_ = 0.8f;
};

} // namespace cw

#endif // CINDERWAVE_SYNTH_H
