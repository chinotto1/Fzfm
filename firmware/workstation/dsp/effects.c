/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../audio/fm_audio.h"
#include <math.h>
#include <string.h>
static float bound(float x, float lo, float hi)
{
    return fminf(hi, fmaxf(lo, x));
}
#ifndef FM_DELAY_FRAMES
#define FM_DELAY_FRAMES 36002u
#endif
float fm_fx_max_delay(void)
{
    return (float)(FM_DELAY_FRAMES - 2u) / FM_RATE;
}
size_t fm_fx_memory(FmFxType type)
{
    switch (type)
    {
    case FM_FX_DELAY:
        return FM_DELAY_FRAMES * 2u * sizeof(float);
    case FM_FX_REVERB:
        return 2048u * 4u * sizeof(float);
    case FM_FX_CHORUS:
        return 2048u * 2u * sizeof(float);
    case FM_FX_CHARACTER:
        return 0;
    default:
        return SIZE_MAX;
    }
}
static FmResult prepare(FmEngine *engine, const FmFxState *current, const FmFxConfig *config,
                        FmFxState *replacement)
{
    if (!isfinite(config->gain) || !isfinite(config->pan))
        return FM_ERR_ARGUMENT;
    for (unsigned i = 0; i < 4; i++)
        if (!isfinite(config->parameter[i]))
            return FM_ERR_ARGUMENT;
    if (config->type == FM_FX_DELAY && config->parameter[0] > fm_fx_max_delay())
        return FM_ERR_RANGE;
    size_t bytes = fm_fx_memory(config->type);
    if (bytes == SIZE_MAX)
        return FM_ERR_ARGUMENT;
    if (current->config.type == config->type && current->bytes == bytes &&
        (!bytes || current->buffer))
    {
        *replacement = *current;
        replacement->config = *config;
        return FM_OK; /* bypass/re-enable retains the existing tail and buffer. */
    }
    memset(replacement, 0, sizeof(*replacement));
    replacement->config = *config;
    replacement->bytes = bytes;
    memcpy(replacement->previous, current->last, sizeof(current->last));
    replacement->transition = current->energy > 1e-10f ? 64 : 0;
    if (bytes)
    {
        replacement->buffer = fm_alloc(engine->project, FM_MEM_FX, bytes);
        if (!replacement->buffer)
            return FM_ERR_MEMORY;
        replacement->frames = bytes / (2 * sizeof(float));
    }
    return FM_OK;
}
static void publish(FmEngine *engine, unsigned slot, const FmFxState *replacement)
{
    FmFxState *old = &engine->fx[slot];
    if (old->buffer && old->buffer != replacement->buffer)
        fm_dealloc(engine->project, FM_MEM_FX, old->buffer, old->bytes);
    *old = *replacement;
}
FmResult fm_engine_fx_prepare(FmEngine *engine, unsigned slot, const FmFxConfig *config)
{
    if (!engine || !engine->project || !config || slot >= FM_FX_SLOTS)
        return FM_ERR_ARGUMENT;
    FmFxState next;
    FmResult r = prepare(engine, &engine->fx[slot], config, &next);
    if (r == FM_OK)
        publish(engine, slot, &next);
    return r;
}
FmResult fm_engine_fx_sync(FmEngine *engine, uint8_t mask)
{
    if (!engine || !engine->project || mask > 15)
        return FM_ERR_ARGUMENT;
    FmFxState next[FM_FX_SLOTS];
    uint8_t ready = 0;
    for (unsigned i = 0; i < 4; i++)
        if (mask & (1u << i))
        {
            FmResult r = prepare(engine, &engine->fx[i], &engine->project->fx[i], &next[i]);
            if (r != FM_OK)
            {
                for (unsigned j = 0; j < i; j++)
                    if ((ready & (1u << j)) && next[j].buffer != engine->fx[j].buffer &&
                        next[j].buffer)
                        fm_dealloc(engine->project, FM_MEM_FX, next[j].buffer, next[j].bytes);
                return r;
            }
            ready |= 1u << i;
        }
    for (unsigned i = 0; i < 4; i++)
        if (ready & (1u << i))
            publish(engine, i, &next[i]);
    return FM_OK;
}
FmResult fm_engine_fx_release(FmEngine *engine, unsigned slot, bool discard_tail)
{
    if (!engine || slot >= FM_FX_SLOTS)
        return FM_ERR_ARGUMENT;
    FmFxState *fx = &engine->fx[slot];
    if (!discard_tail && fx->energy > 1e-10f)
        return FM_ERR_BUSY;
    if (fx->buffer)
        fm_dealloc(engine->project, FM_MEM_FX, fx->buffer, fx->bytes);
    memset(fx, 0, sizeof(*fx));
    return FM_OK;
}
static float tap(const FmFxState *fx, double delay, unsigned channel)
{
    double pos = (double)fx->position - delay;
    if (pos < 0)
        pos += (double)fx->frames;
    size_t a = (size_t)pos % fx->frames, b = (a + 1u) % fx->frames;
    float t = (float)(pos - floor(pos));
    return fx->buffer[2u * a + channel] * (1 - t) + fx->buffer[2u * b + channel] * t;
}
/* All algorithms output wet only. Mono FDN reverb creates a stereo return;
 * other algorithms retain stereo input. No effect-to-effect feedback. */
