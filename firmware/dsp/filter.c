/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "filter.h"

#include "core/types.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int fz_cutoff_hz(int cutoff)
{
    double t;
    if (cutoff < 0)
        cutoff = 0;
    if (cutoff > 127)
        cutoff = 127;
    t = (double)cutoff / 127.0;
    return (int)(20.0 * pow(2.0, t * log2(14000.0 / 20.0)) + 0.5);
}

float fz_resonance_q(int resonance)
{
    if (resonance < 0)
        resonance = 0;
    if (resonance > 127)
        resonance = 127;
    return 0.5f + (float)resonance * (7.5f / 127.0f);
}

void fz_svf_set(FzSvFilter *f, int cutoff, int resonance)
{
    float hz, q, g, k, a1;
    if (!f)
        return;
    if (cutoff < 0)
        cutoff = 0;
    if (cutoff > 127)
        cutoff = 127;
    if (resonance < 0)
        resonance = 0;
    if (resonance > 127)
        resonance = 127;
    f->cutoff = cutoff;
    f->resonance = resonance;
    hz = (float)fz_cutoff_hz(cutoff);
    q = fz_resonance_q(resonance);
    g = tanf((float)M_PI * hz / (float)FZ_ENGINE_RATE_HZ);
    k = 1.0f / q;
    a1 = 1.0f / (1.0f + g * (g + k));
    f->a1 = a1;
    f->a2 = g * a1;
    f->a3 = g * f->a2;
}

static float clamp_state(float x)
{
    if (x > 4.0f)
        return 4.0f;
    if (x < -4.0f)
        return -4.0f;
    if (x > -1.0e-15f && x < 1.0e-15f)
        return 0.0f;
    return x;
}

float fz_svf_low(FzSvFilter *f, float x)
{
    float v3, v1, v2;
    if (!f)
        return x;
    if (x > 4.0f)
        x = 4.0f;
    if (x < -4.0f)
        x = -4.0f;
    v3 = x - f->ic2;
    v1 = f->a1 * f->ic1 + f->a2 * v3;
    v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = clamp_state(2.0f * v1 - f->ic1);
    f->ic2 = clamp_state(2.0f * v2 - f->ic2);
    if (!isfinite(v2)) {
        f->ic1 = 0.0f;
        f->ic2 = 0.0f;
        return 0.0f;
    }
    return clamp_state(v2);
}
