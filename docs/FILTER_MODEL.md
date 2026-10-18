# Filter model

The workstation implements an original **FZ-inspired**, driven two-pole resonant lowpass at 36 kHz (`workstation/dsp/dsp.c`). It is a topology-preserving state-variable integrator with independent stereo state per voice. It is not Open303 or a claimed MB87186 circuit emulation. No ALM code or physical-chip model was copied.

Cutoff is clamped to 25–15,000 Hz and smoothed by 0.015 per frame. The integrator coefficient is `g = tan(pi * cutoff / 36000)`, updated every eight samples. Resonance maps damping to `k = 2 - 1.92*r`. Driven input is `tanh(input*(1+5*drive))/sqrt(1+5*drive)`. Band/low state bounds and denormal suppression are protection measures, not claims about chip saturation. The lowpass output is the musical path, not a bypass label. Track performance cutoff/resonance, region offset, key tracking, filter envelope and LFO feed each voice's cutoff path. Track controls are smoothed and affect held notes.

The host tests compare actual response and stability, exercise legal parameter sweeps, envelope release and LFO behavior, and run all four sends after filtering. Waveform tables are immutable and swapped with a phase-preserving crossfade. Deep envelope/region edits generally apply on the next trigger; waveform tables and track performance controls are live.

This supplies a useful custom filter, not a measured match to an FZ-1. No reference-device transfer function, resonance calibration, switching noise or analog tolerance dataset was acquired. Matching those is a separate evidence gate. The initial `fz_` library's no-filter numerical test is retained only as a legacy baseline; the executable application uses this workstation implementation.
