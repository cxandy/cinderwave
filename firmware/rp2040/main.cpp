// Cinderwave firmware for the Raspberry Pi Pico (RP2040).
//
// Hardware this build targets:
//
//   TFT    ILI9341 320x240, SPI0 @ 24 MHz, rotation 3
//          SCK=GP6  MOSI=GP7  RST=GP8 (strapped to 3V3)  DC=GP10  CS=GP13
//   Audio  OUT-A: GP15 -> 100 R + 10 nF RC -> 3.5 mm jack
//          OUT-B: GP2 -> 470 R -> 3.5 mm jack   (RESERVED: not driven yet)
//          (a physical pot cross-mixes the two outside the MCU)
//   Pots   cutoff=GP26(ADC0)  reso=GP28(ADC2)  tempo=GP27(ADC1)
//   Keys   16 x Cherry MX in two rows of eight
//   Clock  GP8 clock port: 10 ms pulse per step/beat, or UART1 MIDI clock
//   Flash  pattern + octave + waveform + tempo offset persist in a ring of
//          snapshots at the end of the flash (written when the instrument
//          goes quiet, restored at boot)
//
// Audio path: a PWM slice wraps at the engine sample rate; its wrap interrupt
// renders exactly one synth sample and writes it as the next duty cycle.
// OUT-A carries the filtered voice. OUT-B is not driven.
//
// Note on performance: renderSample() runs the full voice, and the filter
// recomputes its coefficients (a tan()) every sample. On the FPU-less M0+ that
// is the hot path — if you add voices, cache coefficients or drop kSampleRate.
//
// Note on threading: everything that touches the sequencer or the voice is
// funnelled through the audio interrupt. The main loop only reads the pots and
// queues edits; the interrupt applies them. That keeps the DSP state single
// threaded without paying for a lock on the hot path.

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/timer.h"

#include "audio.h"
#include "display.h"
#include "keypad.h"
#include "midi.h"
#include "pins.h"
#include "storage.h"

#include "cinderwave/synth.h"
#include "cinderwave/oscillator.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// ---------------------------------------------------------------------------
// UI layout (320 x 240)
// ---------------------------------------------------------------------------

constexpr int kHeaderH   = 20;
constexpr int kGridY     = 24;
constexpr int kGridX     = 2;
constexpr int kCellW     = 37;
constexpr int kCellH     = 54;
constexpr int kCellGap   = 3;
constexpr int kBarY1     = 142;
constexpr int kBarY2     = 156;
constexpr int kStatusY1  = 172;
constexpr int kStatusY2  = 186;
constexpr int kHintY     = 204;

// ---------------------------------------------------------------------------
// Gesture thresholds
// ---------------------------------------------------------------------------

constexpr uint32_t kTapMaxMs    = 400;  // shorter than this -> toggle gate
constexpr uint32_t kFuncHoldMs  = 900;  // held at least this long -> function layer
// (a hold between kTapMaxMs and kFuncHoldMs toggles the accent)

// Two keys pressed within this window of each other start a chord.
constexpr uint32_t kChordWindowMs = 80;

// A chord only fires once both keys have been held down together for this
// long. Without the overlap requirement, fast step-taps with two fingers
// would randomly toggle play/stop.
constexpr uint32_t kChordHoldMs = 60;

// Width of the clock output pulse: 320 samples at 32 kHz == 10 ms.
constexpr int kClockPulseSamples = 320;

// ---------------------------------------------------------------------------
// App state
// ---------------------------------------------------------------------------

enum class Mode { Step, Func };

// What the GP8 clock port emits.
enum class ClockMode {
    Step,  // one pulse per sequencer step (a 16th-note clock)
    Beat,  // one pulse per beat (every 4 steps)
    Midi,  // UART1 MIDI clock on the same pin: 24 ticks per beat
    Off,
};

cwf::Display   g_display;
cwf::Keypad    g_keypad;
cwa::AudioOut  g_audio;
cw::Synth      g_synth(cw::kSampleRate);

// Main-loop copy of the pattern. The application is the only editor, so this
// stays authoritative for the UI while the interrupt keeps the sequencer in
// sync.
cw::Step g_pattern[cw::kNumSteps];

int       g_selectedStep = 0;
int       g_octave       = 0;   // net number of octave transpositions
Mode      g_mode         = Mode::Step;
int       g_funcModifier = -1;  // key index currently held as the modifier
bool      g_running      = true;
ClockMode g_clockMode    = ClockMode::Step;
uint32_t  g_playStep     = 0;

// Clock output state, written by the audio interrupt.
int       g_clockCountdown = 0;   // samples left in the current pulse
uint32_t  g_lastClockPos   = 0;

// MIDI clock phase in [0, 1) — one tick every time it wraps, written and read
// only by the audio interrupt.
float g_midiPhase = 0.0f;

// Pot values, written by the main loop and picked up by the audio interrupt.
volatile float g_potCutoff = 520.0f;
volatile float g_potReso   = 9.0f;
volatile float g_potTempo  = 130.0f;
float g_lastCutoff = -1.0f;
float g_lastReso   = -1.0f;
float g_lastTempo  = -1.0f;

