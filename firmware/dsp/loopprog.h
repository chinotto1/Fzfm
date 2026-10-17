/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R18: up to eight loop sections. A single sustain loop is one section,
 * not the whole feature. */
#ifndef FZ_LOOPPROG_H
#define FZ_LOOPPROG_H

#include <stdint.h>

#include "core/types.h"

#define FZ_LOOP_SECTIONS 8

typedef struct {
    uint32_t start;
    uint32_t end; /* exclusive */
    uint16_t repeats; /* 0 = until note-off, then advance */
    uint8_t next; /* 255 = stop after this section */
    uint16_t crossfade; /* frames, must be shorter than the section */
} FzLoopSection;

typedef struct {
    FzLoopSection sec[FZ_LOOP_SECTIONS];
    int count;
} FzLoopProg;

typedef struct {
    int section;
    uint32_t pos;
    uint16_t done_repeats;
    int released;
    int active;
} FzLoopRun;

FzStatus fz_loop_validate(const FzLoopProg *prog, uint32_t frames);
void fz_loop_start(FzLoopRun *run);
/* Returns the source index to read, or -1 when the program has finished.
 * `released` advances a zero-repeat section. */
int fz_loop_index(const FzLoopProg *prog, FzLoopRun *run, uint32_t frames, int released);
/* Unity-rate preview. The last `crossfade` frames blend into the next section. */
int16_t fz_loop_sample(const FzLoopProg *prog, FzLoopRun *run, const int16_t *pcm, uint32_t frames, int released);

#endif
