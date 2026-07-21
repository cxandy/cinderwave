#include "check.h"

#include "cinderwave/voice.h"

#include <cmath>

CW_TEST(voice_note_on_outputs_finite_bounded_audio) {
    cw::Voice voice(cw::kSampleRate);
    cw::Patch patch{};
    patch.cutoff = 1200.0f;
    patch.resonance = 3.0f;
    patch.envMod = 1500.0f;
    patch.ampSustain = 0.3f;
    voice.setPatch(patch);
    voice.noteOn(48, true, false);

    double energy = 0.0;
    for (int i = 0; i < 4096; ++i) {
        const cw::Sample s = voice.process();
        CHECK(std::isfinite(s));
        CHECK(s >= -1.0f);
        CHECK(s <= 1.0f);
        energy += static_cast<double>(s) * s;
    }
    CHECK(energy > 0.01);
}

CW_TEST(voice_note_off_decays_toward_silence) {
    cw::Voice voice(cw::kSampleRate);
    cw::Patch patch{};
    patch.ampSustain = 0.8f;
    patch.ampRelease = 0.01f;
    patch.resonance = 2.0f;
    voice.setPatch(patch);
    voice.noteOn(40, false, false);
    for (int i = 0; i < 2048; ++i) {
        (void)voice.process();
    }
    voice.noteOff();

    cw::Sample s = 0.0f;
    for (int i = 0; i < 8000; ++i) {
        s = voice.process();
        CHECK(std::isfinite(s));
        CHECK(s >= -1.0f);
        CHECK(s <= 1.0f);
    }
    CHECK_NEAR(s, 0.0f, 0.01f);
    CHECK(!voice.active());
}
