# Workstation model and migration

The initial `fz_` library remains the tested low-level milestone and numerical reference. The executable instrument uses the budgeted `fm_` workstation under `firmware/workstation/`: one project owns stable asset/instrument/track/pattern handles, transactions, recipes and shared FX. The UI, sequencer, sampler, DSP and serializer all use that same model. The simulator never runs two synth engines.

The workstation incorporates the previously developed portable implementation preserved before the Cursor review, with its original source notices, then extends and validates it here. Its code is project-authored, not an imported donor application. The original 330-assertion suite remains to protect the earlier library; additional workstation suites exercise the application itself. New sampler, synth, and arranger functionality belongs in the workstation and is not exposed as an alternative engine collection.

`firmware/interface/` is a separate host-tested router: stereo mix, limiter, step sequencer, arpeggiator, and retrospective MIDI capture. It has no synth engine, and it is not linked into the simulator or the PI32 sampler image. Sampler retrospective capture stays on the workstation clock.

Host object/voice ceilings and budgets are separate from verified target capacity. Allocation, edits, serialization, table regeneration and drawing occur outside audio rendering. Asset and retired event storage remain budgeted while voices/undo can refer to them. Typed before/after transaction dependency snapshots protect superseded cycle versions; these can be collected after all current, history and active/crossfade reader pins expire. Ordinary PCM/event retirement remains conservative. No hardware readiness follows from a host test.
