/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fm_dsp.h"
#include <math.h>
#include <string.h>
#define FM_PI 3.14159265358979323846f
static float limit(float x, float lo, float hi)
{
    return fminf(hi, fmaxf(lo, x));
}
static void stage_begin(FmEnvState *state, unsigned index)
{
    if (index >= state->shape.count)
    {
        state->finished = true;
        state->level = 0;
        return;
    }
    state->stage = (uint8_t)index;
    state->target = limit(state->shape.stage[index].level, 0, 1);
    float duration = limit(state->shape.stage[index].milliseconds, 0, 3600000) * state->rate_scale;
    state->remaining = (uint32_t)fminf(duration * (float)FM_RATE / 1000.0f, 129600000.0f);
    state->increment =
        state->remaining ? (state->target - state->level) / (float)state->remaining : 0;
}
void fm_env_start(FmEnvState *state, const FmEnvelope *shape, uint8_t note, uint8_t velocity)
{
    memset(state, 0, sizeof(*state));
    state->shape = *shape;
    if (state->shape.count > FM_ENV_STAGES)
        state->shape.count = FM_ENV_STAGES;
    state->rate_scale = exp2f(-limit(shape->key_rate, -4, 4) * ((float)note - 60.0f) / 12.0f);
    /* Velocity affects level, not timing. Applied after stage evaluation. */
    (void)velocity;
    stage_begin(state, 0);
}
void fm_env_release(FmEnvState *state)
{
    if (state->released || state->finished)
        return;
    state->released = true;
    state->holding = false;
    unsigned release = state->shape.release;
    if (release >= state->shape.count)
        release = state->shape.count ? state->shape.count - 1u : 0;
    stage_begin(state, release);
}
float fm_env_tick(FmEnvState *state)
{
    if (state->finished)
        return 0;
    if (state->holding)
        return state->level;
    /* At most eight zero-time transitions: malformed shape cannot spin. */
    for (unsigned n = 0; n <= FM_ENV_STAGES; n++)
    {
        if (state->remaining)
        {
            state->level += state->increment;
            if (--state->remaining)
                return state->level;
            state->level = state->target;
        }
        else
            state->level = state->target;
        if (!state->released && state->stage == state->shape.sustain)
        {
            state->holding = true;
            return state->level;
        }
        unsigned next = (unsigned)state->stage + 1u;
        if (!state->released && next == state->shape.release)
        {
            state->holding = true;
            return state->level;
        }
        stage_begin(state, next);
        if (state->finished)
            return 0;
        if (state->remaining)
            return state->level;
    }
    state->finished = true;
    state->level = 0;
    return 0;
}
float fm_filter_tick(FmFilter *filter, float input, float frequency, float resonance, float drive)
{
    float f = limit(frequency, 25, 15000);
    if (filter->cutoff == 0)
        filter->cutoff = f;
    filter->cutoff += 0.015f * (f - filter->cutoff);
    if (!filter->divider--)
    {
        filter->divider = 7;
        filter->g = tanf(FM_PI * filter->cutoff / (float)FM_RATE);
        filter->damping = 2.0f - 1.92f * limit(resonance, 0, 1);
    }
    float amount = 1.0f + 5.0f * limit(drive, 0, 1);
    float x = tanhf(input * amount) / sqrtf(amount);
    float g = filter->g, k = filter->damping;
    float band = (filter->band + g * (x - filter->low)) / (1.0f + g * (g + k));
    float low = filter->low + g * band;
    filter->band = 2 * band - filter->band;
    filter->low = 2 * low - filter->low;
    /* State bounds prevent runaway under corrupted upstream samples. These
     * are protection limits, not a claim of MB87186 circuit saturation. */
    filter->band = limit(filter->band, -16, 16);
    filter->low = limit(filter->low, -16, 16);
    if (fabsf(filter->band) < 1e-20f)
        filter->band = 0;
    if (fabsf(filter->low) < 1e-20f)
        filter->low = 0;
    return low;
}
float fm_lfo_tick(float *phase, float rate, uint8_t shape)
{
    *phase += limit(rate, 0, 100) / (float)FM_RATE;
    if (*phase >= 1)
        *phase -= 1;
    switch (shape)
    {
    case 1:
        return 1 - 4 * fabsf(*phase - 0.5f);
    case 2:
        return *phase < 0.5f ? 1 : -1;
    case 3:
        return 2 * (*phase) - 1;
    default:
        return sinf(2 * FM_PI * (*phase));
    }
}
