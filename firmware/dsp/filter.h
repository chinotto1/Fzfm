/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R22: FZ INSPIRED baseline. A trapezoidal state-variable lowpass at 36 kHz.
 * This is not a measurement of an FZ-1 or an MB87186. Cutoff and resonance
 * are clamped so the recursion stays bounded. */
#ifndef FZ_FILTER_H
#define FZ_FILTER_H

typedef struct {
    float ic1, ic2;
    float a1, a2, a3;
    int cutoff;
    int resonance;
} FzSvFilter;

int fz_cutoff_hz(int cutoff);
float fz_resonance_q(int resonance);
void fz_svf_set(FzSvFilter *f, int cutoff, int resonance);
float fz_svf_low(FzSvFilter *f, float x);

#endif
