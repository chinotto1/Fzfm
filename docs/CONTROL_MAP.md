# Control map

The executable workstation follows [USER_MANUAL.md](USER_MANUAL.md). Its fourteen button indexes match the pinned felucca-bsp matrix: OCT−, OCT+, FX, SCL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC. The 27 note keys and seven encoders are represented in the native simulator. This source-derived binding has not been remeasured on a device.

| Control | Workstation action |
|---|---|
| HOME | Cancel current picker first; otherwise playable HOME, transport continues |
| ALGORITHM | Select track on HOME |
| SELECT rotary | ALL/ONE scope in EDIT; no push action |
| SCL / logical SEL | Confirm/open; hold 500 ms undoes once, or cancels a confirmation |
| EDIT | Tap enters/cycles nine tabs; hold opens category naming |
| REC | Arm/end take; hold retrospectively records through original down timestamp |
| OCT+ during capture | Finish/open HOME immediately at down, without octave change |
| OCT ± elsewhere | Octave or step/pattern page |
| Knobs 1–4 | HOME cutoff/resonance/volume/pan; list row then displayed fields elsewhere |
| GLO | Mixer; subsequent taps cycle send 1–4 |
| FX | Shared FX screen; subsequent taps cycle slot 1–4 |
| SEQ | Steps → patterns → song |
| PLAY | Transport start/stop |
| SAVE | Host save now or queue until stopped; target reports unsupported |
| ENV / LFO | Shortcuts to AMP / MOD edit tab |
| ARP / PRESETS rotary on HOME | Actions (no inherited arpeggiator engine) |
| Notes | Play unless an explicit picker or sequencer entry screen owns them |
| Master ADC pot | Not bound/calibrated in this development adapter |

Logical SEL is mapped to the existing SCL button. No SELECT push is assumed. The original `fz_` library leaves its abstract SEL unbound; the workstation owns the new explicit panel binding.

The arbiter emits one tap or one hold. It preserves button-down time across screen changes and delayed polling. Browser instant mouse clicks receive a minimum six milliseconds of native time so the physical debounce rule does not discard them. Keys retain their note-on track/identity through track changes; blur/cancel releases browser inputs. Continuous knob edits coalesce without merging edits to unrelated targets.