void fm_fx_tick(FmFxState *fx, float left, float right, float *out_left, float *out_right)
{
    float l = 0, r = 0;
    const float *p = fx->config.parameter;
    if (!fx->config.enabled)
        left = right = 0; /* Existing tails still run. */
    switch (fx->config.type)
    {
    case FM_FX_DELAY:
        if (!fx->buffer)
            break;
        if (fx->state[0] == 0)
            fx->state[0] = bound(p[0], 0.001f, fm_fx_max_delay()) * (float)FM_RATE;
        fx->state[0] +=
            0.001f * (bound(p[0], 0.001f, fm_fx_max_delay()) * (float)FM_RATE - fx->state[0]);
        l = tap(fx, fx->state[0], 0);
        r = tap(fx, fx->state[0], 1);
        fx->state[1] += (0.02f + 0.96f * (1 - bound(p[2], 0, 1))) * (l - fx->state[1]);
        fx->state[2] += (0.02f + 0.96f * (1 - bound(p[2], 0, 1))) * (r - fx->state[2]);
        fx->buffer[2u * fx->position] = left + bound(p[1], 0, 0.95f) * fx->state[1];
        fx->buffer[2u * fx->position + 1u] = right + bound(p[1], 0, 0.95f) * fx->state[2];
        fx->position = (fx->position + 1u) % fx->frames;
        break;
    case FM_FX_REVERB:
    {
        if (!fx->buffer)
            break;
        const unsigned base[4] = {1493, 1601, 1747, 1867};
        float d[4], v[4];
        unsigned loc[4];
        for (unsigned i = 0; i < 4; i++)
        {
            unsigned length = (unsigned)((float)base[i] * (0.3f + 0.7f * bound(p[0], 0, 1)));
            loc[i] = (unsigned)(fx->position % length) + i * 2048u;
            d[i] = fx->buffer[loc[i]];
            fx->state[i] += (0.03f + 0.95f * (1 - bound(p[2], 0, 1))) * (d[i] - fx->state[i]);
            d[i] = fx->state[i];
        }
        v[0] = (d[0] + d[1] + d[2] + d[3]) * 0.5f;
        v[1] = (d[0] - d[1] + d[2] - d[3]) * 0.5f;
        v[2] = (d[0] + d[1] - d[2] - d[3]) * 0.5f;
        v[3] = (d[0] - d[1] - d[2] + d[3]) * 0.5f;
        for (unsigned i = 0; i < 4; i++)
            fx->buffer[loc[i]] = (left + right) * 0.25f + (0.2f + 0.75f * bound(p[1], 0, 1)) * v[i];
        l = (d[0] + d[1] - d[2] - d[3]) * 0.5f;
        r = (d[0] - d[1] + d[2] - d[3]) * 0.5f;
        fx->position++;
        break;
    }
    case FM_FX_CHORUS:
    {
        if (!fx->buffer)
            break;
        fx->phase += (0.05f + 5.95f * bound(p[0], 0, 1)) / (float)FM_RATE;
        if (fx->phase >= 1)
            fx->phase -= 1;
        double depth = (double)(1 + bound(p[1], 0, 1) * 350);
        l = tap(fx, 550 + depth * sin(6.283185307179586 * (double)fx->phase), 0);
        r = tap(fx, 550 + depth * cos(6.283185307179586 * (double)fx->phase), 1);
        fx->buffer[2u * fx->position] = left + bound(p[2], 0, 0.7f) * l;
        fx->buffer[2u * fx->position + 1u] = right + bound(p[2], 0, 0.7f) * r;
        fx->position = (fx->position + 1u) % fx->frames;
        break;
    }
    case FM_FX_CHARACTER:
    {
        fx->phase += 1.0f / (1.0f + 31.0f * bound(p[2], 0, 1));
        if (fx->phase >= 1)
        {
            fx->phase -= 1;
            float levels = exp2f(4.0f + 11.0f * (1 - bound(p[1], 0, 1)));
            float drive = 1 + 15 * bound(p[0], 0, 1);
            fx->state[0] = roundf(tanhf(left * drive) * levels) / levels;
            fx->state[1] = roundf(tanhf(right * drive) * levels) / levels;
        }
        l = fx->state[0];
        r = fx->state[1];
        break;
    }
    default:
        break;
    }
    fx->energy = fmaxf(fx->energy * 0.9999f, l * l + r * r);
    float pan = bound(fx->config.pan, -1, 1), gain = bound(fx->config.gain, 0, 4);
    *out_left = l * gain * sqrtf((1 - pan) * 0.5f);
    *out_right = r * gain * sqrtf((1 + pan) * 0.5f);
    if (fx->transition)
    {
        float amount = (float)fx->transition / 64.0f;
        *out_left = *out_left * (1 - amount) + fx->previous[0] * amount;
        *out_right = *out_right * (1 - amount) + fx->previous[1] * amount;
        fx->transition--;
    }
    if (fx->config.return_muted)
        *out_left = *out_right = 0;
    fx->last[0] = *out_left;
    fx->last[1] = *out_right;
}