// Offset added to the tempo pot, so the TEMPO function can re-zero the pot
// without the next reading immediately overriding it.
float g_tempoOffset = 0.0f;

// Raw (uncalibrated) tempo pot reading, 0..1.
float g_potTempoRaw = 0.5f;

// Set by the interrupt when the pattern was replaced wholesale, so the main
// loop can refresh its display copy.
volatile bool g_resyncPattern = false;

// Main-loop time, refreshed every loop iteration. Used to expire transient
// status messages without threading absolute_time_t through the UI helpers.
uint32_t g_nowMs = 0;

// Transient status message shown in the hint line (e.g. after a save).
char     g_statusMsg[24] = "";
uint32_t g_statusMsgUntilMs = 0;
uint32_t g_statusMsgGen = 0;      // bumped on every new message

// Pattern persistence. The pattern is written back to the flash whenever the
// instrument goes quiet, so it survives a power cycle.
bool     g_savePending = false;
uint32_t g_saveDirtyMs = 0;

// ---------------------------------------------------------------------------
// Edit queue: main loop -> audio interrupt
// ---------------------------------------------------------------------------

enum ActionKind : uint8_t {
    kActToggleGate = 0,
    kActToggleAccent,
    kActToggleSlide,
    kActNudgeNote,
    kActTranspose,
    kActSetWave,
    kActClearPattern,
    kActLoadDemo,
    kActResetAll,
    kActReverse,
    kActStartSeq,
    kActStopSeq,
};

struct Action {
    uint8_t kind;
    uint8_t step;
    int16_t value;
};

constexpr int kActionQueueSize = 16;
Action g_actions[kActionQueueSize];
volatile uint32_t g_actionHead = 0;
volatile uint32_t g_actionTail = 0;

