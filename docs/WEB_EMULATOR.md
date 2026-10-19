# Public emulator on iPad

Open https://chinotto1.github.io/Fzfm/ in Safari. Tap **Explore demo** and play the keys or tap PLAY. Sound starts automatically where allowed, otherwise with the first tap or key press. The page compiles the actual C instrument to WebAssembly and runs it in a Web Worker; it needs no local server, desktop computer or account. The display, controls, sampler, synthesis, sequencer and effects use the same portable application as the native simulator.

Touch a button briefly for a tap or hold EDIT/REC/SCL for its alternate action. Drag a knob up/down, scroll over it, or focus it and use arrow keys. Moving indicators and 48-pixel touch targets replace the adjacent +/− buttons. Shift + arrow or Page Up/Down turns ten steps. On narrow screens the keyboard scrolls horizontally. Safari/Web Audio may require the first interaction before sound can start; a separate sound-button press is unnecessary. Switching apps pauses audio safely and returning resumes it when the browser allows; the next interaction recovers an interrupted context. An explicit Sound off stays muted until you turn sound on again. Microphone recording temporarily pauses the monitor and restores its previous mute setting afterward. A modern Safari with WebAssembly, module workers, Web Audio and IndexedDB is required (Safari 16 or later recommended). Tests emulate iPad touch/viewport in Chromium; a physical iPad/Safari session is still separate validation.

SAVE writes the two-generation project journal to IndexedDB on this device, queued until playback stops. A later visit restores it automatically. Restore project validates and locally saves the selected backup. Clearing browser data, storage eviction or private browsing can remove local projects. Use **Export project** and save important backups to the iPad Files app. **Import PCM16 WAV**, **Import single cycle** and **Restore project** use local files; audio/projects are never uploaded to GitHub or another server. Each browser/device has its own project. Export/import moves it between devices.

The browser's test hit exercises the capture detector with generated input. The Sampler also offers microphone and local-file takes; it does not receive real USB or another iPad app's internal audio. The emulator does not flash hardware or establish hardware USB/codec/recovery compatibility.

The display and touch workspaces use the same visual roles: white primary text, blue selections/empty cells, green captured audio and meters, amber focus/values, and red recording/clipping. The actual 240×240 C display uses a larger bold pixel font for primary labels and values. The capture screen keeps all sixteen colored slots, its input meter and the selected setting visible together. Filled slots retain their note/slot labels and selection has a border, so color is not the only state cue. These display pixels also drive the RGB565 board adapter; physical panel color/readability remains device validation.

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

Open **Sampler** to sample without a creation dialog. Choose **Multi · drum kit** or **Simple · pitched instrument**, then choose an existing instrument or **+ New instrument**. Selecting a type does not create an empty track; the first capture creates the instrument and its pattern. Sixteen squares show empty/filled slots, note names and waveform previews. Tap a square to choose the exact destination. A successful capture advances to the next empty slot. Simple instruments use the visible root-note field to make pitched anchors; Multi instruments map consecutive keys.

The memory cards above the slots explicitly show free/total emulator sample memory (a 16 MiB software budget, not verified physical FM1 RAM), estimated recording seconds remaining, and approximate additional 1-second/10-second samples. Estimates reserve one ten-second capture buffer at mono PCM16 / 36 kHz. The slot summary separately shows how many of the instrument’s sixteen slots remain. Captures, imports, synthesis and undo-retained sounds all consume the shared budget, so deleting or replacing a slot may not immediately reclaim memory. These are audio-memory estimates; slots and working memory also constrain capacity.

**Capture test sound** captures a generated hit. **Record microphone** shows a live waveform and level meter; tap **Stop recording** to finish. **Sample from Files** captures a local audio file. Microphone/file takes are limited to ten seconds, resampled to mono 36 kHz, and retain internal silences. The C capture state machine owns slot mapping and commits. Hold **Hold to audition** to hear a filled slot, choose **Replace this slot** before capturing over it, or use **Delete sample** / **Undo** directly. Replacement leaves the old audio intact until the new take succeeds, including in a full kit. **Finish sampling → Play** returns to the keys.
Microphone recording mutes instrument output to avoid feedback. Backgrounding the page cancels a recording and releases the microphone. If microphone access is denied or unavailable, use Sample from Files. Processing stays on this device. The lower **Import PCM16 WAV** control remains a separate operation that creates a new sampled instrument; Sample from Files adds a take to the current sampling track.

## Piano roll and quantization

Open **Piano roll / SEQ** (or press the actual SEQ button) for a single page containing Play, Record / Finish recording, Capture recent playing, pattern selection, tempo, loop length, swing and quantization. Capture recent playing uses the chosen history length in bars. Timing candidates remain directly selectable after a take. Setting Tempo explicitly establishes the project tempo before recording.

