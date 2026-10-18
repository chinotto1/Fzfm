# Boot, storage and recovery gates

The source now cross-compiles and links a range-checked CPU0 BSP baseline and the sampler's raw development image. **Neither has been booted here.** No device was accessed/flashed and there is no physically demonstrated known-good recovery path.

The sampler adapter retains bounded DMA/input queues and a watchdog/noinit failure guard intended to enter the BSP's UBOOT path after repeated incomplete boots. That mechanism is compiled source, not recovery evidence. No clock/voltage/CPU1/cache changes are enabled. The SRAM/linker windows are source-derived and provisional.

The target SAVE callback returns UNSUPPORTED; it never writes PCM, settings, calibration or arbitrary flash. Host generation saves and imported project containers are separate from firmware updates. No Felucca/X0X storage partition is adopted for sampler data. Flash erase/program must execute from a proven RAM path with interrupt timing accounted for before persistence can be enabled.

`package_firmware.sh` and `flash.sh` refuse. `package_source.py` exports corresponding project/BSP source and notices only. Raw `app.bin` is not an installable update and must not be renamed into one. Vendor runtime complete-source/licensing and resource/update packaging need review before a distributable binary release.

The pinned BSP describes `fm1_enter_uboot` writing `usb_update_mode` into its boot mailbox and resetting, and separate rescue tools using stock firmware. Those references were inspected; no helper was run and no write path is authorized by this document. The reproduced baseline image is not yet “known-good.”

Before a future specifically authorized image write: preserve calibration/user data through a demonstrated backup path, verify the exact unit identity/layout and image hash, demonstrate stock restore on that unit, confirm safe app/update ranges and tested SRAM, then obtain authorization for that concrete image. Physical recovery/power interruption tests remain in DEVICE_TESTS.md. The present implementation stops short of these actions.
