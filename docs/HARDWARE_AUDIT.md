# Hardware audit

Status of this document: source inspection on 2026-10-17. No FM-1 was attached. Nothing here was measured on a unit, and none of it is a linker script for this firmware.

## Target

| Item | Source observation | Device-tested here |
|---|---|---|
| Board | M-VAVE FM-1 | No |
| Chip family | JieLi AC79-class, pi32v2 | No |
| ISA / toolchain | pi32v2, JieLi clang 4.0.1, Linux x86-64. Melodee `BUILDING.md` at `84e2d97` | Toolchain not installed in this environment |
| Clock at the boot loader | 360 MHz, recorded in felucca-bsp `docs/CPU.md` | No |
| SDK clock table | felucca-bsp `docs/EXPERIMENTAL.md` says the SDK's own maximum system clock is 320 MHz and that raising the clock is not reproduced in the BSP | No |
| Execution | Code runs from XIP (flash window). Helpers that run while flash is programmed must live in RAM | No |

This project does not change voltage, cache, flash geometry, or the second core. felucca-bsp marks CPU1 and expanded flash as beta and never run on a device in that document. USB host mode is a concept there and may be impossible if the USB-C port cannot supply VBUS. That matches the product: the iPad is the USB host and the FM-1 stays a device.

## Audio

felucca-bsp `boards/fm1/fm1_audio.h` at `7d8f849`: ALNK0 I2S to an external codec, double-buffered DMA, interleaved int32 frames, 24-bit left-justified. The header does not state the sample rate.

Melodee `firmware/src/usb_audio_desc.h` at `84e2d97`: one UAC1 duplex function, PCM16 and PCM24, fixed 44.1 kHz (`0x44, 0xAC, 0x00`). Playback is EP2 OUT, explicit feedback is EP3 IN (3 bytes, 10.14), recording is EP2 IN. 16-bit stereo max packet 180 bytes (45 frames). 24-bit stereo max packet 270 bytes. Alternate setting 0 is zero-bandwidth.

Our engine rate is 36 kHz regardless. 44.1 kHz is a transport rate. The host converter implements 44100↔36000 and 48000→36000. 48 kHz is not advertised by the pinned Melodee descriptor and is not a device format yet.

## Display

Felucca `firmware/src/lcd.c` at `5738617`: ST7789-class 240×240, RGB565, SPI1. Pins from felucca-bsp `fm1_lcd_hw.h`: PC9 clock, PC10 data, PC7 CS, PC8 D/C, backlight PA2 active low. Not re-checked on hardware here. The HOME renderer uses this resolution.

## Controls

felucca-bsp `fm1_input.h`: 11×6 diode matrix, 14 buttons (ids 0..13) and 27 note keys (ids 14..40, comment: F3..G5). Seven quadrature encoders. Debounce is in the scan, separate from our 500 ms hold. A master pot is an ADC reading, not an encoder. Encoders emit steps; the BSP describes no push switch on them.

Melodee `firmware/src/panel.c` at `84e2d97` records a measured label map (not re-measured here):

| Matrix button | Label |
|---|---|
| 0 | OCT- |
| 1 | OCT+ |
| 2 | FX |
| 3 | SCL |
| 4 | ENV |
| 5 | LFO |
| 6 | EDIT |
| 7 | GLO |
| 8 | HOME |
| 9 | SAVE |
| 10 | ARP |
| 11 | SEQ |
| 12 | PLAY |
| 13 | REC |

| Encoder | Label |
|---|---|
| 0 | SELECT |
| 1 | ALGORITHM |
| 6 | PRESETS |
| 2 | KNOB 1 |
| 3 | KNOB 2 |
| 4 | KNOB 3 |
| 5 | KNOB 4 |

SELECT is an encoder. The scanned sources do not show a SEL button and do not show a push on that encoder. The logical SEL gesture exists in the input arbiter and is unbound. See `docs/CONTROL_MAP.md`.

## Flash and RAM

See `docs/MEMORY_MAP.md`. The short version: the upstream hwtest linker uses a 96 KiB general RAM region inside a larger SRAM window, and a 1 MiB XIP window. Those figures belong to that script. They are not this firmware's capacity, and they are not copied into our build.

felucca-bsp `fm1_flash.h` expects JEDEC id `0x856014` and programs the NOR only from RAM with the XIP controller off and interrupts off. That is why capture in this milestone stays in RAM and does not call a flash driver.

## USB receive versus capture

A playback endpoint is not, by itself, a persistent sampler. This milestone converts host PCM only in process memory. Committing a take copies it into the project PCM store, which on the host is a caller buffer. There is no flash write.

## pi32v2 multiply note

felucca-bsp `docs/CPU.md` reports that an inlined 32×32→64 multiply beside 16-bit loads can leave a register wrong at the boot loader's 360 MHz. The sample reader uses a 16-bit fractional step and an 8-bit interpolator so the inner product stays in 32 bits. The transport FIR keeps its wide accumulation in a `noinline` helper. This is a precaution from the published note, not a measurement of our binary on a chip.
