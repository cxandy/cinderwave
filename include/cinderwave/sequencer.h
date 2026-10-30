// 16-step sequencer in the TB-303 idiom: each step carries a note plus gate,
// accent and slide flags. Clocked in samples so it stays sample-accurate and
// needs no wall clock — the same tick() drives host renders and firmware.
#ifndef CINDERWAVE_SEQUENCER_H
#define CINDERWAVE_SEQUENCER_H

#include "cinderwave/config.h"

namespace cw {

struct Step {
    uint8_t note = 36;    // MIDI note
    bool    gate = false; // does this step sound?
    bool    accent = false;
    bool    slide = false; // glide into the next step
};

// Fired by the sequencer when a step boundary is crossed.
struct StepEvent {
    bool    trigger = false; // a note should start this tick
    bool    release = false; // the previous note should stop this tick
    uint8_t note = 36;
    bool    accent = false;
    bool    slide = false;
    int     index = 0;       // which step (0..kNumSteps-1)
};

class Sequencer {
public:
    explicit Sequencer(int sampleRate = kSampleRate);

    void setSampleRate(int sampleRate);
    void setTempo(Sample bpm);            // 16th-note clock: 4 steps per beat
    Sample tempo() const { return bpm_; }

    void setStep(int index, const Step& s);
    Step step(int index) const;
    void clear();                          // all gates off

    void start();                          // reset to step 0, running
    void stop();
    void reset();                          // stop + rewind
    bool running() const { return running_; }
    int  position() const { return current_; }

    // Advance one sample. Returns an event describing what changed this tick;
    // event.trigger/release are false on the vast majority of samples.
    StepEvent tick();

    // External clock support.
    //
    // While enabled the internal tempo is ignored and the sequence only moves
    // when advance() is called. The firmware calls advance() on each edge of
    // the external clock signal, so the box can be slaved to another machine.
    void setExternalClock(bool enabled) { externalClock_ = enabled; }
    bool externalClock() const { return externalClock_; }

    // Advance to the next step immediately, regardless of tempo. Returns the
    // event describing what changed.
    StepEvent advance();

private:
    int    sampleRate_;
    Sample bpm_ = 120.0f;
    Sample samplesPerStep_ = 0.0f;
    Sample phase_ = 0.0f;        // samples into the current step
    int    current_ = 0;
    bool   running_ = false;
    bool   pendingStart_ = false; // emit step 0 on the first tick after start()
    bool   slidingFromPrev_ = false;
    bool   externalClock_ = false;
    Step   steps_[kNumSteps];

    // Shared by tick() and advance(): move to the next step and report it.
    StepEvent advanceToNextStep();
};

} // namespace cw

#endif // CINDERWAVE_SEQUENCER_H
