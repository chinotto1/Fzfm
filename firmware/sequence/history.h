/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R10, R32: bounded live-event ring. Sequencer playback and menu keys are
 * stored only so they can be excluded from a retrospective slice. */
#ifndef FZ_HISTORY_H
#define FZ_HISTORY_H

#include <stdint.h>

#define FZ_HIST_CAP 512

typedef enum {
    FZ_SRC_KEYBOARD = 0,
    FZ_SRC_SEQUENCER = 1,
    FZ_SRC_MENU = 2,
    FZ_SRC_EXTERNAL = 3
} FzEventSource;

typedef struct {
    uint32_t time_us;
    uint8_t note;
    uint8_t velocity; /* 0 = note-off */
    uint8_t track;
    uint8_t source;
} FzHistEvent;

typedef struct {
    FzHistEvent ev[FZ_HIST_CAP];
    uint32_t next;
    uint32_t count;
    uint32_t dropped;
} FzHistory;

void fz_history_init(FzHistory *h);
void fz_history_push(FzHistory *h, FzHistEvent event);
/* Events with time in (end_us - window_us, end_us], keyboard and external only. */
int fz_history_slice(const FzHistory *h, uint32_t end_us, uint32_t window_us, FzHistEvent *out, int max_out);

#endif
