/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "bundle.h"

#include <string.h>

static uint32_t fnv_bytes(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u;
    uint32_t i;
    for (i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

typedef struct {
    uint8_t *p;
    const uint8_t *src;
    uint32_t n;
    uint32_t cap;
    int fail;
    int reading;
} FzBuf;

static void bfail(FzBuf *b)
{
    b->fail = 1;
}

static void need(FzBuf *b, uint32_t k)
{
    if (b->fail)
        return;
    if (k > b->cap || b->n > b->cap - k)
        bfail(b);
}

static void w8(FzBuf *b, uint8_t v)
{
    need(b, 1);
    if (b->fail)
        return;
    if (!b->reading)
        b->p[b->n] = v;
    b->n++;
}

static uint8_t r8(FzBuf *b)
{
    uint8_t v = 0;
    need(b, 1);
    if (!b->fail)
        v = b->src[b->n];
    if (!b->fail)
        b->n++;
    return v;
}

static void w16(FzBuf *b, uint16_t v)
{
    w8(b, (uint8_t)v);
    w8(b, (uint8_t)(v >> 8));
}

static uint16_t r16(FzBuf *b)
{
    uint16_t lo = r8(b);
    uint16_t hi = r8(b);
    return (uint16_t)(lo | (hi << 8));
}

static void w32(FzBuf *b, uint32_t v)
{
    w16(b, (uint16_t)v);
    w16(b, (uint16_t)(v >> 16));
}

static uint32_t r32(FzBuf *b)
{
    uint32_t lo = r16(b);
    uint32_t hi = r16(b);
    return lo | (hi << 16);
}

static void wbytes(FzBuf *b, const void *src, uint32_t n)
{
    need(b, n);
    if (b->fail)
        return;
    if (!b->reading)
        memcpy(b->p + b->n, src, n);
    b->n += n;
}

static void rbytes(FzBuf *b, void *dst, uint32_t n)
{
    need(b, n);
    if (b->fail)
        return;
    if (dst)
        memcpy(dst, b->src + b->n, n);
    b->n += n;
}

static uint32_t tempo_milli(const FzProject *project)
{
    double t = project->tempo_bpm * 1000.0;
    if (t < 0)
        t = 0;
    if (t > 1000000.0)
        t = 1000000.0;
    return (uint32_t)(t + 0.5);
}

FzStatus fz_bundle_write(const FzProject *project, uint8_t *dst, uint32_t cap, uint32_t *out_len)
{
    FzBuf b;
    int i, r, tracks;
    if (!project || !dst || !out_len)
        return FZ_ERR_ARGS;
    memset(&b, 0, sizeof b);
    b.p = dst;
    b.cap = cap;
    wbytes(&b, "FZB1", 4);
    w16(&b, 1);
    w16(&b, 0);
    w32(&b, tempo_milli(project));
    {
        uint16_t ninst = 0;
        for (i = 0; i < FZ_MAX_INSTRUMENTS; i++)
            if (project->instrument[i].alive)
                ninst++;
        w16(&b, ninst);
    }
    for (i = 0; i < FZ_MAX_INSTRUMENTS; i++) {
        const FzInstrument *it = &project->instrument[i];
        if (!it->alive)
            continue;
        w8(&b, (uint8_t)it->kind);
        wbytes(&b, it->name, FZ_NAME_LEN);
        w8(&b, (uint8_t)it->region_count);
        for (r = 0; r < it->region_count; r++) {
            const FzRegion *reg = 0;
            const FzAsset *asset = 0;
            uint32_t f;
            if (it->region[r].index < FZ_MAX_REGIONS)
                reg = &project->region[it->region[r].index];
            if (!reg || !reg->alive || reg->generation != it->region[r].generation)
                return FZ_ERR_BAD_ID;
            if (reg->asset.index < FZ_MAX_ASSETS)
                asset = &project->asset[reg->asset.index];
            if (!asset || !asset->alive || !asset->pcm)
                return FZ_ERR_BAD_ID;
            w8(&b, reg->root);
            w16(&b, (uint16_t)reg->gain_q15);
            wbytes(&b, reg->send_trim, 4);
            w8(&b, (uint8_t)reg->reverse);
            w8(&b, reg->one_shot);
            w32(&b, asset->frames);
            w32(&b, asset->native_rate);
            for (f = 0; f < asset->frames; f++)
                w16(&b, (uint16_t)asset->pcm[f]);
        }
    }
    tracks = 0;
    for (i = 0; i < FZ_MAX_TRACKS; i++)
        if (project->track[i].alive)
            tracks++;
    w16(&b, (uint16_t)tracks);
    for (i = 0; i < FZ_MAX_TRACKS; i++) {
        const FzTrack *t = &project->track[i];
        int s;
        if (!t->alive)
            continue;
        w8(&b, (uint8_t)i);
        w16(&b, (uint16_t)t->volume);
        w16(&b, (uint16_t)t->pan);
        w16(&b, (uint16_t)t->cutoff);
        w16(&b, (uint16_t)t->resonance);
        for (s = 0; s < 4; s++)
            w16(&b, (uint16_t)t->send[s]);
        w8(&b, t->mute);
        w8(&b, t->solo);
    }
    if (b.fail)
        return FZ_ERR_FULL;
    w32(&b, fnv_bytes(dst, b.n));
    if (b.fail)
        return FZ_ERR_FULL;
    *out_len = b.n;
    return FZ_OK;
}

static FzStatus scan(const uint8_t *src, uint32_t len, uint32_t *pcm_frames)
{
    FzBuf b;
    uint16_t version, ninst, ntracks;
    uint32_t sum, frames_total = 0;
    int i, r;
    memset(&b, 0, sizeof b);
    b.src = src;
    b.cap = len;
    b.reading = 1;
    {
        char magic[4];
        rbytes(&b, magic, 4);
        if (b.fail || memcmp(magic, "FZB1", 4) != 0)
            return FZ_ERR_FORMAT;
    }
    version = r16(&b);
    (void)r16(&b);
    if (version != 1)
        return FZ_ERR_FORMAT;
    (void)r32(&b);
    ninst = r16(&b);
    if (b.fail || ninst > FZ_MAX_INSTRUMENTS)
        return FZ_ERR_FORMAT;
    for (i = 0; i < ninst; i++) {
        uint8_t nreg, kind;
        kind = r8(&b);
        rbytes(&b, 0, FZ_NAME_LEN);
        nreg = r8(&b);
        if (b.fail || kind == 0 || nreg > FZ_MULTI_SLOTS)
            return FZ_ERR_FORMAT;
        for (r = 0; r < nreg; r++) {
            uint32_t frames, rate;
            (void)r8(&b);
            (void)r16(&b);
            rbytes(&b, 0, 4);
            (void)r8(&b);
            (void)r8(&b);
            frames = r32(&b);
            rate = r32(&b);
            if (b.fail || frames == 0 || frames > 1000000u || rate == 0)
                return FZ_ERR_FORMAT;
            if (frames > (len / 2u))
                return FZ_ERR_FORMAT;
            rbytes(&b, 0, frames * 2u);
            if (b.fail)
                return FZ_ERR_FORMAT;
            if (frames_total > 0xFFFFFFFFu - frames)
                return FZ_ERR_FORMAT;
            frames_total += frames;
        }
    }
    ntracks = r16(&b);
    if (b.fail || ntracks > FZ_MAX_TRACKS)
        return FZ_ERR_FORMAT;
    for (i = 0; i < ntracks; i++) {
        (void)r8(&b);
        (void)r16(&b);
        (void)r16(&b);
        (void)r16(&b);
        (void)r16(&b);
        (void)r16(&b);
        (void)r16(&b);
        (void)r16(&b);
        (void)r16(&b);
        (void)r8(&b);
        (void)r8(&b);
    }
    if (b.fail || b.n > len || len - b.n < 4)
        return FZ_ERR_FORMAT;
    sum = r32(&b);
    if (b.fail || b.n != len)
        return FZ_ERR_FORMAT;
    if (sum != fnv_bytes(src, len - 4u))
        return FZ_ERR_FORMAT;
    *pcm_frames = frames_total;
    return FZ_OK;
}

FzStatus fz_bundle_read(FzProject *project, const uint8_t *src, uint32_t len)
{
    FzBuf b;
    int16_t *store;
    uint32_t cap, pcm_need = 0;
    uint16_t ninst, ntracks;
    int i, r;
    FzStatus st;
    if (!project || !src)
        return FZ_ERR_ARGS;
    st = scan(src, len, &pcm_need);
    if (st != FZ_OK)
        return st;
    if (pcm_need > project->pcm_capacity)
        return FZ_ERR_SAMPLE_STORAGE;
    store = project->pcm_store;
    cap = project->pcm_capacity;
    fz_project_init(project, store, cap);
    memset(&b, 0, sizeof b);
    b.src = src;
    b.cap = len;
    b.reading = 1;
    rbytes(&b, 0, 4);
    (void)r16(&b);
    (void)r16(&b);
    project->tempo_bpm = (double)r32(&b) / 1000.0;
    ninst = r16(&b);
    for (i = 0; i < ninst; i++) {
        FzId inst, track;
        uint8_t kind = r8(&b);
        char name[FZ_NAME_LEN];
        uint8_t nreg;
        rbytes(&b, name, FZ_NAME_LEN);
        nreg = r8(&b);
        if (fz_project_new_instrument(project, (FzInstKind)kind, &inst, &track) != FZ_OK)
            return FZ_ERR_FULL;
        memcpy(fz_instrument(project, inst)->name, name, FZ_NAME_LEN);
        fz_instrument(project, inst)->name[FZ_NAME_LEN - 1] = 0;
        for (r = 0; r < nreg; r++) {
            uint8_t root = r8(&b);
            int16_t gain = (int16_t)r16(&b);
            uint8_t trim[4];
            uint8_t reverse, one_shot;
            uint32_t frames, rate, f;
            int16_t *pcm;
            FzId asset, region;
            rbytes(&b, trim, 4);
            reverse = r8(&b);
            one_shot = r8(&b);
            frames = r32(&b);
            rate = r32(&b);
            if (project->pcm_used + frames > project->pcm_capacity)
                return FZ_ERR_SAMPLE_STORAGE;
            pcm = project->pcm_store + project->pcm_used;
            for (f = 0; f < frames; f++)
                pcm[f] = (int16_t)r16(&b);
            if (fz_project_add_pcm(project, pcm, frames, rate, 0, &asset) != FZ_OK)
                return FZ_ERR_SAMPLE_STORAGE;
            if (fz_project_add_region(project, inst, asset, root, r, &region) != FZ_OK)
                return FZ_ERR_REGION_CAPACITY;
            {
                FzRegion *reg = fz_region(project, region);
                int s;
                reg->gain_q15 = gain;
                reg->reverse = reverse ? 1 : 0;
                reg->one_shot = one_shot ? 1 : 0;
                for (s = 0; s < 4; s++)
                    reg->send_trim[s] = trim[s];
            }
        }
    }
    ntracks = r16(&b);
    for (i = 0; i < ntracks; i++) {
        uint8_t index = r8(&b);
        int16_t volume = (int16_t)r16(&b);
        int16_t pan = (int16_t)r16(&b);
        int16_t cutoff = (int16_t)r16(&b);
        int16_t resonance = (int16_t)r16(&b);
        int16_t send[4];
        uint8_t mute, solo;
        int s;
        for (s = 0; s < 4; s++)
            send[s] = (int16_t)r16(&b);
        mute = r8(&b);
        solo = r8(&b);
        if (index < FZ_MAX_TRACKS && project->track[index].alive) {
            FzTrack *t = &project->track[index];
            t->volume = volume;
            t->pan = pan;
            t->cutoff = cutoff;
            t->resonance = resonance;
            for (s = 0; s < 4; s++)
                t->send[s] = send[s];
            t->mute = mute;
            t->solo = solo;
        }
    }
    (void)r32(&b);
    return b.fail ? FZ_ERR_FORMAT : FZ_OK;
}
