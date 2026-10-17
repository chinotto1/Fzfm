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
	firmware/sequence/tempo.c \
	firmware/sequence/seq.c \
	firmware/dsp/filter.c \
	firmware/dsp/envelope.c \
	firmware/dsp/loopprog.c \
	firmware/dsp/mangle.c \
	firmware/dsp/fx.c \
	firmware/dsp/bake.c \
	firmware/storage/journal.c \
	firmware/storage/bundle.c \
	firmware/import/import.c \
	firmware/ui/session.c \
	firmware/ui/edit.c \
	firmware/ui/screen.c

.PHONY: test render sim clean

build/test_core: tests/test_main.c tests/test_product.c tests/test_sim.c tests/check.c host/sim/sim.c $(SRCS)
	mkdir -p build
	$(CC) $(CFLAGS) $(SAN) -I tests -I host/sim -o $@ tests/test_main.c tests/check.c tests/test_product.c tests/test_sim.c host/sim/sim.c $(SRCS) $(LDFLAGS)

build/render_fixture: host/render/render_fixture.c $(SRCS)
	mkdir -p build/fixtures
	$(CC) $(CFLAGS) -O2 -o $@ host/render/render_fixture.c $(SRCS) $(LDFLAGS)

test: build/test_core
	./build/test_core

render: build/render_fixture
	./build/render_fixture build/fixtures

# Browser panel. The wasm file is a build product, not a device image.
# wasm32 has native 64-bit arithmetic, so this link does not need compiler-rt.
web/sim/sim.wasm: host/sim/shim.c host/sim/sim.c $(SRCS)
	mkdir -p web/sim
	clang --target=wasm32 -std=c11 -O2 -Wall -Wextra -Werror -nostdlib -nostdlibinc \
		-I firmware -I host/sim -I host/sim/include \
		-Wl,--no-entry -Wl,--export-all -Wl,--export-memory \
		-Wl,--initial-memory=2097152 -Wl,-z,stack-size=65536 \
		-o $@ host/sim/shim.c host/sim/sim.c $(SRCS)

sim: web/sim/sim.wasm

clean:
	rm -rf build web/sim/sim.wasm
