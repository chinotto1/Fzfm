/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "envelope.h"

#include <string.h>

static void aim(FzEnvRun *run)
{
    const FzEnvelope *env = &run->prog;
    int32_t target;
    uint32_t frames;
    if (run->stage < 0 || run->stage >= env->n_stages) {
        run->active = 0;
        run->level = 0;
        return;
    }
    target = env->level[run->stage];
    if (target < 0)
        target = 0;
    if (target > 32767)
        target = 32767;
    frames = env->frames[run->stage];
    if (frames == 0)
        frames = 1;
    run->target = target;
    run->left = frames;
    run->step = (target - run->level) / (int32_t)frames;
    if (run->step == 0 && target != run->level)
        run->step = target > run->level ? 1 : -1;
}

void fz_env_adsr(FzEnvelope *env, uint32_t attack, uint32_t decay, int16_t sustain, uint32_t release)
{
    memset(env, 0, sizeof *env);
    env->n_stages = 4;
    env->sustain = 2;
    env->level[0] = 32767;
    env->frames[0] = attack ? attack : 1;
    env->level[1] = sustain < 0 ? 0 : sustain;
    env->frames[1] = decay ? decay : 1;
    env->level[2] = env->level[1];
    env->frames[2] = 1;
    env->level[3] = 0;
    env->frames[3] = release ? release : 1;
}

void fz_env_scale_time(FzEnvelope *env, int note, int center, int amount)
{
    int i, semis, den;
    if (!env || amount <= 0)
        return;
    if (amount > 127)
        amount = 127;
    semis = note - center;
    if (semis < -48)
        semis = -48;
    if (semis > 48)
        semis = 48;
    den = 128 + (semis * amount) / 8;
    if (den < 32)
        den = 32;
    for (i = 0; i < env->n_stages && i < FZ_ENV_STAGES; i++) {
        uint32_t frames = env->frames[i];
        if (env->sustain == i)
            continue;
        frames = (uint32_t)(((uint64_t)frames * 128u) / (uint32_t)den);
        env->frames[i] = frames ? frames : 1u;
    }
}

void fz_env_start(FzEnvRun *run, const FzEnvelope *env)
{
    memset(run, 0, sizeof *run);
    if (!env || env->n_stages <= 0) {
        return;
    }
    run->prog = *env;
    if (run->prog.n_stages > FZ_ENV_STAGES)
        run->prog.n_stages = FZ_ENV_STAGES;
    run->active = 1;
    run->stage = 0;
    run->level = 0;
    aim(run);
}

void fz_env_release(FzEnvRun *run)
{
    if (!run || !run->active || run->released)
        return;
    run->released = 1;
    if (run->prog.sustain >= 0 && run->prog.sustain + 1 < run->prog.n_stages)
        run->stage = run->prog.sustain + 1;
    else
        run->stage = run->prog.n_stages - 1;
    aim(run);
}

int32_t fz_env_next(FzEnvRun *run)
{
    int32_t y;
    /* A run that was never started stays at unity. A finished release holds
     * its last level, which is silence, instead of jumping back to unity. */
    if (!run || !run->active)
        return run && run->prog.n_stages > 0 ? run->level : 32767;
    y = run->level;
    if (run->left > 0) {
        run->level += run->step;
        run->left--;
        if (run->left == 0 || (run->step > 0 && run->level >= run->target) ||
            (run->step < 0 && run->level <= run->target)) {
            run->level = run->target;
            run->left = 0;
            if (!(run->prog.sustain == run->stage && !run->released)) {
                run->stage++;
                if (run->stage >= run->prog.n_stages) {
                    run->active = run->level > 0;
                } else {
                    aim(run);
                }
            }
        }
    }
    if (y < 0)
        return 0;
    if (y > 32767)
        return 32767;
    return y;
}
