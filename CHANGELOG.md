# Changelog

## Unreleased

### Added

- Hardware and source audit for the FM-1 (JieLi AC79 / pi32v2) from pinned felucca-bsp, Melodee, Felucca, SLOOP, X0X, FM1 Open, and fm1-base commits. No device was attached. Reference commits are locked and not vendored.
- Host-tested 36 kHz engine: variable-speed samples, single-cycle pitch, 16-voice mix, 44.1/48 kHz boundary conversion.
- Multi-sample capture state machine with a RAM store, 16-slot advance, undo of the last slot, and OCT+ into the performance state.
- Octave-anchor zones, 48-harmonic recipe format, HOME frame, tap/hold arbiter, event history, and tempo candidates.
- OCT+ as the confirm and undo button. SELECT stays turn-only (`docs/adr/0002-oct-plus-click.md`).
- Editor tabs with ALL/ONE scope, numeric entry, category naming, envelopes, a section-0 voice loop, and an optional track LFO.
- FZ INSPIRED trapezoidal lowpass on the performance render. Direct `fz_engine_render` stays dry until the filter is enabled (`docs/FILTER_MODEL.md`).
- Four send effects, post-fader routing, region and pattern bake (`docs/adr/0003-bake-routing.md`).
- Sparse sequencer: steps, chains, queued launch, song rows, free take, retrospective capture.
- In-memory `FZJR` journal, `FZB1` bundle, `FZTEST` import, and WAV cycle import.
- Local importer page `web/importer/index.html`.
- Package and flash scripts that refuse.

### Not in this revision

Target firmware, USB device code, flash programming, on-device pixels, a measured FZ-1 filter match, and substitution of a pattern's baked asset into the live voice.
