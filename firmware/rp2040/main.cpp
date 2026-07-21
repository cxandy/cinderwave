// Cinderwave firmware for the Raspberry Pi Pico (RP2040).
//
// Audio path: a PWM slice wraps at the engine sample rate; its wrap interrupt
// renders exactly one synth sample and writes it as the next duty cycle. Feed
// the PWM pin through the RC reconstruction filter from hardware/BUILD.md into a
// 3.5 mm jack. Three pots live-tweak cutoff / resonance / tempo; a button
// toggles the sequencer.
//
// Note on performance: renderSample() runs the full voice, and the filter
// recomputes its coefficients (a tan()) every sample. On the FPU-less M0+ that
// is the hot path — if you add voices, cache coefficients or drop kSampleRate.

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"

#include "cinderwave/synth.h"

#include <cmath>

namespace {

// ---- Pin map (see hardware/BUILD.md) ---------------------------------------
constexpr uint kAudioPin = 0;      // PWM audio out -> RC filter -> jack tip
constexpr uint kPotCutoffAdc = 0;  // GPIO26 / ADC0
constexpr uint kPotResoAdc = 1;    // GPIO27 / ADC1
constexpr uint kPotTempoAdc = 2;   // GPIO28 / ADC2
constexpr uint kBtnPlay = 2;       // to GND, INPUT_PULLUP (active low)

// PWM wrap = sysclk / sampleRate. At 125 MHz / 32 kHz this is ~3906 counts,
// giving close to 12 bits of amplitude resolution.
constexpr uint16_t kPwmWrap = 3905;

cw::Synth g_synth(cw::kSampleRate);
uint g_audioSlice = 0;
uint g_audioChan = 0;

// Rendered in the PWM wrap interrupt — keep it lean.
void __not_in_flash_func(on_pwm_wrap)() {
    pwm_clear_irq(g_audioSlice);
    const cw::Sample s = g_synth.renderSample();          // [-1, 1]
    const cw::Sample unipolar = s * 0.5f + 0.5f;          // [0, 1]
    uint16_t level = static_cast<uint16_t>(unipolar * static_cast<cw::Sample>(kPwmWrap));
    if (level > kPwmWrap) level = kPwmWrap;
    pwm_set_chan_level(g_audioSlice, g_audioChan, level);
}

void init_audio() {
    gpio_set_function(kAudioPin, GPIO_FUNC_PWM);
    g_audioSlice = pwm_gpio_to_slice_num(kAudioPin);
    g_audioChan = pwm_gpio_to_channel(kAudioPin);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 1.0f);
    pwm_config_set_wrap(&cfg, kPwmWrap);
    pwm_init(g_audioSlice, &cfg, true);

    pwm_clear_irq(g_audioSlice);
    pwm_set_irq_enabled(g_audioSlice, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, on_pwm_wrap);
    irq_set_enabled(PWM_IRQ_WRAP, true);
}

void init_controls() {
    adc_init();
    adc_gpio_init(26);
    adc_gpio_init(27);
    adc_gpio_init(28);

    gpio_init(kBtnPlay);
    gpio_set_dir(kBtnPlay, GPIO_IN);
    gpio_pull_up(kBtnPlay);
}

cw::Sample read_pot(uint adcChannel) {
    adc_select_input(adcChannel);
    // 12-bit ADC -> normalized [0, 1].
    return static_cast<cw::Sample>(adc_read()) / 4095.0f;
}

// Map a normalized pot to an exponential range (musical for frequency/tempo).
cw::Sample expmap(cw::Sample norm, cw::Sample lo, cw::Sample hi) {
    const cw::Sample n = cw::clampf(norm, 0.0f, 1.0f);
    // lo * (hi/lo)^n
    return lo * std::pow(hi / lo, n);
}

} // namespace

int main() {
    stdio_init_all();
    init_controls();

    g_synth.loadDemoPattern();
    g_synth.sequencer().start();

    init_audio();

    bool lastBtn = true;          // pulled up = released
    bool running = true;
    absolute_time_t nextPoll = get_absolute_time();

    while (true) {
        // Poll controls ~200 Hz; the audio IRQ keeps running underneath.
        if (absolute_time_diff_us(get_absolute_time(), nextPoll) <= 0) {
            nextPoll = delayed_by_us(get_absolute_time(), 5000);

            cw::Patch p = g_synth.voice().patch();
            p.cutoff = expmap(read_pot(kPotCutoffAdc), 120.0f, 6000.0f);
            p.resonance = 0.7f + read_pot(kPotResoAdc) * 17.3f;   // 0.7 .. 18
            g_synth.voice().setPatch(p);

            const cw::Sample bpm = 90.0f + read_pot(kPotTempoAdc) * 80.0f; // 90 .. 170
            g_synth.sequencer().setTempo(bpm);

            const bool btn = gpio_get(kBtnPlay);                 // active low
            if (lastBtn && !btn) {                               // falling edge
                running = !running;
                if (running) {
                    g_synth.sequencer().start();
                } else {
                    g_synth.sequencer().stop();
                    g_synth.voice().noteOff();
                }
            }
            lastBtn = btn;
        }
        tight_loop_contents();
    }
}
