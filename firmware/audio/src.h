/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R03, T05, T06: streaming sample-rate conversion at the USB/codec boundary.
 * The musical engine stays at 36 kHz. Equal rates copy. Unequal rates use a
 * carried rational position and a windowed-sinc kernel built at init. */
#ifndef FZ_SRC_H
#define FZ_SRC_H

#include <stdint.h>

#define FZ_SRC_TAPS 16
#define FZ_SRC_PHASES 128
#define FZ_SRC_QUEUE 4096

typedef struct {
    uint32_t in_rate;
    uint32_t out_rate;
    int channels; /* 1 or 2 */
    int bypass;
    int16_t coeff[FZ_SRC_PHASES][FZ_SRC_TAPS];
    int16_t hist[2][FZ_SRC_TAPS];
    uint64_t out_i;
    uint64_t consumed; /* input frames shifted into history */
    int16_t queue[2][FZ_SRC_QUEUE];
    uint32_t q_r, q_n;
    uint32_t overrun_frames;
    uint32_t underrun_events;
    uint32_t latency_in_frames;
} FzSrc;

int fz_src_init(FzSrc *s, uint32_t in_rate, uint32_t out_rate, int channels);
void fz_src_reset(FzSrc *s);
uint32_t fz_src_write(FzSrc *s, const int16_t *interleaved, uint32_t frames);
uint32_t fz_src_read(FzSrc *s, int16_t *interleaved, uint32_t max_frames);

#endif
