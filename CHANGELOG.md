# Changelog

## Unreleased

### Added

- Hardware and source audit for the FM-1 (JieLi AC79 / pi32v2) from pinned felucca-bsp, Melodee, Felucca, SLOOP, X0X, FM1 Open, and fm1-base commits. No device was attached. Reference commits are locked and not vendored.
- Host-tested 36 kHz engine: variable-speed samples, single-cycle pitch, 16-voice mix, 44.1/48 kHz boundary conversion.
- Multi-sample capture state machine with a RAM store, 16-slot advance, undo of the last slot, and OCT+ into the performance state.
- Octave-anchor zones, 48-harmonic recipe format, HOME frame, tap/hold arbiter, event history, and tempo candidates.
- Package and flash scripts that refuse.

### Fixed during review

- Preserve newer/shared/actively read audio during capture undo; resume partial kits at the next free key and reject duplicate mappings.
- Retain trigger frames with zero pre-trigger; use a constant-time chronological pre-trigger ring and finalize input loss once. Keep failed takes pinned until explicit cancel.
- Reject PCM arena-size overflow before copying; reject invalid voice starts without stealing a valid note.
- Preserve loop introductions, start reverse playback at the end, and wrap reverse loops correctly.
- Apply HOME volume/pan to held track-bound voices; preserve performed-key note-off identity for fixed-pitch kit playback.
- Handle delayed hold polling and 32-bit microsecond-clock rollover; prevent sequencer/menu events from evicting live history.
- Preserve recipes on rejected imports and validate writer point counts; rank all tempo candidates before truncating, reduce sparse confidence, and reject non-finite project tempos.
- Keep SRC chronology and voice age past their previous counter-rollover limits; rebuild host targets when headers change.

### Not in this revision

Target firmware, USB device code, flash storage, the filter, effects, the sequencer, the editor, and the optional importer.

## Connected workstation implementation

Added the shared workstation application, deep nine-tab UI, capture controls, additive/preset/draw/extract workflows, real filter/four sends, recorder/steps/chains/song/live arrangement, pattern actions and persistent sample unbake. Added validated FM1P v2 projects, fault-tested host saves, native CLI/browser simulator with responsive physical controls and local import/export, workflow/heap/SRC regressions and browser checks. Pinned/vendored the MPL board layer and external compiler/runtime inputs; baseline and sampler now cross-compile/link. Device flash persistence, recovery, clocks/USB qualification and optional EPROM support remain open. Nothing was flashed.

Follow-up workflow verification added real step ties, confirmed anchor deletion, atomic FX undo/redo admission with preserved bypass tails, and history/reader-aware superseded-cycle collection. Six hundred live waveform edits no longer exhaust asset slots; undo/redo and crossfade readers remain valid.
