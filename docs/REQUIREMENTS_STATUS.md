# Requirements and evidence

Current implementation extends the reviewed Cursor commit `ffc0576`. The executable instrument is `firmware/workstation/` (ADR 0002); the initial `fz_` library remains a regression reference. **Host** below means implemented and exercised on the host. **Compiled** means the real PI32v2 sampler linked, not that it booted. **Device pending** means no physical test was performed. No firmware was flashed.

| ID | Current status | Code / evidence / remaining limit |
|---|---|---|
| R01 | Host; device partial | Shared project/engine/UI connects capture through arrangement/save; native demo and browser workflows. Target SAVE explicitly unsupported |
| R02 | Compiled; device pending | BSP UAC OUT raw packets + `platform/fm_usb.c`, MIDI coexistence, lifecycle/ring tests. No iPad enumeration/drift evidence |
| R03 | Host + compiled | `audio/src.c` boundary conversion, immutable PCM16 captures, stereo 36 kHz rendering. Physical codec rate pending |
| R04 | Host + compiled | Variable-rate readers, pitch/duration and 600/1,024-cycle tests |
| R05 | Host + compiled | `sampler/fm_capture.c`: sixteen logical slots shown as colored squares on the capture screen, pretrigger/manual/retry/skip/resume; direct selected-slot capture and atomic replacement/undo, including full kits; native demo fills all sixteen |
| R06 | Host + compiled | Root anchors, automatic zones, manual range priority, mapping UI and confirmed ONE deletion with zone recomputation/undo |
| R07 | Host + compiled | Atomic instrument+track+empty-pattern creation; explicit Synth choice opens a playable sine oscillator without sampling; Sample/Multi go directly to USB capture with no source chooser; undoable removal of empty tracks |
| R08 | Host + compiled | OCT+ down-edge finish; UI tests preserve octave/track and partial takes |
| R09 | Host + compiled | `sequence/fm_sequence.c`: free takes, raw timestamps, ranked timing/correction, preserved existing/external tempo and selected quantization across free/retro takes; reversible nearest-grid timing with short-gate protection |
| R10 | Host + compiled | Bounded live-only history, held/sustained boundary reconstruction, REC hold down-edge slice |
| R11 | Host + compiled | Single tap/hold arbiter; transactional undo/redo and knob gesture coalescing; SCL explicitly mapped as logical SEL |
| R12 | Host + compiled | Sparse steps/chords, probability/microtime/locks, queued changes/scenes, saved ordered/range chains, independent lengths/rates, song editing/playback and live arrangement and same-pitch step ties without retriggers; transactional paired-note add/move/resize/velocity/delete with raw-frame retention |
| R13 | Host + compiled | Eight sample edit tabs with oscillator sources reserved for Synth; synth EDIT cycles only oscillator/amp/filter/LFO, with no trim/loop/mix/map/render or capture tools; hold naming; native UI and browser regression |
| R14 | Host + compiled | Persistent ALL/ONE scope and per-instrument view cache; isolated batch transactions |
| R15 | Host + compiled | Lists scroll, knob 1 row, knobs 2–4 explicit footer fields; reviewed native screenshots |
| R16 | Host + compiled | Silent note/range/sample/numeric pickers, validation, cancel and confirmation |
| R17 | Host + compiled | Hold EDIT category key/automatic numbering, release never cycles tab |
| R18 | Host + compiled; partial | Trim/reverse-write/normalize/mix/cross-mix, eight-stage envelopes with real framebuffer curves and sustain/release markers, eight-section forward loop programs. Reverse playback bypasses forward loop programs; no claim of every historical FZ function |
| R19 | Host + compiled | 48 editable levels and harmonic bar graph, touch/number editing and recipes, saved recipe, immutable regeneration, phase-preserving 64-frame held-note table crossfade |
| R20 | Host + compiled | Sine/triangle/saw/square selection, waveform preview, physical point drawing and touch draft/apply/smooth/clear on Synth with whole-stroke undo; cycle extraction/shared asset attachment remain core APIs |
| R21 | Host + compiled | HOME tracks and four working smoothed performance controls with numeric values; redundant parameter bars and routine OK feedback removed, useful messages/errors retained |
| R22 | Host + compiled; fidelity partial | Per-voice driven TPT resonant filter, amp/filter envelopes/LFO/key scaling, including clocked Sample & Hold with per-note retrigger or shared free-run, stability/response tests. Original custom model, no FZ chip-fit claim |
| R23 | Host + compiled | Shared level/pan fields, sends, mute/solo; graphical GLO channel strips, live browser faders with coalesced undo, voice-peak meters and direct mute/solo; FX return level/pan/mute. Master ADC pot uncalibrated |
| R24 | Host + compiled; device budget pending | Four simultaneous shared wet processors with named parameter rows, input bypass/tails, return mute, admission/reclaim tests |
| R25 | Host; compiled with RAM limits | Atomic isolated sample bake/pattern freeze, persistent unbake recipes and scoped unsend. All active sends required; finite tail/warmups; target temporary-RAM admission may reject |
| R26 | Optional pending | No verified EPROM profiles, ROM decoder or drum-ROM website |
| R27 | Host; partial optional | Strict local PCM16 WAV and cycle import/audition through real engine; no bundled AKWF browser/catalog or device transfer |
| R28 | Host + compiled; device measurements pending | Dynamic metadata/audio/events/FX/undo pools, bounded heap/voice/event admission; peak/timing reports for exact host profiles. Six hundred live/coalesced waveform changes retain at most four assets while preserving undo/redo. Target has explicit smaller profile, no verified flash sample budget |
| R29 | Host; device save pending | Full validated CRC container, two-generation POSIX journal with faults at each step; immutable edits/undo. No flash journal; ordinary retired PCM/event storage persists until project disposal; superseded cycle versions have reader/history-aware collection |
| R30 | Partial | Baseline and sampler actually link, source/dependency hashes, range checks, deterministic source archive. No installable update, device backup tool, authorized flash or tested recovery |
| R31 | Host + compiled | Common FM1P v4 container (reads v2/v3, migrates legacy cycle instruments to Synth, defaults v2 arp to Off) includes audio, synthesis, regions, patterns, chains, song, mixer, FX and bake originals |
| R32 | Host + compiled; partial | USB packet parser, local/USB/TRS identities, sustain/bend, transport/Continue, external tempo estimation/timeout, no incoming echo. Target TRS TX/RX and outgoing clock/drift qualification incomplete |
| R33 | Host + browser | Real 240×240 indexed-color framebuffer, shared native/WASM/RGB565 palette, larger bold primary labels/values, blue selections, green filled slots/meters, amber focus, and 14 buttons/27 keys/7 relative encoders; CLI demo, desktop/mobile browser workflows and native integration tests. Static WebAssembly deployment runs the same C core on iPad-sized touch screens with IndexedDB saves, explicit New track/track selection, named envelope/LFO editors, native MIDI piano roll on first SEQ press plus a touch roll with direct recording/quantization controls, a two-page arp (eight browser fields), hold-HOME commands, and a single-page Simple/Multi sampler with 16 waveform slots, test-hit/file/microphone inputs and a live meter. New workspace tests use a synthetic browser microphone; palette/bold-label/capture-state/RGB PNG and wire-byte tests; actual panel calibration pending |
| R34 | Partial release | Complete project/BSP source, per-file provenance locks, retained notices, pinned compiler/runtime inputs, reproducible commands. Vendor runtime complete-source provenance and distributable binary qualification remain release gates |

