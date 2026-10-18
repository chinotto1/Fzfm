/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "reader.h"

#include <math.h>

#include "core/types.h"

static uint32_t ratio_q16(double ratio)
{
    if (ratio < 0.0)
        ratio = 0.0;
    if (ratio > 65535.0)
        ratio = 65535.0;
    return (uint32_t)(ratio * 65536.0 + 0.5);
}

uint32_t fz_pitch_step_q16(uint32_t native_rate, uint32_t engine_rate, int root, int note, int cents)
{
    double semis;
    double ratio;
    if (engine_rate == 0)
        return 0;
    semis = ((double)note - (double)root) + (double)cents / 100.0;
    ratio = ((double)native_rate / (double)engine_rate) * pow(2.0, semis / 12.0);
    return ratio_q16(ratio);
}

uint32_t fz_cycle_step_q16(uint32_t points, uint32_t engine_rate, int note, int cents)
{
    double semis = ((double)note - 69.0) + (double)cents / 100.0;
    double freq = 440.0 * pow(2.0, semis / 12.0);
    double ratio;
    if (engine_rate == 0)
        return 0;
    ratio = (double)points * freq / (double)engine_rate;
    return ratio_q16(ratio);
}

static void load_step(FzReader *r, uint32_t step_q16)
{
    r->step_i = step_q16 >> 16;
    r->step_f = (uint16_t)(step_q16 & 0xFFFFu);
    r->index = 0;
    r->frac = 0;
    r->active = r->pcm != 0 && r->frames > 0 && step_q16 > 0;
    r->reverse = 0;
    r->looping = 0;
    r->loop_start = 0;
    r->loop_end = r->frames;
}

int fz_reader_start_sample(FzReader *r, const int16_t *pcm, uint32_t frames, uint32_t native_rate,
                           int root, int note, int cents)
{
    if (!r)
        return -1;
    r->pcm = pcm;
    r->frames = frames;
    r->cycle = 0;
    load_step(r, fz_pitch_step_q16(native_rate, FZ_ENGINE_RATE_HZ, root, note, cents));
    return r->active ? 0 : -1;
}

int fz_reader_start_cycle(FzReader *r, const int16_t *table, uint32_t points, int note, int cents)
{
    if (!r)
        return -1;
    r->pcm = table;
    r->frames = points;
    r->cycle = 1;
    r->looping = 1;
    load_step(r, fz_cycle_step_q16(points, FZ_ENGINE_RATE_HZ, note, cents));
    r->looping = 1;
    r->loop_start = 0;
    r->loop_end = points;
    return r->active ? 0 : -1;
}

void fz_reader_set_loop(FzReader *r, uint32_t start, uint32_t end_exclusive)
{
    if (!r || end_exclusive <= start || end_exclusive > r->frames)
        return;
    r->looping = 1;
    r->loop_start = start;
    r->loop_end = end_exclusive;
}

void fz_reader_set_reverse(FzReader *r, int reverse)
{
    if (!r)
        return;
    if (reverse && !r->reverse && r->index == 0 && r->frac == 0 && r->frames)
        r->index = r->frames - 1u;
    r->reverse = reverse ? 1 : 0;
}

static int16_t sample_at(const FzReader *r, uint32_t index)
{
    uint32_t span;

    if (r->frames == 0)
        return 0;
    if ((r->cycle || r->looping) && r->loop_end > r->loop_start && r->index < r->loop_end) {
        span = r->loop_end - r->loop_start;
        if (index >= r->loop_end)
            index = r->loop_start + (index - r->loop_end) % span;
    }
    if (index >= r->frames)
        return 0;
    return r->pcm[index];
}

static int16_t interp(const FzReader *r)
{
    int32_t s0 = sample_at(r, r->index);
    int32_t s1 = sample_at(r, r->index + 1u);
    int32_t d = s1 - s0;
    /* 8-bit fraction keeps the product inside int32 (pi32v2: no wide multiply
     * beside the 16-bit sample loads in this loop). */
    return fz_sat16(s0 + (d * (int32_t)(r->frac >> 8)) / 256);
}

int16_t fz_reader_next(FzReader *r)
{
    int16_t y;
    uint32_t frac;
    uint32_t add;

    if (!r || !r->active)
        return 0;
    y = interp(r);

    if (r->reverse) {
        uint32_t subtract = r->step_i + (r->frac < r->step_f ? 1u : 0u);
        r->frac = (uint16_t)((uint32_t)r->frac - r->step_f);
        if (r->looping && r->index >= r->loop_start &&
            subtract > r->index - r->loop_start) {
            uint32_t span = r->loop_end - r->loop_start;
            uint32_t overshoot = subtract - (r->index - r->loop_start);
            if (!span) { r->active = 0; return y; }
            r->index = r->loop_end - 1u - (overshoot - 1u) % span;
        } else if (subtract > r->index) {
            r->active = 0;
        } else {
            r->index -= subtract;
        }
        return y;
    }

    frac = (uint32_t)r->frac + (uint32_t)r->step_f;
    add = r->step_i + (frac >> 16);
    r->frac = (uint16_t)(frac & 0xFFFFu);
    if (r->cycle || r->looping) {
        uint32_t span = r->loop_end - r->loop_start;
        if (!span) { r->active = 0; return y; }
        if (r->index >= r->loop_end) {
            r->index = r->loop_start + (r->index - r->loop_end) % span;
        }
        if (add >= r->loop_end - r->index)
            r->index = r->loop_start + (add - (r->loop_end - r->index)) % span;
        else
            r->index += add;
        return y;
    }
    if (add >= r->frames - r->index)
        r->active = 0;
    else
        r->index += add;
    return y;
}
