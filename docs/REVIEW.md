> Historical review of Cursor commit ffc0576 before the workstation implementation. The missing-feature section below records that review point; see REQUIREMENTS_STATUS.md for current implemented/partial/device results.

# Specification and correctness review

Reviewed base: `ffc057625771cd9ce65cd47746772e705fc70267` from `origin/main` (the Cursor implementation), against the complete uploaded revision 1.0 specification. The earlier uncommitted cloud draft was preserved and file-hash verified at `/tmp/fm1-pre-cursor-review-20261017T234110Z` before fast-forwarding this checkout. None of that earlier implementation was silently mixed into this patch.

The implementation is a real, tested **host foundation**, not most of the complete instrument. Its existing status table generally discloses this correctly. No target application is built or device behavior established.

## Confirmed defects fixed

| Severity | Finding and concrete failure | Correction / regression |
|---|---|---|
| High | `fz_project_add_pcm` added two 32-bit sizes before comparing capacity. Overflow could admit a huge copy outside the PCM arena | Subtraction-based admission and byte-size check before pointer arithmetic/copy; test valid and inconsistent arena states |
| High | Capture undo rewound the global PCM watermark and retired a possibly shared asset. Newer assets/active voices could subsequently read overwritten audio | Remove the exact captured region and retain immutable asset/PCM storage. Test shared reference, unrelated newer asset and another take after undo |
| Medium | Reopening an existing partial kit started at slot 0, creating duplicate key mappings | Locate the next empty logical slot; reject duplicate slots, invalid keys and duplicate pitched roots before allocating metadata |
| Medium | Threshold trigger vanished when pre-trigger was zero; the pre-trigger path moved the entire buffer for every frame | Always append the trigger frame. Use a chronological constant-time ring; test zero pre-trigger and a wrapped ring |
| Medium | Disconnect after END_DETECTED did not finalize the take; disconnect during capture did not advance the slot. Rearming could discard a pinned failed take | Finalize once from either state, advance correctly, preserve failed takes until explicit cancel, reject skip/rearm while pinned |
| Medium | Manual ranges lost to an earlier automatic region whose min/max straddled the manual range | Resolve manual ranges first; retain nearest-root and lower-tie automatic mapping |
| Medium | Reader loop markers replaced intro audio; reverse started at the first frame and reverse loops could stop at a boundary | Preserve intro reads, begin fresh reverse playback at the last frame, wrap fractional reverse phase with bounded arithmetic |
| Medium | Volume/pan were copied at note-on, so HOME changes did not affect held notes | Bind performance voices to the original track handle and read its live mix controls; fixed-pitch kits retain the performed key for note-off |
| Medium | Invalid note-on could erase a sounding voice before reader validation; voice age wrapped after 65,535 starts and broke stealing order | Validate the reader first; retain existing engine on failure; use a 64-bit age counter |
| Medium | REC/EDIT/SEL released after the hold threshold could become taps if no intermediate poll occurred | Check the threshold on release as well, emitting exactly one hold with the original down timestamp |
| Medium | Sequencer/menu traffic occupied retrospective storage; 32-bit timestamp rollover excluded recent notes | Reject non-live events before storage; slice by modular age with a documented half-period lookback bound |
| Medium | SRC input-frame counter wrapped after 2^32 frames and could emit stale output without consuming input | Keep input chronology/required frame positions in 64 bits; regression shifts equivalent rational phase across the rollover boundary |
| Medium | Semantically invalid additive imports partially overwrote the current recipe; writer accepted an unreadable point count | Decode into a temporary recipe and publish after validation; validate writer/render inputs |
| Medium | Best tempo depended on output capacity; sparse/invalid onsets could report false confidence and NaN/Inf could replace project tempo | Rank all bounded candidates before truncation; cap sparse confidence, ignore out-of-phrase onsets and reject non-finite tempo |
| Low | Header edits did not make the Makefile rebuild its executables | Add firmware headers to both host target dependencies |

