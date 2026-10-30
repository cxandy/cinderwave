#include "audio.h"

#include "pins.h"

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"

namespace cwa {
namespace {

// PWM TOP value. The counter runs 0..3905, i.e. 3906 counts of the 125 MHz
// system clock: 125e6 / 3906 = 32002.6 Hz, which matches the 32 kHz engine
// to within 0.008% (the sequencer counts engine samples, so tempo stays
// exact; only the absolute pitch drifts by a hair).
constexpr uint16_t kPwmWrap = 3905;

// Slice / channel for each output.
//
// The RP2040 hardwires PWM slices to GPIO pairs: slice 7 -> GP14 (A) / GP15
// (B), slice 1 -> GP2 (A) / GP3 (B). GP14 and GP3 are keypad buttons on this
// board, which is harmless: a GPIO only sees the PWM output if it asks for
// GPIO_FUNC_PWM, and the slice itself keeps running either way.
uint g_outASlice = 0;
uint g_outAChan = 0;
uint g_outBSlice = 0;
uint g_outBChan = 0;

RenderFn g_render = nullptr;

// Map a [-1, 1] sample onto the unipolar PWM duty range [0, kPwmWrap].
inline uint16_t toLevel(cw::Sample s) {
    const cw::Sample unipolar = s * 0.5f + 0.5f;
    int32_t level = static_cast<int32_t>(unipolar * static_cast<cw::Sample>(kPwmWrap));
    if (level < 0) level = 0;
    if (level > kPwmWrap) level = kPwmWrap;
    return static_cast<uint16_t>(level);
}

// The one hot path: render one sample, push both duty values.
void __not_in_flash_func(on_pwm_wrap)() {
    pwm_clear_irq(g_outASlice);

    AudioFrame frame;
    if (g_render) g_render(frame);

    pwm_set_chan_level(g_outASlice, g_outAChan, toLevel(frame.a));
    pwm_set_chan_level(g_outBSlice, g_outBChan, toLevel(frame.b));
}

} // namespace

void AudioOut::init(RenderFn render) {
    g_render = render;

    gpio_set_function(cwp::kAudioOutAPin, GPIO_FUNC_PWM);
    gpio_set_function(cwp::kAudioOutBPin, GPIO_FUNC_PWM);

    g_outASlice = pwm_gpio_to_slice_num(cwp::kAudioOutAPin);
    g_outAChan = pwm_gpio_to_channel(cwp::kAudioOutAPin);
    g_outBSlice = pwm_gpio_to_slice_num(cwp::kAudioOutBPin);
    g_outBChan = pwm_gpio_to_channel(cwp::kAudioOutBPin);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 1.0f);
    pwm_config_set_wrap(&cfg, kPwmWrap);

    pwm_init(g_outASlice, &cfg, true);
    pwm_init(g_outBSlice, &cfg, true);

    // Silence until the first sample arrives (duty 0 == 0 V, no DC offset).
    pwm_set_chan_level(g_outASlice, g_outAChan, 0);
    pwm_set_chan_level(g_outBSlice, g_outBChan, 0);

    // Only the OUT-A slice raises the interrupt. The RP2040 has a single PWM
    // IRQ shared by all slices, and the handler writes both duty values.
    pwm_clear_irq(g_outASlice);
    pwm_set_irq_enabled(g_outASlice, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, on_pwm_wrap);
    irq_set_enabled(PWM_IRQ_WRAP, true);

    actualRate_ = 125000000u / (static_cast<uint32_t>(kPwmWrap) + 1u);
}

} // namespace cwa
