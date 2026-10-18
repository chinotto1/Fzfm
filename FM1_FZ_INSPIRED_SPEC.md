# FM1 FZ-Inspired Sampling Synthesizer
## Complete implementation brief for an AI coding agent

Specification revision: 1.0
Prepared: 2026-10-17
Target: existing M-VAVE FM-1 hardware, without hardware modifications
Status: product requirements and engineering instructions, NOT an implemented or hardware-validated firmware
Working project identifier: `fm1-fz-sampler` (not a claim of Casio endorsement)

---

# 0. Your assignment

You are the lead embedded-audio engineer implementing a standalone sampling synthesizer and groovebox on the M-VAVE FM-1. Build working firmware, a host-testable audio core, a simulator, a safe build/package workflow, and an OPTIONAL instrument-import website.

The instrument combines an FZ-1-inspired sampling/synthesis engine and deep editing, a spontaneous free-recording and retrospective MIDI workflow, and X0X-like pattern/song construction. The user interface and behaviors below are the product, not suggestions to replace with a generic synth UI.

Read this entire document before modifying code. Preserve it as the authoritative specification. Create a requirement-to-code-to-test traceability file. Implement incrementally in runnable vertical slices. Do not finish with a plan, mockup, or collection of unconnected DSP examples and call that firmware.

## 0.1 Priority and interpretation

- **MUST / REQUIRED** identifies an agreed product requirement or a safety/correctness requirement necessary to implement it.
- **DEFAULT** specifies an initial engineering choice where the user did not set an exact value. Implement it unless a measured conflict is documented in an architecture decision record (ADR).
- **TARGET** is a performance goal to measure, not an assertion about existing hardware capability.
- **OPTIONAL / LATER** must not displace required sampling, synthesis, recording, arrangement, or editing features.
- Sources `[S1]` onward are listed in section 25. Their current source trees are references, not immutable specifications. Pin exact commits before reuse.
- Requirements in this document take precedence over the donor firmware's interaction design. Do not inherit conflicting shortcuts.
- If a requirement conflicts with measured resources, show the smallest concrete tradeoff, cost, and alternative. Do not silently omit it, invent extra RAM, change the sample rate, or impose an inherited four-track limit.
- Report separately: specified, implemented, host-tested, target-compiled, and device-tested. A simulator is not evidence of hardware timing, USB compatibility, flash safety, or physical recovery.

## 0.2 Non-negotiable product identity

1. Normal use is entirely on the FM-1. No browser, companion editor, account, cloud service, or computer is needed to sample, map, play, edit, synthesize, sequence, arrange, mix, or save.
2. Direct USB-C audio from an iPad is a primary sampling source. The iPad is the USB host; the FM-1 is the audio/MIDI peripheral.
3. The main audio engine renders at **36,000 frames/second**. Default newly captured audio is **16-bit mono PCM**. The stereo output mixer also runs at 36 kHz. USB/codec transport can use another supported rate only through explicit boundary conversion.
4. Pitch transposition is vintage sampler-style variable-speed playback: pitch and duration change together. Do not substitute independent pitch shifting/time stretching.
5. There are two user-facing instrument types: **MULTI SAMPLE** and **SAMPLE**. Additive and single-cycle sources live inside SAMPLE, not a confusing third instrument workflow.
6. Every new instrument automatically receives a sequencer track and an empty pattern. Unrecorded sample slots do not reserve full audio buffers.
7. Track/sample/pattern storage is dynamically allocated from verified budgets. Simultaneous voices and DSP have separate bounded limits.
8. Required deep functions include sample mangling, multi-stage envelopes and multi-section looping, Mix Write, Cross-Mix, waveform extraction/drawing, and **48-harmonic additive synthesis**.
9. The main filter may be a new FZ-1-inspired implementation. X0X/Open303 is not mandatory and must not be disguised as an FZ-1 model.
10. Four shared send-effect slots, a mixer, and baking effects into samples/patterns are required.
11. An optional website imports local drum-machine EPROM files and single-cycle waveforms. It never becomes a prerequisite for the core instrument.

## 0.3 Out of scope unless subsequently requested

Do not spend the initial budget on Wi-Fi, Bluetooth, new hardware, removable storage, a mandatory touch editor, an entire inherited FM/physical-modelling engine collection, commercial sample-library bundling, a full DAW, arbitrary MP3 streaming, or literal reproduction of floppy-drive/parallel-port operations. FZ-1 disk and patch compatibility can be a later separately scoped feature. The goal is musical behavior, not reproducing obsolete media hardware.

---

# 1. Requirement register

Keep these identifiers in code comments, tests, and implementation status reports.

| ID | Required outcome |
|---|---|
| R01 | Standalone sampling, editing, synthesis, sequencing, arranging, mixing, and saving |
| R02 | Direct iPad-host USB audio reception and simultaneous USB MIDI |
| R03 | Fixed 36 kHz engine; 16-bit default captures; explicit transport conversion |
| R04 | Vintage rate-based transposition, not time stretching |
| R05 | MULTI SAMPLE: 16 logical sequentially armed capture slots, consecutive-key mapping |
| R06 | SAMPLE: one sample spans keyboard; octave anchors automatically divide pitch zones |
| R07 | Instrument creation automatically creates/selects a sequencer track |
| R08 | Flashing OCT+ during sampling; one press immediately opens playable sequencing/performance |
| R09 | Free playing/live recording with inferred BPM and loop length |
| R10 | Hold REC retrospectively captures played MIDI and analyzes timing |
| R11 | Hold SEL undoes the latest take/edit; no accidental clear-all behavior |
| R12 | X0X-like step sequencing, pattern changes, chaining, and song arrangement |
| R13 | Tap EDIT opens editing; further taps cycle tabs |
| R14 | Edit target ALL samples or one sample, persistent across tabs |
| R15 | Vertical lists navigated with knob 1; knobs 2-4 edit selected item's fields |
| R16 | Keys enter notes, ranges, and deliberately activated numeric values |
| R17 | Hold EDIT opens one-key category naming with automatic numbering |
| R18 | FZ-1-inspired sample manipulation, multi-stage envelopes, and multi-section loops |
| R19 | On-device 48-harmonic additive synthesis, saved as editable synthesis parameters |
| R20 | On-device preset-wave creation, cycle extraction, and waveform point editing |
| R21 | HOME shows/selects tracks and exposes cutoff, resonance, volume, and pan |
| R22 | Custom FZ-1-inspired analog-modelled filter with per-voice modulation |
| R23 | GLO mixer provides track levels, pan, sends, mute/solo, and return controls |
| R24 | FX opens four project-wide send-effect slots and their editing |
| R25 | Bake selected processing into audio and unsend only the affected material |
| R26 | Optional local-file EPROM decoder/auditioner/kit importer |
| R27 | Optional AKWF/general single-cycle instrument creation/import |
| R28 | Dynamic memory allocation with honest storage, RAM, voice, and CPU limits |
| R29 | Transactional saves, destructive edits, imports, and bounded undo |
| R30 | Safe build, packaging, backup, recovery, and user-authorized flashing |
| R31 | Common instrument/asset format across captured, imported, and synthesized sources |
| R32 | MIDI routing and clock without feedback loops or phantom recording events |
| R33 | Hardware-faithful UI/input simulator plus automated audio/storage/timing tests |
| R34 | Complete corresponding source, notices, pinned dependencies, and reproducible build instructions |

A requirement can be unfinished during development; it cannot disappear from the final product definition.

---

# 2. Evidence, source selection, and assumptions

## 2.1 What is supported by the references

The original Casio operation manual documents 36 kHz/16-bit sampling and 48-harmonic sine synthesis. Its editing coverage includes Mix Write, Cross-Mix, Reverse Write, truncation, DCA/DCF envelopes, looping, LFO, and waveform construction. Printed pages 1, 34, and 63-73 are particularly useful; scanned PDF page numbers differ. This is a reference for the feature audit, not evidence that our DSP already reproduces the original. [S1]

Melodee documents host playback into the FM-1 and USB recording from it, alongside MIDI. Its build notes describe the USB/audio implementation and resource handling. That establishes an implementation to investigate for the required receive direction; it does not constitute a test of our new firmware on the user's iPad. [S2, S3]

SLOOP is a behavioral reference for playing a free take and closing it to obtain a loop/tempo. X0X is a behavioral/source reference for independent part patterns, queued changes, range chains, motion recording, and song arrangement. Copy their useful behavior, not every shortcut, global assumption, or track count. [S4, S5]

Felucca, its separate board-support repository, and official FM1 Open are alternative starting points. They have different licensing and platform arrangements. Official FM1 Open's README describes an application/SDK split and a separately supplied native engine; do not assume every component is open or compatible with any GPL code copied into the project. [S6, S7, S8]

FZ-1 filter research and the ALM Fizzle Guts hardware are useful references. ALM's project controls a physical MB87186 chip; it is not a software emulator of that chip. Treat the switched-capacitor interpretation as published reverse-engineering evidence, not a transistor-level netlist proven in our project. [S9, S10]

## 2.2 Correct the earlier design assumptions

The conversational figures of approximately 96 KiB RAM, 1 MiB flash, and 496 KiB available sample storage are NOT a validated memory map for this firmware. Do not use them as linker constants, guaranteed recording capacity, or a reason to stop implementing. Storage available to another firmware is not automatically available to ours.

Determine physical memory, mapped memory, safe app/data regions, usable RAM banks, and reservations from the selected platform, linker output, device identity, and non-destructive tests. Label any unverified result explicitly.

Also distinguish:

- Maximum original sampling rate versus original converter/output clock: 36 kHz is OUR mandated internal rendering rate; do not claim it proves every historical FZ-1 stage ran at that rate.
- USB host-to-device playback versus device-to-host recording: our sampler requires the former, not merely a device advertised as an audio input to a DAW.
- Sample storage versus working RAM versus executable-code placement versus real-time processing budget.
- Firmware emulation of analog behavior versus new physical analog circuitry.
- A 16-slot user interface versus sixteen preallocated audio recordings.
- A shared effect being bypassed versus its RAM actually being returned to the allocator.

## 2.3 Select a base before constructing a large application

Inspect and compare, at exact commits:

- Felucca and `felucca-bsp`: board access, audio output, display, controls, USB, flash, update packaging, simulator possibilities.
- Melodee: host-to-device USB audio, MIDI coexistence, stream clocking, diagnostics, and relevant resource work.
- SLOOP: free-take workflow/timing implementation.
- X0X: sequencing, patterns, chain/song behavior, automation, and selected effects.
- Official FM1 Open: SDK/build availability, drivers, engine boundary, and terms.

Choose ONE primary board/platform layer. Put all board dependence behind a narrow adapter. Import selected proven components rather than merging multiple entire applications or carrying several incompatible USB stacks.

A custom portable engine over a small board-support layer is the preferred architecture if it builds and has verified audio/USB paths. A carefully reduced existing firmware is acceptable when it provides a materially safer starting point. Document the decision with source commit hashes and measured baseline sizes.

---

# 3. Phase-zero hardware and build gate

Complete this investigation before promising capacity, flashing a new memory layout, or integrating every feature at once.

Create `docs/HARDWARE_AUDIT.md`, `docs/MEMORY_MAP.md`, `docs/BOOT_RECOVERY.md`, and `third_party/sources.lock`.

Record:

1. Exact FM-1 board/chip identifiers actually observed, target ISA/ABI, compiler version and flags, supported execution mode, and clock configuration.
2. Physical versus usable RAM, cache/SRAM distinctions, DMA-accessible banks, audio-code placement, stacks, heap/arena regions, and reserved boot/system state.
3. Flash identity/capacity, actual accessible address range, erase/program granularity, XIP/cache restrictions, known protected regions, and partition ownership.
4. DAC/codec and USB-supported transport formats/rates, DMA block arrangement, endpoint constraints, and whether input/output share a clock domain.
5. Exact display dimensions/pixel format, input scan rate, knob type/ranges, key/LED mapping, button illumination, and calibration persistence.
6. Meaning of the physical SEL button versus any separate SELECT rotary control. Do not invent a push action on a knob that cannot be pushed.
7. Baseline build, package validation, boot flow, application update entry, recovery method, and what data an upstream installer overwrites.
8. Source licence per imported component, supplied blobs, SDK terms, build tools, and required notices.

