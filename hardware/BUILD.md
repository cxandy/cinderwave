# Cinderwave — Hardware Build Guide

Everything you need to turn a Raspberry Pi Pico into the Cinderwave groovebox.
Two audio-output options are covered: a **budget** PWM + RC filter you can build
from the junk drawer, and a **recommended** I2S DAC for clean line-level audio.

---

## Bill of materials

| Part | Qty | ~Price (EUR) | Notes |
|------|-----|-------------|-------|
| Raspberry Pi Pico (RP2040) | 1 | 4 | The whole synth. Pico / Pico W / Pico 2 all work. |
| PCM5102A I2S DAC board | 1 | 4 | *Recommended* output. Skip if using PWM + RC. |
| 10 kΩ linear potentiometer | 3 | 1.5 | Cutoff, resonance, tempo. |
| Tactile push button | 4 | 0.5 | Play/stop (required) + step-edit/accent/shift (future). |
| Rotary encoder (EC11) | 1 | 1.5 | *Optional* — pattern/step scrolling. |
| 1.8 kΩ resistor | 1 | 0.05 | RC filter R (PWM output only). |
| 10 nF ceramic capacitor | 1 | 0.05 | RC filter C (PWM output only). |
| 10 µF electrolytic capacitor | 1 | 0.1 | DC-blocking on line out. |
| 3.5 mm audio jack | 1 | 0.5 | Line/headphone out. |
| Half-size breadboard or protoboard | 1 | 3 | Assembly. |
| Hookup wire / jumpers | — | 2 | — |

Total: roughly **€15–20**, most of which is the breadboard and jack.

---

## Audio output — option A: budget PWM + RC filter

The RP2040 has no DAC, but a PWM pin driven at the sample rate plus a first-order
RC low-pass makes a serviceable 1-bit-ish DAC. The firmware wraps a PWM slice at
32 kHz and writes one sample per period on **GPIO0**.

```
 GPIO0 ──[ 1.8kΩ ]──┬──────[ 10µF + ]──────▶ 3.5mm TIP (left/mono)
                    │
                 [ 10nF ]
                    │
                   GND ──────────────────────▶ 3.5mm SLEEVE (GND)
```

- **R = 1.8 kΩ, C = 10 nF** → corner ≈ 1 / (2π·R·C) ≈ **8.8 kHz**. For a brighter
  top end use 1.0 kΩ + 10 nF (≈16 kHz); for less carrier bleed use 1.8 kΩ + 22 nF.
- The **10 µF** series cap blocks the ~1.65 V DC bias so you don't push DC into an
  amp or headphones. Its + terminal faces the RC node.
- This output is line-ish level and drives headphones quietly. For anything loud,
  feed it into a powered speaker or a small amp.

## Audio output — option B: PCM5102A I2S DAC (recommended)

Clean 16-bit stereo, no carrier whine. Wire the Pico's I2S pins to the DAC:

| Pico pin | Signal | PCM5102A pin |
|----------|--------|--------------|
| GPIO18   | BCK (bit clock)   | BCK  |
| GPIO19   | LRCK / WS (word)  | LRCK |
| GPIO20   | DIN (data)        | DIN  |
| 3V3(OUT) | power             | VCC / VIN |
| GND      | ground            | GND  |

On the PCM5102A breakout, tie the config pins for standard I2S, software-format:
- **SCK** → GND (the DAC runs on an internal PLL; no master clock needed)
- **FMT** → GND (I2S)
- **XMT** → 3V3 (un-mute)

> The reference firmware ships the **PWM output (option A)** so it builds against
> a bare Pico SDK with no extra PIO/I2S library. An I2S sink is a drop-in
> replacement for `on_pwm_wrap()` — the DSP core doesn't change.

---

## GPIO pin assignment

| Function | Pico GPIO | Detail |
|----------|-----------|--------|
| PWM audio out | GPIO0 | Option A only; feeds the RC filter |
| I2S BCK / LRCK / DIN | GPIO18 / 19 / 20 | Option B only |
| Cutoff pot | GPIO26 (ADC0) | wiper → ADC |
| Resonance pot | GPIO27 (ADC1) | wiper → ADC |
| Tempo pot | GPIO28 (ADC2) | wiper → ADC |
| Play/stop button | GPIO2 | to GND, internal pull-up |
| Step-edit / accent / shift | GPIO3 / 4 / 5 | reserved for firmware growth |

---

## Wiring notes

- **Potentiometers** wire as voltage dividers: outer legs to **3V3** and **GND**,
  wiper to the ADC GPIO. The firmware reads the 12-bit ADC and maps it
  exponentially (musical for cutoff and tempo).
- **Buttons** connect the GPIO to **GND**; the firmware enables the internal
  pull-up, so a press reads low (active-low). No external resistor needed.
- **Grounding:** star-ground the DAC/RC output back to a single Pico GND pin to
  keep digital switching noise out of the audio. Keep the audio wire short.
- **Power:** a USB cable from your computer or a 5 V phone charger is plenty. The
  Pico's on-board regulator supplies 3V3 to the pots and DAC.

---

## Flashing

1. Build the firmware (see the repo README) to get `cinderwave_firmware.uf2`.
2. Hold the **BOOTSEL** button on the Pico while plugging in USB.
3. The Pico mounts as a USB drive named **RPI-RP2**.
4. Drag `cinderwave_firmware.uf2` onto it. The Pico reboots and starts playing
   the demo pattern immediately.

Turn the pots to sweep the filter and change tempo; tap the button to stop and
start the sequence. Happy squelching.
