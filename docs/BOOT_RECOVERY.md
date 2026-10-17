# Boot and recovery

This procedure is **not verified**. No device was written, and this repository cannot build a target image yet.

## Rules for this project

- `scripts/flash.sh` always refuses. There is no packaged image and no authorization record.
- `scripts/package_firmware.sh` always refuses. The JieLi toolchain is not in this tree and was not installed here.
- Do not copy Felucca or X0X recovery addresses into a partition layout for this firmware.
- A project import, when it exists, is not a firmware update.
- Calibration and the boot mailbox stay out of the project file. The project file does not exist yet; see `docs/DATA_FORMAT.md`.

## What the pinned BSP says, unverified here

felucca-bsp `7d8f849`:

- `fm1_enter_uboot` writes the 16 bytes `usb_update_mode` at `0x01C7FD80` and resets through P33. The chip's update mode is then supposed to enumerate.
- `fm1_enter_update` writes a 112-byte parameter block at `0x01C7FD88` and resets the core so the loader can run a staged update.
- `tools/fm1_rescue.py` and `fm1_rescue.sh` are described as restoring stock firmware through that update mode on macOS. They download a loader and stock image by hash. Those downloads are not in this repository and were not run.
- `tools/fm1pkg.py` builds a package that includes three AC79 SDK files (`uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin`), which the BSP's licensing note treats as Apache-2.0. We do not ship them.
- Flash program/erase in `fm1_flash.h` requires interrupts off and code in RAM, because XIP is turned off for the duration. A save or a sample commit that calls this from the audio path is unsafe until that interruption is measured.

## Known-good image

We do not have one. The upstream file `examples/hwtest/hwtest.bin` is described as the safe test image (screen, knobs, buttons, LEDs, USB serial) and `hwtest-exp.bin` as the one that includes experimental CPU1 and expanded flash. Neither binary is stored here, and neither was booted here.

## Before any future device write

1. Read back, or otherwise preserve, everything the chosen tool can read: user projects and calibration. Record what could not be read.
2. Confirm the package hash, the device identity, and that the address range is inside the supported update region for that exact tool.
3. Have the stock-restore path from the BSP in hand and a second machine that can run it.
4. Get an explicit yes for that specific image. This document is not that yes.

Interrupted-update experiments are out of scope until that restore path has been used successfully on the same unit.
