# Patterns

The built-in demo pattern lives in `Synth::loadDemoPattern()`
([src/synth.cpp](../src/synth.cpp)): a 16-step, 130 BPM acid line with accents,
slides, and octave jumps over a resonant saw patch.

## Anatomy of a step

Each of the 16 steps is a `cw::Step`:

| Field    | Meaning                                             |
|----------|-----------------------------------------------------|
| `note`   | MIDI note number (36 = C2, 48 = C3 …)               |
| `gate`   | does this step sound at all?                         |
| `accent` | louder + more filter env on this step               |
| `slide`  | portamento into the next step, no envelope retrigger |

## Roll your own

Program the sequencer directly:

```cpp
cw::Synth synth;
synth.sequencer().setTempo(140.0f);
synth.sequencer().setStep(0, cw::Step{ /*note*/ 36, /*gate*/ true,  /*accent*/ true,  /*slide*/ false });
synth.sequencer().setStep(1, cw::Step{ 36, true, false, true  });   // slides up…
synth.sequencer().setStep(2, cw::Step{ 48, true, false, false });   // …to the octave
// … 16 steps …
synth.sequencer().start();
```

Then `renderBlock()` on the host to hear it, or bake your pattern into
`loadDemoPattern()` and reflash. A JSON pattern loader is a natural next
addition — the `Step` struct maps cleanly to `{note, gate, accent, slide}`.
