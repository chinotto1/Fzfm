#!/bin/sh
# Host checks. No device, no network, no proprietary ROM.
set -eu
cd "$(dirname "$0")/.."
make CC=gcc test
make CC=gcc render
echo "host checks ok"
