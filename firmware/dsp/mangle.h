/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#ifndef FZ_MANGLE_H
#define FZ_MANGLE_H

#include <stdint.h>

#include "core/project.h"

FzStatus fz_render_reverse(const int16_t *src, uint32_t frames, int16_t *dst);
FzStatus fz_render_mix(const int16_t *a, uint32_t na, const int16_t *b, uint32_t nb, int gain_a_q15,
                       int gain_b_q15, int32_t offset_b, int16_t *dst, uint32_t dst_cap, uint32_t *out_frames);
FzStatus fz_render_cross(const int16_t *a, uint32_t na, const int16_t *b, uint32_t nb, uint32_t overlap,
                         int16_t *dst, uint32_t dst_cap, uint32_t *out_frames);
FzStatus fz_cut_cycle(const int16_t *src, uint32_t src_frames, uint32_t start, uint32_t length, int16_t *dst);
void fz_draw_set(int16_t *table, uint32_t points, uint32_t index, int16_t sample);
void fz_draw_normalize(int16_t *table, uint32_t points);
void fz_draw_smooth(int16_t *table, uint32_t points);
void fz_preset_sine(int16_t *dst, uint32_t points);
void fz_preset_square(int16_t *dst, uint32_t points);

/* Store a new asset. On failure the project store is unchanged. */
FzStatus fz_project_store_pcm(FzProject *p, const int16_t *pcm, uint32_t frames, int cycle, FzId *out);

#endif
