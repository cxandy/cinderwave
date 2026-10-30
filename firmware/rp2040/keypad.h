// 16-key keypad scanner.
//
// The Cherry MX switches are wired as 16 independent GPIOs (no diode matrix):
// one leg to the GPIO, one leg to GND, with the internal pull-up enabled. A
// key therefore reads LOW while pressed.
//
// Debouncing is time based rather than sample based: a candidate level has to
// survive kDebounceMs before it is accepted as the new stable state, which
// keeps the behaviour identical whether the caller polls at 1 kHz or at
// 5 kHz.
//
// Edges are queued rather than sampled, so a slow UI redraw can never swallow
// a press: the scanner keeps its own ring buffer and the application drains
// it whenever it is ready.
#ifndef CINDERWAVE_KEYPAD_H
#define CINDERWAVE_KEYPAD_H

#include <stdint.h>

namespace cwf {

struct KeyEvent {
    uint8_t index;  // 0..15
    bool    down;   // true = pressed, false = released
};

class Keypad {
public:
    // How long a level must hold before it counts as a real press/release.
    static constexpr uint32_t kDebounceMs = 20;

    // Ring buffer depth; must be a power of two.
    static constexpr int kQueueSize = 32;

    void init();

    // Sample the keys and queue any press/release edges. Call at >= 1 kHz.
    void poll();

    // Current stable state of every key (bit set = pressed).
    uint32_t pressedMask() const { return pressed_; }

    // Pop one queued edge. Returns false when the queue is empty.
    bool nextEvent(KeyEvent& out);

private:
    uint32_t pressed_ = 0;      // stable state
    uint32_t candidate_ = 0;    // level seen most recently
    uint32_t candidateSinceMs_[16] = {};

    KeyEvent queue_[kQueueSize];
    volatile uint32_t head_ = 0;  // next event to read
    volatile uint32_t tail_ = 0;  // next event to write
};

} // namespace cwf

#endif // CINDERWAVE_KEYPAD_H