Rules:

- Do not assume this is an ARM/Cortex-M, ESP32, STM32, or Daisy platform. Use the actual discovered target/toolchain.
- Start with the safest verified clock/core configuration. No speculative overclocking, voltage changes, cache reclamation, expanded flash use, or CPU1 bootstrap changes as a dependency of the first sampler.
- Experimental second-core optimization may follow a stable baseline, with its own device tests and recovery plan. Do not assume it doubles DSP capacity. Some upstream configurations have explicit experimental qualifications. [S2, S7]
- Reproduce a baseline build before changing functionality. Keep a known-good binary plus hashes and the exact source snapshot.
- Read-only identification and host-side builds are not permission to flash. Obtain explicit user authorization before any device write. Never remotely execute an opaque recovery script without inspection.
- Back up everything the chosen path can safely read, especially user content and calibration. Say what could not be backed up.
- Do not copy fixed addresses from a different firmware's recovery notes into our partition layout.
- Produce a practical known-good recovery procedure before testing a custom image. Do not describe a recovery path as verified without a device test.

If hardware is unavailable, continue with source inspection, portable code, simulations, and target builds where possible. Mark hardware tests pending and prepare a precise user-assisted test procedure. Do not claim a usable device release yet.

---

# 4. Repository and architecture

Use a portable, allocation-disciplined C core compatible with the actual target compiler. C++ is acceptable only for a justified imported module, with exceptions/RTTI/runtime costs controlled. A modern host compiler must test the same core. Browser emulation should compile it to WebAssembly rather than rewriting the synth in JavaScript.

Suggested logical layout (adapt paths to the selected build system, not responsibilities):

```text
AGENTS.md
FM1_FZ_INSPIRED_SPEC.md
README.md
CHANGELOG.md
LICENSE
THIRD_PARTY_NOTICES.md
third_party/sources.lock
firmware/
  platform/                 board adapter, USB, DMA, storage, clock, controls
  core/                     IDs, project model, parameters, transactions
  audio/                    engine36k, SRC, sample readers, voices, mixer
  sampler/                  capture state machine, regions, automatic mapping
  dsp/                      filter, envelopes, LFO, additive, loops, effects
  sequence/                 events, recorder, history, tempo, patterns, song
  ui/                       input arbitration, screens, lists, dialogs
  storage/                  serialized records, blob store, journal, migration
  import/                   validated instrument bundle receiver
host/
  simulator/                actual UI + actual core + deterministic input
  render/                   offline audio fixtures and comparisons
web/
  importer/                 local EPROM and single-cycle tools, transport
  emulator/                 optional developer/device preview
schemas/                    file/protocol definitions and generated IDs
scripts/                    bootstrap, build, check, package, memory report
tests/                      unit, integration, fuzz, UI, DSP, system tests
docs/
  REQUIREMENTS_STATUS.md
  HARDWARE_AUDIT.md
  MEMORY_MAP.md
  CONTROL_MAP.md
  USB_AUDIO.md
  FILTER_MODEL.md
  DATA_FORMAT.md
  BOOT_RECOVERY.md
  DEVICE_TESTS.md
  adr/
```

## 4.1 Real-time rules

The render callback, USB/DMA interrupt paths, and other deadline-critical code must never perform blocking I/O, unbounded searches, heap allocation/free, JSON parsing, filesystem work, flash erase/program, logging to a blocking console, or full-screen drawing.

Use bounded queues for control changes/events, bounded arenas for working state, and prebuilt immutable descriptors for assets. Commit control changes at a defined audio boundary; schedule note events at their intra-block offset. Use explicit ownership and lifetime rules for anything shared with interrupts.

Document concurrency and cache/DMA coherence. Do not treat `volatile` as a complete synchronization strategy. Provide target-appropriate barriers or brief proven critical sections. Never invalidate a sample pointer while a voice reads it.

Every loop in real-time code must have a known bound. No unbounded nonlinear-filter solver iteration. No full-project copy in response to a knob turn.

## 4.2 Platform interface

Define a minimal interface for monotonic time, input events, PCM RX/TX, presentation, persistent storage operations, update entry, and resource diagnostics. Portable code must not contain chip register addresses.

A conceptual engine interface is:

```c
/* Illustrative interface, not an assertion of existing BSP functions. */
engine_init(config, arenas);
engine_enqueue_event(timestamp, event);
engine_enqueue_control(change);
engine_render_36k(left, right, frames);
capture_accept_36k(mono, frames, first_frame_timestamp);
engine_service_nonrealtime(budget);
```

Use consistent units and typed wrappers/conventions for frames, bytes, MIDI notes, cents, microseconds, and musical time. Avoid reusing a bare integer for all of them.

---

# 5. Project model and storage

## 5.1 Core entities

Use stable handles, not persisted raw pointers or array positions that change when a track is deleted.

- **SampleAsset**: immutable audio/waveform blob, encoding, frames, channels, native rate or cycle metadata, checksum, provenance, storage location, and generation.
- **SampleRegion**: reference to an asset, start/end, source root note/tuning, playback mode, loop program, gain, sample-specific filter/envelope settings, pan/send trims, and bake-routing metadata.
- **Instrument**: MULTI SAMPLE or SAMPLE; ordered region/zone handles; automatic-mapping policy; common synthesis defaults; name/category; optional additive recipe; revision.
- **Track**: instrument reference; mixer/performance controls; MIDI routing; selected/playing/queued pattern handles; mute/solo state; recording and allocation state.
- **Pattern**: a sparse list of musical events, duration, playback rate, swing/quantization view, automation/parameter locks, and optional render/freeze reference.
- **Chain/Song**: ordered entries referencing per-track patterns/mutes/repeats, with explicit global timing policy.
- **FxSlot**: algorithm identity/version, parameters, return level/pan, resource requirements, and current state.
- **Project**: tracks, patterns, arrangement, tempo/clock policy, references to instruments/assets, FX slots, settings, format version, and persistence generation.

Keep sample bytes, region metadata, and track playback state separate. Duplicating an instrument or pattern must not automatically duplicate its audio. A destructive edit uses copy-on-write and updates only intended references. Check shared assets before deletion.

## 5.2 Allocation

Allocate track headers, pattern events, regions, voice states, FX buffers, undo records, and capture buffers from distinct reported budgets or a tracked shared arena. Do not reserve a full 16-sample recording pool for every kit.

A small validated safety ceiling on object handles is acceptable; an arbitrary musical limit inherited from a donor is not. Distinguish:

- project tracks that exist;
- simultaneous note voices;
- MIDI channels available for external routing;
- sample flash consumed;
- sample/capture RAM consumed;
- sequence event capacity;
- active FX RAM and CPU;
- temporary/undo storage.

Use admission checks before starting a recording, importing a bundle, rendering an effect, or enabling a heavy FX combination. All failures retain the previously valid state and give a specific explanation such as `SAMPLE STORAGE FULL` or `FX WORKING RAM FULL`.

## 5.3 Serialization

Define a versioned binary format with lengths, encoding IDs, endian rules, integrity checks, bounded counts, stable handles, and an atomic commit generation. Never persist compiler struct padding, pointers, or raw ABI-dependent layouts.

Use an append/journal or equivalent verified copy-on-write scheme. Write payload, verify it, write descriptors, then publish the commit marker last. On interrupted power, recover either the old complete project or the new complete project, not a mixture. Garbage collection may remove only unreachable generations, including undo references.

Do not silently steal a firmware partition to increase sample capacity. Future firmware growth must have an explicit migration/storage policy. There may not be space for dual full firmware banks; use only an actually supported recovery/update architecture.

## 5.4 Honest capacity calculations

At our default 36 kHz, 16-bit mono format:

```text
bytes_per_second = 36000 * 2 = 72000
recording_seconds = allocatable_audio_bytes / 72000
stereo_bytes_per_second = 144000
```

Examples are raw payload only, not promises of capacity:

| Material | Raw PCM payload |
|---|---:|
| 16 hits x 0.25 seconds, mono | 288,000 bytes |
| 16 hits x 0.50 seconds, mono | 576,000 bytes |
| 4-bar loop at 120 BPM in 4/4, mono | 576,000 bytes |
| Same 4-bar loop, stereo | 1,152,000 bytes |
| 1,024-point 16-bit single cycle | 2,048 bytes |
| 48 harmonic levels stored as uint16 | 96 bytes |

Track metadata, codec indexes, undo, erase-sector slack, and free transaction space are additional. Display achievable capture time based on the limiting resource, not just total free flash.

Compact 8-bit/companded ROM data can remain compact with bounded on-read decoding. PCM16 is the first required capture format. Optional ADPCM requires restart/seek checkpoints, bounded decoding, correct loop/reverse behavior, and tests; do not present it as free fourfold savings with no consequences. General MP3 decoding is not a first-release dependency.

---

# 6. Audio rate, signal flow, and pitch

## 6.1 Fixed internal domain

All musical voice generation, sample playback, primary filters, envelopes expressed in time, the mixer, and FX integration belong to a **36,000 Hz nominal frame clock**. No hidden fallback to 44.1/48 kHz under load. Device clock tolerance is measured separately.

Use adequate internal headroom and deterministic rounding; 16-bit assets do not require overflowing a 16-bit mix accumulator. Optimize to fixed point if the target warrants it. A float implementation is a host reference, not proof of acceptable target CPU load. Document saturation, accumulator width, and every numeric conversion.

## 6.2 USB/codec boundary conversion

Retain a proven class-compliant USB transport rate, initially 44.1 kHz if supported by the selected implementation. Add 48 kHz only after it is really supported/tested. Melodee provides a receive-path reference, but its current descriptors/rings must be inspected at the pinned commit. [S2, S3]

Nominal conversion ratios:

- 44.1 kHz into 36 kHz: 40/49.
- 36 kHz into 44.1 kHz: 49/40.
- 48 kHz into 36 kHz: 3/4.

Implement streaming SRC with carried phase/history, anti-alias filtering where required, and a measured latency/resource budget. Account for physical clock drift with the correct USB clocking/feedback arrangement or bounded rate correction. Do not independently round every block or assume a fixed integer number of 44.1 kHz frames in each USB millisecond.

Transport resampling should not add gratuitous dirt. Musical character belongs in the playback/filter model. Rate changes and reconnections flush or migrate stream state safely without changing saved sample tuning.

## 6.3 Musical sample transposition

For an ordinary sample:

```text
phase_increment =
  (asset_native_rate / 36000)
  * 2 ^ ((played_note - root_note + tuning_cents / 100) / 12)
```

For a periodic table of N points:

```text
phase_increment_in_table_points = N * desired_frequency_hz / 36000
```

Implement fractional phase, correct forward/reverse wrapping, deterministic root-note tuning, pitch bend, and interpolation. One octave up must double pitch and approximately halve a one-shot's duration; one octave down does the reverse.

Provide a clearly named, useful classic playback baseline. Linear interpolation is an implementation baseline, not a verified FZ-1 algorithm. RAW/nearest-neighbor is an optional character mode, not automatically the authentic one. A calibrated FZ-inspired mode must be supported by measurements/listening notes. Do not add noise, bit reduction, or severe aliasing merely to label something vintage.

Imported ROM assets may retain lower native rates and original encoding. They still render through the 36 kHz engine. Single-cycle assets use cycle-aware tuning; a WAV header alone must not assign their musical root.

## 6.4 Basic signal flow

```text
USB host PCM -> RX/clock handling -> SRC -> input select/gain -> capture detector
                                                        -> sample storage

live/sequenced MIDI -> zone lookup -> sample/cycle reader
                   -> per-voice FZ-inspired filter + modulation
                   -> amplitude envelope / region gain
                   -> per-region send taps and track mix
                   -> track gain/pan -> dry mix + four post-fader sends
                   -> shared FX wet returns -> master protection/headroom
                   -> SRC to codec/USB transport -> output
```

