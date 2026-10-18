# Public emulator on iPad

Open https://chinotto1.github.io/Fzfm/ in Safari. Tap **Explore demo**, then **Enable sound** and play the keys or tap PLAY. The page compiles the actual C instrument to WebAssembly and runs it in a Web Worker; it needs no local server, desktop computer or account. The display, controls, sampler, synthesis, sequencer and effects use the same portable application as the native simulator.

Touch a button briefly for a tap or hold EDIT/REC/SCL for its alternate action. Drag a knob vertically; its +/− buttons also work. On narrow screens the keyboard scrolls horizontally. Normal Safari/Web Audio permission rules require tapping Enable sound; switching apps or interrupting audio mutes it safely, and another tap resumes. A modern Safari with WebAssembly, module workers, Web Audio and IndexedDB is required (Safari 16 or later recommended). Tests emulate iPad touch/viewport in Chromium; a physical iPad/Safari session is still separate validation.

SAVE writes the two-generation project journal to IndexedDB on this device, queued until playback stops. A later visit restores it automatically. Restore project validates and locally saves the selected backup. Clearing browser data, storage eviction or private browsing can remove local projects. Use **Export project** and save important backups to the iPad Files app. **Import PCM16 WAV**, **Import single cycle** and **Restore project** use local files; audio/projects are never uploaded to GitHub or another server. Each browser/device has its own project. Export/import moves it between devices.

The browser's test hit exercises the capture detector with generated input. Sample audio also offers microphone and local-file takes; it does not receive real USB or another iPad app's internal audio. The emulator does not flash hardware or establish hardware USB/codec/recovery compatibility.

## Build and publish

Install Emscripten **3.1.69**, then run:

```sh
make web          # outputs self-contained build/site
make web-test     # requires Playwright 1.55.0 + Chromium
```

`EMCC=/path/to/emcc` selects a specific compiler. No FM-1 SDK/toolchain is required for this web target. Compiler, source commit and file checksums are recorded in `build/site/build-info.json`; content-hashed asset names prevent mixed Safari/CDN cache versions. The directory works under a repository subpath and does not make server API calls. Serve it with any ordinary static server for local verification.

`.github/workflows/pages.yml` builds and tests the core, compiles WebAssembly, runs static-site touch/audio/persistence tests and deploys the exact tested artifact to GitHub Pages after a push to main. Actions are pinned by commit; Emscripten is pinned to 3.1.69. Repository Pages must use **GitHub Actions** as its source. It can also be run manually in the Actions tab. The site ships no compiler, credentials, user samples or proprietary native SDK. Application source and the required notices remain in this public repository and the site contains the licence texts.

## Create tracks and sample

Use **+ New track** at the top of the page. Choose **Drum kit** (up to sixteen sampled sounds), **Sampled instrument** (pitched sampling), **Additive synth**, **Preset wave**, or **Drawn wave**. Each creates a track and first empty pattern. Cancel leaves the project unchanged. The **Playing track** menu selects an existing track. Stop playback before adding a track.

Choose a sampling track, then **Sample audio**. **Capture test sound** adds a generated hit immediately; repeat it to fill consecutive kit slots. **Record microphone** requests browser microphone access: make a sound, then tap **Stop recording**. The take stops automatically after ten seconds. **Sample from Files** decodes supported local audio files of up to ten seconds and captures the take into this same track. Browser decoding resamples these takes to mono 36 kHz; the C capture state machine owns slot mapping and committing the audio. Whole recorded/file takes retain their internal silences. Test hits use the automatic detector and its physical-panel settings. Tap **Finish sampling → Play**, enable sound, and play the keyboard. The browser microphone does not capture another app's internal audio or establish a USB audio connection.

Microphone recording mutes instrument output to avoid feedback. Backgrounding the page cancels a recording and releases the microphone. If microphone access is denied or unavailable, use Sample from Files. Processing stays on this device. The lower **Import PCM16 WAV** control remains a separate operation that creates a new sampled instrument; Sample from Files adds a take to the current sampling track.

## Graphical envelopes, LFOs and synthesis

**Envelope**, **Filter envelope** and **LFO** open a large graph and named fields. **Hold to hear** beside the graph unlocks audio and plays a note while held, so you can audition without scrolling to the keyboard. They also draw graphs on the actual 240×240 instrument display. Envelope stage time and level shape the curve; S marks sustain and R marks release entry. Times are proportional with a minimum width for zero-time stages so they remain selectable. The LFO graph shows one second at its current rate, depth and shape. Choose **Shape → Sample & Hold** for stepped random modulation: Rate controls how often a new level is chosen, and Depth sets its amount. Retrigger restarts the sample’s repeatable random sequence; Free running lets new notes join the current step. The graph and saved project retain this shape and phase mode. Choose All samples or an individual sample in **Edit scope**. Amp and filter envelopes remain independent. Play a new note to hear region/envelope/LFO changes.

**Synthesis** opens a single-cycle instrument's FZ-inspired editor. **48 harmonics** displays the saved 0–256 levels; drag across the bars or use the harmonic/level fields. Recipes provide a fundamental, descending saw partials, odd partials and all partials. The Sine/Triangle/Saw/Square buttons replace the current C4 cycle with a preset. Harmonic changes regenerate the actual C playback table, and existing cycle voices crossfade to it.

Choose **Draw wave**, then draw with your finger on the waveform. Clear and Smooth affect the draft only. **Apply drawing** publishes the complete 1,024-point cycle as one undoable change; **Discard drawing** returns to the current sound. **Undo last edit** restores the previous applied sound. The normal SAVE/export operations retain applied audio and editable harmonic recipes; unapplied drawings are drafts. A kit uses its sample slots; create an additive/preset/drawn single-cycle instrument to use synthesis.
