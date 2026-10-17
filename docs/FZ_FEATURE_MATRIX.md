# FZ feature matrix

Host behavior in this tree, compared with the editing operations the specification requires. "Test" names a host check in `tests/test_product.c` or `tests/test_main.c`. A row with no pixel UI still counts only for the function that exists.

The original FZ-1 manual was not copied into this repository. Where the manual's exact gesture is unknown, the row says what this firmware does.

## SAMPLE

| Operation | Here | Scope | Test | Difference |
|---|---|---|---|---|
| Start / end markers | `start_frame`, `end_frame` (0 means through the asset). Knob 1 nudges; a number assigns | ALL absolute frames return `ONE SAMPLE` and do not write. ALL nudge clamps each region to its own length | edit scope | Coarse and fine are the same detent. There is no drawn waveform |
| Destructive truncate | `fz_edit_truncate` writes a shorter asset | ONE region | mangle/edit | Undo restores the region snapshot and does not compact the store |
| Gain | `gain_q15`, unity 32767 | ALL is one undo group | edit scope | No normalize or clip-preview screen. `fz_draw_normalize` exists for a table |
| Reverse playback | `reverse` flag, reader starts at the last frame of the span | ONE or ALL | filter/playback | |
| Reverse write | `fz_edit_reverse_write` stores reversed PCM and clears the flag | ONE | edit functions | |
| Tune | `tune_cents`, −1200..1200 | ONE or ALL | pitch tests use cents on the reader | |
| Root | MAP row ROOT. A number assigns one region. ALL numbers are refused | ONE for an absolute root. ALL may nudge | edit scope | Nudge on a MULTI kit does not rebuild zones. Zone rebuild runs for SAMPLE |
| One-shot / gated | `one_shot` | Per region | gated note-off test | No editor row yet |
| Duplicate / delete / replace | Delete instrument exists. Replace-audio helpers are mix, cut, and preset | | project tests | No editor row that duplicates a region |

## LOOP

| Operation | Here | Scope | Test | Difference |
|---|---|---|---|---|
| Eight sections | `FzLoopProg`, 8 sections, repeats 0 holds until release | Per region | `fz_loop_validate`, `fz_loop_sample` | |
| Crossfade | Section `crossfade`, shorter than the span. LOOP row XFADE edits section 0 | ALL nudges each region's own section | edit scope, loop preview | The voice reader does not crossfade |
| Voice loop | Section 0 becomes `fz_reader_set_loop` when `start_frame` is 0 and the end sits inside the played span | That region | playback stays alive and wraps | Later sections play only through `fz_loop_sample` |
| Zero length, oversized crossfade, bad next | `fz_loop_validate` rejects them | | loop tests | |

## MIX

| Operation | Here | Scope | Test | Difference |
|---|---|---|---|---|
| Mix write | `fz_edit_mix` / `fz_render_mix`, gains and a sample offset, sources kept | Two chosen regions | negative offset keeps every source frame | The MIX tab row does not call it. The caller supplies the buffer |
| Cross mix | `fz_edit_cross` | Two regions, one overlap | cross test | No curve editor |

## AMP / FILTER envelopes

| Operation | Here | Scope | Test | Difference |
|---|---|---|---|---|
| Eight stages | `FzEnvelope` levels and frame counts, sustain index | Per region | 8-stage run reaches the last level then rests at 0 | |
| ADSR macro | `fz_env_adsr` fills four stages. The ENV button opens AMP rows for attack, decay, sustain level, release, then the filter envelope and its amount. The page draws both envelopes. AMP is not in the EDIT cycle | ALL edits each region | edit scope | Sustain is the envelope level, separate from region gain |
| Keyboard scaling | `fz_env_scale_time` shortens non-sustain stages above the center note | | scale test | Level scaling and velocity-to-envelope are not separate curves |
| Note-off | A voice with an envelope releases. A gated voice without one stops. Eight notes stay up together; the ninth replaces the oldest. Releasing one note leaves the others | | filter/playback; eight cycles survive a ninth note-on | |

## FILTER / MOD

| Operation | Here | Scope | Test | Difference |
|---|---|---|---|---|
| FZ INSPIRED lowpass | See `docs/FILTER_MODEL.md` | Track cutoff/resonance plus region offsets | stability sweep; open cutoff is louder than nearly closed on a 2 kHz sine | Fidelity to an FZ-1 is unmeasured |
| Envelope amount | `env_amount` −127..127 added inside the cutoff sum | Region | ENV row FLT AMT. The FILTER tab keeps cutoff and resonance offsets only | |
| Key track | `key_track` steps per octave from the root | Track | applied in `fz_performance_note` | |
| LFO | Track `FzLfo`: rate about 0.1–20 Hz, depth, shapes triangle / square / sine / saw / sample-and-hold, target off / cutoff / level / pitch / pan, retrigger. The LFO button opens this page and draws every shape. K3 selects the target. K4 selects the shape. Full pitch depth is about two semitones. The page is not in the EDIT cycle | Selected track. Depth 0 leaves the mix alone | MOD rows; level, pitch, and pan targets each change a note; sine and saw level modulation differ | No tempo sync. LFO edits are not in the region undo stack |
| Drive | Optional shaper after the lowpass when `fz_engine_set_filter` drive > 0 | Engine | performance render passes 0 | Not an FZ-1 measurement |

