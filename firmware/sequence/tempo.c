/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "tempo.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const int k_bars[] = {1, 2, 3, 4, 6, 8};

static int cmp_conf(const void *a, const void *b)
{
    const FzTempoCandidate *x = a;
    const FzTempoCandidate *y = b;
    if (x->confidence != y->confidence)
        return y->confidence - x->confidence;
    return x->bars - y->bars;
}

int fz_tempo_propose(uint32_t duration_frames, uint32_t engine_rate, const uint32_t *onset_frames, int n_onsets,
                     FzTempoCandidate *out, int max_out)
{
    double seconds;
    int i, n = 0, valid_onsets = 0;
    FzTempoCandidate candidates[sizeof k_bars / sizeof k_bars[0]];
    if (!out || max_out <= 0 || engine_rate == 0 || duration_frames == 0)
        return 0;
    seconds = (double)duration_frames / (double)engine_rate;
    if (n_onsets <= 0 || !onset_frames) {
        out[0].bpm = 0;
        out[0].bars = 0;
        out[0].quarters = 0;
        out[0].confidence = 0;
        return 1;
    }
    for (i = 0; i < n_onsets; i++)
        if (onset_frames[i] <= duration_frames)
            valid_onsets++;
    if (!valid_onsets) {
        memset(&out[0], 0, sizeof out[0]);
        return 1;
    }
    /* Rank the complete bounded set before truncating to caller capacity. */
    for (i = 0; i < (int)(sizeof k_bars / sizeof k_bars[0]); i++) {
        int bars = k_bars[i];
        int quarters = bars * 4;
        double bpm = 60.0 * (double)quarters / seconds;
        double quarter = seconds / (double)quarters;
        double err = 0.0;
        int k;
        int confidence;
        if (bpm < 40.0 || bpm > 280.0)
            continue;
        for (k = 0; k < n_onsets; k++) {
            double t = (double)onset_frames[k] / (double)engine_rate;
            double pos, nearest, dist;
            if (t < 0.0 || t > seconds)
                continue;
            pos = t / quarter;
            nearest = floor(pos + 0.5);
            dist = fabs(pos - nearest);
            if (dist > 0.5)
                dist = 0.5;
            err += dist;
        }
        err /= (double)valid_onsets;
        confidence = (int)(100.0 * (1.0 - err * 2.0));
        if (confidence < 0)
            confidence = 0;
        if (bpm < 70.0 || bpm > 160.0)
            confidence = confidence * 3 / 4;
        if (valid_onsets < 4 && confidence > valid_onsets * 25)
            confidence = valid_onsets * 25;
        candidates[n].bpm = bpm;
        candidates[n].bars = bars;
        candidates[n].quarters = quarters;
        candidates[n].confidence = confidence;
        n++;
    }
    qsort(candidates, (size_t)n, sizeof candidates[0], cmp_conf);
    if (n > max_out)
        n = max_out;
    memcpy(out, candidates, (size_t)n * sizeof out[0]);
    return n;
}

int fz_tempo_apply_policy(int project_has_music, int external_clock, double *project_bpm, double inferred)
{
    if (!project_bpm || !isfinite(inferred) || inferred <= 0.0)
        return 0;
    if (project_has_music || external_clock)
        return 0;
    *project_bpm = inferred;
    return 1;
}