void queueAction(ActionKind kind, int step = 0, int value = 0) {
    const uint32_t next = (g_actionTail + 1u) % kActionQueueSize;
    if (next == g_actionHead) return;  // full: drop
    g_actions[g_actionTail].kind = static_cast<uint8_t>(kind);
    g_actions[g_actionTail].step = static_cast<uint8_t>(step);
    g_actions[g_actionTail].value = static_cast<int16_t>(value);
    g_actionTail = next;
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

int textWidth(const char* s) {
    return static_cast<int>(strlen(s)) * 6 - 1;
}

void drawTextRight(int xRight, int y, const char* s, uint16_t fg, uint16_t bg) {
    g_display.drawText(xRight - textWidth(s), y, s, fg, bg);
}

const char* noteName(uint8_t note) {
    static const char* kNames[12] = {"C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B"};
    static char buf[8];
    const int octave = static_cast<int>(note) / 12 - 1;
    snprintf(buf, sizeof(buf), "%s%d", kNames[note % 12], octave);
    return buf;
}

const char* waveName(cw::Waveform w) {
    switch (w) {
        case cw::Waveform::Saw:     return "SAW";
        case cw::Waveform::Square:  return "SQR";
        case cw::Waveform::Triangle: return "TRI";
        case cw::Waveform::Sine:    return "SIN";
    }
    return "???";
}

const char* clockModeText() {
    switch (g_clockMode) {
        case ClockMode::Step: return "CLK STP";
        case ClockMode::Beat: return "CLK BET";
        case ClockMode::Midi: return "CLK MID";
        case ClockMode::Off:  return "CLK OFF";
    }
    return "CLK ???";
}

// Map a normalized pot to an exponential range (musical for frequency/tempo).
cw::Sample expmap(cw::Sample norm, cw::Sample lo, cw::Sample hi) {
    const cw::Sample n = cw::clampf(norm, 0.0f, 1.0f);
    return lo * std::pow(hi / lo, n);
}

// Show a transient message in the hint line for ~1.2 s.
void postStatus(const char* s) {
    snprintf(g_statusMsg, sizeof(g_statusMsg), "%s", s);
    g_statusMsgUntilMs = g_nowMs + 1200;
    ++g_statusMsgGen;
}

// ---------------------------------------------------------------------------
// Pattern persistence
// ---------------------------------------------------------------------------

// An edit happened: remember it so that it reaches the flash.
void markPatternDirty() {
    g_savePending = true;
    g_saveDirtyMs = g_nowMs;
}

// Writes the current pattern to the flash. This blocks: erasing a sector
// takes ~45 ms, during which the audio interrupt is held off, so it is only
// done while nothing is sounding.
bool savePatternNow() {
    cws::SavedState st{};
    for (int i = 0; i < cw::kNumSteps; ++i) st.steps[i] = g_pattern[i];
    st.octave = static_cast<int8_t>(g_octave);
    st.wave = static_cast<uint8_t>(g_synth.voice().patch().wave);
    st.tempoOffset = g_tempoOffset;

    const bool ok = cws::save(st);
    g_savePending = false;
    postStatus(ok ? "PATTERN SAVED" : "SAVE FAILED");
    return ok;
}

// ---------------------------------------------------------------------------
// Audio interrupt
// ---------------------------------------------------------------------------

void applyAction(const Action& a) {
    cw::Sequencer& seq = g_synth.sequencer();

    switch (static_cast<ActionKind>(a.kind)) {
        case kActToggleGate: {
            cw::Step s = seq.step(a.step);
            s.gate = !s.gate;
            seq.setStep(a.step, s);
            break;
        }
        case kActToggleAccent: {
            cw::Step s = seq.step(a.step);
            s.accent = !s.accent;
            seq.setStep(a.step, s);
            break;
        }
        case kActToggleSlide: {
            cw::Step s = seq.step(a.step);
            s.slide = !s.slide;
            seq.setStep(a.step, s);
            break;
        }
        case kActNudgeNote: {
            cw::Step s = seq.step(a.step);
            int note = static_cast<int>(s.note) + a.value;
            if (note < 0) note = 0;
            if (note > 127) note = 127;
            s.note = static_cast<uint8_t>(note);
            seq.setStep(a.step, s);
            break;
        }
        case kActTranspose: {
            for (int i = 0; i < cw::kNumSteps; ++i) {
                cw::Step s = seq.step(i);
                int note = static_cast<int>(s.note) + a.value;
                if (note < 0) note = 0;
                if (note > 127) note = 127;
                s.note = static_cast<uint8_t>(note);
                seq.setStep(i, s);
            }
            break;
        }
        case kActSetWave: {
            cw::Patch p = g_synth.voice().patch();
            p.wave = static_cast<cw::Waveform>(a.value);
            g_synth.voice().setPatch(p);
            break;
        }
        case kActClearPattern:
            seq.clear();
            break;
        case kActLoadDemo:
            g_synth.loadDemoPattern();
            g_resyncPattern = true;
            g_lastCutoff = -1.0f;
            g_lastReso = -1.0f;
            g_lastTempo = -1.0f;
            break;
        case kActResetAll:
            g_synth.loadDemoPattern();
            g_resyncPattern = true;
            g_lastCutoff = -1.0f;
            g_lastReso = -1.0f;
            g_lastTempo = -1.0f;
            break;
        case kActReverse: {
            // Mirror the pattern: step i becomes step 15 - i.
            cw::Step tmp[cw::kNumSteps];
            for (int i = 0; i < cw::kNumSteps; ++i) {
                tmp[i] = seq.step(cw::kNumSteps - 1 - i);
            }
            for (int i = 0; i < cw::kNumSteps; ++i) {
                seq.setStep(i, tmp[i]);
            }
            break;
        }
        case kActStartSeq:
            seq.start();
            if (g_clockMode == ClockMode::Midi) {
                g_midiPhase = 1.0f;  // the first tick fires right after START
                cwm::sendStart();
            }
            break;
        case kActStopSeq:
            seq.stop();
            g_synth.voice().noteOff();
            if (g_clockMode == ClockMode::Midi) cwm::sendStop();
            break;
    }
}

// Called at the engine sample rate from the PWM wrap interrupt.
cw::Sample renderSample() {
    // Apply queued edits and pending pot values first: everything below runs
    // in this one context, so the DSP state is never touched concurrently.
    while (g_actionHead != g_actionTail) {
        const Action a = g_actions[g_actionHead];
        g_actionHead = (g_actionHead + 1u) % kActionQueueSize;
        applyAction(a);
    }

    if (g_potCutoff != g_lastCutoff || g_potReso != g_lastReso) {
        cw::Patch p = g_synth.voice().patch();
        p.cutoff = g_potCutoff;
        p.resonance = g_potReso;
        g_synth.voice().setPatch(p);
        g_lastCutoff = g_potCutoff;
        g_lastReso = g_potReso;
    }
    if (g_potTempo != g_lastTempo) {
        g_synth.sequencer().setTempo(g_potTempo);
        g_lastTempo = g_potTempo;
    }

    const cw::Sample out = g_synth.renderSample();

    // External clock port on GP8. Two personalities:
    //   Step/Beat — a 10 ms pulse per step (or per beat) for pulse-clock gear.
    //   Midi      — UART1 TX on the same pin: 24 MIDI clock ticks per beat,
    //               spread evenly across each step.
    if (g_clockMode == ClockMode::Midi) {
        // 6 ticks per 16th-note step. The increment is derived from the live
        // tempo (samplesPerStep = 15 * sampleRate / bpm), so tempo changes
        // stay in sync without losing phase.
        const float bpm = g_synth.sequencer().tempo();
        if (bpm > 1.0f) {
            g_midiPhase +=
                6.0f * bpm / (15.0f * static_cast<float>(cw::kSampleRate));
            while (g_midiPhase >= 1.0f) {
                g_midiPhase -= 1.0f;
                cwm::sendTick();
            }
        }
    } else if (g_clockMode != ClockMode::Off) {
        const uint32_t pos = static_cast<uint32_t>(g_synth.sequencer().position());
        if (pos != g_lastClockPos) {
            g_lastClockPos = pos;
            const bool beat = (pos % 4u) == 0u;
            if (g_clockMode == ClockMode::Step || beat) {
                g_clockCountdown = kClockPulseSamples;
                gpio_put(cwp::kClockPin, 1);
            }
        } else if (g_clockCountdown > 0 && --g_clockCountdown == 0) {
            gpio_put(cwp::kClockPin, 0);
        }
    } else if (g_clockCountdown > 0) {
        gpio_put(cwp::kClockPin, 0);
        g_clockCountdown = 0;
    }

    return out;
}

// ---------------------------------------------------------------------------
// Key handling
// ---------------------------------------------------------------------------

const char* const kFuncLabels[16] = {
    "PLAY", "ACCT", "SLIDE", "OCT-", "OCT+", "WAVE", "TEMPO", "CLEAR",
    "SEL-", "SEL+", "NOTE-", "NOTE+", "CLOCK", "REV", "SAVE", "RESET",
};

void togglePlay() {
    g_running = !g_running;
    // The sequencer is only ever touched from the audio interrupt, so even
    // start/stop goes through the action queue.
    if (g_running) {
        queueAction(kActStartSeq);
    } else {
        queueAction(kActStopSeq);
    }
}

void syncPatternFromSequencer() {
    for (int i = 0; i < cw::kNumSteps; ++i) {
        g_pattern[i] = g_synth.sequencer().step(i);
    }
}

void runFunction(int index) {
    switch (index) {
        case 0:  // PLAY
            togglePlay();
            break;
        case 1:  // ACCT
            g_pattern[g_selectedStep].accent = !g_pattern[g_selectedStep].accent;
            queueAction(kActToggleAccent, g_selectedStep);
            markPatternDirty();
            break;
        case 2:  // SLIDE
            g_pattern[g_selectedStep].slide = !g_pattern[g_selectedStep].slide;
            queueAction(kActToggleSlide, g_selectedStep);
            markPatternDirty();
            break;
        case 3:  // OCT-
            if (g_octave > -3) {
                --g_octave;
                for (int i = 0; i < cw::kNumSteps; ++i) {
                    int note = static_cast<int>(g_pattern[i].note) - 12;
                    if (note < 0) note = 0;
                    g_pattern[i].note = static_cast<uint8_t>(note);
                }
                queueAction(kActTranspose, 0, -12);
            }
            markPatternDirty();
            break;
        case 4:  // OCT+
            if (g_octave < 3) {
                ++g_octave;
                for (int i = 0; i < cw::kNumSteps; ++i) {
                    int note = static_cast<int>(g_pattern[i].note) + 12;
                    if (note > 127) note = 127;
                    g_pattern[i].note = static_cast<uint8_t>(note);
                }
                queueAction(kActTranspose, 0, 12);
            }
            markPatternDirty();
            break;
        case 5: {  // WAVE
            const int next = (static_cast<int>(g_synth.voice().patch().wave) + 1) % 4;
            queueAction(kActSetWave, 0, next);
            markPatternDirty();
            break;
        }
        case 6:  // TEMPO
            // Re-zero the tempo pot at its current position so the readout
            // becomes 130 BPM. Without the offset the next pot reading would
            // immediately override this.
            g_tempoOffset = 130.0f - (60.0f + g_potTempoRaw * 120.0f);
            markPatternDirty();
            break;
        case 7:  // CLEAR
            for (int i = 0; i < cw::kNumSteps; ++i) g_pattern[i] = cw::Step{};
            queueAction(kActClearPattern);
            markPatternDirty();
            break;
        case 8:  // SEL-
            g_selectedStep = (g_selectedStep + cw::kNumSteps - 1) % cw::kNumSteps;
            break;
        case 9:  // SEL+
            g_selectedStep = (g_selectedStep + 1) % cw::kNumSteps;
            break;
        case 10:  // NOTE-
            if (g_pattern[g_selectedStep].note > 0) {
                --g_pattern[g_selectedStep].note;
                queueAction(kActNudgeNote, g_selectedStep, -1);
            }
            markPatternDirty();
            break;
        case 11:  // NOTE+
            if (g_pattern[g_selectedStep].note < 127) {
                ++g_pattern[g_selectedStep].note;
                queueAction(kActNudgeNote, g_selectedStep, 1);
            }
            markPatternDirty();
            break;
        case 12: {  // CLOCK
            // Cycle the GP8 clock port: step -> beat -> midi -> off -> step.
            switch (g_clockMode) {
                case ClockMode::Step: g_clockMode = ClockMode::Beat; break;
                case ClockMode::Beat: g_clockMode = ClockMode::Midi; break;
                case ClockMode::Midi: g_clockMode = ClockMode::Off;  break;
                case ClockMode::Off:  g_clockMode = ClockMode::Step; break;
            }
            // GP8 changes personality with the mode: plain GPIO for pulses,
            // UART1 TX for MIDI.
            cwm::setPinMidi(g_clockMode == ClockMode::Midi);
            if (g_clockMode == ClockMode::Midi) g_midiPhase = 0.0f;
            break;
        }
        case 13:  // REV
            for (int i = 0; i < cw::kNumSteps; ++i) {
                g_pattern[i] = g_pattern[cw::kNumSteps - 1 - i];
            }
            queueAction(kActReverse);
            markPatternDirty();
            break;
        case 14:  // SAVE — persist the pattern to flash
            // Writes it right now, even while the sequencer is playing. The
            // flash write stalls with interrupts off for up to ~45 ms, so the
            // audio briefly mutes — that is the price of an explicit save.
            savePatternNow();
            break;
        case 15:  // RESET
            g_octave = 0;
            g_selectedStep = 0;
            g_clockMode = ClockMode::Step;
            cwm::setPinMidi(false);
            queueAction(kActResetAll);
            break;
    }
}

// ---------------------------------------------------------------------------
// Potentiometers
// ---------------------------------------------------------------------------

cw::Sample readPot(int adcChannel) {
    adc_select_input(adcChannel);

    // The RP2040 ADC needs a moment after switching channels and carries the
    // previous reading into the first conversion, so throw one sample away
    // and average the next few.
    (void)adc_read();

    int32_t acc = 0;
    for (int i = 0; i < 4; ++i) {
        acc += static_cast<int32_t>(adc_read());
    }
    return static_cast<cw::Sample>(acc) / (4095.0f * 4.0f);
}

// ---------------------------------------------------------------------------
// Drawing — dirty-rect updates
// ---------------------------------------------------------------------------
//
// A full-screen repaint pushes ~153 KB over SPI at 24 MHz, which takes about
// 51 ms, stalls the keypad poll and makes the panel flicker. Instead the UI
// keeps a shadow of everything it shows and redraws only the regions that
// actually changed. A typical frame moves the playhead and maybe one text:
// a few KB, roughly a millisecond.

struct UiShadow {
    bool     valid        = false;
    Mode     mode         = Mode::Step;
    int      bpmTenths    = -1;
    bool     running      = true;
    cw::Step pattern[cw::kNumSteps];
    int      selectedStep = -1;
    int      selectedNote = -1;
    uint32_t playStep     = 0xFFFFFFFFu;
    int      cutoffPct    = -1;
    int      resoPct      = -1;
    int      wave         = -1;
    int      octave       = -999;
    int      clockMode    = -1;
    int      hintDrawn    = -1;    // -1: hint shown; >= 0: status message gen
};

UiShadow g_shadow;

void drawCell(int i) {
    const int col = i % 8;
    const int row = i / 8;
    const int x = kGridX + col * (kCellW + kCellGap);
    const int y = kGridY + row * (kCellH + kCellGap);

    if (g_mode == Mode::Func) {
        g_display.fillRect(x, y, kCellW, kCellH, cwf::color::kDarkGrey);
        const char* label = kFuncLabels[i];
        g_display.drawText(x + (kCellW - textWidth(label)) / 2,
                           y + (kCellH - 8) / 2, label,
                           cwf::color::kWhite, cwf::color::kDarkGrey);
        return;
    }

    const cw::Step& st = g_pattern[i];
    const bool on = st.gate;
    const uint16_t bg = on ? cwf::color::kGreen : cwf::color::kDarkGrey;
    const uint16_t fg = on ? cwf::color::kBlack : cwf::color::kWhite;

    g_display.fillRect(x, y, kCellW, kCellH, bg);

    char num[3];
    snprintf(num, sizeof(num), "%d", i + 1);
    g_display.drawText(x + 4, y + 4, num, fg, bg);

    if (st.accent) {
        g_display.fillRect(x + kCellW - 9, y + 4, 6, 6, cwf::color::kRed);
    }
    if (st.slide) {
        g_display.fillRect(x + 9, y + kCellH - 8, kCellW - 18, 3, cwf::color::kYellow);
    }
    if (i == g_selectedStep) {
        g_display.fillRect(x + 4, y + kCellH - 7, 5, 5, cwf::color::kWhite);
    }
    if (i == static_cast<int>(g_playStep)) {
        g_display.drawRect(x, y, kCellW, kCellH, cwf::color::kCyan);
    }
}

void drawAllCells() {
    for (int i = 0; i < cw::kNumSteps; ++i) drawCell(i);
}

// The right-hand part of the header (BPM + run state) is drawn together,
// because the RUN/STOP position depends on the width of the BPM text.
void drawHeaderRight() {
    g_display.fillRect(226, 4, 90, 12, cwf::color::kMidGrey);
    char bpm[16];
    snprintf(bpm, sizeof(bpm), "%.1f BPM",
             static_cast<double>(g_synth.sequencer().tempo()));
    drawTextRight(316, 6, bpm, cwf::color::kBlack, cwf::color::kMidGrey);
    const char* state = g_running ? "RUN" : "STOP";
    const uint16_t stateColor = g_running ? cwf::color::kGreen : cwf::color::kRed;
    drawTextRight(316 - textWidth(bpm) - 10, 6, state, stateColor, cwf::color::kMidGrey);
}

void drawHint() {
    g_display.fillRect(0, kHintY - 2, 320, 12, cwf::color::kBlack);
    const char* hint = (g_mode == Mode::Func)
        ? "FUNC: TAP A KEY"
        : "TAP=STEP  2KEYS=PLAY  HOLD=FUNC";
    g_display.drawText((320 - textWidth(hint)) / 2, kHintY, hint,
                       cwf::color::kGrey, cwf::color::kBlack);
}

// Full repaint — boot only, and only the static chrome. Every dynamic value
// is painted by the dirty checks in updateUi() on the first pass, because
// their shadow entries start at "unset" sentinel values.
void drawAllUi() {
    g_display.fillScreen(cwf::color::kBlack);

    g_display.fillRect(0, 0, 320, kHeaderH, cwf::color::kMidGrey);
    g_display.drawText(4, 6, "CINDERWAVE", cwf::color::kBlack, cwf::color::kMidGrey);

    g_display.drawText(2, kBarY1 + 1, "CUTOFF", cwf::color::kWhite, cwf::color::kBlack);
    g_display.drawText(2, kBarY2 + 1, "RESO", cwf::color::kWhite, cwf::color::kBlack);

    drawAllCells();
    drawHint();

    g_display.drawText(2, kStatusY2, "GP2", cwf::color::kWhite, cwf::color::kBlack);
    g_display.drawText(48, kStatusY2, "FREE", cwf::color::kMidGrey, cwf::color::kBlack);
    drawTextRight(316, kStatusY2, "MIX POT = A/B", cwf::color::kMidGrey, cwf::color::kBlack);
}

// Incremental update: called at ~16 Hz, redraws only what changed.
void updateUi() {
    UiShadow& s = g_shadow;

    const bool selChanged = (g_selectedStep != s.selectedStep);

    if (!s.valid) {
        drawAllUi();
        s.valid        = true;
        s.mode         = g_mode;
        s.playStep     = g_playStep;
        s.selectedStep = g_selectedStep;
        s.selectedNote = g_pattern[g_selectedStep].note;
        for (int i = 0; i < cw::kNumSteps; ++i) s.pattern[i] = g_pattern[i];
        // Dynamic texts and bars fall through to the dirty checks below.
    }

    // ---- header: BPM + run state -----------------------------------------
    const int bpmTenths =
        static_cast<int>(g_synth.sequencer().tempo() * 10.0f + 0.5f);
    if (bpmTenths != s.bpmTenths || g_running != s.running) {
        drawHeaderRight();
        s.bpmTenths = bpmTenths;
        s.running   = g_running;
    }

    // ---- grid --------------------------------------------------------------
    if (g_mode != s.mode) {
        drawAllCells();
        s.mode         = g_mode;
        s.playStep     = g_playStep;
        s.selectedStep = g_selectedStep;
        s.hintDrawn    = -2;  // the hint text changes with the mode
        for (int i = 0; i < cw::kNumSteps; ++i) s.pattern[i] = g_pattern[i];
    } else if (g_mode == Mode::Step) {
        const int prevPlay = static_cast<int>(s.playStep);
        const int nowPlay  = static_cast<int>(g_playStep);
        for (int i = 0; i < cw::kNumSteps; ++i) {
            const cw::Step& a = g_pattern[i];
            const cw::Step& b = s.pattern[i];
            const bool changed = a.gate != b.gate || a.accent != b.accent ||
                                 a.slide != b.slide || a.note != b.note;
            const bool play = (i == nowPlay) != (i == prevPlay);
            const bool sel  = (i == g_selectedStep) != (i == s.selectedStep);
            if (changed || play || sel) drawCell(i);
            s.pattern[i] = a;
        }
        s.playStep     = g_playStep;
        s.selectedStep = g_selectedStep;
    }

    // ---- parameter bars ----------------------------------------------------
    const int cutoffPct =
        static_cast<int>(g_potCutoff / 6000.0f * 200.0f + 0.5f);
    if (cutoffPct != s.cutoffPct) {
        g_display.drawBar(48, kBarY1, 110, 10,
                          g_potCutoff / 6000.0f, cwf::color::kCyan,
                          cwf::color::kDarkGrey);
        s.cutoffPct = cutoffPct;
    }

    const int resoPct =
        static_cast<int>((g_potReso - 0.7f) / 17.3f * 200.0f + 0.5f);
    if (resoPct != s.resoPct) {
        g_display.drawBar(48, kBarY2, 110, 10,
                          (g_potReso - 0.7f) / 17.3f, cwf::color::kMagenta,
                          cwf::color::kDarkGrey);
        s.resoPct = resoPct;
    }

    const int wave = static_cast<int>(g_synth.voice().patch().wave);
    if (wave != s.wave) {
        g_display.fillRect(166, kBarY1 - 2, 60, 12, cwf::color::kBlack);
        g_display.drawText(170, kBarY1 + 1,
                           waveName(g_synth.voice().patch().wave),
                           cwf::color::kYellow, cwf::color::kBlack);
        s.wave = wave;
    }

    if (g_octave != s.octave) {
        char oct[12];
        snprintf(oct, sizeof(oct), "OCT %+d", g_octave);
        g_display.fillRect(166, kBarY2 - 2, 60, 12, cwf::color::kBlack);
        g_display.drawText(170, kBarY2 + 1, oct,
                           cwf::color::kWhite, cwf::color::kBlack);
        s.octave = g_octave;
    }

    // ---- status line -------------------------------------------------------
    if (selChanged ||
        g_pattern[g_selectedStep].note != s.selectedNote) {
        char line[24];
        snprintf(line, sizeof(line), "STEP %02d", g_selectedStep + 1);
        g_display.drawText(2, kStatusY1, line,
                           cwf::color::kWhite, cwf::color::kBlack);
        g_display.fillRect(68, kStatusY1 - 2, 60, 12, cwf::color::kBlack);
        g_display.drawText(70, kStatusY1,
                           noteName(g_pattern[g_selectedStep].note),
                           cwf::color::kGreen, cwf::color::kBlack);
        s.selectedStep = g_selectedStep;
        s.selectedNote = g_pattern[g_selectedStep].note;
    }

    const int clockMode = static_cast<int>(g_clockMode);
    if (clockMode != s.clockMode) {
        g_display.fillRect(268, kStatusY1 - 2, 50, 12, cwf::color::kBlack);
        const uint16_t clkColor = g_clockMode == ClockMode::Off ? cwf::color::kMidGrey
                                : g_clockMode == ClockMode::Midi ? cwf::color::kCyan
                                                                 : cwf::color::kOrange;
        drawTextRight(316, kStatusY1, clockModeText(), clkColor,
                      cwf::color::kBlack);
        s.clockMode = clockMode;
    }

    // ---- hint / transient status message ----------------------------------
    const bool msgActive = g_statusMsgGen != 0 && g_statusMsgUntilMs > g_nowMs;
    const int wantHint = msgActive ? static_cast<int>(g_statusMsgGen) : -1;
    if (wantHint != s.hintDrawn) {
        if (msgActive) {
            g_display.fillRect(0, kHintY - 2, 320, 12, cwf::color::kBlack);
            g_display.drawText((320 - textWidth(g_statusMsg)) / 2, kHintY,
                               g_statusMsg, cwf::color::kWhite,
                               cwf::color::kBlack);
        } else {
            drawHint();
        }
        s.hintDrawn = wantHint;
    }
}

} // namespace

