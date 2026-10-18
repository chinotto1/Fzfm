# Host build of the portable engine. This is not a FM-1 target image.
# `cc` on the current image is clang without an ASan runtime.
ifeq ($(CC),cc)
override CC := gcc
endif
CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -I firmware
SAN ?= -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1
LDFLAGS ?= -lm

SRCS := \
	firmware/core/status.c \
	firmware/core/project.c \
	firmware/audio/src.c \
	firmware/audio/reader.c \
	firmware/audio/engine.c \
	firmware/audio/play.c \
	firmware/sampler/capture.c \
	firmware/dsp/additive.c \
	firmware/ui/input.c \
	firmware/ui/font.c \
	firmware/ui/home.c \
	firmware/sequence/history.c \
	firmware/sequence/tempo.c

.PHONY: test render clean
HEADERS := $(wildcard firmware/*/*.h)

build/test_core: tests/test_main.c $(SRCS) $(HEADERS)
	mkdir -p build
	$(CC) $(CFLAGS) $(SAN) -o $@ tests/test_main.c $(SRCS) $(LDFLAGS)

build/render_fixture: host/render/render_fixture.c $(SRCS) $(HEADERS)
	mkdir -p build/fixtures
	$(CC) $(CFLAGS) -O2 -o $@ host/render/render_fixture.c $(SRCS) $(LDFLAGS)

test: build/test_core
	./build/test_core

render: build/render_fixture
	./build/render_fixture build/fixtures

clean:
	rm -rf build

# The executable workstation is separate from the retained initial library.
WS_SRC := $(wildcard firmware/workstation/core/*.c firmware/workstation/audio/*.c firmware/workstation/dsp/*.c firmware/workstation/sampler/*.c firmware/workstation/sequence/*.c firmware/workstation/storage/*.c firmware/workstation/import/*.c firmware/workstation/ui/*.c firmware/workstation/midi/*.c firmware/workstation/platform/*.c)
WS_HEADERS := $(wildcard firmware/workstation/*/*.h)
WS_TEST := $(filter-out tests/workstation/main.c,$(wildcard tests/workstation/test_*.c))
.PHONY: workstation-test simulator demo
build/test_workstation: tests/workstation/main.c $(WS_TEST) $(WS_SRC) $(WS_HEADERS) $(wildcard tests/workstation/*.h)
	mkdir -p build
	$(CC) $(CFLAGS) -I firmware/workstation -I tests/workstation $(SAN) -o $@ tests/workstation/main.c $(WS_TEST) $(WS_SRC) $(LDFLAGS)
workstation-test: build/test_workstation
	./build/test_workstation
build/fm1-sim: host/simulator/main.c $(WS_SRC) $(WS_HEADERS)
	mkdir -p build
	$(CC) $(CFLAGS) -I firmware/workstation -O2 -o $@ host/simulator/main.c $(WS_SRC) $(LDFLAGS)
simulator: build/fm1-sim
demo: build/fm1-sim
	./build/fm1-sim --demo build/workstation-demo
test: workstation-test

build/libfm1_sim.so: host/simulator/bridge.c host/simulator/main.c $(WS_SRC) $(WS_HEADERS)
	mkdir -p build
	$(CC) $(CFLAGS) -I firmware/workstation -O2 -fPIC -shared -o $@ host/simulator/bridge.c $(WS_SRC) $(LDFLAGS)
.PHONY: serve
serve: build/libfm1_sim.so
	python3 host/simulator/serve.py

.DEFAULT_GOAL := all
.PHONY: all benchmark browser-test target baseline
all: build/fm1-sim build/libfm1_sim.so
build/benchmark: host/benchmark/main.c $(WS_SRC) $(WS_HEADERS)
	mkdir -p build
	$(CC) $(CFLAGS) -I firmware/workstation -O2 -o $@ host/benchmark/main.c $(WS_SRC) $(LDFLAGS)
build/benchmark-small: host/benchmark/main.c $(WS_SRC) $(WS_HEADERS)
	mkdir -p build
	$(CC) $(CFLAGS) -I firmware/workstation -DFM_PROFILE_SMALL -DFM_OBJECT_LIMIT=64 -DFM_ENGINE_EVENTS=128 -DFM_SEQUENCE_HELD_LIMIT=64 -DFM_DELAY_FRAMES=4098 -DFM_SRC_PHASE_LIMIT=64 -DFM_UNDO_DEPTH=2 -DFM_UNDO_PATCHES=24 -O2 -o $@ host/benchmark/main.c $(WS_SRC) $(LDFLAGS)
benchmark: build/benchmark build/benchmark-small
	./build/benchmark > build/benchmark.json
	./build/benchmark-small > build/benchmark-small.json
	cat build/benchmark.json build/benchmark-small.json
browser-test: build/libfm1_sim.so
	python3 tests/browser/test_panel.py
target:
	python3 firmware/workstation/platform/fm1/build.py
baseline:
	python3 firmware/workstation/platform/fm1/build.py --baseline
.PHONY: native-test
native-test: build/libfm1_sim.so
	python3 tests/native/test_bridge.py
