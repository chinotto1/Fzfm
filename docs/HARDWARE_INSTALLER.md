# Hardware installation and recovery page

Open https://chinotto1.github.io/Fzfm/install/ in Chrome or Edge on Windows,
macOS or Linux. The emulator links to it. iPad browsers can read the page; device
updates require Web MIDI SysEx support and HTTPS. Nothing connects automatically.

The current FM1 FZ source revision is shown from the site build. Its Install
button is disabled: the board adapter explicitly sets `FELUCCA_OTA=0`, has not
booted on a physical unit, and has no qualified `.fwsc` or demonstrated recovery.
The browser cannot make that raw `app.bin` installable. `package_firmware.sh`
and `flash.sh` continue to refuse.

## Recover or change to a published firmware

1. Back up the current firmware's music, samples, settings and calibration using
   its documented tools. Skiff's complete music backup is not a full flash dump.
   Keep the matching previous firmware and its recovery instructions.
2. Connect one FM1 using a USB data cable, stop playback, close other MIDI apps,
   and click **Connect FM1**. Allow MIDI SysEx. Connection only queries identity;
   it never sends the upgrade command.
3. Choose **Recover / other firmware**, then Skiff 1.3.2, SLOOP 2.7.1 or official
   M-VAVE V15. Download the selected `.fwsc` from its publisher and choose it on
   this page. Arbitrary or modified packages are rejected. No file is uploaded.
4. Confirm the backup checkbox. **Install** opens a review with the connected
   identity, package filename and full SHA-256. Only the final confirmation sends
   an update command. Keep power/USB stable and the computer awake until it ends.
5. Success requires the device to return with the expected firmware identity.
   Check its screen, controls and sound yourself. The identity is a firmware
   family/product code; it does not independently establish its semantic version
   or prove physical memory capacity. Restore music with that firmware's editor.

The release catalog pins the exact file sizes and SHA-256 digests from the
publishers. Files are chosen locally rather than republished here. Selecting a
different target clears the previous verification. Choosing a corrupt file,
missing catalog information, multiple matching devices, disconnecting during
review, or an unsupported browser prevents the update from starting.

## Interrupted updates and ROM recovery

If the unit reports `ota-FM-1_713`, it is in Skiff's MIDI update loader; finish the
Skiff 1.3.2 package first. Likewise SLOOP requires `ota-FM-1_900` and stock V15
requires `ota-FM-1_015`. The page refuses to stream a different firmware into an
existing loader. A normal firmware install stages the selected package's loader
first; that loader is checked before any image data is served to its write stage.

After an interrupted write, reconnect, identify the device, select and reverify
the same package, and choose **Resume matching update**. An unplug, timeout,
invalid request, unexpected completion stage or different returned identity is
reported as incomplete; progress never becomes 100% merely because data was sent.

`WL80UBOOT` / USB `4C4A:8057` is ROM boot mode, not the MIDI updater. This browser
page does not implement a ROM flasher or install USB drivers. It links to Skiff's
recovery instructions and the separately maintained Windows ROM recovery tool
documentation. Supported packages, driver setup and recovery procedure must be
checked there. Restoring stock does not guarantee custom music survives.

## Evidence and remaining work

`make installer-test` runs Node 20+ checks against a wire-level simulated FM1:
framing, checksums, queued USB acknowledgements across port handover, read-only identity, both transfer stages, exact-loader resume,
final identity, wrong target, disconnects, invalid read ranges/stages and cleanup.
`python3 tests/browser/test_installer.py` exercises the built `/Fzfm/install/`
page with injected MIDI ports, local package selection, corruption/backup/review
gates, simulated install/unplug/resume and 320/390-pixel layouts. It never opens a
physical device. `FM1_TEST_URL` can point to the deployed `/install/` for this test.

Physical Windows USB/MIDI transfers, interrupted-write recovery, calibration/data
preservation and the FM1 FZ firmware's own OTA/recovery path remain unverified.
The page is tested browser tooling, not evidence that a board image boots safely.
See [BOOT_RECOVERY.md](BOOT_RECOVERY.md) and [DEVICE_TESTS.md](DEVICE_TESTS.md).
