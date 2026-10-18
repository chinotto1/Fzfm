/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "engine.h"

#include <string.h>

void fz_engine_init(FzEngine *e)
{
    memset(e, 0, sizeof *e);
}

static int find_free(FzEngine *e)
{
    int i;
    for (i = 0; i < FZ_MAX_VOICES; i++) {
        if (!e->voice[i].alive)
            return i;
    }
    return -1;
}

static int oldest(FzEngine *e)
{
    int i, best = 0;
    uint64_t best_age = UINT64_MAX;
    for (i = 0; i < FZ_MAX_VOICES; i++) {
        if (e->voice[i].age < best_age) {
            best_age = e->voice[i].age;
            best = i;
        }
    }
    return best;
}

int fz_engine_note_on_pcm(FzEngine *e, const int16_t *pcm, uint32_t frames, uint32_t native_rate,
                          int root, int note, int cents, int velocity, int volume, int pan,
                          int one_shot, int cycle, uint16_t asset_tag)
{
    int slot;
    FzVoice *v;
    FzReader reader;
    int rc;

    if (!e || !pcm || frames == 0)
        return -1;
    if (velocity < 0)
        velocity = 0;
    if (velocity > 127)
        velocity = 127;
    if (volume < 0)
        volume = 0;
    if (volume > 127)
        volume = 127;
    if (pan < 0)
        pan = 0;
    if (pan > 127)
        pan = 127;

    /* Reject an invalid reader before stealing an already sounding voice. */
    if (cycle)
        rc = fz_reader_start_cycle(&reader, pcm, frames, note, cents);
    else
        rc = fz_reader_start_sample(&reader, pcm, frames, native_rate, root, note, cents);
    if (rc != 0)
        return -1;

    slot = find_free(e);
    if (slot < 0) {
        slot = oldest(e);
        e->stolen++;
    }
    v = &e->voice[slot];
    memset(v, 0, sizeof *v);
    v->reader = reader;
    v->alive = 1;
    v->gated = one_shot ? 0 : 1;
    v->note = note;
    v->velocity = velocity;
    v->volume = volume;
    v->pan = pan;
    v->age = ++e->age_clock;
    v->asset_tag = asset_tag;
    v->pcm = pcm;
    return slot;
}

void fz_engine_note_off(FzEngine *e, int note)
{
    int i;
    if (!e)
        return;
    for (i = FZ_MAX_VOICES - 1; i >= 0; i--) {
        if (e->voice[i].alive && e->voice[i].gated && e->voice[i].note == note) {
            e->voice[i].alive = 0;
            e->voice[i].reader.active = 0;
            return;
        }
    }
}

int fz_engine_asset_busy(const FzEngine *e, uint16_t asset_tag)
{
    int i;
    if (!e)
        return 0;
    for (i = 0; i < FZ_MAX_VOICES; i++) {
        if (e->voice[i].alive && e->voice[i].asset_tag == asset_tag)
            return 1;
    }
    return 0;
}

void fz_engine_render(FzEngine *e, int16_t *interleaved_lr, uint32_t frames)
{
    uint32_t n;
    int i;

    if (!e || !interleaved_lr)
        return;
    for (n = 0; n < frames; n++) {
        int32_t left = 0;
        int32_t right = 0;
        int alive = 0;
        for (i = 0; i < FZ_MAX_VOICES; i++) {
            FzVoice *v = &e->voice[i];
            int32_t y, l, r;
            int16_t s;
            if (!v->alive)
                continue;
            if (v->project) {
                FzTrack *track = fz_track(v->project, v->track);
                if (!track) {
                    v->alive = 0;
                    v->reader.active = 0;
                    continue;
                }
                v->volume = track->volume;
                v->pan = track->pan;
            }
            s = fz_reader_next(&v->reader);
            if (!v->reader.active && !v->reader.cycle) {
                v->alive = 0;
            }
            y = ((int32_t)s * v->volume * v->velocity) / (127 * 127);
            l = (y * (128 - v->pan)) / 128;
            r = (y * v->pan) / 128;
            left += l;
            right += r;
            if (v->alive)
                alive++;
        }
        interleaved_lr[n * 2u] = fz_sat16(left);
        interleaved_lr[n * 2u + 1u] = fz_sat16(right);
        e->active_notes = alive;
    }
    e->rendered_frames += frames;
}
