/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* Narrow board adapter. Portable code must not include chip addresses.
 * The host simulator implements this with a monotonic counter. The FM-1
 * target will implement it on felucca-bsp (see docs/adr/0001). */
#ifndef FZ_BOARD_H
#define FZ_BOARD_H

#include <stdint.h>

typedef struct FzBoard FzBoard;

struct FzBoard {
    uint32_t (*time_us)(void *ctx);
    /* Interleaved stereo int16 at the transport rate. Return frames accepted. */
    uint32_t (*pcm_tx)(void *ctx, const int16_t *interleaved, uint32_t frames);
    void *ctx;
};

#endif