The live arpeggiator uses an original bounded scheduler with SLOOP’s controls/modes as the reference: local/USB/TRS note routing, independent gates, 15 modes including Off, 8 rates, octave range, swing/probability/latch/order, generated-note recording/history, eight active tracks × sixteen held notes. Core, UI, migration and audible browser regressions exercise it. External sync deadlines and physical MIDI behavior still require device qualification.

## Acceptance cases

These results identify host coverage, not a declaration that the specification's standalone hardware acceptance is complete.

| Case | Actual evidence |
|---|---|
| T01 | Host strict sanitizer suites; baseline/sampler cross-compile and link. Raw images only |
| T02 | Pinned source audit and provisional linked map; physical identity/RAM not measured |
| T03–T04 | Packet/SRC/capture/MIDI/lifecycle host tests and compiled adapter; computer/iPad USB hardware pending |
| T05–T06 | Pitch/duration/cycle and rational SRC chunk equivalence, independent FIR reference, anti-aliasing; physical clock/long-term drift pending |
| T07–T10 | Sixteen/partial capture/retry/finish tests and real-control native demo; browser capture→OCT+→audio |
| T11–T13 | Sample/anchor mapping, manual priority, dynamic allocation/admission. Explicit target resource profile |
| T14 | REC/EDIT/SCL tap/hold across screens and delayed release; browser naming hold/fast-click regression |
| T15–T18 | Free/retro timing, sparse ambiguity/correction, retained raw event times, existing/external tempo ownership; grid/Off/undo raw-timing checks, retained take grid, nearest-grid playback and audible collapsed gates; native roll edits/off-grid selection, arp order/gates/swing/latch/probability/stopped transport and v2 migration |
| T19–T20 | Repeated notes, sustain/source identity, same-frame press/release, stop/loop releases; scoped transactions/redo and persisted unbake |
| T21–T22 | Queued scenes/patterns, repeated ordered chains and reload, independent lengths/rates, song operations/live arrangement |
| T23–T25 | Instrument-specific tabs, direct USB capture for both sample types, rejection of alternative sample sources, remembered row/scope, modal interception/cancel, naming. Playwright desktop/iPad/390/320 px, graphical modulation and synthesis editors, indexed-color display transport and legible state colors, discoverable track creation/cancellation and WAV/microphone capture, touch piano-roll editing, reversible quantization and explicit-slot sampler replacement/undo with no JS errors |
| T26–T28 | PCM edit/mix/cross-mix fixtures, loop transitions/crossfades, scope safety, envelope release/scaling tests; reverse-loop limitation above |
| T29–T30 | 48-level recipes/reload and live table publication; 600/1,024 cycles near A4 |
| T31–T32 | Stable resonant response across test sweeps and actual HOME/GLO shared controls; S&H hold-duration/range/zero-rate, staggered-note phase-mode and project roundtrip tests; no hardware FZ reference matching |
| T33–T34 | Four wet processors run together in two host budget profiles, tails/sends/mute/frozen bypass checks; device CPU/deadline pending |
| T35–T38 | ONE/ALL routing, pattern freeze/unfreeze, explicit shared/nonlinear limits, persistent unbake, future-automation/reader-aware actual buffer release |
| T39 | Budget/cancel rejection retains prior audio/routing; host journal interruption tests. Physical power-cut pending |
| T40 | Optional EPROM pending; unknown WAV/container versions reject |
| T41 | Malformed/truncated/oversized WAV/project/transfer tests, CRC-valid mutation smoke tests; native browser restore retains prior project |
| T42 | Local WAV/cycle audition/backup without a device; no EPROM UI or browser USB/SysEx transfer |
| T43–T44 | Every journal write/barrier fault recovers an old/new valid container; full recipes/PCM/song/chains/routing roundtrip; calibration excluded |
| T45 | Host timing and allocation high water for named profiles; target map fits. Target deadlines, stacks, physical RAM and soak pending |
| T46–T47 | No flash operations enabled. Compiled boot guard is untested. Physical recovery/flash safety pending |
| T48 | Modified MPL BSP source and notices retained, tool/runtime/per-file hashes, deterministic project source package. Vendor-runtime source and binary-release obligations not claimed closed |

## Verification commands

`./scripts/check.sh`: original **330 assertions**, **13 workstation suites**, ASan/UBSan and strict GCC warnings, pitch WAVs, integrated demo and stdlib-only native bridge/import/save/rebind checks. `make benchmark`: two host-only configurations, eight voices/two tracks/four effects. `make browser-test`: isolated native-server workflows and screenshots. `make web-test`: static C/WASM touch/audio, queued saves, IndexedDB reload and subpath tests without server API calls, plus piano-roll/quantized recording/visual sampler/mobile persistence regressions and a mixer/arp/HOME-hold/generated-MIDI recording suite. `scripts/build-target.sh --baseline` and without that flag: actual PI32v2 links/range checks. `scripts/verify_sources.py`: 64 vendored BSP files. `scripts/package_source.py`: reproducible source snapshot. See BUILD.md for dependencies and MEMORY_MAP.md for measured values.

Unfinished work remains visible above: physical USB/codec/panel/resource/recovery qualification, safe flash persistence/transfer, optional EPROM support, and the documented reverse-loop limitation. The historical REVIEW.md describes the earlier Cursor-only milestone.