Specify whether a send algorithm accepts mono or stereo and how pan is handled. The diagram is the intended logical routing; implementation must avoid applying track gain twice.

Input monitoring is separate from recording and from the instrument dry mix. Default routing must not return USB-host playback into the same host's monitored input, creating feedback. A metronome is never accidentally sampled or recorded as MIDI.

---

# 7. USB audio capture and persistence scheduling

Direct USB audio capture is required; a browser microphone bridge is not an acceptable substitute. There is prior FM-1 receive code to reuse. [S2, S3]

Implement host-to-FM1 stereo reception with input source LEFT, RIGHT, or MONO SUM. Default MONO SUM is scaled to avoid a +6 dB sum overload. State clearly when anti-phase material can cancel. Show input level, clipping, threshold, and available capture duration.

Support iPad synth/DAW playback to the FM-1 without special device drivers. Verify actual routing with at least one iPad music app and the tested iPadOS/device version. Also provide a deterministic computer-host test source. Record cable/hub/power conditions in results; do not require a hub unless the tested configuration needs one.

USB requirements:

- Audio RX and MIDI coexist without endpoint conflicts.
- Ring buffers are bounded, with overrun/underrun counters and well-defined recovery.
- Packed 24-bit formats, if advertised, are decoded correctly; otherwise advertise only implemented formats.
- Packet-size/alternate-setting/control requests are validated.
- Disconnect, host sleep, rate/format change, and stopping an app do not crash the synth or corrupt the current take.
- USB monitor/output routing cannot create a default feedback loop.
- MIDI note traffic remains responsive while PCM streams and the UI scrolls.

## 7.1 Do not overlook flash during capture

This is a required engineering gate. A USB RX endpoint alone does not prove reliable persistent sampling. Flash erase/program can interfere with code/data fetch and audio deadlines; X0X explicitly avoids saving while playing in its documented implementation. [S5]

First deliver bounded RAM capture with an honest maximum duration. Then choose and validate one of these persistence approaches:

1. Stage a take in available RAM and commit at a safe point; report VOLATILE until committed.
2. Stream bounded chunks to pre-erased storage only after proving the entire active capture/USB/interrupt path, constants, DMA buffers, and necessary calls remain safe during writes.
3. Use another independently verified storage facility only if it actually exists on the user's unmodified unit.

No imaginary extra RAM, external flash, SD card, or host cache may satisfy the standalone requirement. Pre-erasing does not by itself make programming safe. Measure the maximum service interruption and all relevant fetch/interrupt constraints.

During automatic kit capture, show BUSY while a slot commits and rearm only when safe. Never pretend to listen while samples would be lost. The first milestone can bound capture by RAM, but final declared limits must reflect the proven storage path.

OCT+ should leave the capture screen without waiting for a long blocking save: continue playing from a pinned RAM asset or defer persistence to a safe point. Keep its lifetime reserved. Do not block the UI/audio callback or make the first key press crash because a background save freed the sample.

# 8. Input model and controls

Implement a central input arbiter/state machine. No individual screen should independently interpret the same button-down as a tap while another interprets it as a hold.

## 8.1 Canonical screen/control contract

`HOME` and `SEQUENCER PERFORMANCE` refer to the same playable overview, not two redundant screens. `SEQ` opens step/pattern/song workspaces. The keyboard plays notes on HOME. Do not copy X0X's pattern-key HOME behavior over that requirement.

| Context/control | Required behavior |
|---|---|
| HOME | Playable overview with all tracks accessible; selected track highlighted |
| ALGORITHM on HOME | Select track; clamp/wrap behavior documented and consistent |
| HOME knob 1 | Selected-track cutoff |
| HOME knob 2 | Selected-track resonance |
| HOME knob 3 | Selected-track volume |
| HOME knob 4 | Selected-track pan |
| EDIT tap | Enter selected-track editing; subsequent taps advance/wrap tabs |
| EDIT hold | Quick instrument-name category picker; must not also change tabs |
| Knob 1 in EDIT/parameter lists | Move highlighted row vertically, scrolling as needed |
| Knobs 2-4 in EDIT/lists | Edit highlighted row's displayed fields |
| SEL tap in a list/dialog | Enter selected item or confirm the displayed non-destructive choice |
| SEL hold in performance/recording | Undo latest take/overdub, not clear the entire project |
| SEL hold in editing | Undo the latest edit transaction; ALL-samples edit is one transaction |
| REC tap in performance | Arm/start/close/stop normal recording according to transport state |
| REC hold in performance | Retrospective MIDI capture; no unwanted tap action |
| OCT+ during sampling | Finish/pause capture safely and go straight to playable HOME |
| GLO | Mixer; subsequent taps select FX send 1-4 for knob 4 |
| FX | Open selected FX slot; further taps cycle slots 1-4 |
| SEQ | Enter STEP; further taps cycle STEP, PATTERNS, SONG |
| PLAY | Start/stop sequencer; screen changes never implicitly stop it |
| SAVE tap | Save project safely; show queued/volatile status when a save must wait |
| OCT-/OCT+ on playable HOME | Keyboard octave |
| OCT-/OCT+ in explicit step/pattern page | Page navigation, clearly labelled |

This contract assumes the named physical controls exist; reconcile only board-label differences during the hardware audit. Do not invent extra buttons, click-encoders, or touch capability.

## 8.2 Tap/hold arbitration

DEFAULT hold threshold: approximately 500 ms, configurable internally after user testing. Debounce independently of the hold threshold. A held button must fire once, never repeatedly erase actions while held.

Record the physical button-down timestamp. Decide tap versus hold without losing timing:

- A short REC press closes a free take at its down timestamp, not its release or debounce completion.
- A held REC captures history ending at the down timestamp, excluding the extra half second spent holding it.
- A held EDIT opens naming without also advancing the edit tab on release.
- A held SEL undoes without first confirming an operation.
- Use buffered events to reconcile short-press recognition delay; do not add an unexplained half-second recording error.

Explicitly test holds across screen changes and overlapping held keys. Modal dialogs must capture input predictably. During a destructive confirmation, long SEL should cancel/undo the relevant pending action safely rather than confirm then undo unrelated work.

## 8.3 Persistent edit header and list behavior

Every edit tab displays instrument name, tab name, and target ALL or SAMPLE nn. Selection/scope and each tab's scroll position persist when tabs change. Dynamic parameter lists must not jump to another row merely because its value changes.

Knob 1 selects; knobs 2-4 edit. Bottom labels show the current assignments. Unused knobs do nothing. Parameter changes must use soft pickup or relative deltas appropriate to the actual control hardware; selecting another track must not jump its cutoff to a physical pot's old position.

The list is the primary editor. A small waveform/envelope/harmonic preview is secondary and must not replace readable parameter names with an icon-only interface.

## 8.4 Keyboard role and numeric entry

Normal keys audition/play the selected instrument, including while scrolling and changing parameters. They change role only in a visibly active picker:

- `PICK NOTE`: next key assigns a note/root.
- `PICK LOW`, then `PICK HIGH`: two sequential presses define an inclusive range. Reversed input is normalized after showing the result. Octave navigation remains available.
- `PICK SAMPLE`: a key chooses its underlying region. After selection, normal auditioning resumes.
- `ENTER NUMBER`: ten physical keys labelled 0-9 on screen, plus explicit backspace, sign/decimal where legal, confirm, and cancel.

Use a keyboard legend tied to actual physical key indexes, not assumed printed note labels. DEFAULT numeric entry is activated by SEL on a numeric row. SEL confirms; HOME cancels the entry and returns to the preceding editor without leaving it. Only a later HOME press leaves the editor. Do not let HOME cancellation also launch a pattern or play a note.

Reject overflow and out-of-range numbers before committing. Show units. A partly typed value must never be heard until confirmed unless an explicit preview mode is indicated. Range/numeric keys must not enter MIDI history, send MIDI notes, or accidentally trigger drum sounds.

## 8.5 Quick naming

Hold EDIT, show category-to-key mapping, tap a key, and return immediately. DEFAULT categories include DRUMS, SYNTH, PAD, LEAD, FX, BASS, KEYS, PERC, and LOOP.

Assign the next available number within the project/library naming policy: DRUMS 1, DRUMS 2, PAD 1, etc. No character-by-character typing is required. Let the user pick another number without a text editor. Keep track identity separate from display names and prevent ambiguous duplicate generated names.

---

# 9. Instrument creation and automatic sampling

A clearly accessible NEW INSTRUMENT command offers exactly:

- **MULTI SAMPLE**: a kit/collection of one-shots.
- **SAMPLE**: a pitched instrument with optional octave-spaced source anchors.

Creating either reserves only minimum metadata, creates/selects a track and empty pattern, and opens its source/capture screen. If creation fails, do not leave half a track or orphan asset. If the user cancels an entirely empty creation, remove it cleanly with undo where appropriate.

Default naming is DRUMS n for MULTI SAMPLE and SYNTH n for SAMPLE. Naming can be changed later by hold EDIT.

## 9.1 MULTI SAMPLE auto-capture

Provide 16 logical slots. Each empty slot listens in sequence and maps its captured sound to the next keyboard note. Sixteen is the workflow size, not a requirement to fill all slots or preallocate equal-length storage.

State machine:

```text
CONFIGURE -> WAIT_INPUT -> PRETRIGGER_COMMIT -> CAPTURING
          -> END_DETECTED -> FINALIZE_ASSET -> COMMIT_OR_PIN
          -> ADVANCE_SLOT -> WAIT_INPUT
```

Additional states: PAUSED, RETRY, STORAGE_FULL, INPUT_LOST, FINISHED, CANCELLED. Transitions must be tested, including OCT+ from every state.

DEFAULT detection parameters, adjustable on-device:

- Start threshold: initially around -36 dBFS, then tuned with real material.
- Hysteresis: separate lower stop threshold.
- Short pre-trigger buffer: target about 10 ms to retain transients.
- Minimum valid recording length: configurable, not a hardcoded rejection of short percussion.
- Silence hold: initial target about 150-250 ms, user adjustable.
- Maximum sample length derived from admitted storage/RAM; visible before capture.
- Manual start/end option for sustained sounds or sources with no silence.

Threshold detection must not normalize, gate, high-pass, or truncate the audio beyond the user's selected preprocessing. Capture level controls and actual sample gain are separate.

At the end of a valid sound:

1. Retain the pre-trigger and the full chosen decay.
2. Trim only the configured excess silence; no automatic removal of quiet cymbal tails.
3. Make the new region playable as soon as its RAM/storage lifetime is safe.
4. Advance to the next empty slot and listen again.
5. Stop at 16 or when the user exits; do not wait for an imaginary seventeenth sample.

Controls must allow retry last, skip slot, finish early, and resume later. Hold SEL undoes the last slot recording without discarding the rest.

Detecting separate hits assumes usable separation. Overlapping sounds or a continuous drum loop cannot be guaranteed to split into independent original instruments. Show the limitation and provide manual capture/end rather than claiming source separation.

### Mapping

Default internal mapping is sixteen consecutive chromatic MIDI note numbers starting from a configurable base. DEFAULT base is MIDI note 36; display both name and number when needed. Board key labels/octave offsets may differ, but slot order must remain consecutive.

A white-key/pad layout can be an option, not an involuntary inheritance from SLOOP. A kit region plays its recorded pitch by default rather than transposing with every mapped key. User tuning remains editable. One-shot is the default; gated mode, sample mute, and choke groups belong in EDIT.

## 9.2 SAMPLE pitched capture

Offer visible sample anchors one octave apart. Default anchor pitch class is C; any pitch class, such as F# or A, is selectable. The screen always tells the user which root note the next recording represents.

