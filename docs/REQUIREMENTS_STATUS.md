# Requirements status

Revision: host engine through sequencer, editor, filter, sends, bake, journal, and the FZTEST importer. Date: 2026-10-17.

Legend: **done** means host-tested in this tree. **partial** means the host function exists and a named piece is still missing. **pending device** means the requirement is not met until hardware evidence exists.

No target image was compiled. No device was tested. Nothing was flashed.

| ID | Status | Evidence |
|---|---|---|
| R01 | partial | Host project plays, captures, edits, sequences, mixes, bakes, and writes an in-memory bundle. No on-device screen and no durable save |
| R02 | partial | 44.1 kHz and 48 kHz audio can be converted to 36 kHz mono in process memory. No USB stack and no iPad |
| R03 | partial | Engine and assets are 36 kHz / 16-bit. SRC is explicit. The codec clock is unmeasured |
| R04 | done (host) | Pitch ratio, one-shot duration, and A4 cycle in `tests/test_main.c`. Fixtures from `make render` |
| R05 | partial | 16-slot capture, thresholds, pre-trigger, undo last, storage-full message `SAMPLE STORAGE FULL`. Host only |
| R06 | partial | Closest-root zones, lower-root tie, manual range preserved. Pick-range screen state exists. No pixels |
| R07 | done (host) | `fz_project_new_instrument` creates the instrument, a track, and an empty pattern, or leaves nothing |
| R08 | done (host) | OCT+ tap finishes capture to HOME, keeps the octave, and does not create an instrument. Hold undoes the last slot and stays in capture |
| R09 | partial | Free take arms on REC, starts on the first note, and infers tempo only for an empty unclocked project |
| R10 | partial | History slice plus `fz_seq_capture_history`. REC hold is a command. No retrospective screen |
| R11 | partial | OCT+ is the confirm/undo button (`docs/adr/0002-oct-plus-click.md`). Region and pattern batches undo. LFO edits and orphaned bake PCM are outside that stack |
| R12 | partial | Sparse steps, chains, queued bar launch, song rows. No automation lanes and no step-grid pixels |
| R13 | partial | EDIT tap cycles nine tabs. Hold opens naming and does not change the tab |
| R14 | partial | ALL versus ONE, persistent across tabs. Absolute frame numbers and absolute roots refuse ALL with `ONE SAMPLE` |
| R15 | partial | Knob 0 moves the row. Knob 1 edits it. Knobs 2 and 3 are labelled `-` and do not edit |
| R16 | partial | Numeric keypad (6 digits, out-of-range stays on the keypad) and note/range pick |
| R17 | partial | Hold EDIT plus a category key names `CATEGORY n` |
| R18 | partial | Mix, cross, truncate, reverse write, cut cycle, eight-stage envelopes, eight loop sections. The voice reader uses section 0 only. Crossfade is `fz_loop_sample` |
| R19 | partial | 48-harmonic recipe, cycle playback, and the WAVE tab on the panel. The recipe is not in `FZB1` yet |
| R20 | partial | Sine and naive square presets, cut cycle, and a DRAW page for cycle instruments. Sample instruments keep loop and sample pages |
| R21 | partial | HOME values are the track. `fz_performance_render` filters from them |
| R22 | partial | FZ INSPIRED trapezoidal lowpass. Stability is host-tested. Fidelity is unmeasured. See `docs/FILTER_MODEL.md` |
| R23 | partial | Shared volume/pan, four sends, mute, solo, region trims. No return fader |
| R24 | partial | Delay, one reverb, chorus, drive. Wet return mixes when a rack is attached. Host buffer sizes are not an AC79 budget |
| R25 | partial | Region and pattern bake. See `docs/adr/0003-bake-routing.md`. Pattern audio is stored and not yet substituted into the voice |
| R26 | partial | `FZTEST` only, plus an explicit PCM config. Unknown sizes are rejected. Local page: `web/importer/index.html` |
| R27 | partial | WAV cycles of 8- or 16-bit PCM. The file rate is not a root. AKWF tables are not bundled |
| R28 | partial | Caller PCM budget and the ceilings in `project.h`. Those ceilings are host safety limits |
| R29 | partial | Undo groups, sequencer snapshots, and an in-memory journal whose commit word is last. Not flash |
| R30 | partial | `scripts/package_firmware.sh` and `scripts/flash.sh` exit 2. See `docs/BOOT_RECOVERY.md` |
| R31 | partial | `FZB1` round-trips tempo, instrument PCM, region gain/trim/reverse/root, and track mixer fields. Patterns, song, FX, LFO, envelopes, and loops are not in the file |
| R32 | partial | External clock blocks tempo replacement. No MIDI driver and no clock reader |
| R33 | partial | 240×240 frames for the session screens, and `web/sim` runs that engine in the browser. Not the device panel |
| R34 | partial | Spec, lockfile, notices, this table, the feature matrix, and ADRs. The JieLi toolchain is not downloaded |

