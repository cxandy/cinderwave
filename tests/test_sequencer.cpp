#include "check.h"

#include "cinderwave/sequencer.h"

// One full loop should trigger all 16 steps in order, starting on step 0 (the
// downbeat) — no one-step rotation.
CW_TEST(sequencer_triggers_every_step_in_order_from_the_downbeat) {
    cw::Sequencer seq(cw::kSampleRate);
    seq.setTempo(120.0f);
    for (int i = 0; i < cw::kNumSteps; ++i) {
        cw::Step step{};
        step.note = static_cast<uint8_t>(36 + i);
        step.gate = true;
        seq.setStep(i, step);
    }

    seq.start();
    constexpr int samplesPerStep = cw::kSampleRate * 60 / (120 * 4);
    int order[cw::kNumSteps] = {};
    int triggers = 0;
    // Generous budget: a couple of bars' worth of samples.
    for (int i = 0; i < samplesPerStep * cw::kNumSteps * 2 && triggers < cw::kNumSteps; ++i) {
        const cw::StepEvent ev = seq.tick();
        if (ev.trigger) {
            order[triggers] = ev.index;
            ++triggers;
        }
    }

    CHECK(triggers == cw::kNumSteps);
    for (int i = 0; i < cw::kNumSteps; ++i) {
        CHECK(order[i] == i);   // 0, 1, 2, ... 15 — downbeat first
    }
}

CW_TEST(sequencer_slide_suppresses_release_into_next_step) {
    cw::Sequencer seq(cw::kSampleRate);
    seq.setTempo(120.0f);
    cw::Step first{};
    first.gate = true;
    first.slide = true;
    cw::Step second{};
    second.gate = true;
    seq.setStep(0, first);
    seq.setStep(1, second);

    seq.start();
    // Tick until the sequencer crosses into step 1.
    cw::StepEvent ev{};
    for (int i = 0; i < cw::kSampleRate; ++i) {
        ev = seq.tick();
        if (ev.index == 1 && (ev.trigger || ev.release)) {
            break;
        }
    }

    CHECK(ev.index == 1);
    CHECK(ev.trigger);
    CHECK(!ev.release);   // slide from step 0 holds the note through
}