- One recorded anchor covers the entire playable/MIDI range by rate transposition.
- Adding more anchors partitions the keyboard automatically by the closest recorded root in semitone distance.
- Empty anchor slots use no audio storage and do not create silent gaps.
- Lowest/highest populated anchors cover the remaining lower/upper notes.
- Define and test one midpoint tie rule. DEFAULT ties choose the lower anchor.
- Changing a root or adding/removing an anchor recomputes only the automatic zones and preserves manual overrides deliberately marked as such.
- Root note and exact tuning are metadata, not destructive resampling on each map change.

Example: roots MIDI 48 and 60 meet at note 54. With the lower-root tie policy, notes up to 54 use root 48 and notes from 55 use root 60. The exact octave labels depend on the display convention, which must be documented.

Capture one anchor, play immediately, and add another later. The device does not have to auto-detect the source's pitch to support this workflow. Manual root confirmation is sufficient; incoming MIDI-note assistance or pitch detection is optional and must not silently mislabel a sample.

## 9.3 OCT+ instant performance

While capture is active, OCT+ flashes. Use its physical illumination if verified; otherwise keep a prominent flashing on-screen OCT+ indication and explicitly document the hardware limitation.

Pressing OCT+:

- stops auto-arming another slot;
- finalizes a valid in-progress take at the press boundary, or leaves an empty slot empty;
- selects the newly created track;
- opens HOME/SEQUENCER PERFORMANCE with the keyboard live;
- retains recorded slots and unfilled slots for later continuation;
- creates no extra duplicate instrument/track;
- does not alter the keyboard octave as a side effect;
- does not stop an already playing project or start recording notes automatically.

TARGET: visible screen change and playable-ready state within 100 ms, excluding only documented unavoidable hardware limits. Persistence may remain pending, but the instrument must be safely usable from its pinned data.

---

# 10. HOME performance screen

The home page is not a patch browser. It is the project's playable track overview.

Show project/tempo/transport status, the track list, track activity and mute/record state, selected instrument name, selected pattern, and selected-track cutoff/resonance. Volume and pan stay visible at the bottom.

More tracks than fit must scroll/page; they must not disappear behind a four-track-only UI assumption. Keep selected-track controls visible independent of list position. Use ALGORITHM to select tracks and leave keys playable.

The four home controls are cutoff, resonance, volume, pan. They operate the selected track as a whole regardless of which sample was last selected in EDIT. Selecting SAMPLE 04 in EDIT must not make the home cutoff affect only that sample.

HOME volume/pan and GLO volume/pan refer to the same state, not cascaded duplicate gains. Cutoff/resonance are common performance controls combined with per-region settings and each voice's modulation. Show their base value, not a misleading claim that every sounding voice currently has the same instantaneous cutoff.

Knob movements can be recorded as motion/automation in sequencer recording mode. Do not record them while entering numeric values or operating a picker. Avoid changing a sound when merely entering/leaving a screen.

---

# 11. Instrument editing and sample mangling

## 11.1 Tabs

Tap EDIT enters the selected track's instrument; repeated taps cycle:

`SAMPLE -> LOOP -> MIX -> AMP -> FILTER -> MOD -> WAVE -> MAP -> RENDER -> SAMPLE`

This is the initial complete tab set. Keep it consistent and avoid burying required functions inside an unrelated donor menu. Split a tab only when measured readability warrants it, without changing knob-1/list behavior.

Every tab retains the scope ALL or one region. On a pitched track, sample selection refers to the source anchor/region, not a new asset for every transposed note.

## 11.2 ALL versus one

ALL means all recorded regions in this instrument, never the whole project. Empty slots are skipped. A batch edit is a single undo transaction and shows its affected count.

For different-length audio:

- Absolute start/end frames are meaningful for one sample.
- ALL uses an explicitly labelled normalized/relative operation or separate per-region conversion.
- Do not copy a frame offset blindly into shorter recordings.
- Numeric absolute assignment versus relative change must be visible, especially for tuning, gains, and filter offsets.

For source-combining operations, the second source and batch pairing policy must be explicit. Never interpret ALL as permission to collapse all drum sounds into one waveform.

## 11.3 Required editing functions

The original manual is the reference for the FZ-1 feature audit. Build `docs/FZ_FEATURE_MATRIX.md` with each relevant original operation, our equivalent, scope behavior, test, and any declared difference. Include all sample/synthesis operations in scope; do not replace a deep feature with a label that does nothing. [S1]

### SAMPLE

- Start/end markers with coarse and single-frame fine editing.
- Reversible marker trimming and separately confirmed destructive truncation that can reclaim storage.
- Gain, normalize, and explicit clipping/headroom preview.
- Reverse playback and optional rendered Reverse Write.
- Root/tuning, one-shot/gated playback, sample audition, duplicate/delete/replace.
- Replacing audio must retain mappings/parameters when valid, and clamp/review invalid loop markers rather than reading out of bounds.

### LOOP

- Loop start/end and fine control, enable/disable, and crossfade length.
- Multiple sequential loop sections, not only one sustain loop.
- Explicit repeat/dwell and continue/jump/skip/end behavior, with note-release handling.
- A documented loop-program model and bounds; initial engineering capacity can be eight section descriptors per region. Treat that capacity as our design until exact original semantics are verified.
- Preserve the original-style jump-back/crossfade and section-transition concepts where supported; do not relabel a conventional single loop as the complete feature.
- Forward mode required; reverse/ping-pong are useful extensions but must not replace original behavior.
- No zero-length loops, infinite CPU loops, invalid reverse seeks, or crossfades longer than the defined source range.

### MIX

- Mix Write: choose two regions/assets, levels, offset/delay, relative tuning, and destination.
- Cross-Mix: choose sources, overlap/transition interval, curve, and destination.
- Preview before writing; render using the engine's sample-reader rules at 36 kHz.
- Check storage before starting and retain sources by default.
- Destructive output creation is one transaction, with no half-published result after cancellation/power loss.

### AMP / FILTER envelopes

- Implement editable multi-stage rate/level envelopes with sustain/release behavior; do not reduce the required model to ADSR only.
- Support up to eight stages in our first full implementation, plus a clearly defined stage role/transition model.
- Simple ADSR can be a macro/preset that fills the full model, not the only data structure.
- Keyboard-dependent rate/level scaling, velocity influence, and copying between amplitude/filter envelopes where meaningful.
- Held/retriggered notes and early note-off must produce deterministic transitions.

### FILTER / MOD

- Cutoff, resonance, model/character, drive where supported, envelope amount/polarity, velocity and keyboard tracking.
- Per-region adjustments plus common track performance control.
- LFO rate/depth, destinations, sine/triangle/square/saw/sample-and-hold shapes, free/retrigger behavior; optional tempo sync after core timing is proven.
- Smooth audible controls without excessively smearing deliberately fast envelope behavior.

### MAP

- Root notes, one-shot key assignments, automatic anchor pitch class, automatic zones, and manual low/high overrides.
- Keys select note/range; numeric entry uses the same input system.
- Region volume/pan/send trims and choke group settings for drums.
- Zone edits must not break note-off association for already sounding voices.

## 11.4 Non-destructive first

Metadata changes should be reversible without copying entire recordings. Render new audio only when required or explicitly requested. Maintain immutable source references so active voices and undo remain valid.

A live waveform edit that changes sample bytes builds a new small asset/table and swaps it at a controlled boundary, with a brief crossfade if necessary. Never overwrite a buffer currently read by the audio callback.

---

# 12. Additive synthesis and single-cycle sound design

Additive synthesis is a core on-device feature, not a later website-only substitute.

Inside SAMPLE creation or WAVE, choose a source: USB CAPTURE, ADDITIVE, PRESET WAVE, CUT SAMPLE, DRAW, or existing/imported waveform. All produce the same playable instrument/region abstractions and automatic track behavior.

## 12.1 Forty-eight harmonics

Expose independent levels for harmonics 1-48. DEFAULT display range 0-256 is convenient and consistent with the reference workflow; persist sufficient precision and deterministic scaling. [S1]

Use the common list UI:

- Knob 1: choose row such as HARMONIC, SHAPE, NORMALIZE, or APPLY.
- On HARMONIC: knob 2 selects partial, knob 3 edits level; knob 4 may provide a clearly labelled fine/group control only if useful.
- Keys remain playable except during explicit numerical entry.
- Small harmonic bars/waveform preview support the text, not replace it.

Generate a periodic table when levels change:

```text
wave[n] = sum(h=1..48) level[h] * sin(2*pi*h*n/N + phase[h])
```

Fixed zero phase is the baseline; phase editing is optional, not required for the initial FZ-inspired feature. Use a sufficiently wide accumulator and an explicit normalization/headroom policy. Do not clip each partial independently.

DEFAULT table length: 1,024 points if measured memory permits. Rebuild outside the audio callback, coalesce rapid knob changes, and crossfade/swap tables safely. Save both the compact harmonic recipe and generated/cached waveform as appropriate. Reloading must retain editable harmonics.

Do not run 48 independent real-time oscillators per voice unnecessarily. Repeated-table playback is the intended efficient implementation. Harmonic aliasing at high musical notes must be understood and documented; a single rich cycle and a 36 kHz playback rate do not become alias-free by labelling the table additive. Avoid a hidden high-rate engine. Optional offline band-limited table variants need a measured storage budget and an explicit character policy.

## 12.2 CUT SAMPLE and drawing

Allow selection of a short repeating segment from a recording, precise loop/end adjustment, preview, and creation of a periodic instrument. Preserve the original recording unless the user explicitly deletes it to reclaim space.

Cycle drawing edits point position and amplitude using knobs, with optional key-based numeric entry. Add useful clear/normalize/smooth operations as deliberate commands, not automatic changes that erase intended sharp shapes. Protect against DC offsets and output overload while retaining intentional waveform character.

## 12.3 AKWF and imported cycles

AKWF's original collection describes 600-point, 16-bit mono waveforms. Support that length as well as other validated cycles; do not assume every imported file is 1,024 points or derive its playable root solely from its WAV sample rate. [S11]

A 600-point 16-bit cycle is 1,200 bytes before metadata. The oscillator phase formula, not a guessed C note, gives correct musical pitch. Optional offline conversion to a preferred table length must preserve a complete cycle and avoid duplicating its endpoint. All imported cycles still render at 36 kHz.

---

# 13. Custom FZ-1-inspired filter

## 13.1 Goal and evidence boundary

Develop a software model guided by the FZ-1's filter architecture, controls, and measured sound. Reuse generic math/support code where licensed, but do not make X0X/Open303 the compulsory main filter.

Reference the Casio manuals, published DCF research, and the MB87186 control/hardware project. Preserve the distinction between a switched/time-discrete analog circuit and an ordinary transistor ladder or a purely digital historical implementation. The available research is not a complete verified chip model. [S1, S9, S10]

Our firmware cannot physically add an analog filter. The deliverable is analog-modelled DSP. Use an honest working name such as `FZ INSPIRED` rather than `BIT-EXACT FZ1` until fidelity is demonstrated.

## 13.2 Implementation stages

1. Create a stable reference resonant low-pass implementation at 36 kHz behind a replaceable `FilterModel` interface. It is a development baseline, not the final fidelity claim.
2. Specify the hypothesized topology, state equations, cutoff/resonance mapping, gain staging, nonlinearities, and numeric bounds in `FILTER_MODEL.md`.
3. Compare candidate models with measured hardware response and musical tests. Avoid deciding the topology solely because a familiar four-pole filter is convenient.
4. Tune or replace the baseline. Record what is matched and what remains approximate.
5. Optimize for the target with reference comparisons. Prove stability and bounded cost across legal control/envelope ranges.

No uncontrolled self-oscillation, NaNs, integer wrap, denormal stalls, or unbounded solver loops. Resonance and cutoff ranges must have documented safety limits. Do not clamp them so aggressively that the required character is lost without reporting the tradeoff.

## 13.3 Per-voice processing and home control

Each voice needs independent filter/envelope state so two notes or two drum samples do not reset each other's modulation. HOME cutoff/resonance are common track controls combined with region offsets, keyboard tracking, velocity and envelope/LFO values.

