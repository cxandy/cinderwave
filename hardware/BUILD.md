# Cinderwave — Hardware Build Guide

This build guide describes the machine the firmware in this repo actually
targets:

| Part | Notes |
|------|-------|
| Raspberry Pi Pico (RP2040) | the whole synth. Pico / Pico W / Pico 2 all work. |
| ILI9341 TFT, 320×240 | SPI @ 24 MHz, rotation 3 |
| 3× 10 kΩ linear potentiometer | cutoff, resonance, tempo |
| 16× Cherry MX switch | two rows of eight, 16 sequencer steps |
| 2× 3.5 mm jack | OUT-A (in use) and OUT-B (reserved) |
| 100 Ω + 10 nF + 10 µF | OUT-A reconstruction filter and DC block |
| 470 Ω | OUT-B series resistor (unused for now) |

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
| Audio OUT-B | GPIO2 | **reserved** — the firmware does not configure or drive this pin |
| Cutoff pot | GPIO26 (ADC0) | wiper → ADC |
| Tempo pot | GPIO27 (ADC1) | wiper → ADC |
| Resonance pot | GPIO28 (ADC2) | wiper → ADC |
| Keys, top row | GPIO14 12 11 16 17 19 20 21 | steps 1–8 |
| Keys, bottom row | GPIO0 1 3 4 5 9 18 22 | steps 9–16 |
| Clock / MIDI output | GPIO8 | 10 ms pulse per step/beat, or UART1 TX MIDI clock |

### Notes on this pin map

- **GPIO8 does double duty in the schematic** — it is the TFT reset line *and*
  the clock output jack. Because the reset line is permanently jumpered to 3V3
  the panel is always out of reset, so the firmware leaves GPIO8 alone for the
  display and uses it only as the clock output.

- **The PWM output and the keypad share GPIO pairs.** The RP2040 hardwires each
  PWM slice to a pair of pins (slice 7 → GPIO14/GPIO15). Only the pin that asks
  for `GPIO_FUNC_PWM` sees the PWM output, so GPIO14 stays usable as a push
  button while its slice-mate drives the jack.

- **The RP2040 has a single PWM interrupt** (`NUM_PWM_IRQS == 1`). When OUT-B
  comes back it will be a second PWM slice (GPIO2 is slice 1, channel A) whose
  duty value is written from the *same* wrap interrupt — the slice free-runs
  with an identical clkdiv and wrap, so the two stay phase-locked.

---

## Audio output

```
 OUT-A:  GPIO15 ──[ 100Ω ]──┬─────────────[ 10µF ]──────▶ 3.5 mm
                            │                              (TIP)
                         [ 10nF ]
                            │
                           GND ──────────────────────────▶ 3.5 mm (SLEEVE)
```

- **OUT-A carries the full voice**: oscillator → resonant filter → VCA →
  master gain.
- **OUT-B is reserved.** The firmware skips GP2 entirely — no PWM, no GPIO
  setup. When it is given a job it will feed the same jack through the
  potentiometer that cross-mixes the two outputs.

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
| **Two keys pressed together** | **PLAY / STOP** (a chord: both keys within 80 ms) |
| Long hold (≥ 900 ms) | become the function modifier: the grid on screen switches to function labels, and the other keys run those functions |

The chord is the fast way to start and stop — you never have to hunt for the
function layer. The long hold is how the editing functions are reached.

The screen mirrors the physical 2×8 key grid in both layers, so the on-screen
grid is always a map of the keys.

### Function layer

| Key | Function | Key | Function |
|-----|----------|-----|----------|
| 1 | PLAY / STOP | 9 | SELECT − |
| 2 | ACCENT (on the selected step) | 10 | SELECT + |
| 3 | SLIDE (on the selected step) | 11 | NOTE − |
| 4 | OCTAVE − (transposes the pattern) | 12 | NOTE + |
| 5 | OCTAVE + (transposes the pattern) | 13 | CLOCK (step / beat / MIDI / off) |
| 6 | WAVEFORM | 14 | REVERSE the pattern |
| 7 | TEMPO (re-zero the pot at 130 BPM) | 15 | SAVE (write the pattern to the flash) |
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

