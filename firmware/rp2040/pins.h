// Cinderwave — hardware pin map for the RP2040 build.
//
// Single source of truth for every GPIO the firmware touches. The values below
// describe the *actual* machine this firmware targets:
//
//   MCU    Raspberry Pi Pico / RP2040
//   TFT    ILI9341 320x240, SPI @ 24 MHz, rotation 3
//          SCK=GP6  MOSI=GP7  RST=GP8 (hard-jumpered to 3V3)  DC=GP10  CS=GP13
//   Audio  OUT-A: GP15 -> 100 R + 10 nF RC -> 3.5 mm jack
//          OUT-B: GP2  -> 470 R -> 3.5 mm jack   (reserved, not driven yet)
//          a physical pot cross-mixes OUT-A and OUT-B
//   Pots   3 x potentiometer -> ADC: GP26 / GP28 / GP27
//   Keys   16 x Cherry MX in two rows of eight (see kKeyPins)
//   Clock  external clock OUT on GP8
//
// Note on GP8: the TFT reset line is permanently strapped to 3V3, so the panel
// is never software-reset and GP8 is left free for the external clock port.
// The firmware must therefore NOT reconfigure GP8 as the display reset.
//
// GP8 is also the *only* pin that appears twice in this file (clock port and
// the historical TFT reset). Nothing here drives it for the display.

#ifndef CINDERWAVE_PINS_H
#define CINDERWAVE_PINS_H

#include <stdint.h>

namespace cwp {

// ---------------------------------------------------------------------------
// Display — ILI9341, 320x240, rotation 3
// ---------------------------------------------------------------------------

constexpr int kTftSckPin  = 6;   // GP6  -> SPI0 SCK
constexpr int kTftMosiPin = 7;   // GP7  -> SPI0 MOSI (the panel is write-only)
constexpr int kTftDcPin   = 10;  // GP10 -> data/command
constexpr int kTftCsPin   = 13;  // GP13 -> chip select (active low)

// GP8 would be the TFT reset, but it is hard-jumpered to 3V3 and reused as the
// external clock port. Declared for documentation only — never initialised.
constexpr int kTftResetPin = 8;

// SPI0 runs at 24 MHz. The ILI9341 datasheet quotes 10 MHz as the nominal
// maximum; 24 MHz usually works on breakout boards but is out of spec. Drop
// this to 12 MHz if the panel shows colour noise.
constexpr uint32_t kTftSpiBaudrate = 24'000'000;

// Panel orientation. Rotation 3 is the 320x240 landscape orientation.
//
// MADCTL values below follow the Adafruit_ILI9341 convention, where rotation 3
// is MX|MY|MV|BGR = 0xE8 and the address window is 320x240:
//   rotation 0 -> 0x48, 240x320 portrait
//   rotation 1 -> 0x28, 320x240 landscape
//   rotation 2 -> 0x88, 240x320 portrait
//   rotation 3 -> 0xE8, 320x240 landscape
//
// If your panel ends up mirrored or sideways, flip kTftRotation to another
// value (0..3) — no other change is needed.
constexpr int kTftRotation = 3;

constexpr int kTftWidth    = 320;
constexpr int kTftHeight   = 240;

// ---------------------------------------------------------------------------
// Audio outputs
// ---------------------------------------------------------------------------

// OUT-A: GP15 -> PWM slice 7, channel B -> 100 R + 10 nF RC filter -> jack.
constexpr int kAudioOutAPin = 15;

// OUT-B: GP2 -> PWM slice 1, channel A -> 470 R -> jack.
//
// RESERVED. The firmware deliberately does not configure or drive GP2 yet, so
// the pin stays free for whatever the second output ends up carrying. When it
// comes back it will be a second PWM slice written from the same wrap
// interrupt (the RP2040 has only one PWM IRQ).
constexpr int kAudioOutBPin = 2;

// ---------------------------------------------------------------------------
// Potentiometers — voltage dividers into the RP2040 ADC
// ---------------------------------------------------------------------------

constexpr int kPotCutoffPin = 26;  // ADC0 — filter cutoff
constexpr int kPotResoPin   = 28;  // ADC2 — filter resonance
constexpr int kPotTempoPin  = 27;  // ADC1 — tempo

// The RP2040 ADC channels that belong to those GPIOs.
constexpr int kPotCutoffAdc = 0;   // GP26 -> ADC0
constexpr int kPotTempoAdc  = 1;   // GP27 -> ADC1
constexpr int kPotResoAdc   = 2;   // GP28 -> ADC2

// ---------------------------------------------------------------------------
// Keypad — 16 independent Cherry MX switches, active low with internal pull-up
//
// Top row    : GP14 GP12 GP11 GP16 GP17 GP19 GP20 GP21
// Bottom row : GP0  GP1  GP3  GP4  GP5  GP9  GP18 GP22
//
// Index 0..7 is the top row, index 8..15 the bottom row.
// ---------------------------------------------------------------------------

constexpr int kNumKeys = 16;

constexpr int kKeyPins[kNumKeys] = {
    14, 12, 11, 16, 17, 19, 20, 21,  // top row
    0,  1,  3,  4,  5,  9, 18, 22,   // bottom row
};

// ---------------------------------------------------------------------------
// Clock / MIDI output
// ---------------------------------------------------------------------------

// GP8 is the clock port. It has two personalities, switched at runtime:
//   - a 10 ms pulse per sequencer step (or per beat) for pulse-clock gear
//   - a real MIDI clock, because GP8 doubles as UART1 TX: 0xF8 ticks at
//     24 per quarter note plus START / STOP (see midi.cpp)
constexpr int kClockPin = 8;  // GP8

} // namespace cwp

#endif // CINDERWAVE_PINS_H