Define composition in an appropriate domain, for example logarithmic frequency offsets for cutoff and bounded offsets for resonance. Do not overwrite per-region differences when sweeping HOME. Editing ALL can explicitly set/offset those region values as one transaction.

Use parameter smoothing for manual changes, but schedule envelopes at a sufficient rate for audible attack accuracy. Update coefficients economically without flattening fast modulation. Include drive and level compensation only as documented model choices, not arbitrary distortion disguised as historical accuracy.

## 13.4 Hardware comparison plan

The user owns an FZ-1, which can supply recordings when requested. Do not require access before implementing the reference engine, but do not claim measured matching without it.

Prepare a reproducible fixture set: sine sweeps, noise, impulses or suitable short bursts, saw/cycle sources, drums, and sustained sampled notes; several input levels, cutoff settings, resonance settings, and transposition intervals. Capture an open-filter baseline to help separate sample/output-path coloration. Fit using some settings and validate on others.

Report frequency response, resonance peak/level behavior, transient response, saturation, envelope sweep shape, and tuning/aliasing behavior. Normalize only where the comparison calls for it; otherwise preserve level differences. Do not modify the original hardware or ask the user to open it for this project.

---

# 14. Sequencer recording, history, and tempo inference

Audio sampling and sequencer recording are separate state machines. The former captures USB PCM into an asset; the latter captures performed MIDI/control events into a pattern. A REC gesture is interpreted by screen/context and the central input arbiter.

## 14.1 Event and time model

Keep an audio-derived monotonic frame clock for scheduling and a precise mapping to musical time. Use adequate fixed-point or rational accumulation to avoid drift from integer samples-per-tick rounding. A 64-bit time accumulator can be useful, but benchmark target arithmetic and validate compiler/platform behavior.

Preserve note identity and source information. Handle repeated same-pitch note-ons, chords, note-offs, sustain, pitch bend, velocity, and track selection changes. An incoming zero-velocity note-on is handled consistently as note-off where appropriate. Bind each held note to the track/region chosen at note-on; switching the selected track before release must not send its note-off to the wrong track. Record a canonical musical note and mapping context so octave/zone transforms are not applied twice on playback.

Store original timing separately from a quantization view/transform. DEFAULT quantization is OFF for freely performed/retrospective takes. Quantization and swing must remain adjustable/reversible. Never throw away the original performance to imitate another firmware's automatic grid snapping.

## 14.2 Conventional recording

Support live recording, overdub, step recording, and clearing/replacing a take deliberately. A new instrument already has a track/pattern, so recording does not open a routing/setup wizard.

While transport plays, a REC tap records into the selected track's pattern at the existing clock; another tap stops overdubbing while playback continues. The completed overdub is one undo unit.

When stopped, a REC tap arms recording. DEFAULT free take starts on the first performed note; a count-in/fixed-tempo alternative is available without changing instrument creation.

## 14.3 Free take / first-loop capture

Play without a metronome; close on the intended next downbeat with REC. Use elapsed time and the performed onsets to propose loop duration and tempo. Manual closure provides stronger evidence than guessing a phrase end from silence.

For a candidate containing Q quarter-note beats over T seconds:

```text
BPM = 60 * Q / T
```

DEFAULT first candidates include common 1/2/4/8-bar interpretations in 4/4, expanded when the phrase justifies it and memory permits. Candidate count is bounded. Do not force every take into exactly 1, 2, or 4 bars solely because a donor did.

Infer, show, and begin looping promptly. Keep a correction view for tempo x2/half, bar count, start/end, and quantization. Do not promise unique automatic interpretation of arbitrary music. Preserve pickups and intentional rests where distinguishable.

Project tempo rule:

- An empty/unclocked project may adopt the inferred tempo automatically.
- A project with existing music keeps its established tempo unless the user explicitly changes it. Fit/crop/extend a new take in that context with original timing retained.
- External MIDI clock always takes precedence while selected and valid. Inference may annotate a take but must not change the master clock.

This follows the useful principle behind SLOOP's free recording while extending it to the specified non-destructive timing workflow. [S4]

## 14.4 Always-on retrospective MIDI history

Maintain a bounded ring of live performed note/controller events with timestamps and track/source IDs, plus the state needed to reconstruct notes crossing its start boundary. This is not a rolling audio buffer.

TARGET: approximately 30-60 seconds for a typical performance, subject to an explicitly reported event budget. Dense MIDI must not imply an unlimited guaranteed duration. Expose retained duration/overflow status diagnostically.

Exclude:

- notes generated by sequencer playback itself;
- metronome events;
- menu/name/numeric entry keys;
- duplicate notes echoed back from the connected host;
- unrelated tracks unless the user deliberately captures them.

Hold REC to capture the selected track's recent played phrase. Anchor the end at REC button-down. Do not include the hold delay. Do not erase the ring immediately; a retry with another interpretation must remain possible.

Inference runs outside real-time code. Use bounded onset/IOI and repetition analysis, candidate downbeats, phrase bounds, durations, and a tempo prior. Preserve original event timing. Generate a small candidate set with confidence and an easy x2/half correction. Sparse sustained notes, rubato, and no recent notes require honest fallback behavior, not random BPM with false certainty.

If the current pattern is empty, create the loop and play it immediately. If it contains music, retain that material as the undo predecessor and use an explicit replace/overdub policy. DEFAULT retrospective capture replaces the latest take as one undoable transaction; do not wipe unrelated patterns. While a normal recording is already active, a hold must not accidentally fire a short REC stop: build a candidate separately and resolve the recording-state transition explicitly before publication.

Handle notes held across window boundaries, sustain at the beginning/end, overlapping same-note instances, and note-offs after capture. Loop playback must not accumulate stuck notes each repetition.

## 14.5 Undo/redo

Hold SEL undoes the latest completed take/overdub or current edit transaction. One hold equals one undo. Releasing after the hold does not trigger SEL's tap action. Undoing a take leaves its instrument intact.

Provide REDO as a visible command, without requiring an undisclosed destructive chord. DEFAULT multiple lightweight undo entries when space allows; waveform renders may have fewer because audio versions consume storage. Always report when an action cannot be retained for undo and obtain explicit commitment before discarding the only copy.

Coalesce a continuous knob movement into a sensible single edit, not hundreds of full snapshots. Undo of a batch operation restores all affected regions and routing atomically.

# 15. Step sequencing, patterns, and song construction

Implement the X0X-style musical behavior in our screen model. X0X's documented pattern system is a reference for queued pattern changes, per-part selection, range chains, and song work; our HOME stays a playable keyboard overview. [S5]

## 15.1 Step workflow

SEQ opens STEP for the selected track. Sixteen white keys represent sixteen visible steps. Additional pages extend the pattern. The active page, playhead, pattern number, and selected drum sound/note remain visible.

Support:

- toggle a step on/off;
- select a kit sound without leaving sequencing;
- melodic note and chord entry;
- rests, ties, note length, velocity, microtiming, and step probability;
- copy/paste/clear with explicit scope;
- overdub and normal keyboard playing in the appropriate live view;
- parameter locks and recorded knob motion for useful sound/mixer controls;
- independent pattern length and rate per track;
- swing with raw timing retained.

Display all modifier/key roles while held. Define a conflict-free note-selection gesture, such as a labelled temporary PICK NOTE action, rather than stealing the numeric-entry mode or long EDIT naming gesture. Rest/tie entry must be available directly on the step page, not hidden in a settings menu.

Patterns need not be a dense fixed-size grid internally. Sparse events with a step editing view reduce empty-pattern cost and preserve free timing. Grid conversion must not destroy chord voicing or note length.

## 15.2 Pattern selection and chaining

The PATTERNS workspace uses sixteen white keys for a bank. DEFAULT 32 logical pattern slots with two pages; empty slots use negligible event memory. The format and UI must not preallocate full dense patterns for every track and slot.

There are two explicit scopes: SELECTED TRACK and ALL TRACKS/SCENE. Show the scope. Each track can play a different pattern. The shared scene view coordinates changes without forcing all tracks to contain the same notes or length.

- Select a pattern while stopped: immediate selection.
- Select while playing: queue at the chosen global launch boundary, default next bar.
- Hold two pattern keys: chain the inclusive range, then loop it.
- Provide an ordered chain editor for nonconsecutive/repeated entries such as A, A, C, B.
- Copy/paste a pattern or scene with an overwrite check and undo.
- A selected-track change resets only the affected track's phase when appropriate; others continue.
- Pattern changes do not reload every instrument or reset FX tails.

Resolve two-key chain gestures without accidentally queuing the first key as a single-pattern launch. Distinguish a chord in PLAY mode from a chain in PATTERNS mode. Octave keys navigate pattern banks only in the explicit pattern workspace.

Use a global meter/bar definition independent of any fixed drum track. Do not carry over a rule where a mandatory 909 track owns song length or swing. Different track lengths may create polymeters; document what happens at scene/chain boundaries.

## 15.3 SONG

SONG displays a compact ordered list of sections. Each entry stores per-track pattern assignments, mutes, duration/repeat count, and deliberate scene overrides. Knob 1 scrolls entries; knobs 2-4 edit the selected fields; keys choose patterns where requested.

Support insert/delete/duplicate/reorder, finite repeats, end/loop behavior, queued section launch, and recording live pattern/mute changes into an arrangement. Save and reload all references. Dynamic storage determines practical song length; do not claim a fixed large number of bars copied from another firmware without accounting for memory.

A full release must construct and play a song without a website or manually switching every pattern. A simple loop recorder without chaining does not meet the requirement.

## 15.4 Deterministic timing and automation

Advance transport using an accumulated musical clock, not a rounded integer block/tick counter. Schedule intra-block note timing. Handle fractional BPM, swing, changing tempo, external clock, and pattern lengths that do not divide the global bar.

Motion uses stable parameter IDs. Record and play cutoff/resonance, volume/pan, sample controls where meaningful, and sends. Coalesce/controller-rate-limit automation without changing notes. Define parameter-lock priority versus automation versus manual movement, including what happens when a user grabs an automated knob.

Deleting a track or instrument cannot redirect stored automation to a different object that reused its array index. Seed probabilistic playback so test renders are reproducible; decide whether normal playback keeps or advances that seed across loops.

---

# 16. Mixer and four send effects

## 16.1 GLO mixer

GLO opens a vertical track mixer. Knob 1 selects the track, knob 2 adjusts volume, knob 3 pan, and knob 4 the highlighted send. Subsequent GLO taps choose send 1, 2, 3, or 4. Show all four send values, highlighting the edited one.

Provide mute/solo and return controls through visible mixer actions/pages. HOME and GLO share volume/pan state. Mixer settings live in the project and are not silently reset when an instrument is renamed or replaced.

Per-sample send trims under EDIT allow a dry kick and reverberant snare within the same kit. Combine track send and region trim exactly once. Define whether trim 0 means no send and the neutral/default trim. Choke groups must stop appropriate voices without resetting unrelated effects.

DEFAULT sends are post-fader. A muted track stops new dry audio and new sends according to a documented smooth mute policy; existing FX tails continue. FX return mute is distinct from input mute.

## 16.2 FX slots

FX opens slot 1 or the last used slot; further presses cycle all four. Parameters use the same vertical-list UI. These are four shared project processors, not four processors per track.

Required initial useful configuration:

1. Delay: time or tempo division, feedback, damping/tone.
2. Reverb: size/decay and damping, using a resource-appropriate design.
3. Chorus/modulation: rate, depth, and relevant mix/feedback control.
4. Character: parallel drive/lo-fi or another compact useful effect.

Slots have selectable algorithm types, enable/bypass, return gain, and defined mono/stereo behavior. Make four simultaneous useful effects work on hardware within a declared supported configuration. Four arbitrary heavy reverbs are not an unmeasured guarantee.

Felucca's effect code is one candidate reference; inspect its actual mono/stereo paths, static buffers, rate assumptions, and licences before adapting it. Do not assume static arrays disappear when a send becomes zero. [S6, S12]

