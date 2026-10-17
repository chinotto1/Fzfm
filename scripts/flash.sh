#!/bin/sh
# Device writes are not implemented and are not authorized by the specification.
set -eu
echo "flash: refused."
echo "This script never writes firmware. There is no packaged image, no"
echo "verified recovery test, and no user authorization recorded for a device."
echo "See docs/BOOT_RECOVERY.md."
exit 2
