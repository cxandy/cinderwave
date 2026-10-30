#include "cinderwave/sequencer.h"

namespace cw {
namespace {

int wrapStepIndex(int index) {
    int wrapped = index % kNumSteps;
    if (wrapped < 0) {
        wrapped += kNumSteps;
    }
    return wrapped;
}

} // namespace

Sequencer::Sequencer(int sampleRate)
    : sampleRate_(sampleRate > 0 ? sampleRate : kSampleRate) {
    setTempo(bpm_);
}

void Sequencer::setSampleRate(int sampleRate) {
    sampleRate_ = sampleRate > 0 ? sampleRate : kSampleRate;
    setTempo(bpm_);
}

void Sequencer::setTempo(Sample bpm) {
    bpm_ = clampf(bpm, 20.0f, 300.0f);
    samplesPerStep_ = static_cast<Sample>(sampleRate_) * 60.0f / (bpm_ * 4.0f);
}

void Sequencer::setStep(int index, const Step& s) {
    steps_[wrapStepIndex(index)] = s;
}

Step Sequencer::step(int index) const {
    return steps_[wrapStepIndex(index)];
}

void Sequencer::clear() {
    for (int i = 0; i < kNumSteps; ++i) {
        steps_[i] = Step{};
    }
    slidingFromPrev_ = false;
}

void Sequencer::start() {
    current_ = 0;
    phase_ = 0.0f;
    slidingFromPrev_ = false;
    pendingStart_ = true;   // step 0 fires on the very next tick (the downbeat)
    running_ = true;
}

void Sequencer::stop() {
    running_ = false;
}

void Sequencer::reset() {
    stop();
    current_ = 0;
    phase_ = 0.0f;
    slidingFromPrev_ = false;
    pendingStart_ = false;
}

StepEvent Sequencer::tick() {
    StepEvent ev{};
    if (!running_) {
        return ev;
    }

    // In external clock mode the sequence only moves on an external edge, so
    // there is nothing for the sample tick to do (not even the downbeat:
    // advance() handles that).
    if (externalClock_) {
        return ev;
    }

    // First tick after start(): sound step 0 on the downbeat rather than
    // waiting a full bar for the sequence to wrap around to it.
    if (pendingStart_) {
        pendingStart_ = false;
        const Step first = steps_[current_];
        ev.index = current_;
        ev.release = false; // nothing is playing yet
        if (first.gate) {
            ev.trigger = true;
            ev.note = first.note;
            ev.accent = first.accent;
            ev.slide = first.slide;
        }
        return ev;
    }

    phase_ += 1.0f;
    if (phase_ < samplesPerStep_) {
        return ev;
    }

    phase_ -= samplesPerStep_;
    return advanceToNextStep();
}

StepEvent Sequencer::advance() {
    StepEvent ev{};
    if (!running_) {
        return ev;
    }

    // The first external edge fires step 0, mirroring the downbeat behaviour
    // of the internal clock.
    if (pendingStart_) {
        pendingStart_ = false;
        const Step first = steps_[current_];
        ev.index = current_;
        ev.release = false;
        if (first.gate) {
            ev.trigger = true;
            ev.note = first.note;
            ev.accent = first.accent;
            ev.slide = first.slide;
        }
        return ev;
    }

    return advanceToNextStep();
}

StepEvent Sequencer::advanceToNextStep() {
    StepEvent ev{};
    const Step previous = steps_[current_];
    slidingFromPrev_ = previous.slide;
    current_ = (current_ + 1) % kNumSteps;
    const Step current = steps_[current_];

    ev.index = current_;
    ev.release = !slidingFromPrev_;
    if (current.gate) {
        ev.trigger = true;
        ev.note = current.note;
        ev.accent = current.accent;
        ev.slide = current.slide;
    }
    return ev;
}

} // namespace cw
