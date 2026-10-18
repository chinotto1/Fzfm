#!/bin/sh
# Check local host dependencies; never download or open devices implicitly.
set -eu
cd "$(dirname "$0")/.."
command -v gcc
command -v make
command -v python3
python3 scripts/verify_sources.py
printf '%s\n' 'Host dependencies ready. Run make test, make demo, or make serve.'
printf '%s\n' 'Target compiler/runtime acquisition is documented in docs/BUILD.md.'