## Clock output / MIDI

GPIO8 is the clock port. It drives a **10 ms pulse** on every sequencer step,
so another machine (a drum machine, a DAW, another Cinderwave) can follow this
one. The `CLOCK` function key switches it between:

- **STP** — one pulse per step (a 16th-note clock)
- **BET** — one pulse per beat (every 4 steps)
- **MID** — a real MIDI clock on the same jack: `0xF8` ticks at 24 per
  quarter note, plus `0xFA` (start) / `0xFC` (stop). GPIO8 doubles as UART1
  TX, and the firmware reconfigures the pin automatically.
- **OFF** — the pin idles low

Pulses and MIDI ticks are generated inside the audio interrupt, so they stay
locked to the sequencer no matter what the main loop is doing.

Wire it as a plain logic-level output with a common ground. The pulse is 3.3 V
and 10 ms wide.

For MIDI, wire the jack as a MIDI TRS-A **type A** output:

```
 GPIO8 (UART1 TX) ──[ 33 Ω ]── TIP    (MIDI pin 5)
 3V3              ──[ 33 Ω ]── RING   (MIDI pin 4)
 GND                          ── SLEEVE (MIDI pin 2)
```

Any standard MIDI-in device (or a TRS-A-to-DIN adapter) then receives the
clock.

> Clock *input* (slaving this box to an external clock) is not implemented
> yet. When it is, it will need a sequencer change in the core plus an edge
> detector — see `Sequencer::setExternalClock()` in a later revision.

---

## Pattern storage

The pattern survives a power cycle. The last four sectors of the Pico's flash
(16 KB, at 0x1FC000 on a 2 MB board) hold a **ring of 64 snapshots**, one per
256-byte page, and the newest valid one is restored at the next power-up.

What is stored: the 16 steps (note, gate, accent, slide), the octave, the
waveform and the tempo offset. The tempo *pot* is not stored — it is a
physical knob, so the offset that was applied to it is.

**When it is written**

- Automatically, whenever the instrument is quiet: the sequencer is stopped,
  or the step that is currently playing is silent. Writing to flash takes the
  chip out of execute-in-place mode, so it must not happen while something is
  sounding. If the pattern never goes quiet, it is written anyway 10 seconds
  after the last edit.
- Immediately when you press the `SAVE` function key. That one writes even
  while the sequencer is playing, so you may hear a short dropout (up to
  ~45 ms when a sector has to be erased, otherwise under a millisecond).

The hint line shows `PATTERN SAVED` after a successful write and
`PATTERN LOADED` when a stored pattern was restored at boot.

**How it is made safe**

- A snapshot is never written over the page that holds the live one, so a
  power cut during a write leaves the previous pattern intact.
- Every snapshot carries a CRC32; a half-written page fails the checksum and
  is ignored.
- The firmware refuses to touch the storage area if the program image ever
  grows into it.

`RESET` brings back the demo pattern but never erases your save.

The record format and the ring bookkeeping live in
`firmware/rp2040/storage_format.h`; they are covered by the host test suite
(`tests/test_storage.cpp`), which runs the same code against a byte buffer
instead of real flash.

---

## Flashing

1. Build the firmware (see the repo README) to get `cinderwave_firmware.uf2`.
2. Hold **BOOTSEL** while plugging in USB.
3. The Pico mounts as a USB drive named **RPI-RP2**.
4. Drag `cinderwave_firmware.uf2` onto it. The Pico reboots and starts playing
   the demo pattern immediately.

Turn the pots to sweep the filter and change tempo; tap the keys to write the
pattern; press any two keys together to stop and start.

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
