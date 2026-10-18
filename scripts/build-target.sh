#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
exec python3 firmware/workstation/platform/fm1/build.py "$@"
