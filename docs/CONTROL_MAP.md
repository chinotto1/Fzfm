# Control map

The behavior column is the product contract. The implementation column is what this tree does on the host. SELECT and knobs 1–4 are encoders, so edits are relative steps. The master control is an ADC pot in the board support package and is unread here.

Binding for confirm and undo: `docs/adr/0002-oct-plus-click.md`.

## Physical labels from Melodee's measured map

Source: `firmware/src/panel.c` at melodee `84e2d97`, cross-checked against felucca-bsp `fm1_input.h`. Not re-measured on a unit for this project.

| Control | Kind | Contract | In this tree |
|---|---|---|---|
| HOME | button | Playable overview. Another HOME leaves an editor and does not stop playback | `fz_session_handle` returns to HOME. A dialog cancels first. Playback is a separate flag |
| ALGORITHM | encoder | Select track, clamped at the ends | `fz_home_select_track` |
| KNOB 1–4 on HOME | encoders | Cutoff, resonance, volume, pan of the selected track | `fz_home_turn`. Volume and pan are the mixer fields. Cutoff and resonance feed `fz_performance_render` |
| EDIT tap / hold | button | Tap enters the editor and then cycles tabs. Hold opens naming and must not also change tabs | Host router does this. A sample instrument cycles SAMPLE, LOOP, MIX, FILTER, WAVE, MAP, RENDER. A cycle instrument opens on WAVE and cycles WAVE, DRAW, FILTER, MAP. Envelope and LFO pages are not in either cycle. DRAW is the point editor: K2 picks the point, K3 sets its level. EDIT while the envelope or LFO page is open returns to WAVE or SAMPLE |
| ENV | button | Open the amplitude and filter envelopes | Screen `FZ_SCR_EDIT`, tab AMP, not part of the EDIT cycle. Both envelopes are drawn. Rows are attack, decay, sustain, release, then the filter envelope and its amount |
| LFO | button | Open the track modulator and choose a target and a shape | Screen `FZ_SCR_EDIT`, tab MOD, not part of the EDIT cycle. The page draws the selected shape large and a picture of each shape. K3 steps the target: off, cutoff, level, pitch, pan. K4 steps triangle, square, sine, saw, and sample-and-hold |
| OCT+ tap / hold | button | Context confirm, undo, octave, or page. See ADR 0002 | `fz_session_handle` |
| SELECT | encoder | Turn only. No push | Unbound as a button. Logical `FZ_BTN_SEL` is a test input, not this encoder |
| REC tap / hold | button | Tap arms or closes a sequencer take, or toggles overdub while playing. Hold opens sampling | A short tap calls `fz_seq_rec_tap`. Holding REC opens `FZ_SCR_CAPTURE`, creates a pitched sample instrument, and arms the microphone take. The screen shows SAMPLE and MULTI together; the selected type is gold. K1 switches before the first slot is kept, and the other name dims after that. A kit draws sixteen squares from its base note (default MIDI 36). A pitched sample draws eight octave anchors, C2 through C9 when the pitch class is C. The square that is listening has a gold bar, and a kept slot turns green. The caption names the next root. Octave labels are scientific pitch, MIDI/12 - 1, so MIDI 60 is C 4 and MIDI 36 is C 2. Sharps are spelled CS, DS, FS, GS, and AS. K2 sets the start threshold. Tap REC starts or ends the current take. OCT+ keeps the recording and returns to HOME. HOME drops the new instrument when no slot was kept. The browser feeds the microphone at 36 kHz. Retrospective MIDI remains `fz_seq_capture_history` for the host tests |
| OCT- | button | Octave down, or the previous pattern page | Host router |
| GLO | button | Mixer. Later taps pick send 1–4 | Screen `FZ_SCR_MIXER`. Knob 0 selects the track, knob 1 volume, knob 2 pan, knob 3 the highlighted send |
| FX | button | Four send slots | Screen `FZ_SCR_FX` cycles `fx_slot`. The rack itself is `fz_fx_set` |
| SEQ | button | STEP, then PATTERNS, then SONG | Those three screens, in that order |
| PLAY | button | Start/stop. Screen changes do not stop it | `fz_session_apply` toggles `fz_seq_play` |
| SAVE | button | Open the project menu. Confirm writes the current samples, recipes, mixer, modulators, and sequence | Screen `FZ_SCR_SAVE`. The first row saves and the second loads. A second tap, or K1, switches rows. OCT+ confirms. The browser downloads `project.fzb` or asks for a file. There is no flash commit |
| ARP | button | Open the arpeggiator. A second tap switches to its second page. HOME leaves the page and the pattern keeps running | Screen `FZ_SCR_ARP`. Page 1: K1 mode (off, up, down, up-down, random, order), K2 rate, K3 octaves 1–4, K4 gate. Page 2: K1 swing, K2 probability, K3 hold, K4 order (note or play). The clock follows the sequencer BPM and runs while the sequencer is stopped. Mode off keeps ordinary notes |
| SCL | button | No new job in the specification | Id exists. No action |
| PRESETS | encoder | Step a cycle instrument through waveform presets | Encoder 6. On a cycle instrument, clockwise steps sine, saw, square, and triangle. Each one rewrites the 48-harmonic recipe and the playing cycle. A recorded sample is left alone |
| Note keys | 27 keys, F3–G5 in the board comment | Play, except inside a picker. Eight notes sound together; a ninth replaces the oldest | `fz_performance_note` on HOME, EDIT, MIXER, FX, and ARP. Releasing one key stops that note and leaves the others. When the arpeggiator mode is not off, those keys feed the arpeggio instead of sounding together. On the panel, a mouse click latches a key and a second click releases it. Letter keys play while held. Numeric, naming, pick, confirm, STEP, PATTERNS, SONG, SAVE, and capture do not record those keys into the history ring |
| Master pot | ADC | Separate from the four HOME knobs | Unread |

## Hold rule

Default threshold is 500 ms (`FZ_HOLD_US`). Debounce belongs to the board scanner and is not this threshold. A hold fires once. The action's `at_us` is the button-down time. The release after a hold does not also tap. OCT+ or the logical SEL input, while `fz_input_set_confirming` is set, emits cancel.
