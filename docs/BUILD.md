# Build and verification

Host prerequisites: Linux, GCC with ASan/UBSan, make, Python 3. `scripts/bootstrap.sh` checks these tools and all vendored BSP bytes without fetching anything. `make` builds the CLI and native shared simulator; `scripts/check.sh` runs the retained 330 assertions, workstation suites, offline pitch fixtures and full native demo plus native bridge/import/runtime replacement tests. Host checks need no network or device.

`make serve` starts a single-owner loopback HTTP service at http://127.0.0.1:8765. Its data defaults to `build/interactive`; `python3 host/simulator/serve.py --data PATH --port PORT` chooses another directory/port. This process serves native audio/display/control state, accepts bounded local WAV/project files and never opens USB. Do not expose it as a public service. The browser is a control/presentation layer, not a second DSP implementation.

Optional browser tests use `playwright==1.55.0` and `/usr/bin/chromium`: `python3 -m pip install playwright==1.55.0`, then `make browser-test`. They start their own temporary server/save directory and produce screenshots in `build/ui/`. Installing Chromium/system libraries is platform-specific; tests do not silently download a browser.

`make benchmark` writes two JSON reports: the default host build and the smaller target configuration compiled for the host ABI. Both exercise two tracks, eight voices and four effects for 600 blocks of 128 frames. These timing numbers measure this host only.

## Target dependencies

The target builder is `scripts/build-target.sh` (or `make baseline` / `make target`). Obtain the compiler separately from the vendor URL in `third_party/toolchain.lock.json`. The archive SHA-256 is `a1545cddbc451a06bac43eb5cd96d831470c42a97c5c0b1f98879a20fc7647bd`. Verify it before extracting. It contains `jieli-linux-toolchains-20260730.1/`, with PI32v2 clang 4.0.1. An ARM/host compiler is not a substitute. No compiler blobs are committed here.

Obtain official https://github.com/m-vave/fm1_open at `b178ac43c25f010c60a878056fb5331fe693fd62` separately. Only its Newlib headers, `sdk/lib/libc.a` and `libcompiler_rt.a` are used. The compiler supplies additional Newlib `libg.a` (stdio) and `libm.a` (math). Exact headers/tools/libraries are verified against `runtime.lock.json` and `toolchain.lock.json`; mismatch aborts. No native synth engine or application code is linked. Review THIRD_PARTY_NOTICES.md and retain the upstream runtime notices with any resulting binary.

```sh
export JIELI_TOOLCHAIN=/absolute/path/jieli-linux-toolchains-20260730.1
export FM1_RUNTIME_DIR=/absolute/path/fm1_open/sdk
./scripts/build-target.sh --baseline
./scripts/build-target.sh
```

These commands actually compiled and linked in the cloud. They write `build/target/{baseline,sampler}/app.elf`, `app.map`, `app.bin` and `manifest.json`. The builder checks the XIP entry, non-overlapping load images, image extent and linker RAM ranges; records source/dependency hashes, exact flags and section sizes; and never connects to a device. The linker windows remain source-derived assumptions until physical verification. Baseline is a reproduced CPU0 BSP hardware test, not a boot-proven known-good image.

The old compiler reports aggregate `{0}` initializer warnings and one upstream USB initializer warning; host builds use modern GCC with `-Werror`. No warnings are treated as evidence of a successful device test. See MEMORY_MAP.md for the target's deliberately limited profile and resource tradeoffs.

`python3 scripts/package_source.py` creates a deterministic source snapshot with a SHA-256 manifest, including modified BSP source/notices, excluding generated audio/user saves/compiler/SDK binaries. It is not an installable update. `package_firmware.sh` and `flash.sh` still refuse because flash layout, persistence and recovery qualification are incomplete. No device backup/update tool is advertised as working before those gates close.

## Hardware installer page

`make web` also builds `/install/` with content-addressed assets, release catalog and current source commit. Node 20+ is needed for `make installer-test`; it checks the browser update protocol with simulated MIDI ports and never accesses USB. `make web-test` includes that suite and the installer browser workflow. The page accepts only catalog-pinned user-selected firmware packages; current FM1 FZ hardware installation remains disabled. See [HARDWARE_INSTALLER.md](HARDWARE_INSTALLER.md).

`make installer-download` builds `build/FM1-FZ-Installer.html` using Python's
standard library alone. It embeds all installer scripts, styles, catalog and GPL
notices; no firmware or native SDK is included. `make web` also places a copy in
the published installer folder. The browser suite verifies the single-document
version over trusted HTTP without additional asset or catalog requests.
