# Data format

## Project container

Not implemented. There is no on-disk project, no journal, and no flash transaction. Saving is a refused operation until the flash rules in `docs/BOOT_RECOVERY.md` are satisfied and a versioned container exists.

In memory, instruments, regions, tracks, patterns, and assets use index-plus-generation handles. PCM bytes live in a caller-provided store. An asset records a FNV-1a checksum of its PCM. Deleting the last reference does not yet compact the store except for capture undo, which rewinds the store watermark of the last committed slot and retires that asset's generation.

## Additive recipe (version 1)

Implemented. Little-endian, no struct padding:

| Offset | Type | Value |
|---|---|---|
| 0 | 4 bytes | ASCII `FZH1` |
| 4 | uint16 | version = 1 |
| 6 | uint16 | count = 48 |
| 8 | uint16 × 48 | level of harmonic 1..48, each 0..256 |
| 104 | uint16 | table points, 1..1024 |
| 106 | uint32 | FNV-1a of all preceding bytes |

`fz_additive_read` rejects a bad magic, version, count, checksum, point count, or level above 256. Rebuild is offline: 48 sines at zero phase, summed in double, then scaled so the peak is 22936 (about 70% of full scale). Partials are not clipped one by one. A zero recipe renders silence. The rendered int16 table is what the 36 kHz cycle reader plays. The recipe is the editable source.

## Rates

Internal musical audio is 36,000 frames/s, 16-bit. A cycle's pitch comes from its point count and the requested note (A4 = 440 Hz at MIDI 69), not from a WAV header.
