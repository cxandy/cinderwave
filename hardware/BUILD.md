# Cinderwave — Hardware Build Guide

This build guide describes the machine the firmware in this repo actually
targets:

| Part | Notes |
|------|-------|
| Raspberry Pi Pico (RP2040) | the whole synth. Pico / Pico W / Pico 2 all work. |
| ILI9341 TFT, 320×240 | SPI @ 24 MHz, rotation 3 |
| 3× 10 kΩ linear potentiometer | cutoff, resonance, tempo |
| 16× Cherry MX switch | two rows of eight, 16 sequencer steps |
| 2× 3.5 mm jack | OUT-A (filtered voice) and OUT-B (raw voice) |
| 100 Ω + 10 nF + 10 µF | OUT-A reconstruction filter and DC block |
| 470 Ω | OUT-B series resistor |

---

## GPIO pin assignment

| Function | Pico GPIO | Detail |
|----------|-----------|--------|
| TFT SCK | GPIO6 | SPI0 |
| TFT MOSI | GPIO7 | SPI0, write-only (no MISO is wired) |
| TFT DC | GPIO10 | data / command |
| TFT CS | GPIO13 | chip select, active low |
| TFT RST | GPIO8 | **strapped to 3V3** — the panel is never software-reset, and the firmware does not drive this pin |
| Audio OUT-A | GPIO15 | PWM slice 7 ch B → 100 Ω + 10 nF RC → jack |
| Audio OUT-B | GPIO2 | PWM slice 1 ch A → 470 Ω → jack |
| Cutoff pot | GPIO26 (ADC0) | wiper → ADC |
| Tempo pot | GPIO27 (ADC1) | wiper → ADC |
| Resonance pot | GPIO28 (ADC2) | wiper → ADC |
| Keys, top row | GPIO14 12 11 16 17 19 20 21 | steps 1–8 |
| Keys, bottom row | GPIO0 1 3 4 5 9 18 22 | steps 9–16 |
| External clock | GPIO8 | input, rising edge advances the sequence |

### Notes on this pin map

- **GPIO8 does double duty in the schematic** — it is the TFT reset line *and*
  the external clock jack. Because the reset line is permanently jumpered to
  3V3 the panel is always out of reset, so the firmware leaves GPIO8 alone for
  the display and uses it only for the clock input. If you ever want a
  software reset, move the clock jack to another pin (or add a reset button);
  the firmware will then need `kTftResetPin` to be driven as well.

- **The PWM output and the keypad share GPIO pairs.** The RP2040 hardwires each
  PWM slice to a pair of pins (slice 7 → GPIO14/GPIO15, slice 1 → GPIO2/GPIO3).
  Only the pin that asks for `GPIO_FUNC_PWM` sees the PWM output, so GPIO14 and
  GPIO3 stay usable as push buttons while their slice-mates drive the jacks.

- **The RP2040 has a single PWM interrupt** (`NUM_PWM_IRQS == 1`). Both outputs
  are written from one wrap interrupt on the OUT-A slice; the OUT-B slice
  free-runs with an identical clkdiv and wrap, so the two stay phase-locked.

---

## Audio outputs

```
 OUT-A:  GPIO15 ──[ 100Ω ]──┬─────────────[ 10µF ]──────▶ 3.5 mm
                            │                              (TIP)
                         [ 10nF ]
                            │
                           GND ──────────────────────────▶ 3.5 mm (SLEEVE)

 OUT-B:  GPIO2  ──[ 470Ω ]──────────────────────────────▶ 3.5 mm
```

- **OUT-A carries the full voice**: oscillator → resonant filter → VCA →
  master gain.
- **OUT-B carries the same frame with the filter bypassed** (oscillator → VCA).
  A physical potentiometer cross-mixes the two, so you can dial anywhere
  between "dark and filtered" and "bright and raw". The firmware can also mute
  OUT-B entirely (see the `OUT-B` function key).

### Two things to fix on the board

1. **The RC corner frequency is far too high.** With R = 100 Ω and C = 10 nF
   the corner is `1/(2π·R·C)` ≈ **159 kHz**, which attenuates the 32 kHz PWM
   carrier by only 0.34 dB — you will hear raw PWM buzz on OUT-A. Use
   **100 Ω + 100 nF** (corner ≈ 15.9 kHz, −7 dB at the carrier) or
   **100 Ω + 220 nF** (corner ≈ 7.2 kHz, −13 dB) instead.

2. **OUT-A has no DC blocking capacitor.** The PWM output is unipolar, so at
   50 % duty it sits at ~1.65 V DC. A **10 µF electrolytic in series** (＋
   toward the RC node) blocks that; with a 470 Ω load it puts the corner at
   roughly 28 Hz. Without it you push DC into whatever you plug the jack into.

