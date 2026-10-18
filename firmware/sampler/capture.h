/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R02, R05, R06, R08: USB-domain PCM is converted by the caller to 36 kHz
 * mono and fed here. Flash is not touched. A take lives in the caller buffer
 * until service() copies it into the project store. */
#ifndef FZ_CAPTURE_H
#define FZ_CAPTURE_H

#include <stdint.h>

#include "core/project.h"

#define FZ_PRE_MAX 2048
#define FZ_CAP_TRACE 32

typedef enum {
    FZ_CAP_CONFIGURE = 0,
    FZ_CAP_WAIT_INPUT,
    FZ_CAP_CAPTURING,
    FZ_CAP_END_DETECTED,
    FZ_CAP_FINALIZE,
    FZ_CAP_COMMIT,
    FZ_CAP_ADVANCE,
    FZ_CAP_STORAGE_FULL,
    FZ_CAP_INPUT_LOST,
    FZ_CAP_PAUSED,
    FZ_CAP_FINISHED,
    FZ_CAP_CANCELLED,
    FZ_CAP_PERFORMANCE
} FzCapState;

typedef struct {
    FzProject *project;
    FzId instrument;
    FzId track;
    int16_t *take;
    uint32_t take_cap;
    uint32_t take_frames;
    int16_t pre[FZ_PRE_MAX];
    uint32_t pre_n;
    uint32_t pre_write;
    uint32_t pre_frames;
    int16_t start_amp;
    int16_t stop_amp;
    uint32_t silence_hold;
    uint32_t silence_run;
    uint32_t tail_keep;
    uint32_t min_frames;
    int slot;
    int committed[FZ_MULTI_SLOTS];
    FzId slot_region[FZ_MULTI_SLOTS];
    FzId slot_asset[FZ_MULTI_SLOTS];
    uint32_t slot_watermark[FZ_MULTI_SLOTS];
    int root_set;
    uint8_t next_root;
    FzCapState state;
    int auto_arm;
    int octave; /* unchanged by OCT+ */
    int volatile_take;
    int take_pinned;
    uint16_t peak;
    int clipped;
    uint32_t clip_count;
    int input_alive;
    FzCapState trace[FZ_CAP_TRACE];
    int trace_n;
    char message[48];
} FzCapture;

int16_t fz_amp_from_dbfs(int dbfs);
int16_t fz_pcm_mono_sum(int16_t left, int16_t right);

void fz_capture_init(FzCapture *c, FzProject *project, FzId instrument, FzId track, int16_t *take,
                     uint32_t take_cap);
void fz_capture_set_thresholds(FzCapture *c, int start_dbfs, int stop_dbfs, uint32_t pre_frames,
                               uint32_t silence_hold, uint32_t min_frames);
void fz_capture_arm(FzCapture *c);
void fz_capture_set_root(FzCapture *c, uint8_t root);
/* Block of 36 kHz mono PCM. Real-time safe: no allocation. */
void fz_capture_accept(FzCapture *c, const int16_t *mono, uint32_t frames);
/* Non-real-time: commit at most one finished take. */
FzStatus fz_capture_service(FzCapture *c);
void fz_capture_oct_plus(FzCapture *c);
void fz_capture_undo_last(FzCapture *c);
void fz_capture_skip(FzCapture *c);
void fz_capture_pause(FzCapture *c);
void fz_capture_input_lost(FzCapture *c);
void fz_capture_cancel(FzCapture *c);
int fz_capture_oct_flash(const FzCapture *c);

#endif
