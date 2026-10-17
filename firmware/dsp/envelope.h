/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R18: up to eight rate/level stages. ADSR is a preset that fills this
 * model, not a second data structure. */
#ifndef FZ_ENVELOPE_H
#define FZ_ENVELOPE_H

#include <stdint.h>

#define FZ_ENV_STAGES 8

typedef struct {
    int n_stages;
    int sustain; /* stage index that holds, or -1 */
    int16_t level[FZ_ENV_STAGES]; /* 0..32767 */
    uint32_t frames[FZ_ENV_STAGES]; /* frames spent moving toward that level */
} FzEnvelope;

typedef struct {
    int active;
    int stage;
    int released;
    int32_t level;
    int32_t target;
    int32_t step;
    uint32_t left;
    FzEnvelope prog;
} FzEnvRun;

void fz_env_adsr(FzEnvelope *env, uint32_t attack, uint32_t decay, int16_t sustain, uint32_t release);
/* Shorten stage times as the note rises above `center`. Amount 0 leaves times alone. */
void fz_env_scale_time(FzEnvelope *env, int note, int center, int amount);
void fz_env_start(FzEnvRun *run, const FzEnvelope *env);
void fz_env_release(FzEnvRun *run);
int32_t fz_env_next(FzEnvRun *run); /* Q15 gain, 32767 = unity */

#endif
