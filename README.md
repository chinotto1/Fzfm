# fm1-fz-sampler

A sampling synthesizer and groovebox for the M-VAVE FM-1, specified in `FM1_FZ_INSPIRED_SPEC.md`. The instrument is meant to be used entirely on the hardware. This repository currently contains the host-testable engine and the hardware audit. It does not contain a device image, and it will not flash one.

The FM-1 is a JieLi AC79-class board (pi32v2). The engine renders at 36 kHz. USB and the codec, when they are connected, stay at their own rate and pass through an explicit converter. Pitch changes are variable-speed playback: an octave up doubles pitch and halves a one-shot's length.

## What works on the host

- 36 kHz sample and cycle readers, 16-voice mixer, volume and pan.
- Streaming conversion between 44.1 kHz and 36 kHz, and from 48 kHz to 36 kHz, with a bounded queue.
- Multi-sample capture (16 slots, threshold, pre-trigger, undo last, OCT+ to the performance state) into a caller-provided RAM store.
- Octave-anchor zone mapping, including the lower-root tie.
- 48-harmonic additive recipes and a versioned byte format.
- Tap/hold arbitration, a retrospective event ring, and tempo candidates.
- A 240×240 HOME frame.

Cutoff and resonance are stored and do not filter yet. There is no USB device stack, no sequencer transport, no effect, no flash journal, and no website. `docs/REQUIREMENTS_STATUS.md` is the list that matters.

## Commands that exist

```sh
./scripts/check.sh          # host tests and the offline WAV fixtures
./scripts/report_memory.sh  # host object sizes and the safety ceilings
./scripts/package_firmware.sh
./scripts/flash.sh
```

The last two exit 2. They do not build or write a device image. `make test` and `make render` are the same host steps the check script runs. Host tests need gcc and do not need a device, a network connection, or any sample library.

Generated WAVs land in `build/fixtures/` (`a4_cycle.wav`, `a4_plus_octave.wav`) at 36,000 Hz, 16-bit mono.

## Layout

`firmware/` is the portable C core. `host/render/` writes fixtures. `tests/test_main.c` is the host suite. `docs/` holds the audit, the memory notes, and the requirement table. Pinned reference commits are in `third_party/sources.lock`. Those repositories are not vendored.

## Device status

Read `docs/HARDWARE_AUDIT.md`, `docs/MEMORY_MAP.md`, and `docs/BOOT_RECOVERY.md` before assuming a RAM size, a flash layout, or a recovery path. The 96 KiB figure in an upstream linker script is that script's general RAM section, not this instrument's sample budget.
