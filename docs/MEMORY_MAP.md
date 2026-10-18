# Memory and performance evidence

No physical SRAM/flash capacity or usable sample partition was measured. The target map below **links inside provisional source-derived windows**; a link is not proof those banks are safe on an FM-1. No second core, overclock, cache reclamation or external RAM is assumed.

## Linked sampler

PI32v2 clang 4.0.1, `-Os -mcpu=r3 -mfprev1`; actual sampler adapter and portable modules, pinned BSP, external Newlib/stdio/math and compiler-rt. Current range-checked `app.bin`: **132,568 bytes**, SHA-256 `3f9932bfad767e57488e81b8c9231c704fff55ad95ab031dc498e19a70a6ef08`. These are development artifacts in `build/target/sampler/`, not an installable update. `manifest.json` records exact source/dependency hashes and flags.

| Section/window | Linked bytes | Provisional limit |
|---|---:|---:|
| XIP load image (text + data) | 132,568 | 581,564 at 0x02000120 |
| .data | 1,188 | Shared with .bss in 96 KiB RAM |
| .bss | 39,136 | Shared with .data in 96 KiB RAM |
| .pool | 311,568 | 327,680 at 0x01C20000 |
| Heap inside .pool | 229,376 (224 KiB reserved) | Cannot grow beyond that array |
| .ram_text | 0 | No flash erase/program path enabled |

Stacks, guard bands and boot mailbox reservations follow the explicitly provisional adapter linker script. Their physical validity, runtime stack high water and interrupted-boot recovery are untested. Earlier initial 192 KiB-heap/unconfigured-object build overflowed POOL; the explicit profile below resolved that actual link failure. The reproduced CPU0 baseline is 16,408 bytes, SHA-256 `65704584f9d68821c6e153ae25da77b4e51e2329a912c48b3a5ea5a0a4cd0720`, still not boot-proven.

The development board profile has eight simultaneous voices, 64 handles per object class, sixteen logical sample regions per instrument, 128 engine events, 128 history and take events, 64 held-boundary records, two undo transactions/24 patches, two 64-phase SRCs and a 4,096-frame initial capture maximum (113.78 ms). Object handles are metadata ceilings, not preallocated full recordings. Four FX are admitted at initialization: delay is limited to 4,098 buffer frames (113.78 ms maximum, 80 ms default), plus reverb/chorus/character. A longer requested delay rejects before replacement. This short delay/capture profile is a bring-up tradeoff, not the final product capacity.

Metadata/audio/events/FX/undo budgets are respectively **64/128/64/96/48 KiB upper bounds**, sharing the **224 KiB total bounded heap** with runtime overhead. They are not five reserved banks and cannot all be simultaneously filled. Admission can fail earlier because of the shared heap or fragmentation. The first-fit/coalescing allocator is host-tested with randomized allocations/reallocations, alignment, overflow and full coalescence. It is single-owner, non-real-time. No ROM allocator addresses are borrowed.

Sources and ordinary retired PCM/event buffers remain budgeted while voices/undo may reference them; this conservatively spends capacity until project disposal (reloading also retires old event buffers). Superseded cycles are collected using typed before/after transaction dependencies plus current regions, bake originals and active/crossfade readers. A 600-change held-note waveform stress test stays below four asset slots and preserves undo/redo. Empty kits reserve one temporary capture scratch, not sixteen PCM recordings. Offline bake allocates its render owner from budgeted RAM rather than the target stack. Its temporary second-engine/FX footprint can exceed this board profile; failure retains old assets/routing. Larger captures, simultaneous four-FX playback, deep offline render and arrangement recording require measured admission policies before a release claim. Explicitly releasing unused FX can reclaim their real buffers; bypass alone does not.

## Host stress fixture

`make benchmark` exercises two tracks, eight held saw-cycle voices and four send effects over **600 × 128-frame blocks** (2.133 seconds of instrument time). Both use a real sequencer/history allocation and two preset transactions. Current measured GCC `-O2` cloud results:

| Configuration | Mean block | Max observed block | FX allocated | Payload peaks metadata/audio/events/FX/undo |
|---|---:|---:|---:|---|
| Default host | 0.0994 ms | 0.1415 ms | 337,168 B | 20,248 / 3,600 / 72,152 / 337,168 / 63,408 B |
| Smaller board constants on host ABI | 0.0896 ms | 0.1658 ms | 81,936 B | 20,248 / 3,600 / 27,608 / 81,936 / 30,336 B |

A 128-frame block at 36 kHz spans 3.556 ms. These are observed host timings, **not** PI32v2 deadline bounds, not a worst-case proof, and not a device voice-capacity claim. The fixture excludes SRC/UI/USB/flash load and target interrupt cost. JSON reports in `build/benchmark*.json` include exact sizes/counts; reruns naturally vary. The smaller host ABI uses different pointer/alignment sizes than the linked target.

The SRC streaming hot path uses cached coefficients and no per-frame trigonometry. Actual coefficient response/chunk invariance is tested. Device drift, codec clock correction, concurrent USB/display work, maximum stack/IRQ/render time and multi-hour soak remain required.

## Storage

Mono PCM16 costs 72,000 bytes/second, stereo PCM16 144,000. A 1,024-point cycle costs 2,048 PCM bytes, plus region/recipe metadata. The general host project admits separate 4/16/4/2/4 MiB payload pools and a 16-voice simulator; those are host safety limits, not device resources. There is no qualified target sample-flash partition. Upstream flash addresses, JEDEC guesses and conversational “496 KiB available” are not used as sampler storage capacity.
