/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R04: vintage variable-speed playback. Pitch and duration move together.
 * Inner loop is 16-bit fractional phase. No 32x32->64 multiply here: the
 * pi32v2 erratum (felucca-bsp docs/CPU.md) shows up when those multiplies
 * sit beside 16-bit loads. */
#ifndef FZ_READER_H
#define FZ_READER_H

#include <stdint.h>

typedef struct {
    const int16_t *pcm;
    uint32_t frames;
    uint32_t index;
    uint16_t frac;
    uint32_t step_i;
    uint16_t step_f;
    int reverse;
    int looping;
    uint32_t loop_start;
    uint32_t loop_end; /* exclusive */
    int active;
    int cycle; /* table wraps every `frames` points; step is in table points */
} FzReader;

/* step = (native_rate / engine_rate) * 2^((note-root+cents/100)/12), Q16. */
int fz_reader_start_sample(FzReader *r, const int16_t *pcm, uint32_t frames,
                           uint32_t native_rate, int root, int note, int cents);
/* step_points = table_points * freq(note,cents) / engine_rate. */
int fz_reader_start_cycle(FzReader *r, const int16_t *table, uint32_t points,
                          int note, int cents);
void fz_reader_set_loop(FzReader *r, uint32_t start, uint32_t end_exclusive);
void fz_reader_set_reverse(FzReader *r, int reverse);
int16_t fz_reader_next(FzReader *r);

/* Control-rate pitch ratio in Q16 (16 integer bits + 16 fraction). */
uint32_t fz_pitch_step_q16(uint32_t native_rate, uint32_t engine_rate, int root,
                           int note, int cents);
uint32_t fz_cycle_step_q16(uint32_t points, uint32_t engine_rate, int note, int cents);

#endif
