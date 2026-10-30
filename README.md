# Cinderwave

**An open-hardware acid groovebox that fits on a $4 microcontroller.**

Cinderwave is a monophonic synth voice and 16-step sequencer in the TB-303
idiom, band-limited oscillators, a resonant zero-delay-feedback filter,
per-step accent and slide running on a Raspberry Pi Pico (RP2040). The entire
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

Full bill of materials, GPIO pin map, and wiring notes live in
**[hardware/BUILD.md](hardware/BUILD.md)**.

This build targets a Raspberry Pi Pico (RP2040) with an ILI9341 320x240
display, sixteen Cherry MX keys and an audio output:

| Part                        | Qty | Notes                                  |
|-----------------------------|-----|----------------------------------------|
| Raspberry Pi Pico (RP2040)  | 1   | the whole synth                        |
| ILI9341 TFT, 320x240        | 1   | SPI @ 24 MHz, rotation 3               |
| 10 kΩ linear potentiometer  | 3   | cutoff, resonance, tempo               |
| Cherry MX switch            | 16  | 2 rows of 8, the 16 sequencer steps    |
| 3.5 mm audio jack           | 2   | OUT-A (in use) and OUT-B (reserved)    |
| 100 Ω + 10 nF + 10 µF       | 1   | OUT-A reconstruction filter + DC block |
| 470 Ω                       | 1   | OUT-B series resistor (unused for now) |

> **Two things worth fixing on the board** — the OUT-A RC corner sits at
> 159 kHz, which leaves the PWM carrier almost unattenuated, and OUT-A has no
> DC-blocking capacitor. See
> [hardware/BUILD.md](hardware/BUILD.md#two-things-to-fix-on-the-board) for the
> recommended values.

## Flash the firmware

```sh
cd firmware/rp2040
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Hold **BOOTSEL** while plugging in the Pico, then drop
`build/cinderwave_firmware.uf2` onto the `RPI-RP2` drive. It boots straight into
the demo pattern — or into the pattern you last saved with the `SAVE` function
(see below).

Every push builds the core, runs the unit tests, renders the demo WAV, and
cross-compiles the RP2040 `.uf2` — both artifacts are uploaded from the run.
See [.github/workflows/ci.yml](.github/workflows/ci.yml).

## Using it

The screen mirrors the physical 2x8 key grid, so the on-screen grid is always
a map of the keys.

| Gesture | Effect |
|---------|--------|
| Tap | toggle the gate of that step, and select it |
| Medium hold | toggle the accent of that step |
| **Two keys held together (~60 ms)** | **PLAY / STOP** |
| Long hold | become the function modifier — the grid switches to function labels |

Pressing any two keys and holding them together briefly is the quick way to
start and stop. A brief overlap of the two presses is required, so fast
step-taps with two fingers never trigger play/stop by accident; if either key
is released before that, both keys act as normal taps. The long hold is how
the editing functions (accent, slide, octave, waveform, ...) are reached.

Turn the pots to sweep the filter and change tempo. The `CLOCK` function
switches the GPIO8 clock port between a pulse per step (`CLK STP`), a pulse
per beat (`CLK BET`), a real MIDI clock on the same jack (`CLK MID` — 24
ticks per quarter note plus START/STOP, GP8 driven as UART1 TX at 31250
baud), and off.

The `SAVE` function writes the current pattern, octave and waveform to a
spare flash sector (audio mutes for a moment while it writes) and the
pattern is restored automatically at the next power-up. `RESET` brings back
the demo pattern; it does not erase your save.

## Layout

```
include/cinderwave/   Public API — the frozen DSP contract (headers only)
src/                  DSP core: oscillator, envelope, filter, voice, sequencer, synth
host/                 Desktop WAV renderer (uses the identical core)
firmware/rp2040/      Pico SDK target: PWM audio + TFT + keypad + pots
  pins.h                The single source of truth for the GPIO pin map
  audio.cpp/h           PWM audio output on OUT-A (GP15)
  display.cpp/h         ILI9341 SPI driver and drawing primitives
  keypad.cpp/h          16-key scanner with time-based debouncing
  font5x7.h             5x7 bitmap font
  bin2uf2.py            bin -> UF2 packer (keeps picotool out of CI)
tests/                Dependency-free unit tests (built and run in CI)
hardware/             Build guide, BOM, wiring, pin map
```

## License

MIT — see [LICENSE](LICENSE).
