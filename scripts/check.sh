#!/bin/sh
# Offline host verification, no device writes or network.
set -eu
cd "$(dirname "$0")/.."
python3 scripts/verify_sources.py
make CC=gcc test render demo native-test
printf '%s\n' 'Host checks passed. Browser/device qualification is separate; see docs/BUILD.md.'
