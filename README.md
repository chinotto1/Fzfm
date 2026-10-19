# FM1 FZ-inspired sampler

**[Open the public iPad emulator](https://chinotto1.github.io/Fzfm/)** — tap **Explore demo** and play. Sound starts automatically, or on your first interaction where the browser requires it. No computer or account is needed. [Browser saves and controls](docs/WEB_EMULATOR.md).

**[Hardware installer & recovery](https://chinotto1.github.io/Fzfm/install/)** — choose published Skiff, SLOOP or official V15 recovery packages in Chrome/Edge on a computer. The page shows the current FM1 FZ source build; its hardware install remains unavailable until OTA and physical recovery are qualified. [Installer guide](docs/HARDWARE_INSTALLER.md).

A 36 kHz sampling synthesizer and groovebox for the M-VAVE FM-1. The portable application now connects sampling, synthesis, editing, sequencing, arrangement, mixing and project saves. A local simulator runs that same C application with its actual 240×240 display and physical controls.

```sh
./scripts/bootstrap.sh   # check host tools and pinned board sources
./scripts/check.sh       # sanitizer suites, audio fixtures and full instrument demo
make serve              # open http://127.0.0.1:8765
```

Requires GCC, make and Python 3 on Linux. No account, network, sample library or attached instrument is needed for host use. Click **Explore demo** or press SCL to create an instrument. Sound is enabled automatically; browsers that require activation start it on the first tap or key press. Use **Sound on/off** to mute. See the [user manual](docs/USER_MANUAL.md) for controls.

Implemented host workflows include sixteen-slot capture, octave sample anchors, free/retrospective MIDI recording, step probability and parameter locks, independent patterns, ordered chains, song arrangement and live arrangement recording. New instrument offers separate Synth, Sample and Multi Sample tracks. Synth starts as a playable sine oscillator, with EDIT limited to waveform/additive synthesis, amplitude envelope, filter and LFO. Sample and Multi Sample open USB capture immediately, with no source chooser. Their eight edit tabs provide ALL/ONE scope, trim, reverse, Mix Write/Cross-Mix, envelopes, loop programs, mapping and bake/unbake. The engine has a resonant per-voice filter and four shared send effects. Complete project backups include PCM, recipes, patterns, chains, song and routing; saves use a fault-tested two-generation host journal.

The application **builds and links for PI32v2**, producing a range-checked raw development image. It has **not been run on a device**. The board adapter uses a short-capture/short-delay development profile; SAVE on the device explicitly reports UNSUPPORTED until a safe flash partition and persistence path are qualified. iPad USB, codec timing, CPU deadlines, physical controls and recovery remain pending. Nothing was flashed. The [requirements table](docs/REQUIREMENTS_STATUS.md) records partial features and exact evidence, including optional EPROM/transfer work still missing.

Other commands:

```sh
make web                         # static C/WebAssembly emulator (Emscripten 3.1.69)
make web-test                    # static-site touch/audio/local-save tests
make demo                        # native controls -> WAV + display fixtures
make benchmark                   # host timing/memory for two explicit profiles
make browser-test                # optional Playwright + Chromium end-to-end checks
./scripts/build-target.sh --baseline
./scripts/build-target.sh        # separately acquired pinned compiler/runtime required
python3 scripts/package_source.py
```

[Build/dependency instructions](docs/BUILD.md), [memory/performance report](docs/MEMORY_MAP.md), [format](docs/DATA_FORMAT.md), [filter model](docs/FILTER_MODEL.md), and [device gates](docs/DEVICE_TESTS.md) explain the practical limits. Firmware packaging and flashing scripts refuse until release/recovery gates are met.

The executable application is in `firmware/workstation/`; `host/simulator/` supplies the native CLI/browser bridge; `web/simulator/` presents its controls. The earlier `fz_` numerical library remains under the original `firmware/` directories with its 330-assertion regression suite. [ADR 0002](docs/adr/0002-workstation-model.md) explains this migration. The simulator runs one workstation engine. Dependencies and retained notices are described in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
