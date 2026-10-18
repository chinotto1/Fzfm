/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "additive.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <string.h>

void fz_additive_clear(FzAdditiveRecipe *r)
{
    memset(r, 0, sizeof *r);
    r->points = FZ_ADDITIVE_POINTS;
}

FzStatus fz_additive_render(const FzAdditiveRecipe *r, int16_t *dst, uint32_t points)
{
    uint32_t n, h;
    double peak = 0.0;
    double *acc;
    /* Bounded stack table: the default length is 1024. Longer requests use a
     * fixed ceiling so this rebuild cannot allocate. */
    double stack[FZ_ADDITIVE_POINTS];

    if (!r || !dst || points == 0 || points > FZ_ADDITIVE_POINTS)
        return FZ_ERR_ARGS;
    for (h = 0; h < FZ_HARMONICS; h++) {
        if (r->level[h] > 256)
            return FZ_ERR_RANGE;
    }
    acc = stack;
    memset(acc, 0, points * sizeof(double));
    for (n = 0; n < points; n++) {
        double sum = 0.0;
        for (h = 0; h < FZ_HARMONICS; h++) {
            double level = r->level[h];
            if (level == 0.0)
                continue;
            sum += level * sin(2.0 * M_PI * (double)(h + 1u) * (double)n / (double)points);
        }
        acc[n] = sum;
        if (fabs(sum) > peak)
            peak = fabs(sum);
    }
    if (peak < 1e-12) {
        memset(dst, 0, points * sizeof(int16_t));
        return FZ_OK;
    }
    for (n = 0; n < points; n++) {
        double y = acc[n] / peak * (double)FZ_ADDITIVE_PEAK;
        dst[n] = fz_sat16((int32_t)lround(y));
    }
    return FZ_OK;
}

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t fnv(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u;
    uint32_t i;
    for (i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

FzStatus fz_additive_write(const FzAdditiveRecipe *r, uint8_t *dst, uint32_t cap, uint32_t *out_len)
{
    uint32_t i;
    uint32_t need = 4u + 2u + 2u + FZ_HARMONICS * 2u + 2u + 4u;
    if (!r || !dst || !out_len)
        return FZ_ERR_ARGS;
    if (cap < need || r->points > FZ_ADDITIVE_POINTS)
        return FZ_ERR_RANGE;
    for (i = 0; i < FZ_HARMONICS; i++) {
        if (r->level[i] > 256)
            return FZ_ERR_RANGE;
    }
    dst[0] = 'F';
    dst[1] = 'Z';
    dst[2] = 'H';
    dst[3] = '1';
    put_u16(dst + 4, 1);
    put_u16(dst + 6, FZ_HARMONICS);
    for (i = 0; i < FZ_HARMONICS; i++)
        put_u16(dst + 8 + i * 2u, r->level[i]);
    put_u16(dst + 8 + FZ_HARMONICS * 2u, r->points ? r->points : FZ_ADDITIVE_POINTS);
    put_u32(dst + need - 4u, fnv(dst, need - 4u));
    *out_len = need;
    return FZ_OK;
}

FzStatus fz_additive_read(FzAdditiveRecipe *r, const uint8_t *src, uint32_t len)
{
    uint32_t need = 4u + 2u + 2u + FZ_HARMONICS * 2u + 2u + 4u;
    uint32_t i;
    uint16_t count, points;
    FzAdditiveRecipe decoded;
    if (!r || !src)
        return FZ_ERR_ARGS;
    if (len < need || src[0] != 'F' || src[1] != 'Z' || src[2] != 'H' || src[3] != '1')
        return FZ_ERR_FORMAT;
    if (get_u16(src + 4) != 1)
        return FZ_ERR_FORMAT;
    count = get_u16(src + 6);
    if (count != FZ_HARMONICS)
        return FZ_ERR_FORMAT;
    if (get_u32(src + need - 4u) != fnv(src, need - 4u))
        return FZ_ERR_FORMAT;
    points = get_u16(src + 8 + FZ_HARMONICS * 2u);
    if (points == 0 || points > FZ_ADDITIVE_POINTS)
        return FZ_ERR_FORMAT;
    memset(&decoded, 0, sizeof decoded);
    decoded.points = points;
    for (i = 0; i < FZ_HARMONICS; i++) {
        uint16_t level = get_u16(src + 8 + i * 2u);
        if (level > 256)
            return FZ_ERR_FORMAT;
        decoded.level[i] = level;
    }
    *r = decoded;
    return FZ_OK;
}