## 16.3 Effect engineering

- Effects return wet audio. Do not double the dry signal by importing an insert effect's full wet/dry output unchanged.
- Adapt all time constants, delay lengths, filter coefficients, and modulation rates to the 36 kHz design.
- No FX may silently switch the entire engine to another sample rate. A deliberately lower-rate internal reverb network may be considered only with explicit multirate conversion, a documented sound/resource tradeoff, and no change to the 36 kHz voice/mix contract.
- All feedback paths are bounded and stable for legal controls; smooth delay-time changes or explicitly offer tape-speed behavior.
- No FX-to-FX feedback routing in the initial required implementation. It complicates baking, admission, and safety unnecessarily.
- Disabled/idle FX can release their buffers only through non-real-time resource management and after tails/state requirements are resolved.
- An effect that may be enabled by upcoming automation must be prepared before its launch deadline. No allocation in the render callback.
- Switching algorithms should fade safely and allocate/validate the new resources before discarding the old valid configuration.

Store a resource declaration per algorithm: state size, buffer size/rate/channels, supported max delay/decay, and measured CPU cost. The UI must reject unsupported combinations before audio failure. Do not hide overload by changing the user's sound unexpectedly.

---

# 17. BAKE FX + UNSEND

The user wants to burn effects into audio and remove live sends. Implement it, but preserve its intended routing and report the resource trade correctly: **baking spends sample storage and can save DSP and active FX RAM**. It is not automatically a saving of total memory.

Expose this under EDIT -> RENDER, with an optional shortcut from the mixer.

## 17.1 Targets

### A. INSTRUMENT: ALL samples or ONE sample

Render selected send processing into the chosen region(s), preserving key assignments and sequence notes. Default to one-shots/drums first. Retain the source until the user commits deletion or undo expiry is explicitly accepted.

For ALL samples, removing a baked track send may be appropriate after every intended region is covered. For ONE sample, zero only that region's contribution to those sends, NOT the whole track's send. A snare bake must not remove reverb from other samples that still need it.

Explain musical consequences in the UI/help: the baked tail now transposes and changes duration with the sample; new velocity/filter changes cannot generally recreate the old real-time FX interaction. A rendered one-shot is not equivalent to an infinite sustained loop through a live effect. Sustained/looped sources require an explicit render duration/strategy.

### B. TRACK PATTERN

Render the selected track's current pattern or selected pattern range into audio. Preserve its pattern ID/arrangement references, and replace that pattern's live voice path with a bounded loop/one-shot renderer. Keep the original notes and instrument as an optional unfreeze recipe if storage allows.

The unsend state is scoped to the rendered pattern. Do NOT zero global track sends and break other unbaked patterns in the same chain. Store a pattern-level routing override or another rigorously equivalent representation.

Changing notes requires unfreeze/rerender. Tempo changes use vintage rate behavior or require rerendering; do not introduce time stretching to hide this. A waveform at a new playback rate changes pitch.

## 17.2 Render controls

- Target and scope.
- Selected FX slot mask.
- One-shot versus repeating-pattern render.
- Tail duration/threshold and a finite maximum.
- Mono/stereo output with preview of width loss when collapsing to mono.
- Keep source / commit-and-reclaim.
- Estimated sample bytes required, temporary RAM required, FX RAM actually recoverable, and estimated live DSP reduction.
- Audition, apply, cancel, and undo status.

All output is rendered from the actual 36 kHz engine. Offline rendering can run faster than real time only if it stays deterministic and uses the same algorithms; wall-clock callbacks must not change its sound.

## 17.3 Routing correctness is mandatory

Define the precise render tap, gain/pan treatment, parameter snapshots, and subsequent playback routing in an ADR. Do not apply the filter, amplitude envelope, dry path, gain, pan, or return gain twice after baking.

Partial-send baking is especially easy to get wrong. Adding baked wet audio into a new sample and then sending that entire sample to an unbaked effect creates extra processing that did not exist before. Support partial baking only with a correct dry/wet stem or equivalent routing representation, or explicitly mark the transformation as a creative approximation. The first working implementation may support ALL active sends before partial combinations, but it must not claim all partial combinations are transparent.

Preserve audible level/pan at the moment of a confirmed bake. If frozen material needs a relative trim or neutralized filter/envelope, expose/describe it and save the original state in the recipe. Never silently reset controls and call a mismatched render identical.

Shared nonlinear effects pose another limitation: in general, `F(a+b)` is not `F(a)+F(b)`. A solo render of one track through a shared saturated/modulated effect cannot be assumed to preserve its contribution to a simultaneous multi-track mix exactly. Distinguish:

- isolated creative sample/track bake;
- approximate per-track freeze of a shared nonlinear bus;
- bus/group render when exact aggregate behavior is necessary and storage permits.

Acknowledge this in preview/help and tests. Do not record other tracks into a supposedly isolated sample, or turn other tracks' sends off to compensate.

## 17.4 Tails and looping

One-shots retain selected decays. Use configurable silence detection and a finite maximum; non-decaying feedback cannot render forever.

For a repeating pattern, support a defined warm-up/steady-state loop render or a separate tail/overlap strategy. Warm-up must be bounded; an infinite-feedback delay or very long reverb may not converge. Avoid an abrupt end that clicks or retriggers the tail badly on every bar. Test seam behavior with reverb and tempo delay, not just dry sine waves.

Seed random/modulated effects or snapshot their deterministic state as needed. State which version of the effect and parameters produced a render.

## 17.5 When to free effects

A shared effect can be deactivated/released only when no active or imminently scheduled input needs it and its tail policy permits. Inspect other tracks, regions, unbaked patterns, chains, automation, and queued scenes.

Longer-term unused effects can relinquish buffers; those needed in a future section can be re-admitted before the boundary. If memory cannot support that future state, reject or warn before starting the arrangement. Current send=0 alone does not prove an effect is globally unused.

The operation is atomic:

1. Check all resources and capture a consistent recipe.
2. Render to new unpublished asset(s).
3. Validate length, checksum, routing, and output.
4. Commit new references and the scoped unsend state together.
5. Retain old references for undo if admitted.
6. Release now-unneeded live resources at a safe boundary.

Failure or cancellation leaves audio, routing, and old samples unchanged.

---

# 18. Optional companion importer

The companion is an optional source preparation/import tool, not a required editor or sampling bridge. Its scope is local EPROM decoding/auditioning, cycle-based instrument creation, transfer, and backup/restore where useful.

No login, cloud storage, analytics dependency, or upload of user audio. Process selected files locally by default.

## 18.1 EPROM kit import

Import raw user-provided drum-machine ROM/EPROM files, identify or select a supported machine profile, decode samples, audition them, choose up to 16, and export/transfer a MULTI SAMPLE instrument.

Each supported profile defines:

- expected file sizes and optional known hashes;
- byte layout, bank/interleave rules, address offsets and lengths;
- encoding, signedness, companding table/logic, and any reconstruction requirements;
- native playback rate/tuning per sound;
- silence/termination conventions;
- default sample names and key order;
- source/provenance and tested fixtures.

Do not assume every vintage EPROM is linear PCM, standard mu-law, or at the same rate. A format lacking reliable information is unsupported until implemented/tested; do not apply a guessed decoder silently. Begin with one documented profile plus a generic explicitly configured PCM importer, then add more verified machines.

Audition the decoded sound, expose memory impact, retain compact original encoding when the device can decode it cheaply, and use a documented normalized format otherwise. Preserve intended tuning and character; no automatic enhancement/normalization/downsampling that changes a kit without preview.

ROM import alone does not emulate every original drum machine's analog output stages. Label that distinction. Users must supply files they are authorized to use; do not bundle copyrighted commercial ROM dumps just because they are available online. Use synthetic or cleared fixtures in the repository.

## 18.2 Single-cycle instruments

Accept WAV single cycles and supported waveform collections including AKWF. Show shape, audition at musical notes, set basic amplitude/filter/envelope defaults, and produce SAMPLE instruments using cycle-aware tuning.

Support one or several selected cycles with an explicit mapping/layering policy. Default one cycle across the keyboard; avoid silently allocating dozens of octave copies of identical data. Additive recipe transfer is useful but must not replace on-device harmonic editing.

Audit the licence of the exact waveform collection before bundling it; retain provenance. The format facts in AKWF's README are not a licence grant for every other collection. [S11]

## 18.3 Transfer and browser capability

Use a versioned, documented bundle format and a validated USB-MIDI SysEx transfer protocol or another already supported, measured transport. Web MIDI requires capability/permission checks and a suitable secure context. Browser support varies; do not promise iPad-browser SysEx transfer without testing it. [S13]

The website must still decode/audition and export an instrument bundle when direct device transfer is unavailable. That limitation does not affect direct iPad synth AUDIO sampling, which uses the class-compliant USB audio path and no website.

Protocol design must include device/firmware identity, capabilities, target schema version, length/resource preflight, 7-bit-safe SysEx encoding, sequence numbers, checksums, bounded chunks, acknowledgements/retries, cancellation, and atomic finalization. Never expose an unrestricted write-any-flash-address command through the instrument importer.

The firmware confirms destructive replacements. Interrupted transfer retains the old instrument and frees incomplete temporary records safely. Repeated chunks are idempotent. MIDI notes/clock have appropriate priority, and data transfer cannot flood audio deadlines.

## 18.4 Input validation

Treat ROMs, WAVs, bundles, and SysEx as untrusted data. Check multiplication/offset overflows, lengths, frame counts, encodings, channel counts, loop ranges, object cycles, nesting, and total resource claims. Fuzz parsers and transfer state machines. Do not decompress unbounded data or execute imported scripts.

---

# 19. MIDI, clock, and external integration

Support USB MIDI alongside audio. Retain TRS MIDI input when the selected board support proves it. External sequencing is useful, but the on-device sequencer remains complete.

Define per-track channel assignment and optional note filters. MIDI 1.0's channel count is not the same as the number of internally stored tracks; shared-channel/split routing is explicit when needed.

Support notes, note-off/zero-velocity note-on, sustain, bend, and the chosen standard CC controls. Useful defaults include volume, pan, cutoff/resonance, and sends, but publish exact mappings and allow conflict-free routing. Do not map a controller twice through both a donor layer and our parameter engine.

Clock settings: INTERNAL, USB, TRS if supported. Implement start/continue/stop, stable pulse accumulation, missing-clock handling, and deliberate takeover behavior. A half-second disconnect must not unexpectedly reset the song or double its tempo. Show sync loss rather than guessing silently.

MIDI Thru/echo is OFF by default. Distinguish local keyboard output, sequencer output, incoming performance, and loopback. Avoid returning incoming notes to the same host/path unless explicitly requested. Deduplicate only known loopback paths, not legitimately repeated same-pitch notes.

Internal numeric/picker input never emits MIDI. Remote controller edits must not saturate MIDI or cause timing starvation. A panic command clears active gates/sustain and releases voices safely without deleting patterns or resetting user settings.

---

# 20. Saving, backup, updates, and recovery

## 20.1 User data

SAVE commits the project, instruments, audio dependencies, arrangement, FX/mixer settings, and relevant recipes. Show a dirty/volatile indicator until the commit is durable. A backup must include or explicitly inventory sample dependencies; do not provide a project file that silently omits all custom audio.

Autosave is useful only when it is safe. Do not repeatedly erase flash on every knob tick. If safe flash writes require stopping playback, queue the save and say so. User samples recorded but not committed need a visible warning before shutdown/update/project replacement.

Restore validates the entire manifest, checks space, asks before replacement, and publishes atomically. Keep calibration and device identity separate from ordinary projects. Document which settings belong to the device and which to a project.

## 20.2 Update/recovery

Reuse a verified loader/packager where possible. Keep bootloader/calibration regions protected. Verify package integrity, target identity, sizes, and address ranges before a write. A project import is never a firmware update.