**Quantize** offers Off, quarter/eighth/sixteenth/thirty-second/sixty-fourth notes and eighth/sixteenth triplets. It affects playback and the roll immediately, preserves the original event ticks and frame timestamps, and persists across recording, retrospective capture and saves. **Off · original timing** restores the performed timing; changing velocity preserves that timing too. New patterns default to Off. Short notes remain audible even when both ends would snap to one grid point.

Tap the grid to add notes, drag to move them in time/pitch, and drag a note’s right edge or choose **Resize** to change its gate. **Erase**, **Delete note** and **Undo** act directly. The fields below the roll edit pitch, start/length in quarter-note beats and velocity; Previous/Next note selects small notes. **Snap** governs editing separately from playback quantization. **Pan** lets a finger scroll the roll; View, Zoom and the bar arrows navigate longer patterns. The piano keys at the left play into the same recording/history path as the main keyboard. On desktop, arrow keys move a selected note, Shift+Left/Right resizes it, and Delete removes it. Frozen patterns must be unfrozen before editing. **Save** at the top stores the project locally (queued until playback stops).

## Mixer and arpeggiator

**Mixer / GLO** and the GLO panel button open the channel-strip mixer. Each track has a vertical level fader, a voice-peak meter, pan, four sends and direct Mute/Solo buttons. Faders update while dragging and continuous changes coalesce for undo. Tap a channel’s name to play that track. The playing-track selector stays in the current mixer/arp/roll workspace. On the native screen, four strips are visible at a time; knob 1/ALGORITHM selects tracks, knobs 2/3 set level/pan, and knob 4 sets the highlighted send. GLO cycles sends; SCL mutes and hold GLO solos.

**Arpeggiator / ARP** or the ARP panel button opens the selected track’s arp. All eight fields are directly editable in the browser; the physical ARP button alternates two pages with four direct knobs. Use **Hold C major** to audition, the keyboard for your own chord, and **Stop / clear held notes** to end a latched phrase. The C scheduler generates the actual recorded MIDI notes even with transport stopped. See USER_MANUAL.md for modes, timing and bounded capacity.

Hold the actual HOME button for commands. ARP no longer opens that menu. SEQ now opens the native MIDI piano roll as well as the large touch editor; further presses cycle steps, patterns and song. The physical roll provides note selection, pitch/gate/velocity editing, time-move mode (EDIT), add/delete (SCL), and undo. Notes loaded from earlier projects appear in both views.

## Graphical envelopes, LFOs and synthesis

**Envelope**, **Filter envelope** and **LFO** open a large graph and named fields. **Hold to hear** beside the graph unlocks audio and plays a note while held, so you can audition without scrolling to the keyboard. They also draw graphs on the actual 240×240 instrument display. Envelope stage time and level shape the curve; S marks sustain and R marks release entry. Times are proportional with a minimum width for zero-time stages so they remain selectable. The LFO graph shows one second at its current rate, depth and shape. Choose **Shape → Sample & Hold** for stepped random modulation: Rate controls how often a new level is chosen, and Depth sets its amount. Retrigger restarts the sample’s repeatable random sequence; Free running lets new notes join the current step. The graph and saved project retain this shape and phase mode. Choose All samples or an individual sample in **Edit scope**. Amp and filter envelopes remain independent. Play a new note to hear region/envelope/LFO changes.

**Synthesis** opens a single-cycle instrument's FZ-inspired editor. **48 harmonics** displays the saved 0–256 levels; drag across the bars or use the harmonic/level fields. Recipes provide a fundamental, descending saw partials, odd partials and all partials. The Sine/Triangle/Saw/Square buttons replace the current C4 cycle with a preset. Harmonic changes regenerate the actual C playback table, and existing cycle voices crossfade to it.

Choose **Draw wave**, then draw with your finger on the waveform. Clear and Smooth affect the draft only. **Apply drawing** publishes the complete 1,024-point cycle as one undoable change; **Discard drawing** returns to the current sound. **Undo last edit** restores the previous applied sound. The normal SAVE/export operations retain applied audio and editable harmonic recipes; unapplied drawings are drafts. A kit uses its sample slots; create an additive/preset/drawn single-cycle instrument to use synthesis.

## Hardware installer and recovery

The header links to [Install & recover](https://chinotto1.github.io/Fzfm/install/). Use Chrome/Edge on a computer for device connection. This is separate from the emulator: no MIDI permission or hardware update starts when you open the instrument. The installer provides published firmware recovery choices and validates selected packages locally; the current FM1 FZ board build is shown but unavailable for installation until its OTA/recovery path is qualified. See [HARDWARE_INSTALLER.md](HARDWARE_INSTALLER.md).
