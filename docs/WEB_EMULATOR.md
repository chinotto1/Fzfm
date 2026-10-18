# Public emulator on iPad

Open https://chinotto1.github.io/Fzfm/ in Safari. Tap **Explore demo**, then **Enable sound** and play the keys or tap PLAY. The page compiles the actual C instrument to WebAssembly and runs it in a Web Worker; it needs no local server, desktop computer or account. The display, controls, sampler, synthesis, sequencer and effects use the same portable application as the native simulator.

Touch a button briefly for a tap or hold EDIT/REC/SCL for its alternate action. Drag a knob vertically; its +/− buttons also work. On narrow screens the keyboard scrolls horizontally. Normal Safari/Web Audio permission rules require tapping Enable sound; switching apps or interrupting audio mutes it safely, and another tap resumes. A modern Safari with WebAssembly, module workers, Web Audio and IndexedDB is required (Safari 16 or later recommended). Tests emulate iPad touch/viewport in Chromium; a physical iPad/Safari session is still separate validation.

SAVE writes the two-generation project journal to IndexedDB on this device, queued until playback stops. A later visit restores it automatically. Restore project validates and locally saves the selected backup. Clearing browser data, storage eviction or private browsing can remove local projects. Use **Export project** and save important backups to the iPad Files app. **Import PCM16 WAV**, **Import single cycle** and **Restore project** use local files; audio/projects are never uploaded to GitHub or another server. Each browser/device has its own project. Export/import moves it between devices.

The browser's Send test hit exercises the capture detector with generated input; it does not receive real USB or another iPad app's audio. Use WAV import for your own material. The emulator does not flash hardware or establish hardware USB/codec/recovery compatibility.

## Build and publish

Install Emscripten **3.1.69**, then run:

```sh
make web          # outputs self-contained build/site
make web-test     # requires Playwright 1.55.0 + Chromium
```

`EMCC=/path/to/emcc` selects a specific compiler. No FM-1 SDK/toolchain is required for this web target. Compiler, source commit and file checksums are recorded in `build/site/build-info.json`; content-hashed asset names prevent mixed Safari/CDN cache versions. The directory works under a repository subpath and does not make server API calls. Serve it with any ordinary static server for local verification.

`.github/workflows/pages.yml` builds and tests the core, compiles WebAssembly, runs static-site touch/audio/persistence tests and deploys the exact tested artifact to GitHub Pages after a push to main. Actions are pinned by commit; Emscripten is pinned to 3.1.69. Repository Pages must use **GitHub Actions** as its source. It can also be run manually in the Actions tab. The site ships no compiler, credentials, user samples or proprietary native SDK. Application source and the required notices remain in this public repository and the site contains the licence texts.
