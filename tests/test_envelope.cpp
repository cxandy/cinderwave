#include "check.h"

#include "cinderwave/envelope.h"

CW_TEST(envelope_runs_adsr_and_stays_bounded) {
    cw::Envelope env(cw::kSampleRate);
    env.setAttack(0.01f);
    env.setDecay(0.03f);
    env.setSustain(0.4f);
    env.setRelease(0.02f);
    env.gate(true);

    cw::Sample level = 0.0f;
    for (int i = 0; i < 320; ++i) {
        level = env.process();
        CHECK(level >= 0.0f);
        CHECK(level <= 1.0f);
    }
    CHECK_NEAR(level, 1.0f, 0.01f);

    // Exponential decay toward sustain asymptotes: ~6 time constants of the
    // 0.03 s decay (about 5800 samples at 32 kHz) to settle onto the sustain.
    for (int i = 0; i < 8000; ++i) {
        level = env.process();
        CHECK(level >= 0.0f);
        CHECK(level <= 1.0f);
    }
    CHECK_NEAR(level, 0.4f, 0.01f);
    CHECK(env.stage() == cw::Envelope::Stage::Sustain);

    env.gate(false);
    for (int i = 0; i < 6000; ++i) {
        level = env.process();
        CHECK(level >= 0.0f);
        CHECK(level <= 1.0f);
    }
    CHECK_NEAR(level, 0.0f, 0.002f);
    CHECK(env.stage() == cw::Envelope::Stage::Idle);
}
