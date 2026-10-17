#!/bin/sh
# Host object sizes and the safety ceilings compiled into this tree.
# These are not FM-1 linker results. A target map does not exist yet.
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/objects
gcc -std=c11 -Wall -Wextra -O2 -I firmware -c firmware/core/project.c -o build/objects/project.o
gcc -std=c11 -Wall -Wextra -O2 -I firmware -c firmware/audio/engine.c -o build/objects/engine.o
gcc -std=c11 -Wall -Wextra -O2 -I firmware -c firmware/audio/src.c -o build/objects/src.o
gcc -std=c11 -Wall -Wextra -O2 -I firmware -c firmware/sampler/capture.c -o build/objects/capture.o
gcc -std=c11 -Wall -Wextra -O2 -I firmware -c firmware/dsp/additive.c -o build/objects/additive.o
echo "host object sizes (not target flash/RAM):"
size build/objects/*.o
echo
echo "safety ceilings in this host build (not device capacity):"
grep -n -E "FZ_MAX_|FZ_ENGINE_RATE_HZ|FZ_SRC_QUEUE|FZ_HIST_CAP|FZ_MULTI_SLOTS|FZ_HARMONICS|FZ_ADDITIVE_POINTS" \
  firmware/core/project.h firmware/audio/engine.h firmware/audio/src.h firmware/sequence/history.h \
  firmware/dsp/additive.h firmware/core/types.h
echo
python3 - << 'PY'
rate = 36000
print(f"mono PCM16 bytes/second at {rate} Hz: {rate * 2}")
print(f"stereo PCM16 bytes/second: {rate * 4}")
print("16 hits x 0.25 s mono raw payload: ", 16 * int(0.25 * rate) * 2)
print("4 bars at 120 BPM 4/4 mono raw payload: ", int(rate * 2 * 8))
print("1024-point int16 cycle: ", 1024 * 2)
print("48 uint16 harmonic levels: ", 48 * 2)
print("No FM-1 sample-storage budget is claimed here.")
PY
