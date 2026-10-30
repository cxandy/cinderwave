#include "keypad.h"

#include "pins.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"

namespace cwf {
namespace {

inline bool bit(uint32_t mask, int index) { return (mask >> index) & 1u; }

} // namespace

void Keypad::init() {
    for (int i = 0; i < cwp::kNumKeys; ++i) {
        gpio_init(cwp::kKeyPins[i]);
        gpio_set_dir(cwp::kKeyPins[i], GPIO_IN);
        gpio_pull_up(cwp::kKeyPins[i]);
    }

    // Seed the debounce state from a first reading so a key that is already
    // held at boot does not register as a press.
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    for (int i = 0; i < cwp::kNumKeys; ++i) {
        const bool down = (gpio_get(cwp::kKeyPins[i]) == 0);
        if (down) {
            candidate_ |= (1u << i);
            pressed_ |= (1u << i);
        }
        candidateSinceMs_[i] = now;
    }
    head_ = tail_ = 0;
}

void Keypad::poll() {
    const uint32_t now = to_ms_since_boot(get_absolute_time());

    for (int i = 0; i < cwp::kNumKeys; ++i) {
        const bool down = (gpio_get(cwp::kKeyPins[i]) == 0);  // active low

        if (down != bit(candidate_, i)) {
            candidate_ = down ? (candidate_ | (1u << i)) : (candidate_ & ~(1u << i));
            candidateSinceMs_[i] = now;
            continue;
        }

        if (bit(pressed_, i) == down) continue;  // already settled

        if ((now - candidateSinceMs_[i]) < kDebounceMs) continue;

        pressed_ = down ? (pressed_ | (1u << i)) : (pressed_ & ~(1u << i));

        // Queue the edge. The queue is large enough that a full buffer can
        // only happen if the application stops draining it entirely.
        const uint32_t next = (tail_ + 1u) % kQueueSize;
        if (next == head_) break;  // full: drop the edge rather than corrupt
        queue_[tail_].index = static_cast<uint8_t>(i);
        queue_[tail_].down = down;
        tail_ = next;
    }
}

bool Keypad::nextEvent(KeyEvent& out) {
    if (head_ == tail_) return false;
    out = queue_[head_];
    head_ = (head_ + 1u) % kQueueSize;
    return true;
}

} // namespace cwf
