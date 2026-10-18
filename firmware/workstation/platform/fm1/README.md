# FM-1 development adapter

This binds the actual 36 kHz workstation engine, sequencer, UI, MIDI/capture and four FX to the pinned MPL board layer. Both the CPU0 baseline and sampler **compile and link** with the pinned PI32v2 toolchain. Neither has run on a device. Raw `app.bin` is not an installable release.

See [BUILD.md](../../../../docs/BUILD.md) for exact dependency hashes/commands, [MEMORY_MAP.md](../../../../docs/MEMORY_MAP.md) for linked ranges/profile/tradeoffs, and [DEVICE_TESTS.md](../../../../docs/DEVICE_TESTS.md) for pending qualification.

CPU0 main is the single project/render owner. Audio IRQ copies a prepared block or emits silence and increments a miss counter; TIMER5 records physical input edges and polls USB. Bounded RX/input/MIDI queues report overflow and panic ambiguous notes. Main performs 44.1↔36 kHz SRC and capture publication outside IRQs. Display sends one RGB565 row at a time from the actual 240×240 monochrome UI. SCL is logical SEL; SELECT remains rotary. USB MIDI input is not echoed. Physical indexing, bank/cache/DMA behavior and deadline costs require device tests.

The explicitly limited bring-up configuration uses a 224 KiB bounded/coalescing heap, eight voices, 64 object handles, 128 history/take events, two undo transactions and a short delay/capture profile. Four effects are prepared at initialization; unsupported larger settings or offline renders reject without replacing valid data. Those software limits are not verified physical capacity. The source-derived linker still reserves stacks/boot areas; no CPU1/overclock/reclaimed cache assumptions are enabled.

SAVE returns UNSUPPORTED; audio remains VOLATILE. No flash persistence, project transfer or update operation runs in this adapter. The compiled watchdog/noinit guard is intended to enter the BSP's UBOOT path after repeated incomplete boots, but actual recovery is untested. Never install an image on the strength of host tests alone.