These are fixes to implemented behavior. They do not complete the missing synth/sequencer/storage subsystems. Capture undo intentionally does **not** reclaim PCM now: safe ownership-aware reclamation is still needed, and repeated takes/undo can exhaust the caller-provided arena. Track-bound voices also require the caller to stop/reset the engine before replacing or disposing of its project/store. No concurrent mutation/render support is claimed.

## Required features still missing

| Area | What remains required | Spec IDs |
|---|---|---|
| Actual firmware and direct sampling | Implement/link the chosen board adapter, USB audio RX with simultaneous MIDI, clock feedback/drift handling, lifecycle recovery, codec TX and physical controls. Produce a baseline and actual target image | R01–R03, R30, T01–T04, T46–T47 |
| Recording and arrangement | A history ring/tempo helper is not a recorder. Free/live/retrospective recording publication, transport, event scheduling, step/chord entry, queued patterns, chains, song playback, automation and correction UI are absent | R09–R12, T15–T22 |
| Deep on-device editor | Nine actual EDIT tabs, ALL/ONE scope, numeric/note/range pickers, category naming, trimming, Mix Write/Cross-Mix, multi-stage envelopes and multi-section loop programs remain absent. A single reader-loop flag is insufficient | R13–R18, T23–T28 |
| Synthesis workflow | Additive math and its recipe are present; on-device 48-harmonic editing, table swapping, presets, cycle extraction, drawing and sample-source selection are not integrated | R19–R20, T29–T30 |
| Filter/mixer/FX/baking | Cutoff and resonance remain stored values with no filter. Four shared effects, sends/returns, mute/solo, modulation and atomic scoped bake/freeze/reclaim are absent | R21–R25, T31–T38 |
| Durable project data | There is no project container, audio dependency serialization, journal, transactional general edits, undo/redo or complete backup/restore. FZH1 saves only an additive recipe | R29–R31, T39, T43–T44 |
| MIDI and resources | Full MIDI routing, sustain, bend, per-note source identity, external-clock transport, dynamic metadata/event/FX/undo budgets and measured target stress/deadline/stack limits remain incomplete | R28, R32, T19, T45 |
| Simulator and release | Only HOME rendering/offline fixtures exist. Full physical-event simulator, broad malformed-container/power-cut tests, target toolchain artifact pin and qualified installation/recovery remain missing | R30, R33–R34, T01, T41, T47–T48 |

The EPROM/cycle website and browser transfer are also absent (R26–R27, T40/T42). Those are explicitly optional and should follow the required standalone workflows. The logical SEL remains physically unbound; this review does not invent a SELECT push or change panel bindings without evidence.

## Validation actually performed

- Original `./scripts/check.sh`: 239 assertions passed; offline fixtures passed.
- Initial added regressions before fixes: 259 assertions passed, 14 failed. These failures were reproduced, not inferred from comments alone.
- Final `./scripts/check.sh`: **330 assertions passed, 0 failed**, GCC with ASan/UBSan and `-Wall -Wextra -Werror`; offline fixtures remained 439/879 positive crossings for the A4/+12 waveforms.
- Independent optimized `-O2` host test executable: **330 passed, 0 failed**.
- GCC `-fanalyzer -fsyntax-only` on modified core/capture/reader/engine/additive/history/tempo/input modules: exit 0, no diagnostics.
- `./scripts/report_memory.sh`: exit 0; host object report only, no target memory/deadline claim.
- Package and flash refusal scripts: both returned the expected exit 2. No device was accessed or written.
- `git diff --check`: clean.

The 91 added successful assertions include checks inside bounded test loops; they are not 91 newly completed acceptance cases. Hardware, missing-feature and recovery tests remain unrun. Code and documentation changes remain local in the working tree; nothing was pushed or published.

## Suggested next sequence

First make a real, safe target baseline plus direct USB→RAM capture→play path while retaining these host tests. Then implement recorder/pattern/song and durable project publication, followed by deep editor/synthesis integration and measured filter/four-FX/baking. Keep optional website work after the standalone instrument. A complete device release needs the specification's actual hardware acceptance evidence.
