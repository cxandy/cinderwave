// Minimal 16-bit PCM mono WAV writer — no dependencies, little-endian hosts.
#ifndef CINDERWAVE_HOST_WAV_H
#define CINDERWAVE_HOST_WAV_H

#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>

namespace cwhost {

// Writes `samples` (nominally [-1, 1]) as a 16-bit mono WAV. Returns true on
// success. Values are clamped before quantization.
inline bool writeWav(const std::string& path, const std::vector<float>& samples,
                     int sampleRate) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };

    const uint32_t dataBytes = static_cast<uint32_t>(samples.size()) * 2u;
    std::fwrite("RIFF", 1, 4, f);
    u32(36u + dataBytes);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
    u32(16u);                    // PCM chunk size
    u16(1);                      // PCM format
    u16(1);                      // mono
    u32(static_cast<uint32_t>(sampleRate));
    u32(static_cast<uint32_t>(sampleRate) * 2u); // byte rate
    u16(2);                      // block align
    u16(16);                     // bits per sample
    std::fwrite("data", 1, 4, f);
    u32(dataBytes);

    for (float s : samples) {
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        int16_t q = static_cast<int16_t>(s * 32767.0f);
        u16(static_cast<uint16_t>(q));
    }
    std::fclose(f);
    return true;
}

} // namespace cwhost

#endif // CINDERWAVE_HOST_WAV_H