OUT-B currently has neither a reconstruction filter nor a DC block. That is
fine if OUT-B is meant to drive an external trigger input (the 470 Ω is a
sensible series resistor for that), but if you want to listen to it, it needs
the same 100 Ω + 100 nF + 10 µF treatment as OUT-A.

---

## Display

The ILI9341 is wired to SPI0 and driven in 8-bit mode: commands and parameters
go out as plain bytes, pixel data as big-endian RGB565 byte pairs.

| TFT pin | Pico |
|---------|------|
| SCK | GPIO6 |
| MOSI | GPIO7 |
| DC | GPIO10 |
| CS | GPIO13 |
| RST | 3V3 (hard jumper — not GPIO8) |
| VCC | 3V3 |
| GND | GND |

The panel runs at **rotation 3**, which is a 320×240 landscape orientation
(`MADCTL = 0xE8`). If your panel ends up mirrored or sideways, change
`kTftRotation` in `firmware/rp2040/pins.h` — that is the only thing you need
to touch.

The SPI clock is set to 24 MHz. The ILI9341 datasheet quotes 10 MHz as the
nominal maximum, so 24 MHz is technically out of spec; it works on most
breakout boards but if you see colour noise, drop `kTftSpiBaudrate` to
12 MHz.

---

## Keypad

16 independent Cherry MX switches — **no diode matrix**. Each switch connects
one leg to its GPIO and the other to GND; the internal pull-up makes a press
read LOW. Debouncing is time based (20 ms) and edges are queued, so a slow
screen redraw can never swallow a press.

### Gestures

| Gesture | Effect |
|---------|--------|
| Tap (< 400 ms) | toggle the gate of that step, and select it |
| Medium hold (400–900 ms) | toggle the accent of that step |
| Long hold (≥ 900 ms) | become the function modifier: the grid on screen switches to function labels, and the other keys run those functions |

The screen mirrors the physical 2×8 key grid in both layers, so the on-screen
grid is always a map of the keys.

### Function layer

| Key | Function | Key | Function |
|-----|----------|-----|----------|
| 1 | PLAY / STOP | 9 | SELECT − |
| 2 | ACCENT (on the selected step) | 10 | SELECT + |
| 3 | SLIDE (on the selected step) | 11 | NOTE − |
| 4 | OCTAVE − (transposes the pattern) | 12 | NOTE + |
| 5 | OCTAVE + (transposes the pattern) | 13 | CLOCK (external clock in/out) |
| 6 | WAVEFORM | 14 | OUT-B (raw / off) |
| 7 | TEMPO (reset to 130 BPM) | 15 | DEMO (reload the demo pattern) |
| 8 | CLEAR pattern | 16 | RESET everything |

---

## Pots

Three potentiometers wire as voltage dividers: outer legs to **3V3** and
**GND**, wiper to the ADC GPIO. The firmware reads the 12-bit ADC, discards
the first conversion after switching channels (the RP2040 ADC carries the
previous channel's value into it) and averages the next four.

| Pot | GPIO | Range |
|-----|------|-------|
| Cutoff | GPIO26 (ADC0) | 120 Hz … 6 kHz, exponential |
| Resonance | GPIO28 (ADC2) | Q 0.7 … 18 |
| Tempo | GPIO27 (ADC1) | 60 … 180 BPM |

---

## External clock

GPIO8 is an input. With the `CLOCK` function key enabled, the internal tempo
is ignored and the sequence advances on each **rising edge** of the signal on
GPIO8 — one edge per step. The edge is detected inside the audio interrupt, so
the sequencer and the voice are never touched from two contexts at once.

Wire it as a plain logic-level input with a common ground. A pull-down is
enabled on GPIO8 so the pin idles low when nothing is plugged in.

---

## Flashing

1. Build the firmware (see the repo README) to get `cinderwave_firmware.uf2`.
2. Hold **BOOTSEL** while plugging in USB.
3. The Pico mounts as a USB drive named **RPI-RP2**.
4. Drag `cinderwave_firmware.uf2` onto it. The Pico reboots and starts playing
   the demo pattern immediately.

Turn the pots to sweep the filter and change tempo; tap the keys to write the
pattern; hold a key for the function layer.

### Building without picotool

The Pico SDK normally converts the ELF to UF2 with `picotool`, which it builds
from source with Rust. That is a heavy dependency for CI, so the firmware
build here sets `PICO_NO_UF2=1` / `PICO_NO_PICOTOOL=1` and converts the raw
binary instead:

```sh
arm-none-eabi-objcopy -O binary cinderwave_firmware.elf cinderwave_firmware.bin
python firmware/rp2040/bin2uf2.py cinderwave_firmware.bin cinderwave_firmware.uf2
```

`bin2uf2.py` is plain Python with no dependencies — it writes the same UF2
block format the bootloader expects (RP2040 family ID `0xe48bff56`, flash base
`0x10000000`).
