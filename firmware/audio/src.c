/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "src.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <string.h>

static int16_t sat_q15(int64_t acc)
{
    int64_t y = acc >> 15;
    if (y > 32767)
        return 32767;
    if (y < -32768)
        return -32768;
    return (int16_t)y;
}

/* The 64-bit accumulation stays in this helper so a target build can keep
 * the wide multiply away from 16-bit sample loads (pi32v2, CPU.md). */
__attribute__((noinline)) static int64_t fz_src_mac(int64_t acc, int16_t sample, int16_t coeff)
{
    return acc + (int32_t)sample * (int32_t)coeff;
}

static void build_kernel(FzSrc *s)
{
    const int half = FZ_SRC_TAPS / 2;
    double cutoff = 0.90;
    int p, i;

    if (s->out_rate < s->in_rate)
        cutoff *= (double)s->out_rate / (double)s->in_rate;

    for (p = 0; p < FZ_SRC_PHASES; p++) {
        double f = (double)p / (double)FZ_SRC_PHASES;
        double raw[FZ_SRC_TAPS];
        double sum = 0.0;
        int isum = 0;

        for (i = 0; i < FZ_SRC_TAPS; i++) {
            int k = i - (half - 1);
            double x = (double)k - f;
            double sx = cutoff * x;
            double sinc;
            double w;

            if (fabs(x) >= (double)half) {
                raw[i] = 0.0;
                continue;
            }
            sinc = fabs(sx) < 1e-12 ? 1.0 : sin(M_PI * sx) / (M_PI * sx);
            w = 0.5 * (1.0 + cos(M_PI * x / (double)half));
            raw[i] = sinc * w;
            sum += raw[i];
        }
        if (sum == 0.0)
            sum = 1.0;
        for (i = 0; i < FZ_SRC_TAPS; i++) {
            s->coeff[p][i] = (int16_t)lround(raw[i] / sum * 32767.0);
            isum += s->coeff[p][i];
        }
        s->coeff[p][half - 1] = (int16_t)(s->coeff[p][half - 1] + (32767 - isum));
    }
}

int fz_src_init(FzSrc *s, uint32_t in_rate, uint32_t out_rate, int channels)
{
    if (!s || in_rate == 0 || out_rate == 0 || (channels != 1 && channels != 2))
        return -1;
    memset(s, 0, sizeof *s);
    s->in_rate = in_rate;
    s->out_rate = out_rate;
    s->channels = channels;
    s->bypass = in_rate == out_rate;
    s->latency_in_frames = s->bypass ? 0u : (uint32_t)(FZ_SRC_TAPS / 2);
    if (!s->bypass)
        build_kernel(s);
    return 0;
}

void fz_src_reset(FzSrc *s)
{
    uint32_t in_rate = s->in_rate;
    uint32_t out_rate = s->out_rate;
    int channels = s->channels;
    fz_src_init(s, in_rate, out_rate, channels);
}

uint32_t fz_src_write(FzSrc *s, const int16_t *interleaved, uint32_t frames)
{
    uint32_t accepted = 0;
    uint32_t i, ch;

    if (!s || !interleaved)
        return 0;
    for (i = 0; i < frames; i++) {
        uint32_t slot;
        if (s->q_n >= FZ_SRC_QUEUE) {
            s->overrun_frames += frames - i;
            break;
        }
        slot = (s->q_r + s->q_n) % FZ_SRC_QUEUE;
        for (ch = 0; ch < (uint32_t)s->channels; ch++)
            s->queue[ch][slot] = interleaved[i * (uint32_t)s->channels + ch];
        s->q_n++;
        accepted++;
    }
    return accepted;
}

static int pull_input(FzSrc *s)
{
    uint32_t ch;
    if (s->q_n == 0)
        return 0;
    for (ch = 0; ch < (uint32_t)s->channels; ch++) {
        int16_t sample = s->queue[ch][s->q_r];
        memmove(&s->hist[ch][0], &s->hist[ch][1], (FZ_SRC_TAPS - 1) * sizeof(int16_t));
        s->hist[ch][FZ_SRC_TAPS - 1] = sample;
    }
    s->q_r = (s->q_r + 1u) % FZ_SRC_QUEUE;
    s->q_n--;
    s->consumed++;
    return 1;
}

static void emit(FzSrc *s, int16_t *dst, uint32_t n, uint32_t phase)
{
    uint32_t ch, tap;
    for (ch = 0; ch < (uint32_t)s->channels; ch++) {
        int64_t acc = 0;
        for (tap = 0; tap < FZ_SRC_TAPS; tap++)
            acc = fz_src_mac(acc, s->hist[ch][tap], s->coeff[phase][tap]);
        dst[ch] = sat_q15(acc);
    }
    (void)n;
}

uint32_t fz_src_read(FzSrc *s, int16_t *interleaved, uint32_t max_frames)
{
    uint32_t produced = 0;

    if (!s || !interleaved)
        return 0;
    if (s->bypass) {
        while (produced < max_frames && s->q_n > 0) {
            uint32_t ch;
            for (ch = 0; ch < (uint32_t)s->channels; ch++)
                interleaved[produced * (uint32_t)s->channels + ch] = s->queue[ch][s->q_r];
            s->q_r = (s->q_r + 1u) % FZ_SRC_QUEUE;
            s->q_n--;
            produced++;
        }
        if (produced < max_frames)
            s->underrun_events++;
        return produced;
    }

    while (produced < max_frames) {
        uint64_t num = s->out_i * (uint64_t)s->in_rate;
        uint32_t n = (uint32_t)(num / s->out_rate);
        uint32_t rem = (uint32_t)(num % s->out_rate);
        uint32_t phase = (uint32_t)(((uint64_t)rem * FZ_SRC_PHASES) / s->out_rate);
        uint32_t need = n + (FZ_SRC_TAPS / 2) + 1u;

        while (s->consumed < need) {
            if (!pull_input(s))
                goto starved;
        }
        emit(s, interleaved + produced * (uint32_t)s->channels, n, phase);
        s->out_i++;
        produced++;
    }
    return produced;

starved:
    if (produced < max_frames)
        s->underrun_events++;
    return produced;
}