## WAVE / MAP / RENDER

| Operation | Here | Scope | Test | Difference |
|---|---|---|---|---|
| Additive | 48 levels, zero phase, peak scale, recipe `FZH1` | Instrument recipe. WAVE tab: K2 selects partial, K3 sets level 0–256 and rebuilds the cycle | `test_main`, panel test | Version 2 of `FZB1` stores the recipe with the cycle flag |
| Preset wave | `fz_preset_sine`, `fz_preset_square` (±12000, not band-limited), `fz_edit_preset` | New cycle asset | preset test | WAVE tab row does not call it |
| Cut cycle | `fz_edit_cut_cycle` copies a span and leaves the source bytes | | cut test | |
| Draw | `fz_draw_set`, `fz_draw_normalize`, `fz_draw_smooth`. DRAW tab on a cycle instrument plots the cycle. K2 steps the point by 8, K3 adds 1024 to that sample. NORMALIZE and SMOOTH are rows | The playing cycle, up to 1024 points | normalize reaches 28000; the panel status follows the point | A drawn cycle is replaced if a harmonic level is edited afterwards |
| Choke | `choke` 0 means none. A new voice with the same nonzero group stops the older one | Region, MAP row | two notes with choke 3 are quieter than two without | |
| Send trim | `send_trim[4]`, 0 silent, 127 follow the track. MAP row edits trim 0 | Region | send math | |
| Bake / unsend | `docs/adr/0003-bake-routing.md` | Region or one pattern | fx/bake tests | RENDER tab row does not call it. Pattern `baked_asset` is stored and is not substituted for the live region |

## Sequencer, mixer, import

| Operation | Here | Test | Difference |
|---|---|---|---|
| Sparse steps | 512 events, 16th = 24 ticks, PPQ 96, bar = 384. The panel playhead is `tick % pattern length` and the panel loops that length | advance lands on 96, 288, 384 | No probability seed, no recorded knob lanes |
| Arpeggiator | ARP opens the page. A second tap opens swing, probability, hold, and order. Modes are off, up, down, up-down, random, and order. Rate, 1–4 octaves, and gate are on the first page. The clock is the sequencer BPM and it runs while the sequencer is stopped. Hold keeps the chord after the keys come up. One voice; mode off keeps direct notes. The page draws the current cycle | order, octaves, up-down, swing, probability, hold, and the panel: a chord sounds and goes quiet on release | Not the extra SLOOP chord modes. The knob layout follows the public SLOOP guide. That source was not copied |
| Chain and queue | Inclusive range in one call. A queued pattern launches at the next bar and does not advance again on that same bar | chain and queue tests | No two-key gesture capture from the matrix |
| Song | Rows of per-track pattern, mute, repeats | one bar advances to the next row | No insert/reorder UI |
| Free take | REC arms, first note starts, close infers tempo only when the project has no music and no external clock | 2 s phrase near 120 BPM; existing music stays at 100 | |
| Retrospective | `fz_seq_capture_history` uses the history slice. Sequencer and menu sources are excluded | one keyboard note kept | The panel hold on REC opens sampling. This function stays available to the host |
| Sampling | Hold REC opens the capture screen and arms a pitched sample. SAMPLE and MULTI are both shown; K1 switches before the first slot is kept. A kit is sixteen squares from the base note. A pitched sample is eight octave squares, and each new take is the next octave. The listening square has a gold bar. K2 sets the threshold. Tap REC starts or ends a take. OCT+ returns to HOME with the new instrument selected | eight pitched takes land on MIDI 36, 48, 60, 72, 84, 96, 108, and 120. A loud burst fills the first square and plays at its root. HOME with no slot restores the previous instrument | The panel microphone is the source. There is no iPad USB capture |
| Undo a take | Sequencer snapshot. The instrument generation is unchanged | seq undo | Six snapshots, 96 events each |
| Four sends live | `fz_performance_render_sends` / `fz_engine_set_fx`. Wet is added once. One reverb maximum | delay of an impulse appears on time; a second reverb is refused | `fz_performance_render` itself does not attach a rack. CPU cost on the AC79 is unmeasured |
| Journal | In-memory `FZJR` records. The commit word is written last | a torn tail is dropped | Not flash |
| Bundle | `FZB1` version 1 is unchanged. Version 2 adds the cycle flag, additive recipe, LFO, key track, pattern lengths, song, and events. SAVE opens that menu; OCT+ writes it | a bad checksum leaves the project and the sequence untouched. The panel round-trips one step | FX slots, envelopes, and loop sections are still absent. There is no flash write |
| EPROM | Profile `FZTEST` only: 4×256 unsigned 8-bit, rate 8000, roots 36–39, `(u-128)<<8` | wrong size or unknown name is `FZ_ERR_FORMAT` | Not a commercial ROM. The page in `web/importer/` repeats this rule locally |
| Cycle WAV | Point count is the cycle. The file rate is returned and is not a root | 600 points at 44100 | |