int main() {
    stdio_init_all();

    // ---- display ---------------------------------------------------------
    g_display.init();

    // ---- keypad ----------------------------------------------------------
    g_keypad.init();

    // ---- pots ------------------------------------------------------------
    adc_init();
    adc_gpio_init(cwp::kPotCutoffPin);
    adc_gpio_init(cwp::kPotTempoPin);
    adc_gpio_init(cwp::kPotResoPin);

    // ---- external clock output -------------------------------------------
    // GP8 drives a short pulse on every step (or beat) so another machine
    // can follow this one. The TFT reset line is strapped to 3V3, so this pin
    // is free.
    gpio_init(cwp::kClockPin);
    gpio_set_dir(cwp::kClockPin, GPIO_OUT);
    gpio_put(cwp::kClockPin, 0);

    // ---- MIDI clock --------------------------------------------------------
    // UART1 is set up at boot; the GP8 pin stays a plain GPIO output until
    // the CLOCK function is switched to MIDI.
    cwm::init();

    // ---- pattern ---------------------------------------------------------
    // Restore the last saved pattern from flash; fall back to the demo
    // pattern on fresh or unreadable flash. At this point the audio
    // interrupt is not running yet, so the sequencer can be touched directly.
    cws::SavedState saved{};
    if (cws::load(saved)) {
        for (int i = 0; i < cw::kNumSteps; ++i) {
            g_synth.sequencer().setStep(i, saved.steps[i]);
        }
        if (saved.wave <= 3) {
            cw::Patch p = g_synth.voice().patch();
            p.wave = static_cast<cw::Waveform>(saved.wave);
            g_synth.voice().setPatch(p);
        }
        g_octave = saved.octave;
        g_tempoOffset = saved.tempoOffset;
        postStatus("PATTERN LOADED");
    } else {
        g_synth.loadDemoPattern();
        // Nothing stored yet: store the demo pattern on the first quiet
        // moment, so the flash holds a valid record from now on.
        markPatternDirty();
    }
    g_synth.sequencer().setTempo(130.0f);
    g_synth.sequencer().start();
    syncPatternFromSequencer();

    // ---- audio -----------------------------------------------------------
    g_audio.init(renderSample);

    updateUi();

    // Scheduling deadlines. The Pico SDK 2.x time API is timestamp based, so
    // these are absolute_time_t rather than plain counters.
    absolute_time_t nextPoll = get_absolute_time();
    absolute_time_t nextPot  = nextPoll;
    uint32_t nextUiMs = to_ms_since_boot(nextPoll);

    uint32_t pressStartMs[16] = {};

    // Chord detection state. A second key inside the chord window only opens
    // a *pending* chord; it becomes play/stop once both keys are still held
    // after kChordHoldMs. If either key is released first the chord is
    // cancelled and both keys fall back to normal tap/hold handling, so
    // quick step-taps never trigger play/stop.
    int      g_chordFirst    = -1;  // first key of a potential chord
    uint32_t g_chordFirstMs  = 0;   // when it went down
    bool     g_chordPending  = false;
    int      g_chordSecond   = -1;  // second key
    uint32_t g_chordStartMs  = 0;   // when both were down at once
    uint32_t g_chordConsumed = 0;

    while (true) {
        const absolute_time_t now = get_absolute_time();
        const uint32_t nowMs = to_ms_since_boot(now);
        g_nowMs = nowMs;

        // ---- keypad: drain the event queue --------------------------------
        if (absolute_time_diff_us(now, nextPoll) >= 0) {
            nextPoll = delayed_by_us(now, 1000);  // 1 kHz
            g_keypad.poll();

            cwf::KeyEvent ev;
            while (g_keypad.nextEvent(ev)) {
                const int index = ev.index;
                if (ev.down) {
                    pressStartMs[index] = nowMs;

                    // A second key inside the chord window opens a pending
                    // chord; confirmation happens in the poll check below.
                    if (!g_chordPending && g_chordFirst >= 0 &&
                        g_chordFirst != index &&
                        (nowMs - g_chordFirstMs) <= kChordWindowMs) {
                        g_chordPending = true;
                        g_chordSecond  = index;
                        g_chordStartMs = nowMs;
                    } else if (!g_chordPending) {
                        g_chordFirst    = index;
                        g_chordFirstMs  = nowMs;
                    }
                } else {
                    if (g_chordPending &&
                        (index == g_chordFirst || index == g_chordSecond)) {
                        // A chord key went up before the overlap window
                        // closed: not a chord. Cancel it and fall through so
                        // this release is processed as a normal tap or hold.
                        g_chordPending = false;
                        g_chordSecond  = -1;
                    } else if (index == g_chordFirst) {
                        g_chordFirst = -1;
                    }
                    if (g_chordConsumed & (1u << index)) {
                        g_chordConsumed &= ~(1u << index);
                        continue;  // this press belonged to a chord
                    }

                    const uint32_t held = nowMs - pressStartMs[index];

                    if (index == g_funcModifier) {
                        // The modifier key itself: leaving the function layer.
                        g_funcModifier = -1;
                        g_mode = Mode::Step;
                        continue;
                    }

                    if (g_mode == Mode::Func) {
                        runFunction(index);
                        continue;
                    }

                    if (held < kTapMaxMs) {
                        // Tap: toggle the gate of this step and select it.
                        g_pattern[index].gate = !g_pattern[index].gate;
                        g_selectedStep = index;
                        queueAction(kActToggleGate, index);
                        markPatternDirty();
                    } else if (held < kFuncHoldMs) {
                        // Medium hold: toggle the accent of this step.
                        g_pattern[index].accent = !g_pattern[index].accent;
                        queueAction(kActToggleAccent, index);
                        markPatternDirty();
                    }
                }
            }

            // Confirm a pending chord: after the overlap window both keys
            // must still be down. Then it is play/stop, and both keys are
            // consumed so their releases do not toggle a step.
            if (g_chordPending && (nowMs - g_chordStartMs) >= kChordHoldMs) {
                const uint32_t mask = g_keypad.pressedMask();
                if (((mask >> g_chordFirst) & 1u) &&
                    ((mask >> g_chordSecond) & 1u)) {
                    g_chordConsumed |= (1u << g_chordFirst) | (1u << g_chordSecond);
                    togglePlay();
                    g_chordPending = false;
                    g_chordSecond  = -1;
                    g_chordFirst   = -1;
                }
            }

            // A key held long enough becomes the function modifier.
            if (g_funcModifier < 0) {
                const uint32_t mask = g_keypad.pressedMask();
                for (int i = 0; i < 16; ++i) {
                    if ((mask >> i) & 1u) {
                        if (g_chordConsumed & (1u << i)) continue;
                        if (nowMs - pressStartMs[i] >= kFuncHoldMs) {
                            g_funcModifier = i;
                            g_mode = Mode::Func;
                            break;
                        }
                    }
                }
            }
        }

        // ---- pots ----------------------------------------------------------
        if (absolute_time_diff_us(now, nextPot) >= 0) {
            nextPot = delayed_by_us(now, 5000);  // 200 Hz

            g_potCutoff = expmap(readPot(cwp::kPotCutoffAdc), 120.0f, 6000.0f);
            g_potReso   = 0.7f + readPot(cwp::kPotResoAdc) * 17.3f;   // 0.7 .. 18
            g_potTempoRaw = readPot(cwp::kPotTempoAdc);
            const float bpm = 60.0f + g_potTempoRaw * 120.0f + g_tempoOffset;
            g_potTempo = cw::clampf(bpm, 40.0f, 220.0f);
        }

        // ---- pick up a pattern that was replaced wholesale -------------------
        if (g_resyncPattern) {
            g_resyncPattern = false;
            syncPatternFromSequencer();
            // RESET replaces the pattern in the audio interrupt, so this is
            // the first moment where the new pattern is known.
            markPatternDirty();
        }

        // ---- pattern persistence -------------------------------------------
        // Writing to the flash takes the chip out of execute-in-place mode,
        // so it may only happen while nothing is sounding: the sequencer is
        // stopped, or the step that is currently playing is silent. If the
        // pattern never goes quiet, the 10 s fallback still gets it stored.
        if (g_savePending) {
            const uint32_t pos = static_cast<uint32_t>(g_synth.sequencer().position());
            const bool quiet = !g_running ||
                               (pos < cw::kNumSteps && !g_pattern[pos].gate);
            const bool stale = static_cast<int32_t>(nowMs - g_saveDirtyMs) > 10000;
            if (quiet || stale) {
                savePatternNow();
            }
        }

        // ---- UI ------------------------------------------------------------
        g_playStep = static_cast<uint32_t>(g_synth.sequencer().position());
        if (static_cast<int32_t>(nowMs - nextUiMs) >= 0) {
            nextUiMs = nowMs + 60;  // ~16 Hz
            updateUi();
        }

        tight_loop_contents();
    }
}
