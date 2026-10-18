#!/bin/sh
set -eu
printf '%s\n' 'package_firmware: refused. Raw development app.bin can be target-built, but no installable release is qualified.'
printf '%s\n' 'Device recovery, safe flash partitions and project flash persistence remain unverified.'
printf '%s\n' 'Use python3 scripts/package_source.py for a source snapshot. See docs/BOOT_RECOVERY.md.'
exit 2
