# Filter model

The FZ-inspired filter is **not implemented**.

HOME cutoff and resonance are stored on the track. They are not read by the renderer. A host test locks that fact: the same note with two cutoff values produces the same samples. That test will have to change when a real model lands. Until then, the controls are not a bypass dressed up as an FZ filter, and the engine is not named bit-exact.

## Evidence boundary

The Casio manual, published DCF notes, and the ALM Fizzle Guts project (a board that drives a physical MB87186) are references for a later model. Fizzle Guts is not a software filter, and it was not copied. X0X / Open303 is not the main filter.

## Next implementation step, when it starts

A replaceable filter interface at 36 kHz, one state per voice, with cutoff as a sum of the track performance value, the region offset, and modulation. Stability across the legal range has to be tested before any fidelity claim. Matching an FZ-1 recording is a separate measurement and has not been done.
