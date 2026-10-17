/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "fx.h"

#include <string.h>

void fz_fx_init(FzFxRack *rack)
{
    int i;
    if (!rack)
        return;
    memset(rack, 0, sizeof *rack);
    for (i = 0; i < FZ_FX_SLOTS; i++)
        rack->slot[i].ret_gain = 127;
}

static int reverb_count(const FzFxRack *rack, int except)
{
    int i, n = 0;
    for (i = 0; i < FZ_FX_SLOTS; i++) {
        if (i == except)
            continue;
        if (rack->slot[i].kind == FZ_FX_REVERB)
            n++;
    }
    return n;
}

FzStatus fz_fx_set(FzFxRack *rack, int slot, FzFxKind kind)
{
    FzFxSlot *s;
    if (!rack || slot < 0 || slot >= FZ_FX_SLOTS)
        return FZ_ERR_ARGS;
    if (kind == FZ_FX_REVERB && reverb_count(rack, slot) > 0)
        return FZ_ERR_STATE;
    s = &rack->slot[slot];
    memset(s->delay, 0, sizeof s->delay);
    memset(s->comb, 0, sizeof s->comb);
    memset(s->chorus, 0, sizeof s->chorus);
    s->kind = kind;
    s->enabled = kind != FZ_FX_OFF;
    s->ret_gain = 127;
    s->param_a = kind == FZ_FX_DELAY ? 1800 : kind == FZ_FX_REVERB ? 70 : kind == FZ_FX_CHORUS ? 40 : 64;
    s->param_b = kind == FZ_FX_DELAY ? 40 : kind == FZ_FX_REVERB ? 40 : 80;
    s->tail = 0;
    s->pos = 0;
    s->phase = 0;
    s->damp = 0;
    return FZ_OK;
}

void fz_fx_enable(FzFxRack *rack, int slot, int enabled)
{
    if (!rack || slot < 0 || slot >= FZ_FX_SLOTS)
        return;
    rack->slot[slot].enabled = enabled ? 1 : 0;
}

static int slot_live(const FzFxSlot *s)
{
    return s->kind != FZ_FX_OFF && (s->enabled || s->tail > 0);
}

uint32_t fz_fx_ram_used(const FzFxRack *rack)
{
    uint32_t n = 0;
    int i;
    if (!rack)
        return 0;
    for (i = 0; i < FZ_FX_SLOTS; i++) {
        const FzFxSlot *s = &rack->slot[i];
        if (!slot_live(s))
            continue;
        if (s->kind == FZ_FX_DELAY)
            n += sizeof s->delay;
        else if (s->kind == FZ_FX_REVERB)
            n += sizeof s->comb;
        else if (s->kind == FZ_FX_CHORUS)
            n += sizeof s->chorus;
    }
    return n;
}

static int16_t delay_tick(FzFxSlot *s, int32_t in)
{
    int time = s->param_a;
    int fb = s->param_b;
    int16_t y;
    int32_t rec;
    if (time < 1)
        time = 1;
    if (time > 4095)
        time = 4095;
    if (fb < 0)
        fb = 0;
    if (fb > 96)
        fb = 96;
    y = s->delay[s->pos % (uint32_t)time];
    rec = in + ((int32_t)y * fb) / 128;
    s->delay[s->pos % (uint32_t)time] = fz_sat16(rec);
    s->pos++;
    if ((uint32_t)time > 0 && s->pos >= (uint32_t)time)
        s->pos = 0;
    return y;
}

static int16_t reverb_tick(FzFxSlot *s, int32_t in)
{
    static const int len[4] = {311, 373, 461, 593};
    int decay = s->param_a;
    int32_t sum = 0;
    int i;
    if (decay < 0)
        decay = 0;
    if (decay > 100)
        decay = 100;
    for (i = 0; i < 4; i++) {
        uint32_t at = s->pos % (uint32_t)len[i];
        int16_t y = s->comb[i][at];
        int32_t rec = in + ((int32_t)y * decay) / 128;
        s->damp += (((int32_t)y << 8) - s->damp) / 8;
        s->comb[i][at] = fz_sat16(rec - s->damp / 512);
        sum += y;
    }
    s->pos++;
    return fz_sat16(sum / 4);
}

