/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R03, R28: units stay distinct at API boundaries. Hot loops use the
 * named fields, not a bare int reused for frames, notes, and cents. */
#ifndef FZ_TYPES_H
#define FZ_TYPES_H

#include <stdint.h>

#define FZ_ENGINE_RATE_HZ 36000u
#define FZ_PCM_BYTES_PER_FRAME 2u
#define FZ_MONO_BYTES_PER_SECOND (FZ_ENGINE_RATE_HZ * FZ_PCM_BYTES_PER_FRAME)

typedef struct {
    uint32_t frames;
} FzFrames;

typedef struct {
    uint32_t us;
} FzMicros;

typedef struct {
    int16_t note; /* MIDI note number, -1 when unused */
} FzMidiNote;

typedef struct {
    int16_t cents;
} FzCents;

typedef enum {
    FZ_OK = 0,
    FZ_ERR_ARGS = 1,
    FZ_ERR_FULL = 2,
    FZ_ERR_TRACK_CAPACITY = 3,
    FZ_ERR_PATTERN_CAPACITY = 4,
    FZ_ERR_REGION_CAPACITY = 5,
    FZ_ERR_ASSET_CAPACITY = 6,
    FZ_ERR_SAMPLE_STORAGE = 7,
    FZ_ERR_BAD_ID = 8,
    FZ_ERR_BUSY = 9,
    FZ_ERR_RANGE = 10,
    FZ_ERR_FORMAT = 11,
    FZ_ERR_STATE = 12
} FzStatus;

const char *fz_status_str(FzStatus status);

static inline int16_t fz_sat16(int32_t x)
{
    if (x > 32767)
        return 32767;
    if (x < -32768)
        return -32768;
    return (int16_t)x;
}

static inline int32_t fz_iabs32(int16_t x)
{
    return x < 0 ? -(int32_t)x : (int32_t)x;
}

#endif
