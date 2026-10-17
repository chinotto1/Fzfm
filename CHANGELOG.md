# Changelog

## Unreleased

### Added

- Hardware and source audit for the FM-1 (JieLi AC79 / pi32v2) from pinned felucca-bsp, Melodee, Felucca, SLOOP, X0X, FM1 Open, and fm1-base commits. No device was attached. Reference commits are locked and not vendored.
- Host-tested 36 kHz engine: variable-speed samples, single-cycle pitch, 16-voice mix, 44.1/48 kHz boundary conversion.
- Multi-sample capture state machine with a RAM store, 16-slot advance, undo of the last slot, and OCT+ into the performance state.
- Octave-anchor zones, 48-harmonic recipe format, HOME frame, tap/hold arbiter, event history, and tempo candidates.
- Package and flash scripts that refuse.

### Not in this revision

Target firmware, USB device code, flash storage, the filter, effects, the sequencer, the editor, and the optional importer.
