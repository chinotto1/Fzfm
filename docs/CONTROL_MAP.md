# Control map

The behavior column is the product contract from the specification. The implementation column is what this tree actually does. SELECT and the four knobs are encoders, so edits are relative steps. The master control is an ADC pot in the BSP and is not used yet.

## Physical labels from Melodee's measured map

Source: `firmware/src/panel.c` at melodee `84e2d97`, cross-checked against the matrix in felucca-bsp `fm1_input.h`. Not re-measured on a unit for this project.

| Control | Kind | Contract | In this tree |
|---|---|---|---|
| HOME | button | Playable overview. Another HOME leaves an editor; it does not stop playback | Button id exists. HOME screen draws. Editor stack does not |
| ALGORITHM | encoder | Select track. This tree clamps at the ends | `fz_home_select_track` clamps |
| KNOB 1–4 on HOME | encoders | Cutoff, resonance, volume, pan of the selected track | Relative steps stored. Volume and pan reach the mixer. Cutoff and resonance are stored and do not change audio |
| EDIT tap / hold | button | Tap cycles editor tabs. Hold opens naming and must not also change tabs | Arbiter emits tap or hold, never both. No editor tabs yet |
| SEL tap / hold | **no verified control** | Tap confirms. Hold undoes the latest take or edit | Logical `FZ_BTN_SEL` only. Not bound to hardware |
| SELECT | encoder | Not a push. Do not invent a click | Unbound. Not treated as SEL |
| REC tap / hold | button | Tap follows transport. Hold is retrospective capture ending at the down edge | Arbiter timestamps the down edge. History slice can end at that time. Transport state machine is not built |
| OCT+ while sampling | button | Finish capture and open HOME. Do not change octave. Do not duplicate the track | `fz_capture_oct_plus` does this on the host |
| OCT- / OCT+ on HOME | buttons | Keyboard octave | Ids exist. Octave state is not applied to the keyboard yet |
| GLO | button | Mixer. Later taps pick send 1–4 | Volume/pan are the same fields as HOME. Sends are not implemented |
| FX | button | Four send slots | Not implemented |
| SEQ | button | STEP, then PATTERNS, then SONG | Not implemented |
| PLAY | button | Start/stop, and screen changes do not stop it | Id exists. No transport |
| SAVE | button | Commit when it is safe to write | Id exists. No project file yet |
| SCL, ENV, LFO, ARP | buttons | Not given new jobs by the specification | Ids exist so the matrix is complete. No actions |
| PRESETS | encoder | Not part of the sampler contract | Unbound |
| Note keys | 27 keys, F3..G5 in the BSP comment | Play the selected instrument, except in an explicit picker | `fz_performance_note` on the host. No picker yet |
| Master pot | ADC | Not one of the four HOME knobs | Unread |

## Hold rule

Default threshold is 500 ms (`FZ_HOLD_US`). The scanner's debounce stays in the board layer and is not this threshold. A hold fires once. The action's `at_us` is the button-down time, including for REC, so the half-second of holding is not part of the recorded phrase. Changing screens while the button is down does not create a second tap.

SEL hold while `confirming` is set emits cancel, not confirm.

## SEL remains open

The specification's SEL gestures need a real control. The sources inspected for M0 do not list a SEL key, and they describe SELECT as a quadrature encoder without a push. Binding SEL to SELECT would invent a click. Binding it to ARP, SCL, ENV, or LFO would take over a printed label without a decision. The arbiter is ready for a binding once the printed panel or a unit says which control it is.
