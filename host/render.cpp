#include "cinderwave/synth.h"

#include "wav.h"

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "cinderwave_demo.wav";
    constexpr float durationSeconds = 8.0f;
    const int sampleRate = cw::kSampleRate;
    const int sampleCount = static_cast<int>(durationSeconds * static_cast<float>(sampleRate));

    cw::Synth synth(sampleRate);
    synth.loadDemoPattern();
    synth.sequencer().start();

    std::vector<float> samples(static_cast<std::size_t>(sampleCount));
    synth.renderBlock(samples.data(), sampleCount);

    if (!cwhost::writeWav(path, samples, sampleRate)) {
        std::fprintf(stderr, "failed to write %s\n", path.c_str());
        return 1;
    }

    std::printf("wrote %s (%.2f seconds)\n", path.c_str(), durationSeconds);
    return 0;
}