Do not design a fictional dual-bank rollback if flash cannot hold it. Document the actual failure windows and recovery path. Hardware tests cover interrupted application updates only after safe recovery is demonstrated and the user authorizes them; do not deliberately brick a unit as an early experiment.

Release artifacts must include source revision, build configuration, dependency lock, licence notices, checksum manifest, device compatibility statement, installation/recovery instructions, and clearly listed unverified behavior. No automatic publication or flashing is authorized by this document.

---

# 21. Performance, memory, and failure policy

## 21.1 Required measurements

Report executable flash, constant/sample flash, persistent data, static RAM, peak stack, dynamic arena usage, capture reserve, history/undo usage, FX buffers, maximum block time, and USB underrun/overrun counts. List maxima and high-water marks, not only averages.

Provide build-time map checks and runtime admission reporting. Track unused total versus largest free block/fragmentation. Use fixed-size pools or slab/arena strategies where they improve predictability.

TARGET: a useful mixed arrangement with at least eight modest sample voices, several independent tracks, and all four default FX, subject to actual measurements. This is an engineering target, not permission to fabricate success or cap all project tracks at eight. Define the final supported voice/FX combinations from tests.

TARGET steady-state worst-case audio block usage below roughly 70-75% of the available deadline in the supported stress configuration, leaving measured room for USB/UI/event bursts. The exact target budget must reflect actual interrupt and DMA scheduling.

## 21.2 Overload policy

First refuse unsafe new allocations/configurations before audio starts. During an unforeseen overload, protect memory and timing: controlled voice stealing with a short ramp, priority to currently held/foreground notes where appropriate, and a visible diagnostic. Do not corrupt data, skip arbitrary note-offs, hang, or silently change the 36 kHz engine.

Do not silently reduce filter quality, disable effects, or change interpolation as a normal strategy. An explicitly selected economy mode may have documented sonic differences and separate tests.

Report the difference between presentable project tracks and active polyphony. Empty tracks and short single-cycle instruments should be inexpensive; large sample recordings or effect delay lines should consume the corresponding explicit budget.

## 21.3 Resource examples and tradeoffs

A stereo 500 ms PCM16 delay line at 36 kHz costs 72,000 bytes before extra state. A rendered mono four-bar loop at 120 BPM costs 576,000 bytes before metadata. These calculated costs explain why short-sample baking, compact ROM assets, and periodic-wave instruments matter.

Do not force stereo storage for every instrument. Default captured instruments are mono; allow stereo only when specifically admitted for effects renders or later sources. Do not downmix a stereo bake without telling the user.

If implementing all features simultaneously exceeds actual storage, report measured alternatives such as optional compiled effect families, smaller configured delay limits, fewer cached waveform variants, or reduced optional assets. Preserve all required workflows in the main target; an early development milestone may be narrower, but its status must be honest.

---

# 22. Verification strategy

Tests must exercise the real core and real serialization/input code. Mocks are appropriate for physical hardware boundaries, not for replacing the feature being tested.

## 22.1 Host tests

Build with strict warnings and available address/undefined-behavior sanitizers. Include deterministic audio fixtures, randomized property tests, malformed-file fuzzing, allocation-failure injection, and simulated power failure at storage transaction boundaries.

Compare optimized DSP with a high-precision reference using documented tolerances. Exact hashes are appropriate for deterministic fixed-point renders on a defined implementation; approximate comparisons are appropriate across different arithmetic implementations. Never update golden files automatically just to make a failing sonic test pass.

## 22.2 UI tests

Render the actual screen code at the actual device resolution. Drive physical events: press/release/hold durations, knob increments, simultaneous keys, octave changes, and modal input. Check clipping/overlap and key roles; do not validate only a pretty desktop mockup.

Include enough tracks to require scrolling, long auto-generated names, numeric edge cases, ALL-versus-one scope, pending saves, USB disconnection, recording/capture states, FX allocation failure, and pattern-bank navigation.

## 22.3 Hardware tests

A target compile is necessary but insufficient. Measure actual boot, keys/knobs/display, DAC output, iPad USB audio/MIDI, flash behavior, timing, and recovery on a real FM-1.

A long soak must include several hours of playback where practical, track changes, recording/undo, high-rate controller input, USB audio, screen activity, saves at safe points, and sample/FX resource churn. Log peak timing and errors without itself disrupting audio.

Target latency goals must be reported with the measurement method. Distinguish keyboard-to-sound, USB-input monitoring, capture pre-trigger alignment, and playback latency. Do not subtract guessed latency from MIDI timestamps; calibrate the relevant path and retain the original event timestamps.

## 22.4 Mandatory acceptance cases

Use the following IDs and extend them as needed.

| Test | Acceptance condition |
|---|---|
| T01 Build/reproduce | Clean documented environment builds host tests and target package; dependencies pinned; output hash/config recorded |
| T02 Hardware audit | Memory/controls/transport claims are backed by sources and/or device observations, with unknowns marked |
| T03 USB RX | iPad synth audio reaches the capture engine directly, with MIDI active and no browser bridge |
| T04 USB resilience | Disconnect/format changes/host sleep do not corrupt an instrument or deadlock playback |
| T05 36 kHz | Offline engine and capture assets prove 36,000-frame timing; transport SRC does not retune notes |
| T06 SRC continuity | Long streams have bounded drift/buffer occupancy and no per-block rounding clicks |
| T07 Kit sequence | Sixteen separated test hits auto-fill sixteen slots in order, with original attacks retained |
| T08 Partial kit | Stop after three sounds: all three play immediately; remaining slots are empty and resumable |
| T09 Capture retry | Undo/retry last captured hit leaves earlier slots intact |
| T10 OCT+ | From waiting/capturing/finalizing, OCT+ opens playable HOME safely with no octave change or duplicate track |
| T11 Single source | One pitched sample plays across the keyboard with correct root tuning and rate-based duration changes |
| T12 Anchors | Adding/moving/deleting octave anchors recomputes zones; midpoint ties and extremes match the documented rule |
| T13 Dynamic tracks | Additional tracks consume measured metadata/events, not preallocated audio banks; admission errors are specific |
| T14 Tap/hold | REC/EDIT/SEL holds never also fire their tap actions, even across UI changes |
| T15 Free recording | A known performed phrase yields a useful loop/tempo candidate; raw timing remains recoverable |
| T16 Retro capture | Hold REC retrieves already-played notes ending at press-down, not at hold completion |
| T17 Sparse timing | Rubato/sparse notes/half-time ambiguity produce correctable candidates, not fabricated certainty |
| T18 Clock policy | New captures do not silently retime an existing or externally clocked project |
| T19 Note lifetime | Sustain, repeated same-pitch events, boundaries and overdubs never leave accumulating stuck notes |
| T20 Undo | Undo removes only the latest take/edit; batch and routing changes restore atomically; redo works where retained |
| T21 X0X-like chain | Pattern range chains, repeated/noncontiguous chains, queued changes and a saved song all work standalone |
| T22 Poly lengths | Independent lengths/rates remain synchronized to the documented global launch policy |
| T23 Edit UI | Knob 1 always scrolls lists; knobs 2-4 match labels; tab and sample scope persist |
| T24 Keys as input | Note/range/numeric modes accept correct values and emit no unwanted notes/history/MIDI |
| T25 Naming | Hold EDIT + category key produces the next name and returns without tab changes |
| T26 Mangling | Reverse/truncate/mix/cross-mix and multi-section loops produce validated audio and bounded transactions |
| T27 Scope | ALL changes only this instrument; ONE changes only its selected region; shared sample blobs remain safe |
| T28 Envelopes | Multi-stage amp/filter envelopes, key scaling and early note-off behave deterministically |
| T29 Additive | All 48 levels affect the waveform; save/reload restores the editable recipe; edits do not glitch the render path |
| T30 Cycle tuning | 600-point and 1,024-point cycles play the same requested MIDI pitch within tolerance |
| T31 Filter | Full legal cutoff/resonance/drive/modulation range is stable; model claims match measured evidence |
| T32 HOME controls | HOME controls the entire selected track; mixer level/pan are the same underlying values |
| T33 Four FX | Four default slots run simultaneously within measured budgets; unsupported combinations fail before dropouts |
| T34 Sends | No duplicate dry path, double track gain, unintended mute-tail cut, or per-sample send leakage |
| T35 Sample bake | Baking one drum sound removes only its selected sends and preserves the other sounds' routing |
| T36 Pattern bake | A frozen pattern unsends its own processing; unbaked patterns in its chain retain their effects |
| T37 Partial/shared bake | Remaining sends do not process baked wet audio accidentally; nonlinear/shared limitations are explicit |
| T38 Reclaim | FX RAM is actually freed when safe; no future pattern/automation crashes because of stale references |
| T39 Render failure | Out-of-space/cancel/power failure leaves old audio and routing valid |
| T40 EPROM | A verified profile decodes a fixture correctly; an unknown format is not guessed silently |
| T41 Import validation | Malformed/oversized bundles and interrupted chunks cannot overwrite arbitrary memory or half-replace instruments |
| T42 Website fallback | Local audition/export works without device-browser support; normal iPad USB sampling remains independent |
| T43 Save/power loss | Every simulated transaction interruption recovers an old/new complete state with valid asset references |
| T44 Backup/restore | Audio dependencies, patterns, mixer, FX and recipes round-trip, with calibration isolated |
| T45 Resource stress | Measured memory high-water marks/block deadlines stay within supported bounds under mixed load |
| T46 Flash safety | Capture/saves respect proven flash execution/IRQ constraints and never rely on an unsafe background write |
| T47 Recovery | Authorized hardware update/recovery path is documented and actually tested before a release claim |
| T48 Licences/source | Every reused component has provenance, exact version and notices; distributable package/source obligations are met |

Important numerical checks:

- A known single-cycle A4 source should render near 440 Hz, with a defined cents tolerance.
- Rate transposition by +12 semitones doubles pitch and halves one-shot duration, within interpolation/end-boundary tolerance.
- Timing tests include non-integer BPM and long accumulated duration, not just 120 BPM.
- Test interpolation/loops at asset boundaries, tiny sample lengths, and maximum phase steps.
- Test fast resonance/cutoff sweeps at 36 kHz with high-level material and multiple voices.
- Storage tests fail each write/program step in a small transaction model, not only the final commit.

# 23. Implementation milestones and gates

Do not implement all screens first and leave the audio/storage path for the end. Maintain a runnable build after each milestone. Preserve all unfinished requirements in the tracker.

## M0 - Evidence, baseline, and safety

Deliver the hardware/source audit, dependency lock, base selection ADR, legal/source inventory, known-good baseline build, preliminary memory ledger, and recovery/test plan. Identify the actual USB receive path and investigate capture-versus-flash safety immediately.

Exit gate: a credible target build route, no invented hardware assumptions, and all destructive device operations disabled unless separately authorized. If target tools/hardware are unavailable, show exactly what is missing and continue with portable implementation.

## M1 - Minimal instrument core and physical controls

Bring up a deterministic 36 kHz core with one generated cycle, correct pitch, a voice allocator, safe output SRC, HOME control mapping, and basic on-device/simulated sound. Implement the input arbiter before adding holds throughout the UI.

Exit gate: pitch/time tests pass; HOME keys play; SELECT/SEL/knob semantics are reconciled with real hardware; target build has a measured map. Hardware sound is required before claiming bring-up complete.

## M2 - Direct USB capture vertical slice

Receive host PCM, convert to 36 kHz mono, capture a bounded take, make it playable, and save safely. Start with one sample and explicit resource limits. Test the actual iPad route. Add clear input/volatile/error status.

Exit gate: direct sampling with no browser bridge, simultaneous MIDI, correct tuning, and a proven safe staging/commit strategy. This is the highest-priority integration gate.

## M3 - Two instrument workflows and automatic tracks

Implement kit auto-arming/advance, octave-anchor pitched mapping, automatic tracks/patterns, early completion/resume, quick names, and OCT+ immediate performance. Use compact synthetic hits/cycles to test many tracks without requiring long recordings.