## Acceptance tests

| Test | Result |
|---|---|
| T01 | Host `make test` and `make render`. Target package: not built. Dependencies: `third_party/sources.lock` |
| T02 | Source audit written. Unknowns marked. No device observation |
| T03 | Pending device |
| T04 | Pending device |
| T05 | Host: 36 kHz ratios, A4 within a few cents, octave doubles pitch and halves one-shot length |
| T06 | Host: two chunk sizes of the same stream match. The queue is 4096 frames, so the test pumps |
| T07 | Host: sixteen separated hits fill slots 36..51 |
| T08 | Host: three hits play; the next key does not; OCT+ leaves those three |
| T09 | Host: undo last slot keeps earlier slots |
| T10 | Host: OCT+ during capture does not change octave or add an instrument |
| T11 | Host: one sample, rate transposition |
| T12 | Host: roots 48 and 60, tie at 54, manual override |
| T13 | Host: creating a track does not reserve PCM |
| T14 | Host: OCT+, REC, and EDIT hold do not also tap |
| T15 | Host: a 2.0 s phrase proposes a tempo near 120 when the project is empty |
| T16 | Host: the history slice ends at the timestamp and drops a sequencer event |
| T17 | Host: no onsets yields confidence 0. Rubato fallback is not a separate scorer |
| T18 | Host: existing music or external clock keeps the project tempo |
| T19 | Host: two gated notes of the same pitch need two note-offs |
| T20 | Host: region undo/redo and sequencer undo. A take undo keeps the instrument |
| T21–T28 | Host pieces of edit, loop, envelope, and mangle exist. See `docs/FZ_FEATURE_MATRIX.md` for the rows that are still functions rather than screens |
| T29 | Host: additive recipe and cycle render |
| T30 | Host: 600-point and 1,024-point tables near 440 Hz |
| T31 | Host: legal cutoff/resonance extremes stay finite and inside ±4. No FZ-1 measurement, so no fidelity claim |
| T32 | Host: HOME and GLO share volume. Pan law tested. Performance render uses cutoff |
| T33 | Host: four slot types can be selected; a second reverb is refused. A delay return lands in `fz_engine_render` when a rack is attached. CPU on device is unmeasured |
| T34 | Host: send formula, mute, wet-stem exclusion |
| T35 | Host: baking one region clears only that region's masked trims |
| T36 | Host: pattern unsend hits that pattern and not another |
| T37 | Host: partial bake keeps a wet asset that is not sent again. Shared nonlinear sum is reported inexact |
| T38 | Host: `fz_fx_ram_used` drops after disable and drain. The static arrays remain |
| T39 | Host: storage-full bake leaves routing and `pcm_used` unchanged |
| T40 | Host: `FZTEST` decodes; an unknown profile does not |
| T41 | Host: a bad bundle checksum does not mutate the destination. WAV rejects a short chunk |
| T42 | Local page `web/importer/index.html` auditions `FZTEST` and a WAV cycle in the browser. No device and no upload |
| T43 | Host: a journal record without the commit word is dropped and the previous record remains. Not flash |
| T44 | Host: `FZB1` restores the fields it stores. Calibration is absent on purpose. Patterns and FX are not in the file yet |
| T45 | Host ceilings only. No device high-water mark |
| T46 | Pending device. Flash scripts refuse |
| T47 | Pending device |
| T48 | No third-party source is vendored. Lockfile and `THIRD_PARTY_NOTICES.md` list what was read |
| T49 | `make sim` then `web/sim/index.html?selftest=1`. Octave, BPM 120, a sounding key that stops on release, the EDIT screen, DRAW, ENV outside that cycle, an LFO target and shape, the ARP page starting off, and a non-blank 240×240 frame. Headless Chrome, not the device |

## Commands for this revision

Host only.

- `make test` — 10900 passed, 0 failed (gcc, ASan, UBSan, `-Wall -Wextra -Werror`)
- The same suite at `-O2` without sanitizers — 10900 passed, 0 failed
- `make sim` — wasm32 panel at `web/sim/sim.wasm`. Needs clang with `wasm-ld`
- `make render` — A4 zero-crossings 439, +12 semitones 879, both WAV files 36,000 Hz mono int16
- `scripts/package_firmware.sh` and `scripts/flash.sh` — exit 2

## Still required before a device claim

A JieLi pi32v2 compile, USB device descriptors, iPad receive, on-device pixels, a measured FZ-1 comparison, and a flash journal that has been tested on hardware. Those are not started here.
