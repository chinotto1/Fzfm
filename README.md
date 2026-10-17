# fm1-fz-sampler

A sampling synthesizer and groovebox for the M-VAVE FM-1, specified in `FM1_FZ_INSPIRED_SPEC.md`. The instrument is meant to be used entirely on the hardware. This repository currently contains the host-testable engine and the hardware audit. It does not contain a device image, and it will not flash one.

The FM-1 is a JieLi AC79-class board (pi32v2). The engine renders at 36 kHz. USB and the codec, when they are connected, stay at their own rate and pass through an explicit converter. Pitch changes are variable-speed playback: an octave up doubles pitch and halves a one-shot's length.

## What works on the host

- 36 kHz sample and cycle readers, 16-voice mixer, volume and pan.
- Streaming conversion between 44.1 kHz and 36 kHz, and from 48 kHz to 36 kHz, with a bounded queue.
- Multi-sample capture (16 slots, threshold, pre-trigger, undo last, OCT+ to the performance state) into a caller-provided RAM store.
- Octave-anchor zone mapping, including the lower-root tie.
- 48-harmonic additive recipes and a versioned byte format.
- Tap/hold arbitration. OCT+ confirms and undoes. SELECT does not click.
- Editor tabs, ALL versus ONE, numeric entry, and category naming.
- A trapezoidal lowpass named FZ INSPIRED, per-voice envelopes, a section-0 loop, and a track LFO. An FZ-1 measurement has not been made.
- Four send effects, bake/unsend, a sparse sequencer, an in-memory journal, and an `FZB1` bundle. Version 2 of that file keeps the samples and the sequence.
- `FZTEST` and WAV cycle import. `web/importer/index.html` auditions those files locally.
- A 240×240 HOME frame, a retrospective event ring, and tempo candidates.
- `web/sim/index.html` is a browser panel over the same engine (`make sim`). Drag or scroll a knob to turn it. PRESETS steps sine, saw, square, and triangle on a cycle instrument. SELECT does not click. OCT+ is the click.

`docs/REQUIREMENTS_STATUS.md` is the list that matters. There is no USB device stack and no flash image.

## Open the panel

https://htmlpreview.github.io/?https://github.com/chinotto1/Fzfm/blob/cursor/host-sampler-slice-83c0/web/sim/index.html

That address serves the committed page. Sound starts after the first click. Drag or scroll a knob to turn it. PRESETS steps sine, saw, square, and triangle on a cycle instrument. SELECT does not click. OCT+ is the click. Releasing a key stops that note. The screen and the status line show the current BPM. PLAY moves the playhead on HOME and on SEQ. ENV opens the amplitude and filter envelopes and draws both. LFO opens the modulator and draws each shape; K3 selects off, cutoff, level, pitch, or pan, and K4 selects triangle, square, sine, saw, or sample-and-hold. EDIT does not include those pages. A cycle instrument opens on WAVE: K2 chooses a harmonic, K3 sets its level. The next EDIT page is DRAW: K2 picks a waveform point, K3 sets its level. Sample instruments keep the loop and sample pages. ARP opens the arpeggiator. A second tap switches to swing, probability, hold, and key order. K1 selects off, up, down, up-down, random, or order. The pattern follows the BPM and keeps running when you leave the page. Mode off plays keys as ordinary notes. Eight notes can sound at once, and a ninth replaces the oldest. On the panel, click a key to latch it and click it again to release it. Letter keys play while they are held. SAVE opens the project menu. K1 chooses save or load, and OCT+ confirms. Saving downloads `project.fzb` with the samples and the sequence. Loading asks for that file. Nothing is written to flash. Hold REC to sample through the microphone. The screen shows SAMPLE and MULTI together, and K1 switches before the first slot is kept. A pitched sample shows eight octave squares from C2, and a kit shows sixteen squares. The square with the gold bar is the note that will be recorded next. K2 sets the input level, tap REC starts or ends a take, and OCT+ returns to the keys with that recording.

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

`firmware/` is the portable C core. `host/render/` writes fixtures. `host/sim/` is the panel the browser loads. `tests/test_main.c`, `tests/test_product.c`, and `tests/test_sim.c` are the host suite. `docs/` holds the audit, the feature matrix, and the requirement table. `web/importer/` auditions cycle files. `web/sim/` is the panel. Pinned reference commits are in `third_party/sources.lock`. Those repositories are not vendored.

## Device status

Read `docs/HARDWARE_AUDIT.md`, `docs/MEMORY_MAP.md`, and `docs/BOOT_RECOVERY.md` before assuming a RAM size, a flash layout, or a recovery path. The 96 KiB figure in an upstream linker script is that script's general RAM section, not this instrument's sample budget.
