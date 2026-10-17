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

build/test_core: tests/test_main.c $(SRCS)
	mkdir -p build
	$(CC) $(CFLAGS) $(SAN) -o $@ tests/test_main.c $(SRCS) $(LDFLAGS)

build/render_fixture: host/render/render_fixture.c $(SRCS)
	mkdir -p build/fixtures
	$(CC) $(CFLAGS) -O2 -o $@ host/render/render_fixture.c $(SRCS) $(LDFLAGS)

test: build/test_core
	./build/test_core

render: build/render_fixture
	./build/render_fixture build/fixtures

clean:
	rm -rf build
