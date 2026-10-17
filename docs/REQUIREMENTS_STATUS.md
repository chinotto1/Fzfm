# Requirements status

Revision: host milestone M0 plus the first 36 kHz vertical slice. Date: 2026-10-17.

Legend: **done** means host-tested in this tree. **partial** means real code and tests exist, and a named piece is still missing. **not started** means there is no implementation. **pending device** means the host part may exist but the requirement is not met until hardware evidence exists.

No target image was compiled. No device was tested. A simulator screen is not a claim that the product is finished.

| ID | Status | Evidence |
|---|---|---|
| R01 | partial | Host engine plays, captures, and mixes. No on-device save, sequence, or edit surface |
| R02 | partial | 44.1 kHz stereo can be converted and summed to 36 kHz mono in process memory. No USB stack, no iPad |
| R03 | partial | Engine and assets are 36 kHz / 16-bit. SRC is explicit. Codec clock is unmeasured |
| R04 | done (host) | `tests/test_main.c` pitch ratio, duration, and A4 cycle. Fixture WAVs from `make render` |
| R05 | partial | 16-slot auto-advance, thresholds, pre-trigger, undo last, storage-full. Host only |
| R06 | partial | Closest-root zones, lower-root tie, manual range preserved. Capture root can be set. UI for anchors is not built |
| R07 | partial | `fz_project_new_instrument` creates a track and an empty pattern or leaves nothing |
| R08 | partial | `fz_capture_oct_plus` opens the performance state, keeps the octave, does not duplicate the instrument |
| R09 | partial | Tempo candidates and the "do not replace an existing or external tempo" policy. No free-take transport |
| R10 | partial | History ring excludes sequencer and menu events and ends at a timestamp. No REC-hold screen |
| R11 | partial | Arbiter: one hold, down-edge time, cancel during confirm. Not wired to every screen |
| R12 | not started | Patterns exist only as empty objects |
| R13 | not started | EDIT tap/hold is classified and then discarded |
| R14 | not started | No ALL/ONE editor |
| R15 | partial | HOME list and four relative knobs. No editor lists |
| R16 | not started | No note, range, or numeric picker |
| R17 | not started | Names are automatic (`DRUMS n`, `SYNTH n`) but there is no hold-EDIT category picker |
| R18 | not started | No trim, reverse-write, mix, cross-mix, or multi-section loop editor. A reader loop flag exists for playback tests only |
| R19 | partial | 48 levels, versioned recipe, reload, cycle playback. No on-device list UI |
| R20 | not started | No preset-wave, cut, or draw UI |
| R21 | partial | HOME draws tracks and the four values. Cutoff/resonance do not filter |
| R22 | not started | See `docs/FILTER_MODEL.md`. Stored values are not a model |
| R23 | partial | Volume and pan are one track state. No send, mute, or solo |
| R24 | not started | |
| R25 | not started | |
| R26 | not started | Optional, and not started |
| R27 | not started | 600-point cycles play at the requested pitch. No importer |
| R28 | partial | Caller PCM budget, specific full errors, no preallocated 16-slot audio. Device budgets unknown |
| R29 | partial | Capture undo of the last slot. No general undo journal, no power-fail test |
| R30 | partial | Host build and tests. Package and flash scripts refuse. See `docs/BOOT_RECOVERY.md` |
| R31 | partial | PCM assets and the additive recipe share the project. No bundle file |
| R32 | partial | External clock blocks tempo replacement. No MIDI driver, no clock |
| R33 | partial | 240×240 HOME renderer and the input arbiter. Not a full panel simulator |
| R34 | partial | Spec, lockfile, notices, and this table. Toolchain not pinned as a downloaded blob |

## Acceptance tests

| Test | Result |
|---|---|
| T01 | Host `make test` and `make render` pass. Target package: not built. Dependencies: `third_party/sources.lock` |
| T02 | Source audit written. Unknowns marked. No device observation |
| T03 | Pending device |
| T04 | Pending device |
| T05 | Host: 36 kHz ratios, A4 within a few cents, octave doubles pitch and halves one-shot length |
| T06 | Host: two chunk sizes of the same stream match sample for sample. Overrun counter recovers |
| T07 | Host: sixteen separated hits fill slots 36..51 in order, peak preserved |
| T08 | Host: three hits play; the next key does not; OCT+ leaves those three |
| T09 | Host: undo last slot keeps earlier slots |
| T10 | Host: OCT+ from the states covered in `test_capture` does not change octave or add an instrument |
| T11 | Host: one sample, rate transposition |
| T12 | Host: roots 48 and 60, tie at 54, extremes, manual override |
| T13 | Host: creating a track does not reserve PCM; the full error names the exhausted pool |
| T14 | Host: REC/EDIT/SEL hold does not also tap, including across a screen change |
| T15 | Host: a 2.0 s four-onset phrase proposes 120 BPM at 1 bar. No transport |
| T16 | Host: slice ends at the given timestamp and drops a later event and a sequencer event |
| T17 | Not started beyond "no onsets → confidence 0" |
| T18 | Host: existing music or external clock keeps the project tempo |
| T19 | Host: two gated notes of the same pitch need two note-offs; then the mix is silent |
| T20 | Capture undo only. Redo and batch undo are not started |
| T21–T29 | Not started, except T29's recipe/render half, which is host-tested |
| T30 | Host: 600-point and 1,024-point tables both land near 440 Hz (± about 10 cents on the zero-cross count) |
| T31 | Not started. No model, so no fidelity claim |
| T32 | Host: HOME/GLO volume is one field; pan law tested; cutoff does not change audio yet |
| T33–T47 | Not started or pending device, as appropriate |
| T48 | No third-party source is vendored. Lockfile and `THIRD_PARTY_NOTICES.md` list what was read |

## Commands run for this revision

Host only. Exit 0 unless noted.

- `make test` — 239 passed, 0 failed (gcc, ASan, UBSan, `-Wall -Wextra -Werror`)
- `make render` — A4 zero-crossings 439, +12 semitones 879, both WAV files 36,000 Hz mono int16. The fixture tool itself is built at `-O2`
- `scripts/package_firmware.sh` and `scripts/flash.sh` — exit 2 on purpose

## Next implementation step

Wire felucca-bsp only after the JieLi toolchain is available, and keep the first device image to a bootable tone plus the existing engine. Do not start the website, the filter-matching claim, or flash writes before that. The open product question that source inspection cannot close is which physical control is SEL.