static int16_t chorus_tick(FzFxSlot *s, int32_t in)
{
    int depth = s->param_b;
    int tri;
    int delay;
    uint32_t read;
    int16_t y;
    if (depth < 0)
        depth = 0;
    if (depth > 400)
        depth = 400;
    s->chorus[s->pos % 1024u] = fz_sat16(in);
    s->phase += s->param_a > 0 ? (uint32_t)s->param_a * 4096u : 4096u;
    tri = (int)((s->phase >> 16) & 511u);
    delay = 48 + (tri * depth) / 512;
    if (delay < 1)
        delay = 1;
    if (delay > 1023)
        delay = 1023;
    read = (s->pos + 1024u - (uint32_t)delay) % 1024u;
    y = s->chorus[read];
    s->pos++;
    return y;
}

static int16_t drive_tick(FzFxSlot *s, int32_t in)
{
    int amount = s->param_a;
    int32_t shaped;
    if (amount < 0)
        amount = 0;
    if (amount > 127)
        amount = 127;
    shaped = (in * (8 + amount)) / 8;
    shaped = fz_sat16(shaped);
    return fz_sat16(shaped - in);
}

void fz_fx_process(FzFxRack *rack, const int32_t in[FZ_FX_SLOTS], int32_t *wet_l, int32_t *wet_r)
{
    int32_t wet = 0;
    int i;
    if (!rack || !in || !wet_l || !wet_r)
        return;
    for (i = 0; i < FZ_FX_SLOTS; i++) {
        FzFxSlot *s = &rack->slot[i];
        int32_t x = in[i];
        int16_t y = 0;
        if (s->kind == FZ_FX_OFF)
            continue;
        if (!s->enabled && s->tail <= 0)
            continue;
        if (x != 0)
            s->tail = 2400 + (s->kind == FZ_FX_DELAY ? s->param_a : 800);
        else if (s->tail > 0)
            s->tail--;
        if (s->kind == FZ_FX_DELAY)
            y = delay_tick(s, s->enabled ? x : 0);
        else if (s->kind == FZ_FX_REVERB)
            y = reverb_tick(s, s->enabled ? x : 0);
        else if (s->kind == FZ_FX_CHORUS)
            y = chorus_tick(s, s->enabled ? x : 0);
        else if (s->kind == FZ_FX_DRIVE)
            y = drive_tick(s, s->enabled ? x : 0);
        wet += ((int32_t)y * s->ret_gain) / 127;
        if (!s->enabled && s->tail <= 0) {
            memset(s->delay, 0, sizeof s->delay);
            memset(s->comb, 0, sizeof s->comb);
            memset(s->chorus, 0, sizeof s->chorus);
        }
    }
    *wet_l = wet;
    *wet_r = wet;
}

int32_t fz_send_amount(int32_t post_fader, int track_send, int trim)
{
    if (track_send <= 0 || trim <= 0)
        return 0;
    if (track_send > 127)
        track_send = 127;
    if (trim > 127)
        trim = 127;
    return (post_fader * track_send * trim) / (127 * 127);
}

void fz_voice_sends(const FzTrack *track, const FzPattern *pattern, const FzRegion *region, int32_t post_fader,
                    int wet_stem, int32_t *dry, int32_t send[FZ_FX_SLOTS])
{
    int i;
    if (!send)
        return;
    for (i = 0; i < FZ_FX_SLOTS; i++)
        send[i] = 0;
    if (dry)
        *dry = 0;
    if (!track || !region)
        return;
    if (track->mute)
        return;
    if (dry)
        *dry = post_fader;
    if (wet_stem)
        return;
    for (i = 0; i < FZ_FX_SLOTS; i++) {
        if (pattern && pattern->baked && (pattern->unsend_mask & (uint8_t)(1u << i)))
            continue;
        send[i] = fz_send_amount(post_fader, track->send[i], region->send_trim[i]);
    }
}
