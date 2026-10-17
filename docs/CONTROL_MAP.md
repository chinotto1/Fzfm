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
| REC tap / hold | button | Tap arms or closes a take, or toggles overdub while playing. Hold is retrospective capture ending at the down edge | `fz_session_apply` calls `fz_seq_rec_tap`. A hold is `fz_seq_capture_history` in the panel, which owns the history ring |
| OCT- | button | Octave down, or the previous pattern page | Host router |
| GLO | button | Mixer. Later taps pick send 1–4 | Screen `FZ_SCR_MIXER`. Knob 0 selects the track, knob 1 volume, knob 2 pan, knob 3 the highlighted send |
| FX | button | Four send slots | Screen `FZ_SCR_FX` cycles `fx_slot`. The rack itself is `fz_fx_set` |
| SEQ | button | STEP, then PATTERNS, then SONG | Those three screens, in that order |
| PLAY | button | Start/stop. Screen changes do not stop it | `fz_session_apply` toggles `fz_seq_play` |
| SAVE | button | Commit when it is safe to write | Emits `FZ_CMD_SAVE`. There is no flash commit |
| SCL, ARP | buttons | No new jobs in the specification | Ids exist. No actions |
| PRESETS | encoder | Outside the sampler contract | Unbound |
| Note keys | 27 keys, F3–G5 in the board comment | Play, except inside a picker | `fz_performance_note` on HOME, EDIT, MIXER, and FX. Numeric, naming, pick, confirm, STEP, PATTERNS, SONG, and capture do not record those keys into the history ring |
| Master pot | ADC | Separate from the four HOME knobs | Unread |

## Hold rule

Default threshold is 500 ms (`FZ_HOLD_US`). Debounce belongs to the board scanner and is not this threshold. A hold fires once. The action's `at_us` is the button-down time. The release after a hold does not also tap. OCT+ or the logical SEL input, while `fz_input_set_confirming` is set, emits cancel.
