/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "mangle.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

FzStatus fz_render_reverse(const int16_t *src, uint32_t frames, int16_t *dst)
{
    uint32_t i;
    if (!src || !dst || frames == 0)
        return FZ_ERR_ARGS;
    for (i = 0; i < frames; i++)
        dst[i] = src[frames - 1u - i];
    return FZ_OK;
}

FzStatus fz_render_mix(const int16_t *a, uint32_t na, const int16_t *b, uint32_t nb, int gain_a_q15,
                       int gain_b_q15, int32_t offset_b, int16_t *dst, uint32_t dst_cap, uint32_t *out_frames)
{
    int32_t start_b = offset_b;
    int32_t end_b;
    uint32_t total, i;
    if (!a || !b || !dst || !out_frames || na == 0 || nb == 0)
        return FZ_ERR_ARGS;
    end_b = start_b + (int32_t)nb;
    /* A negative offset shifts both streams so every source frame lands in dst. */
    {
        int32_t shift = start_b < 0 ? -start_b : 0;
        int32_t b_end = end_b + shift;
        total = na + (uint32_t)shift;
        if (b_end > (int32_t)total)
            total = (uint32_t)b_end;
        if (total == 0 || total > dst_cap)
            return FZ_ERR_RANGE;
        memset(dst, 0, total * sizeof(int16_t));
        for (i = 0; i < na; i++)
            dst[i + (uint32_t)shift] = fz_sat16((int32_t)a[i] * gain_a_q15 / 32767);
        for (i = 0; i < nb; i++) {
            int32_t at = start_b + shift + (int32_t)i;
            dst[at] = fz_sat16((int32_t)dst[at] + (int32_t)b[i] * gain_b_q15 / 32767);
        }
    }
    *out_frames = total;
    return FZ_OK;
}

FzStatus fz_render_cross(const int16_t *a, uint32_t na, const int16_t *b, uint32_t nb, uint32_t overlap,
                         int16_t *dst, uint32_t dst_cap, uint32_t *out_frames)
{
    uint32_t total, i;
    if (!a || !b || !dst || !out_frames || na == 0 || nb == 0)
        return FZ_ERR_ARGS;
    if (overlap > na || overlap > nb)
        return FZ_ERR_RANGE;
    total = na + nb - overlap;
    if (total > dst_cap)
        return FZ_ERR_RANGE;
    for (i = 0; i < na - overlap; i++)
        dst[i] = a[i];
    for (i = 0; i < overlap; i++) {
        int32_t wa = (int32_t)(overlap - i);
        int32_t wb = (int32_t)i;
        int32_t y = ((int32_t)a[na - overlap + i] * wa + (int32_t)b[i] * wb) / (int32_t)overlap;
        dst[na - overlap + i] = fz_sat16(y);
    }
    for (i = overlap; i < nb; i++)
        dst[na - overlap + i] = b[i];
    if (overlap == 0) {
        memcpy(dst, a, na * sizeof(int16_t));
        memcpy(dst + na, b, nb * sizeof(int16_t));
    }
    *out_frames = total;
    return FZ_OK;
}

FzStatus fz_cut_cycle(const int16_t *src, uint32_t src_frames, uint32_t start, uint32_t length, int16_t *dst)
{
    if (!src || !dst || length == 0 || start >= src_frames || length > src_frames - start)
        return FZ_ERR_RANGE;
    memcpy(dst, src + start, length * sizeof(int16_t));
    return FZ_OK;
}

void fz_draw_set(int16_t *table, uint32_t points, uint32_t index, int16_t sample)
{
    if (!table || index >= points)
        return;
    table[index] = sample;
}

void fz_draw_normalize(int16_t *table, uint32_t points)
{
    uint32_t i;
    int32_t peak = 0;
    if (!table || points == 0)
        return;
    for (i = 0; i < points; i++) {
        int32_t a = table[i] < 0 ? -(int32_t)table[i] : table[i];
        if (a > peak)
            peak = a;
    }
    if (peak < 1)
        return;
    for (i = 0; i < points; i++)
        table[i] = fz_sat16((int32_t)table[i] * 28000 / peak);
}

void fz_draw_smooth(int16_t *table, uint32_t points)
{
    uint32_t i;
    int16_t prev;
    if (!table || points < 3)
        return;
    prev = table[0];
    for (i = 1; i + 1 < points; i++) {
        int16_t cur = table[i];
        table[i] = fz_sat16(((int32_t)prev + table[i] + table[i + 1]) / 3);
        prev = cur;
    }
}

void fz_preset_sine(int16_t *dst, uint32_t points)
{
    uint32_t i;
    for (i = 0; i < points; i++)
        dst[i] = (int16_t)lround(sin(2.0 * M_PI * (double)i / (double)points) * 14000.0);
}

void fz_preset_square(int16_t *dst, uint32_t points)
{
    uint32_t i;
    for (i = 0; i < points; i++)
        dst[i] = (i < points / 2u) ? 12000 : -12000;
}

FzStatus fz_project_store_pcm(FzProject *p, const int16_t *pcm, uint32_t frames, int cycle, FzId *out)
{
    uint32_t before;
    FzStatus st;
    if (!p || !out)
        return FZ_ERR_ARGS;
    before = p->pcm_used;
    st = fz_project_add_pcm(p, pcm, frames, FZ_ENGINE_RATE_HZ, cycle, out);
    if (st != FZ_OK)
        p->pcm_used = before;
    return st;
}
