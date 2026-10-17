/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R09, R32: tempo candidates from a closed free take. BPM = 60 * quarters / seconds.
 * This does not replace the project's tempo when music or an external clock
 * is already in charge. */
#ifndef FZ_TEMPO_H
#define FZ_TEMPO_H

#include <stdint.h>

typedef struct {
    double bpm;
    int bars;
    int quarters;
    int confidence; /* 0..100; 0 means no claim */
} FzTempoCandidate;

int fz_tempo_propose(uint32_t duration_frames, uint32_t engine_rate, const uint32_t *onset_frames, int n_onsets,
                     FzTempoCandidate *out, int max_out);

/* Returns 1 if the project tempo was replaced by `inferred`. */
int fz_tempo_apply_policy(int project_has_music, int external_clock, double *project_bpm, double inferred);

#endif
