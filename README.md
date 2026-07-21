# Cinderwave

**An open-hardware acid groovebox that fits on a $4 microcontroller.**

Cinderwave is a monophonic synth voice and 16-step sequencer in the TB-303
idiom — band-limited oscillators, a resonant zero-delay-feedback filter,
per-step accent and slide — running on a Raspberry Pi Pico (RP2040). The entire
DSP core is platform-independent C++17 with **no dynamic allocation, no
exceptions, and no dependencies**, so the *exact same code* that drives the
hardware also renders a `.wav` on your desktop.

```
   ┌───────────┐   ┌──────────┐   ┌──────────────┐   ┌─────────┐
   │ 16-step   │──▶│ Oscillator│──▶│ SVF filter   │──▶│ VCA     │──▶ out
   │ sequencer │   │ saw/sqr/  │   │ (cutoff env  │   │ (amp    │
   │ note/gate/│   │ tri/sine  │   │  + resonance)│   │  env)   │
   │ accent/   │   │ PolyBLEP  │   └──────▲───────┘   └────▲────┘
   │ slide     │   └───────────┘   filter env          amp env
   └───────────┘
```

## Why it's fun

- **Band-limited oscillators** — PolyBLEP saw and square, so the high notes
  don't alias into mush on a cheap DAC.
- **Cytomic TPT state-variable filter** — the *good* resonant filter, stable and
  self-oscillating across the whole cutoff range.
- **Real acid behavior** — accent boosts amplitude *and* opens the filter;
  slide portamentos between steps without retriggering the envelope.
- **One codebase, two worlds** — flash it to a Pico, or `cmake && ctest` it on
  your laptop and bake the demo pattern to audio.

## Quick start (no hardware needed)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure     # DSP unit tests
./build/cinderwave_render out.wav              # bake the demo acid line to WAV
```

`cinderwave_render` writes ~8 seconds of the built-in demo pattern (a driving
130 BPM bassline with accents and slides) to a 16-bit mono WAV.

## Build the hardware

Full bill of materials, GPIO pin map, and two audio-output options (a budget
PWM + RC filter, or a PCM5102A I2S DAC) live in
**[hardware/BUILD.md](hardware/BUILD.md)**.

Minimum viable Cinderwave:

| Part                        | Qty | Notes                                  |
|-----------------------------|-----|----------------------------------------|
| Raspberry Pi Pico (RP2040)  | 1   | the whole synth                        |
| 10 kΩ linear potentiometer  | 3   | cutoff, resonance, tempo               |
| Tactile button              | 1   | play / stop                            |
| 1.8 kΩ resistor + 10 nF cap | 1   | RC reconstruction filter (~16 kHz)     |
| 10 µF cap + 3.5 mm jack      | 1   | DC-blocked line out                    |

## Flash the firmware

```sh
cd firmware/rp2040
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Hold **BOOTSEL** while plugging in the Pico, then drop
`build/cinderwave_firmware.uf2` onto the `RPI-RP2` drive. It boots straight into
the demo pattern; the three pots take over cutoff, resonance, and tempo, and the
button toggles playback.

## Layout

```
include/cinderwave/   Public API — the frozen DSP contract (headers only)
src/                  DSP core: oscillator, envelope, filter, voice, sequencer, synth
host/                 Desktop WAV renderer (uses the identical core)
firmware/rp2040/      Pico SDK target: PWM audio + pot/button controls
tests/                Dependency-free unit tests (built and run in CI)
hardware/             Build guide, BOM, wiring, pin map
```

## Continuous integration

Every push builds the core, runs the unit tests, renders the demo WAV, and
cross-compiles the RP2040 `.uf2` — both artifacts are uploaded from the run. See
[.github/workflows/ci.yml](.github/workflows/ci.yml).

## License

MIT — see [LICENSE](LICENSE).