static bool slot_has_future_input(FmEngine *engine, unsigned slot)
{
    FmProject *project = engine->project;
    for (unsigned t = 0; t < FM_OBJECT_LIMIT; t++)
    {
        FmTrack *track = project->tracks[t];
        if (!track)
            continue;
        bool send = track->sends[slot] > 0;
        /* Every stored pattern is conservatively reachable, including a queued
         * scene/chain. Muted tracks can be unmuted; do not use mute as proof. */
        for (unsigned p = 0; p < FM_OBJECT_LIMIT; p++)
        {
            FmPattern *pattern = project->patterns[p];
            if (!pattern || pattern->track != track->id || pattern->frozen_asset)
                continue;
            for (uint32_t e = 0; e < pattern->count; e++)
            {
                const FmEvent *event = &pattern->events[e];
                if (event->kind == FM_EVENT_CONTROL && event->parameter == FM_PARAM_SEND1 + slot &&
                    event->value > 0)
                    send = true;
            }
        }
        for (unsigned e = 0; e < engine->event_count; e++)
        {
            const FmEvent *event = &engine->events[e];
            if (event->track == track->id && event->kind == FM_EVENT_CONTROL &&
                event->parameter == FM_PARAM_SEND1 + slot && event->value > 0)
                send = true;
        }
        if (!send)
            continue;
        FmInstrument *instrument = fm_instrument(project, track->instrument);
        if (instrument)
            for (unsigned r = 0; r < FM_ANCHORS; r++)
            {
                const FmRegion *region = &instrument->regions[r];
                if (region->id && region->asset && !(region->baked_mask & (0x80u | (1u << slot))) &&
                    region->sends[slot] > 0)
                    return true;
            }
    }
    /* A note-on snapshot may retain routing that an edit already changed. */
    for (unsigned v = 0; v < engine->max_voices; v++)
    {
        const FmVoice *voice = &engine->voices[v];
        if (!voice->active || voice->frozen ||
            (voice->region.baked_mask & (0x80u | (1u << slot))) || voice->region.sends[slot] <= 0)
            continue;
        if (voice->track->sends[slot] > 0)
            return true;
        for (unsigned t = 0; t < FM_OBJECT_LIMIT; t++)
            if (engine->tracks[t].track == voice->track->id &&
                engine->tracks[t].sends[slot] > 1e-8f)
                return true;
    }
    return false;
}
FmResult fm_engine_fx_reclaim_unused(FmEngine *engine, uint8_t requested, uint8_t *released)
{
    if (!engine || !engine->project || !released || requested > 15)
        return FM_ERR_ARGUMENT;
    *released = 0;
    FmResult result = FM_OK;
    for (unsigned slot = 0; slot < FM_FX_SLOTS; slot++)
        if (requested & (1u << slot))
        {
            if (slot_has_future_input(engine, slot) || engine->fx[slot].energy > 1e-10f ||
                engine->fx[slot].transition)
            {
                result = FM_ERR_BUSY;
                continue;
            }
            FmResult status = fm_engine_fx_release(engine, slot, false);
            if (status == FM_OK)
                *released |= (uint8_t)(1u << slot);
            else
                result = status;
        }
    return result;
}
