/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "loopprog.h"

FzStatus fz_loop_validate(const FzLoopProg *prog, uint32_t frames)
{
    int i;
    if (!prog || prog->count < 1 || prog->count > FZ_LOOP_SECTIONS)
        return FZ_ERR_ARGS;
    for (i = 0; i < prog->count; i++) {
        const FzLoopSection *s = &prog->sec[i];
        uint32_t span;
        if (s->end <= s->start || s->end > frames)
            return FZ_ERR_RANGE;
        span = s->end - s->start;
        if (s->crossfade >= span)
            return FZ_ERR_RANGE;
        if (s->next != 255 && s->next >= (uint8_t)prog->count)
            return FZ_ERR_RANGE;
    }
    return FZ_OK;
}

void fz_loop_start(FzLoopRun *run)
{
    run->section = 0;
    run->pos = 0;
    run->done_repeats = 0;
    run->released = 0;
    run->active = 1;
}

int fz_loop_index(const FzLoopProg *prog, FzLoopRun *run, uint32_t frames, int released)
{
    const FzLoopSection *s;
    uint32_t span, index;
    (void)frames;
    if (!prog || !run || !run->active || run->section < 0 || run->section >= prog->count)
        return -1;
    if (released)
        run->released = 1;
    s = &prog->sec[run->section];
    span = s->end - s->start;
    if (span == 0) {
        run->active = 0;
        return -1;
    }
    index = s->start + (run->pos % span);
    run->pos++;
    if (run->pos >= span) {
        run->pos = 0;
        run->done_repeats++;
        if (s->repeats == 0) {
            if (run->released)
                run->section = s->next == 255 ? -1 : (int)s->next;
        } else if (run->done_repeats >= s->repeats) {
            run->done_repeats = 0;
            run->section = s->next == 255 ? -1 : (int)s->next;
        }
        if (run->section < 0)
            run->active = 0;
    }
    return (int)index;
}

int16_t fz_loop_sample(const FzLoopProg *prog, FzLoopRun *run, const int16_t *pcm, uint32_t frames, int released)
{
    const FzLoopSection *s;
    uint32_t span, idx, into;
    int16_t sample;
    if (!prog || !run || !pcm || !run->active || run->section < 0 || run->section >= prog->count)
        return 0;
    if (released)
        run->released = 1;
    s = &prog->sec[run->section];
    if (s->end <= s->start || s->end > frames) {
        run->active = 0;
        return 0;
    }
    span = s->end - s->start;
    idx = s->start + run->pos;
    if (idx >= frames) {
        run->active = 0;
        return 0;
    }
    sample = pcm[idx];
    into = span - run->pos;
    if (s->crossfade > 0 && into <= s->crossfade && s->next != 255 && s->next < (uint8_t)prog->count) {
        const FzLoopSection *next = &prog->sec[s->next];
        uint32_t nspan = next->end - next->start;
        uint32_t fade_i = s->crossfade - into;
        uint32_t nidx;
        if (nspan > 0 && next->end <= frames) {
            int32_t wb = (int32_t)fade_i;
            int32_t wa = (int32_t)s->crossfade - wb;
            nidx = next->start + (fade_i % nspan);
            sample = fz_sat16(((int32_t)sample * wa + (int32_t)pcm[nidx] * wb) / (int32_t)s->crossfade);
        }
    }
    run->pos++;
    if (run->pos >= span) {
        run->pos = 0;
        run->done_repeats++;
        if (s->repeats == 0) {
            if (run->released)
                run->section = s->next == 255 ? -1 : (int)s->next;
        } else if (run->done_repeats >= s->repeats) {
            run->done_repeats = 0;
            run->section = s->next == 255 ? -1 : (int)s->next;
        }
        if (run->section < 0)
            run->active = 0;
    }
    return sample;
}
