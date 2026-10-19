# Third-party notices

The repository's original program is GPL-3.0-only (`LICENSE`); newly authored workstation files permit GPL-3.0-or-later and can be combined under GPLv3. Preserve each file's notice.

`third_party/felucca-bsp` is vendored from https://github.com/hugelton/felucca-bsp at `7d8f8490c2245f334775889cb7107573b12289e0`. Its board/chip/assembly/examples files are MPL-2.0, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; USB retains additional upstream attribution. Its MPL license, NOTICE and LICENSING.md remain alongside the source. The optional raw USB PCM/stream-reset hooks and enlarged RX staging are local changes listed with original and modified hashes in `felucca-bsp.lock.json`. `platform/fm1/app.ld` is an adapted MPL-2.0 linker script with its notice retained. No proprietary native engine is copied or linked.

Target builds separately require the pinned JieLi Linux compiler and its Newlib-derived `libg.a`/`libm.a`, and the official FM1 Open SDK's Newlib headers/`libc.a` and compiler-rt archive. Exact bytes are in `toolchain.lock.json` and `runtime.lock.json`. These binaries are not redistributed in this source tree. The upstream runtime notices are retained verbatim in [third_party/notices/fm1_open-runtime.md](third_party/notices/fm1_open-runtime.md); that document also describes components we do not use. Newlib's component notices and compiler-rt's MIT / University of Illinois-NCSA notices apply to the identified routines. These notices identify upstream license texts, not the complete source revision of vendor runtime archives. No installable binary release or claim of complete vendor-runtime corresponding source is made here.

Other sources in `third_party/sources.lock` were inspected as behavioral/hardware references, not copied application engines. ALM Fizzle Guts is a physical-chip controller, not our filter implementation. No AKWF waveforms, ROMs or commercial samples are bundled. Generated demos use newly synthesized tones.

The AC79 boot/config resources are not shipped or used by the target builder. Any future distribution containing them must retain their separate licenses and prove update/recovery compatibility.

Casio, Adventure Kid, M-VAVE, JieLi and ALM names identify references or compatibility; no endorsement is implied.

The public WebAssembly emulator builds the project-owned C core with Emscripten 3.1.69; the native FM-1 SDK, board layer and proprietary engines are not linked into it. Retained compiler/system-library notices are in `third_party/notices/emscripten-web.txt` (the Debian 3.1.69 source distribution's component notice inventory). The static site includes that file and the GPL text; complete application source and the web build script are in this repository. GitHub Actions used for deployment are pinned by commit in `.github/workflows/pages.yml`.

SLOOP (`https://github.com/isod89/sloop-fm1`, pinned in `third_party/sources.lock`) is also the behavioral reference for the two ARP pages/modes and the note-grid view. The workstation arp scheduler and native/browser editors are original implementations over this project's event model; SLOOP application code, fonts, icons and audio assets were not copied.
