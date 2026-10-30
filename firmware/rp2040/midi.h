// MIDI clock output on the clock port (GP8).
//
// GP8 doubles as UART1 TX on the RP2040, so the same jack can either emit the
// 10 ms sync pulse or a real MIDI clock: 0xF8 ticks at 24 per quarter note,
// plus 0xFA (start) / 0xFC (stop). The pin function is switched between plain
// GPIO and UART by setPinMidi() whenever the CLOCK mode changes.
//
// Sending is strictly non-blocking: bytes go into the 32-deep hardware FIFO
// and are dropped if it is full. At 31250 baud one byte takes ~320 us (10
// samples), while consecutive ticks are ~600 samples apart even at the top
// tempo — the FIFO never backs up in practice.
#ifndef CINDERWAVE_MIDI_H
#define CINDERWAVE_MIDI_H

#include <stdint.h>

namespace cwm {

// Set up UART1 at the MIDI baud rate (31250 8N1). The GP8 pin function is
// left alone here; setPinMidi() decides what drives it.
void init();

// Switch GP8 between MIDI (UART1 TX) and the plain GPIO pulse output.
void setPinMidi(bool midi);

// Realtime bytes; safe to call from the audio interrupt.
void sendStart();  // 0xFA
void sendStop();   // 0xFC
void sendTick();   // 0xF8

} // namespace cwm

#endif // CINDERWAVE_MIDI_H
