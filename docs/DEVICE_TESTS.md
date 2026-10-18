# Device tests

No physical test in this file has been run. A PI32v2 baseline and sampler now compile/link, but no FM-1 or iPad is attached. Target compilation does not close a device case.

## Pending, in order

1. **Identity.** Read the USB product string, the flash JEDEC id, and the boot-loader clock. Compare them with `docs/HARDWARE_AUDIT.md`. Do not program flash to do this.
2. **Controls.** Press each printed button and turn each encoder. Confirm the Melodee label map, and verify SCL as the intended logical SEL binding and verify SELECT remains rotary. Photograph the panel if the map disagrees.
3. **Display.** Confirm 240×240 RGB565 and that a flat color frame completes.
4. **Codec rate.** Measure ALNK frame timing against a known clock. Record the rate. Do not assume 44.1 kHz from the USB descriptor alone.
5. **USB playback.** From a computer, then from one named iPad music app, play a 440 Hz tone into the device with MIDI active. Record cable, hub, power, and OS version. Confirm the tone reaches the 36 kHz capture buffer and that the monitored output does not loop back into the host.
6. **Disconnect.** Unplug during capture and during playback. The current project must still play, and the device must not lock up.
7. **Flash interruption.** Only after a stock restore has succeeded on this unit. Measure the longest time interrupts stay off during one erase. Do not stream a take to flash until that number is known.
8. **Recovery.** Restore stock with the BSP rescue tool, then return to a known image. Record the tool version and the image hashes.

Host tests do not substitute for any line above. `docs/REQUIREMENTS_STATUS.md` marks T03, T04, T46, and T47 pending for that reason.
