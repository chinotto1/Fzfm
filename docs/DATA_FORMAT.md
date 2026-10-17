# Data format

## Project container

In memory, instruments, regions, tracks, patterns, and assets use index-plus-generation handles. PCM bytes live in a caller-provided store. An asset records a FNV-1a checksum of its PCM. Capture undo of the last slot rewinds the store watermark and retires that asset's generation. Other deletes do not compact the store. There is no flash transaction. `docs/BOOT_RECOVERY.md` still gates any device write.

### Bundle `FZB1` (version 1)

Little-endian. `fz_bundle_read` checks the whole buffer before it changes the destination project. A bad magic, version, length, or checksum leaves that project untouched. The checksum is FNV-1a of every byte except the final `uint32`.

| Field | Type |
|---|---|
| magic | 4 bytes `FZB1` |
| version | uint16 = 1 |
| reserved | uint16 |
| tempo | uint32 milli-BPM |
| instruments | uint16 count, then each instrument |
| instrument | uint8 kind, 20-byte name, uint8 region count |
| region | uint8 root, uint16 gain, 4 trim bytes, uint8 reverse, uint8 one-shot, uint32 frames, uint32 rate, int16 PCM |
| tracks | uint16 count, then each track |
| track | uint8 index, uint16 volume, pan, cutoff, resonance, four uint16 sends, uint8 mute, uint8 solo |
| checksum | uint32 FNV-1a |

The file does not contain pattern events, the song, FX slots, the LFO, envelopes, or loop sections. Calibration is not stored.

### Journal `FZJR`

An in-memory stand-in for a commit record. Not flash.

Each record is a 16-byte header plus the payload. The header is little-endian:

| Offset | Value |
|---|---|
| 0 | magic `0x465A4A52` (bytes `52 4A 5A 46`) |
| 4 | payload length |
| 8 | FNV-1a of the payload |
| 12 | commit word, written last: `0x465A4F4B` (`FZOK`) |

`fz_journal_recover` keeps a prefix of complete records and drops a torn tail. A missing commit word or a bad checksum ends the valid prefix.

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

## Import profiles

`FZTEST` is a synthetic profile, not a commercial ROM. The file is exactly 1024 bytes: four sounds of 256 unsigned 8-bit frames. The native rate is 8000 Hz. Roots are 36, 37, 38, and 39. Each sample is `(byte - 128) << 8`. Any other length, or any other profile name, returns `FZ_ERR_FORMAT` and does not guess a layout. `fz_import_pcm` accepts an explicit count, length, rate, signedness, and root, and the byte length must equal count times frames. A failure rolls the new regions back and restores the PCM watermark.

`fz_import_wav_cycle` accepts PCM format 1, 8- or 16-bit, mono or stereo. Stereo is summed. The point count is the cycle length. The function returns the file's sample rate and does not treat that rate as a root note. A truncated chunk or a huge frame count is rejected.

The page `web/importer/index.html` repeats the `FZTEST` and WAV rules in the browser. The C functions above are the tested decoder.