Exit gate: the user's complete sample -> play -> new track workflow works, including partial kits and failed/retried captures.

## M4 - Recording and retrospective capture

Implement conventional/free takes, non-destructive timing, event history, retrospective capture, confidence/correction, undo/redo, and clock ownership. Do not silently quantize the only copy of a performance.

Exit gate: known performances are captured before REC was pressed, hold timing is correct, and existing/external project tempo is respected.

## M5 - Patterns and song

Build step editing, pattern pages, independent track lengths, range/ordered chains, queued launches, motion, and the song list. Make a short complete arrangement from sampled instruments.

Exit gate: the entire song is assembled, saved, reloaded, and played on the device without an editor.

## M6 - Deep editing and waveform synthesis

Complete the common list editor, ALL/ONE semantics, numeric/note entry, sample/mix/loop operations, multi-stage envelopes, additive synthesis, cycle extraction, and drawing. Validate destructive transactions and table swapping.

Exit gate: no required editing tab is a placeholder; 48 partials remain editable after save/reload; sample manipulation is audible and undoable within admitted budgets.

## M7 - FZ-inspired filter development

Develop/optimize the custom filter model and modulation. Run reference stability and audio tests. Incorporate actual FZ-1 recordings when available; keep the fidelity claim provisional until measured.

Exit gate: the model is stable/useful on the target, integrates with HOME/per-region controls, and has honest documentation of what is matched versus approximate. Exact hardware matching is a separate evidentiary claim, not a reason to skip implementing a custom model.

## M8 - Mixer and four effects

Add all four shared effects, per-track/per-region sends, return control, allocation/admission, tails, motion, and safe algorithm switching.

Exit gate: all four default effects and a documented polyphonic sampler arrangement fit within measured CPU/RAM bounds. No static unused FX buffers remain counted as reclaimed memory.

## M9 - Baking and resource reclamation

Deliver sample baking first, then pattern freezing with scoped routing, tail handling, undo/unfreeze, and correct reclamation. Document nonlinearity/partial-send limitations instead of pretending exact equivalence.

Exit gate: baking actually reduces live work when eligible, preserves unrelated track/pattern sends, reports storage cost, and survives cancellation/allocation failure.

## M10 - Optional website and import protocol

Deliver local EPROM decoding for verified profiles, general cycle/AKWF import, audition/export, capability-aware device transfer, validation, and backup compatibility.

Exit gate: the standalone instrument still works with the website completely absent. An interrupted or malformed import cannot damage existing content.

## M11 - Hardening and release candidate

Run acceptance, fuzz, power-failure simulation, UI, timing, USB, and actual device soak/recovery tests. Verify complete source/notices and reproducible packaging. Produce a user manual with the exact final controls and tested limitations.

Exit gate: every mandatory requirement has implementation/test evidence or an explicitly documented unresolved blocker. No target firmware is called production-ready without actual hardware validation. Do not label unfinished requirements optional just to reach a release label.

---

# 24. Agent work discipline and final deliverables

## 24.1 How to work

At the start:

1. Read this file and the repository instructions completely.
2. Inventory the current repository without overwriting existing user work.
3. Inspect the source references, choose/pin a base, and create the requirement tracker and ADRs.
4. Build/test the baseline, then implement the first runnable milestone. Do not stop after presenting the plan.

For each meaningful change:

- State the requirement/test IDs affected.
- Add or update tests alongside implementation.
- Run the appropriate host/target checks and resource report.
- Record command, exit result, produced artifact, and whether a device was involved.
- Keep patches focused and review changes to boot, linker, ISR, USB, allocator, and serialization paths especially carefully.
- Do not hide compiler warnings, weaken tests, accept new golden audio blindly, or replace unsupported functionality with stubs that return success.
- Update `REQUIREMENTS_STATUS.md`, `CHANGELOG.md`, and a short next-step handoff.

When blocked, separate blocking facts from guesses. Continue independent work where useful; do not invent device-test results or claim impossible hardware is present. Ask only questions that source inspection or the current specification cannot resolve. Do not ask the user to redesign already agreed workflows.

## 24.2 Required command surface

Provide simple documented wrappers for:

```text
bootstrap/check dependencies
build host core and simulator
run unit/integration/fuzz-smoke tests
render audio/UI fixtures
build target firmware
report memory and worst-case budgets
validate and package firmware (no device write)
back up an explicitly selected device
flash only with explicit user confirmation and preflight
```

The exact commands depend on the chosen toolchain. Put actual working commands in README once implemented. Do not document hypothetical commands as if they already exist. Host-only tests should not require a device, internet access, or proprietary ROM/sample content.

## 24.3 Deliverables

- Complete firmware/application source and pinned required dependencies, with clear instructions for separately obtained SDK/compiler material.
- A target build/package only when it actually builds, plus hashes and exact source/configuration.
- Host-testable DSP/sequencer/storage core and input/UI simulator.
- Optional companion importer and documented bundle/SysEx protocol.
- Requirement traceability and acceptance results, including failures and pending hardware tests.
- Memory/performance report with exact tested voice/track/effect configurations.
- Filter model rationale, reference fixtures, and honest fidelity statement.
- Data format/versioning/migration and backup documentation.
- Installation/recovery documentation identifying real risks and verified paths.
- User manual/control cheat sheet for all specified gestures and workflows.
- Licence/provenance notices and complete corresponding source obligations for reused code.

## 24.4 Definition of done

The firmware is complete only when a user can:

1. Connect an iPad, capture a sequence of drum sounds directly over USB, press flashing OCT+, and play/sequence the resulting kit.
2. Capture one pitched sample, add octave anchors later, and have correct automatic zones without manual mapping work.
3. Play freely, capture a loop with inferred tempo, or hold REC to retrieve an already-played phrase; correct the interpretation and undo a take without losing the instrument.
4. Build and save a pattern-based song using the agreed physical controls.
5. Open EDIT, select ALL or one sample, navigate lists with knob 1, adjust values with knobs 2-4/keys, and use the full sample/synthesis feature set.
6. Create and edit a 48-harmonic additive sound on the FM-1 itself.
7. Sweep the custom FZ-inspired filter from HOME, mix via GLO, use four send effects via FX, and bake/reclaim resources with correct routing.
8. Save/reload/backup without corrupted audio references, while seeing honest limits and pending persistence.
9. Optionally import EPROM/cycle instruments without making the website part of everyday operation.

A host demo, screenshots, successful compilation, or a firmware image with silent placeholder engines does not meet this definition.

---

# 25. Source register and audit instructions

These references were consulted for this brief on 2026-10-17. The brief is not tied to the latest label shown by a cached search page. Source trees can move; fetch exact source and record commit hashes, submodules, file paths, licences, and checksums during M0. A cited repository's existence does not certify every experimental feature in it.

## [S1] Casio FZ-1 Operation Manual (original manual, hosted scan)

`https://www.smemmusic.ch/sites/default/files/2025-12/casio_fz-1_user_manual_en.pdf`

Use printed page numbers: main features p.1; Sine Synthesis p.34; source/mix operations in the contents; Voice Edit from p.61; amp/filter envelopes pp.66-68; Loop Set pp.69-71. The prepared brief visually checked the relevant scan pages. Verify exact operation semantics from the full manual during the feature audit rather than guessing from headings. The historical manual's sampling-rate specification does not by itself establish the DAC clock.

## [S2] Melodee build/USB/platform notes

`https://github.com/keremimo/melodee/blob/main/BUILDING.md`

`https://raw.githubusercontent.com/keremimo/melodee/main/BUILDING.md`

Inspect actual receive code, descriptors, resource allocation, source layout, build flags and current tested/experimental qualifications. Do not transplant recovery addresses or treat a version-specific resource budget as ours.

## [S3] Melodee overview and source

`https://github.com/keremimo/melodee`

`https://github.com/keremimo/melodee/blob/main/README.md`

Reference for USB host playback into the FM-1, MIDI/audio integration, and available source. Paths and features have changed across versions; locate the actual implementation through the repository tree rather than assuming an old file path.

## [S4] SLOOP

`https://github.com/isod89/sloop-fm1`

`https://raw.githubusercontent.com/isod89/sloop-fm1/main/SLOOP.md`

Reference for free-take closure and musical looping. The custom firmware deliberately changes undo/REC shortcuts and retains original unquantized timing.

## [S5] X0X

`https://github.com/charlesvestal/fm1-x0x`

`https://charlesvestal.github.io/fm1-x0x/manual/`

`https://raw.githubusercontent.com/charlesvestal/fm1-x0x/main/docs/manual.html`

Reference for patterns/chaining/song/automation, and for the importance of flash-save scheduling. Our track/home/mixer/filter design is not a literal copy of X0X.

## [S6] Felucca and licensing

`https://github.com/hugelton/Felucca`

`https://raw.githubusercontent.com/hugelton/Felucca/main/BUILDING.md`

`https://raw.githubusercontent.com/hugelton/Felucca/main/LICENSING.md`

Inspect the per-file licences, submodules and corresponding-source requirements. Do not remove authorship or treat permissively licensed subcomponents as relicensing the entire application.

## [S7] felucca-bsp

`https://github.com/hugelton/felucca-bsp`

`https://raw.githubusercontent.com/hugelton/felucca-bsp/main/README.md`

Board/chip abstraction candidate. Its README separates experimental capabilities and states its licensing arrangement. Audit the pinned version, including experimental CPU/cache/flash features, instead of assuming every directory represents working hardware.

## [S8] Official FM1 Open

`https://github.com/m-vave/fm1_open`

`https://raw.githubusercontent.com/m-vave/fm1_open/main/README.md`

`https://raw.githubusercontent.com/m-vave/fm1_open/main/LICENSE.md`

`https://github.com/m-vave/fm1_open/blob/main/THIRD_PARTY.md`

Alternative application/SDK/build reference. Review application licence, SDK exception, native binary engine and third-party terms separately before combining source. This brief does not grant permission to redistribute every bundled component.

## [S9] FZ-1 DCF research

`https://www.buchty.net/casio/dcf.html`

Published hardware analysis and control information. Treat inferred internal behavior as evidence to investigate/model, not a complete certified chip schematic.

## [S10] ALM Fizzle Guts

`https://github.com/busycircuits/alm014-fizzle-guts`

Hardware and control-code reference using a donor MB87186. It is not a drop-in software audio filter. Observe its code/hardware notices and naming restrictions if reusing material.

## [S11] Adventure Kid Waveforms

`https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE`

`https://raw.githubusercontent.com/KristofferKarlAxelEkstrand/AKWF-FREE/master/README.md`

Reference for cycle format and imported-waveform workflow. Verify exact assets and licence before bundling; support actual file lengths rather than assuming every cycle has the same size.

## [S12] Felucca effects implementation

`https://raw.githubusercontent.com/hugelton/Felucca/main/firmware/src/fx.c`

Candidate DSP/reference for shared effects, static buffer costs, channel layouts, and time/rate assumptions. All reuse must preserve appropriate notices and be adapted/tested at our rate.

## [S13] Web MIDI API documentation

`https://developer.mozilla.org/en-US/docs/Web/API/Web_MIDI_API`

Reference for capability/permission/secure-context requirements. Feature-detect and device-test browser transfer rather than hardcoding a universal support claim.

---

# 26. Final instruction to the coding agent

Build this as a real small instrument, not a collection of names copied from other firmwares. Preserve the exact fast workflows and the deep FZ-inspired editing underneath them. Use existing reliable platform work where it helps, write the sampling/synthesis logic needed for this product, and prove the result in progressively broader tests.

Your first response should identify the chosen investigation/build path and begin M0/M1 work. Your first implementation should establish the 36 kHz core and direct USB capture path, not a mandatory browser editor. Keep the user informed of measured constraints, but do not repeatedly reopen settled design decisions.

Above all: do not fabricate resources or test results, do not flash without authorization, and do not silently simplify away the sampler, additive synthesis, retrospective recording, arranger, four sends, or deep editing that define this project.
