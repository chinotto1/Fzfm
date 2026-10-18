/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FM_DSP_H
#define FM_DSP_H
#include "../core/fm.h"
typedef struct
{
    float low, band, cutoff, g, damping;
    unsigned divider;
} FmFilter;
typedef struct
{
    float level, target, increment;
    uint32_t remaining;
    uint8_t stage;
    bool released, finished, holding;
    FmEnvelope shape;
    float rate_scale;
} FmEnvState;
void fm_env_start(FmEnvState *, const FmEnvelope *, uint8_t note, uint8_t velocity);
void fm_env_release(FmEnvState *);
float fm_env_tick(FmEnvState *);
float fm_filter_tick(FmFilter *, float input, float frequency, float resonance, float drive);
float fm_lfo_tick(float *phase, float rate, uint8_t shape);
#endif
