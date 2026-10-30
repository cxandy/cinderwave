#include "midi.h"

#include "pins.h"

#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"

namespace cwm {
namespace {

constexpr uint32_t kBaud = 31250;

constexpr uint8_t kMidiStart = 0xFA;
constexpr uint8_t kMidiStop  = 0xFC;
constexpr uint8_t kMidiTick  = 0xF8;

// Non-blocking write: drop rather than stall the audio interrupt.
inline void send(uint8_t b) {
    if (uart_writable(uart1)) {
        uart_putc_raw(uart1, b);
    }
}

} // namespace

void init() {
    // 125 MHz peripheral clock / (16 * 31250) = 250 — an exact divider, so
    // the MIDI baud rate has zero error.
    uart_init(uart1, kBaud);
    uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart1, true);
}

void setPinMidi(bool midi) {
    if (midi) {
        gpio_set_function(cwp::kClockPin, GPIO_FUNC_UART);
    } else {
        // Back to the pulse personality: plain output, held low until the
        // next pulse comes along.
        gpio_set_function(cwp::kClockPin, GPIO_FUNC_SIO);
        gpio_set_dir(cwp::kClockPin, GPIO_OUT);
        gpio_put(cwp::kClockPin, 0);
    }
}

void sendStart() { send(kMidiStart); }
void sendStop()  { send(kMidiStop); }
void sendTick()  { send(kMidiTick); }

} // namespace cwm
