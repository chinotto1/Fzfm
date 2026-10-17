/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R19: 48-harmonic recipe. Rebuild is non-real-time. Playback is the
 * resulting periodic table through the 36 kHz reader. Levels are 0..256.
 * Fixed zero phase. The table is scaled once so the peak sits at 70% of
 * full scale; partials are not clipped individually. */
#ifndef FZ_ADDITIVE_H
#define FZ_ADDITIVE_H

#include <stdint.h>

#include "core/types.h"

#define FZ_HARMONICS 48
#define FZ_ADDITIVE_POINTS 1024
#define FZ_ADDITIVE_PEAK 22936 /* ~70% of 32767 */

typedef struct {
    uint16_t level[FZ_HARMONICS];
    uint16_t points;
} FzAdditiveRecipe;

void fz_additive_clear(FzAdditiveRecipe *r);
FzStatus fz_additive_render(const FzAdditiveRecipe *r, int16_t *dst, uint32_t points);
FzStatus fz_additive_write(const FzAdditiveRecipe *r, uint8_t *dst, uint32_t cap, uint32_t *out_len);
FzStatus fz_additive_read(FzAdditiveRecipe *r, const uint8_t *src, uint32_t len);

#endif
