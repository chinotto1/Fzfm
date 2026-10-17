#!/bin/sh
# Target packaging is not available in this tree yet.
set -eu
echo "package_firmware: refused."
echo "The JieLi pi32v2 toolchain and AC79 SDK are not part of this repository."
echo "No target image has been built. Nothing was written to a device."
echo "See docs/BOOT_RECOVERY.md and docs/HARDWARE_AUDIT.md."
exit 2
