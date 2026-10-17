# Filter model

Working name: **FZ INSPIRED**. An FZ-1 frequency-response measurement has not been made. This page describes the host reference that `fz_performance_render` runs.

## Topology

One Cytomic / Andrew Simper trapezoidal state-variable lowpass per voice, at 36 kHz. It is a development baseline with a replaceable call (`fz_svf_set` / `fz_svf_low`), not a switched-capacitor model of the MB87186 and not an Open303 ladder.

```text
hz = 20 * 2^(cutoff/127 * log2(14000/20))     cutoff 0..127 → 20 Hz .. 14 kHz
Q  = 0.5 + resonance * (7.5/127)              resonance 0..127 → Q 0.5 .. 8
g  = tan(pi * hz / 36000)
k  = 1/Q
a1 = 1 / (1 + g * (g + k))
a2 = g * a1
a3 = g * a2
v3 = x - ic2
v1 = a1*ic1 + a2*v3
v2 = ic2 + a2*ic1 + a3*v3
ic1' = clamp(2*v1 - ic1)
ic2' = clamp(2*v2 - ic2)
y  = clamp(v2)
```

`clamp` limits state and the returned lowpass to ±4 and flushes values smaller than 1e-15 to zero. A non-finite `v2` clears both integrators and returns 0. The input is clamped to ±4 before the recursion.

The ±4 bound is about 12 dB above a full-scale ±1 sample. High cutoff together with Q = 8 can reach it. The host test sweeps both extremes with a ±4 input and requires a finite result inside that bound. The bound is a stability limit, not a measured FZ-1 headroom figure.

## Where it runs

`fz_engine_render` leaves the filter off unless `fz_engine_set_filter` enabled it. A host test locks that: the same one-shot rendered twice through `fz_engine_render`, with the enable flag held at 0, matches sample for sample even if the cutoff arguments differ.

`fz_performance_render` enables the filter from the selected track, then calls `fz_engine_render`. Cutoff is the track value plus the region's `cut_offset`, plus `key_track` steps per octave from the region root, plus the filter envelope times `env_amount` when that envelope is on. Resonance is the track value plus `res_offset`. Each sum is clamped to 0..127. Coefficients refresh when the voice's cutoff or resonance changes.

`drive` on `fz_engine_set_filter` is an extra shaper after the lowpass, used only when drive is above 0. `fz_performance_render` passes drive 0. The shaper is not an FZ-1 saturation measurement.

## Still open

Per-voice state is independent, so one note does not clear another's integrators. Manual cutoff moves update coefficients on the next sample; there is no separate smoothing filter on the knob. Matching an FZ-1 recording remains a later measurement, with the fixture list in the specification (sine, noise, impulse, sustained notes, several levels and transpositions).
