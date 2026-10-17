# USB audio

## What was read

Melodee `firmware/src/usb_audio_desc.h` at `84e2d97` advertises one UAC1 device:

- Fixed 44.1 kHz.
- Playback and capture.
- 16-bit (alternate 1) and 24-bit (alternate 2).
- EP2 OUT playback, EP3 IN explicit feedback (3 bytes), EP2 IN capture.
- 16-bit stereo playback packet up to 180 bytes, which is 45 frames, so a 1 ms microframe is not assumed to hold exactly 44 frames.
- The descriptor comment says CDC serial shares EP2/EP3 with audio, so both are not on together.

felucca-bsp `chip/usb.c` is the device stack (MIDI, serial, audio). It was not copied. Its 48 kHz option needs a generated `uac_fir.h` from Felucca. 48 kHz is not part of our first transport.

The FM-1 is the USB device. The iPad, or a computer, is the host. USB host mode in the BSP is unimplemented and possibly impossible on this port.

## What this tree does

`FzSrc` converts interleaved PCM between rates with a carried rational position:

- 44,100 → 36,000
- 36,000 → 44,100
- 48,000 → 36,000 (host-tested, not advertised on the wire)
- equal rates copy

Unequal rates use a 16-tap windowed sinc built at init, normalized per phase so a constant survives. The queue holds 4,096 frames. A write that does not fit counts `overrun_frames` and accepts only what fits. A read that cannot produce a requested frame counts `underrun_events`. `fz_src_reset` clears the state. Chunk boundaries do not change the output; a host test compares 1,000-frame and 17-frame feeds.

Downsampling attenuates a 20 kHz tone at 44.1 kHz relative to a 1 kHz tone. That is the anti-alias check for this kernel. It is not a measured hardware frequency response.

`fz_pcm_mono_sum` is `(left + right) / 2`, so a full-scale mono pair does not jump +6 dB. Equal-amplitude opposite channels cancel. Capture stores the samples it is given; it does not normalize or gate them. The engine does not read the capture buffer, so monitoring is not wired back into the instrument mix.

## What this tree does not do

There is no USB descriptor, no endpoint, no MIDI stream, and no iPad test. A host buffer converted to 36 kHz is not evidence that an iPad can play into the device. That is test T03, and it is pending. See `docs/DEVICE_TESTS.md`.
