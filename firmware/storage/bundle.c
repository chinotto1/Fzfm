/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "bundle.h"

#include "sequence/seq.h"

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

static int count_regions(const FzProject *project)
{
    int i, n = 0;
    for (i = 0; i < FZ_MAX_INSTRUMENTS; i++)
        if (project->instrument[i].alive)
            n += project->instrument[i].region_count;
    return n;
}

static int count_alive_inst(const FzProject *project)
{
    int i, n = 0;
    for (i = 0; i < FZ_MAX_INSTRUMENTS; i++)
        if (project->instrument[i].alive)
            n++;
    return n;
}

static int count_alive_tracks(const FzProject *project)
{
    int i, n = 0;
    for (i = 0; i < FZ_MAX_TRACKS; i++)
        if (project->track[i].alive)
            n++;
    return n;
}

static void write_trailer(FzBuf *b, const FzProject *project, const FzSeq *seq)
{
    int i, r, n;
    n = 0;
    for (i = 0; i < FZ_SEQ_EVENTS; i++)
        if (seq->ev[i].used)
            n++;
    w16(b, (uint16_t)count_regions(project));
    for (i = 0; i < FZ_MAX_INSTRUMENTS; i++) {
        const FzInstrument *it = &project->instrument[i];
        if (!it->alive)
            continue;
        for (r = 0; r < it->region_count; r++) {
            const FzRegion *reg = &project->region[it->region[r].index];
            const FzAsset *asset = &project->asset[reg->asset.index];
            w8(b, asset->cycle ? 1 : 0);
        }
    }
    w16(b, (uint16_t)count_alive_inst(project));
    for (i = 0; i < FZ_MAX_INSTRUMENTS; i++) {
        const FzInstrument *it = &project->instrument[i];
        int h;
        if (!it->alive)
            continue;
        for (h = 0; h < FZ_HARMONICS; h++)
            w16(b, it->additive.level[h]);
        w16(b, it->additive.points);
    }
    w16(b, (uint16_t)count_alive_tracks(project));
    for (i = 0; i < FZ_MAX_TRACKS; i++) {
        const FzTrack *t = &project->track[i];
        if (!t->alive)
            continue;
        w8(b, t->lfo.rate);
        w8(b, t->lfo.depth);
        w8(b, t->lfo.shape);
        w8(b, t->lfo.dest);
        w8(b, t->lfo.retrigger);
        w16(b, (uint16_t)t->key_track);
    }
    w32(b, seq->bpm_milli);
    w16(b, (uint16_t)n);
    for (i = 0; i < FZ_SEQ_EVENTS; i++) {
        const FzSeqEvent *ev = &seq->ev[i];
        if (!ev->used)
            continue;
        w8(b, ev->note);
        w8(b, ev->velocity);
        w8(b, ev->prob);
        w16(b, ev->pattern);
        w16(b, ev->duration);
        w32(b, ev->tick);
        w16(b, (uint16_t)ev->micro);
    }
    n = seq->song_n < 0 ? 0 : seq->song_n;
    if (n > FZ_SONG_ROWS)
        n = FZ_SONG_ROWS;
    w16(b, (uint16_t)n);
    for (i = 0; i < n; i++) {
        int t;
        for (t = 0; t < FZ_MAX_TRACKS; t++)
            w8(b, seq->song[i].pattern[t]);
        for (t = 0; t < FZ_MAX_TRACKS; t++)
            w8(b, seq->song[i].mute[t]);
        w16(b, seq->song[i].repeats);
    }
    n = 0;
    for (i = 0; i < FZ_MAX_PATTERNS; i++)
        if (project->pattern[i].alive)
            n++;
    w16(b, (uint16_t)n);
    for (i = 0; i < FZ_MAX_PATTERNS; i++) {
        const FzPattern *pat = &project->pattern[i];
        if (!pat->alive)
            continue;
        w8(b, (uint8_t)i);
        w32(b, pat->length_ticks);
        w8(b, pat->baked);
        w8(b, pat->unsend_mask);
    }
    for (i = 0; i < FZ_MAX_TRACKS; i++) {
        int cur = seq->current[i];
        w8(b, (cur < 0 || cur > 254) ? 255 : (uint8_t)cur);
    }
    w8(b, (uint8_t)(seq->swing < 0 ? 0 : seq->swing > 50 ? 50 : seq->swing));
    w16(b, (uint16_t)(seq->quantize_grid < 0 ? 0 : seq->quantize_grid));
}

static FzStatus write_bundle(const FzProject *project, const FzSeq *seq, uint8_t *dst, uint32_t cap, uint32_t *out_len)
{
    FzBuf b;
    int i, r, tracks;
    if (!project || !dst || !out_len)
        return FZ_ERR_ARGS;
    memset(&b, 0, sizeof b);
    b.p = dst;
    b.cap = cap;
    wbytes(&b, "FZB1", 4);
    w16(&b, seq ? 2 : 1);
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
    if (seq)
        write_trailer(&b, project, seq);
    if (b.fail)
        return FZ_ERR_FULL;
    w32(&b, fnv_bytes(dst, b.n));
    if (b.fail)
        return FZ_ERR_FULL;
    *out_len = b.n;
    return FZ_OK;
}

FzStatus fz_bundle_write(const FzProject *project, uint8_t *dst, uint32_t cap, uint32_t *out_len)
{
    return write_bundle(project, 0, dst, cap, out_len);
}

FzStatus fz_bundle_write_all(const FzProject *project, const FzSeq *seq, uint8_t *dst, uint32_t cap, uint32_t *out_len)
{
    if (!seq)
        return FZ_ERR_ARGS;
    return write_bundle(project, seq, dst, cap, out_len);
}

static FzStatus open_buf(FzBuf *b, const uint8_t *src, uint32_t len, uint16_t *version)
{
    char magic[4];
    memset(b, 0, sizeof *b);
    b->src = src;
    b->cap = len;
    b->reading = 1;
    rbytes(b, magic, 4);
    if (b->fail || memcmp(magic, "FZB1", 4) != 0)
        return FZ_ERR_FORMAT;
    *version = r16(b);
    (void)r16(b);
    return b->fail ? FZ_ERR_FORMAT : FZ_OK;
}

static FzStatus skip_payload(FzBuf *b, uint32_t len, uint16_t *ninst_out, uint16_t *nregions_out, uint16_t *ntracks_out,
                             uint32_t *frames_out)
{
    uint16_t ninst, nregions = 0, ntracks;
    uint32_t frames_total = 0;
    int i, r;
    (void)r32(b);
    ninst = r16(b);
    if (b->fail || ninst > FZ_MAX_INSTRUMENTS)
        return FZ_ERR_FORMAT;
    for (i = 0; i < ninst; i++) {
        uint8_t nreg, kind;
        kind = r8(b);
        rbytes(b, 0, FZ_NAME_LEN);
        nreg = r8(b);
        if (b->fail || kind == 0 || nreg > FZ_MULTI_SLOTS)
            return FZ_ERR_FORMAT;
        if (nregions > 0xFFFFu - nreg)
            return FZ_ERR_FORMAT;
        nregions = (uint16_t)(nregions + nreg);
        for (r = 0; r < nreg; r++) {
            uint32_t frames, rate;
            (void)r8(b);
            (void)r16(b);
            rbytes(b, 0, 4);
            (void)r8(b);
            (void)r8(b);
            frames = r32(b);
            rate = r32(b);
            if (b->fail || frames == 0 || frames > 1000000u || rate == 0)
                return FZ_ERR_FORMAT;
            if (frames > (len / 2u))
                return FZ_ERR_FORMAT;
            rbytes(b, 0, frames * 2u);
            if (b->fail)
                return FZ_ERR_FORMAT;
            if (frames_total > 0xFFFFFFFFu - frames)
                return FZ_ERR_FORMAT;
            frames_total += frames;
        }
    }
    ntracks = r16(b);
    if (b->fail || ntracks > FZ_MAX_TRACKS)
        return FZ_ERR_FORMAT;
    for (i = 0; i < ntracks; i++) {
        (void)r8(b);
        (void)r16(b);
        (void)r16(b);
        (void)r16(b);
        (void)r16(b);
        (void)r16(b);
        (void)r16(b);
        (void)r16(b);
        (void)r16(b);
        (void)r8(b);
        (void)r8(b);
    }
    if (b->fail)
        return FZ_ERR_FORMAT;
    *ninst_out = ninst;
    *nregions_out = nregions;
    *ntracks_out = ntracks;
    *frames_out = frames_total;
    return FZ_OK;
}

static FzStatus skip_trailer(FzBuf *b, uint16_t nregions, uint16_t ninst, uint16_t ntracks)
{
    uint16_t n, i, h;
    uint32_t seen = 0;
    n = r16(b);
    if (b->fail || n != nregions)
        return FZ_ERR_FORMAT;
    for (i = 0; i < n; i++)
        (void)r8(b);
    n = r16(b);
    if (b->fail || n != ninst)
        return FZ_ERR_FORMAT;
    for (i = 0; i < n; i++) {
        for (h = 0; h < FZ_HARMONICS; h++) {
            uint16_t level = r16(b);
            if (!b->fail && level > 256)
                return FZ_ERR_FORMAT;
        }
        {
            uint16_t points = r16(b);
            if (!b->fail && points > FZ_ADDITIVE_POINTS)
                return FZ_ERR_FORMAT;
        }
    }
    n = r16(b);
    if (b->fail || n != ntracks)
        return FZ_ERR_FORMAT;
    for (i = 0; i < n; i++) {
        (void)r8(b);
        (void)r8(b);
        (void)r8(b);
        (void)r8(b);
        (void)r8(b);
        (void)r16(b);
    }
    (void)r32(b);
    n = r16(b);
    if (b->fail || n > FZ_SEQ_EVENTS)
        return FZ_ERR_FORMAT;
    for (i = 0; i < n; i++) {
        (void)r8(b);
        (void)r8(b);
        (void)r8(b);
        (void)r16(b);
        (void)r16(b);
        (void)r32(b);
        (void)r16(b);
    }
    n = r16(b);
    if (b->fail || n > FZ_SONG_ROWS)
        return FZ_ERR_FORMAT;
    for (i = 0; i < n; i++) {
        int t;
        for (t = 0; t < FZ_MAX_TRACKS; t++)
            (void)r8(b);
        for (t = 0; t < FZ_MAX_TRACKS; t++)
            (void)r8(b);
        (void)r16(b);
    }
    n = r16(b);
    if (b->fail || n > FZ_MAX_PATTERNS)
        return FZ_ERR_FORMAT;
    for (i = 0; i < n; i++) {
        uint8_t index = r8(b);
        (void)r32(b);
        (void)r8(b);
        (void)r8(b);
        if (b->fail || index >= FZ_MAX_PATTERNS)
            return FZ_ERR_FORMAT;
        if (seen & (1u << index))
            return FZ_ERR_FORMAT;
        seen |= 1u << index;
    }
    for (i = 0; i < FZ_MAX_TRACKS; i++)
        (void)r8(b);
    {
        uint8_t swing = r8(b);
        if (!b->fail && swing > 50)
            return FZ_ERR_FORMAT;
    }
    (void)r16(b);
    if (b->fail)
        return FZ_ERR_FORMAT;
    return FZ_OK;
}

static FzStatus finish_sum(FzBuf *b, const uint8_t *src, uint32_t len)
{
    uint32_t sum;
    if (b->fail || b->n > len || len - b->n < 4)
        return FZ_ERR_FORMAT;
    sum = r32(b);
    if (b->fail || b->n != len)
        return FZ_ERR_FORMAT;
    if (sum != fnv_bytes(src, len - 4u))
        return FZ_ERR_FORMAT;
    return FZ_OK;
}

static FzStatus scan(const uint8_t *src, uint32_t len, uint32_t *pcm_frames)
{
    FzBuf b;
    uint16_t version, ninst, nregions, ntracks;
    FzStatus st = open_buf(&b, src, len, &version);
    if (st != FZ_OK)
        return st;
    if (version != 1)
        return FZ_ERR_FORMAT;
    st = skip_payload(&b, len, &ninst, &nregions, &ntracks, pcm_frames);
    if (st != FZ_OK)
        return st;
    (void)ninst;
    (void)nregions;
    (void)ntracks;
    return finish_sum(&b, src, len);
}

static FzStatus scan_v2(const uint8_t *src, uint32_t len, uint32_t *pcm_frames)
{
    FzBuf b;
    uint16_t version, ninst, nregions, ntracks;
    FzStatus st = open_buf(&b, src, len, &version);
    if (st != FZ_OK)
        return st;
    if (version != 2)
        return FZ_ERR_FORMAT;
    st = skip_payload(&b, len, &ninst, &nregions, &ntracks, pcm_frames);
    if (st != FZ_OK)
        return st;
    st = skip_trailer(&b, nregions, ninst, ntracks);
    if (st != FZ_OK)
        return st;
    return finish_sum(&b, src, len);
}

static void adopt_seq_tempo(FzSeq *seq, const FzProject *project)
{
    double t = project->tempo_bpm * 1000.0;
    uint32_t milli;
    if (t < 0.0)
        t = 0.0;
    if (t > 1000000.0)
        t = 1000000.0;
    milli = (uint32_t)(t + 0.5);
    fz_seq_init(seq);
    fz_seq_set_tempo(seq, milli);
}

static FzStatus read_project_body(FzProject *project, const uint8_t *src, uint32_t len, FzBuf *b)
{
    int16_t *store;
    uint32_t cap;
    uint16_t ninst, ntracks;
    int i, r;
    store = project->pcm_store;
    cap = project->pcm_capacity;
    fz_project_init(project, store, cap);
    memset(b, 0, sizeof *b);
    b->src = src;
    b->cap = len;
    b->reading = 1;
    rbytes(b, 0, 4);
    (void)r16(b);
    (void)r16(b);
    project->tempo_bpm = (double)r32(b) / 1000.0;
    ninst = r16(b);
    for (i = 0; i < ninst; i++) {
        FzId inst, track;
        uint8_t kind = r8(b);
        char name[FZ_NAME_LEN];
        uint8_t nreg;
        rbytes(b, name, FZ_NAME_LEN);
        nreg = r8(b);
        if (b->fail)
            return FZ_ERR_FORMAT;
        if (fz_project_new_instrument(project, (FzInstKind)kind, &inst, &track) != FZ_OK)
            return FZ_ERR_FULL;
        memcpy(fz_instrument(project, inst)->name, name, FZ_NAME_LEN);
        fz_instrument(project, inst)->name[FZ_NAME_LEN - 1] = 0;
        for (r = 0; r < nreg; r++) {
            uint8_t root = r8(b);
            int16_t gain = (int16_t)r16(b);
            uint8_t trim[4];
            uint8_t reverse, one_shot;
            uint32_t frames, rate, f;
            int16_t *pcm;
            FzId asset, region;
            rbytes(b, trim, 4);
            reverse = r8(b);
            one_shot = r8(b);
            frames = r32(b);
            rate = r32(b);
            if (b->fail)
                return FZ_ERR_FORMAT;
            if (project->pcm_used + frames > project->pcm_capacity)
                return FZ_ERR_SAMPLE_STORAGE;
            pcm = project->pcm_store + project->pcm_used;
            for (f = 0; f < frames; f++)
                pcm[f] = (int16_t)r16(b);
            if (b->fail)
                return FZ_ERR_FORMAT;
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
    ntracks = r16(b);
    for (i = 0; i < ntracks; i++) {
        uint8_t index = r8(b);
        int16_t volume = (int16_t)r16(b);
        int16_t pan = (int16_t)r16(b);
        int16_t cutoff = (int16_t)r16(b);
        int16_t resonance = (int16_t)r16(b);
        int16_t send[4];
        uint8_t mute, solo;
        int s;
        for (s = 0; s < 4; s++)
            send[s] = (int16_t)r16(b);
        mute = r8(b);
        solo = r8(b);
        if (b->fail)
            return FZ_ERR_FORMAT;
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
    return b->fail ? FZ_ERR_FORMAT : FZ_OK;
}

static FzStatus read_trailer(FzProject *project, FzSeq *seq, FzBuf *b)
{
    uint16_t ncycle, nrecipe, nlfo, nevents, song_n, npat;
    uint32_t bpm;
    int i, seen;
    ncycle = r16(b);
    seen = 0;
    for (i = 0; i < FZ_MAX_INSTRUMENTS && seen < ncycle; i++) {
        FzInstrument *it = &project->instrument[i];
        int r;
        if (!it->alive)
            continue;
        for (r = 0; r < it->region_count && seen < ncycle; r++) {
            uint8_t cycle = r8(b);
            FzRegion *reg = &project->region[it->region[r].index];
            FzAsset *asset = 0;
            if (reg->asset.index < FZ_MAX_ASSETS)
                asset = &project->asset[reg->asset.index];
            if (asset && asset->alive)
                asset->cycle = cycle ? 1 : 0;
            seen++;
        }
    }
    while (seen < ncycle) {
        (void)r8(b);
        seen++;
    }
    nrecipe = r16(b);
    seen = 0;
    for (i = 0; i < FZ_MAX_INSTRUMENTS && seen < nrecipe; i++) {
        FzInstrument *it = &project->instrument[i];
        int h;
        if (!it->alive)
            continue;
        for (h = 0; h < FZ_HARMONICS; h++) {
            uint16_t level = r16(b);
            if (level > 256)
                level = 256;
            it->additive.level[h] = level;
        }
        it->additive.points = r16(b);
        if (it->additive.points > FZ_ADDITIVE_POINTS)
            it->additive.points = FZ_ADDITIVE_POINTS;
        seen++;
    }
    while (seen < nrecipe) {
        int h;
        for (h = 0; h < FZ_HARMONICS; h++)
            (void)r16(b);
        (void)r16(b);
        seen++;
    }
    nlfo = r16(b);
    seen = 0;
    for (i = 0; i < FZ_MAX_TRACKS && seen < nlfo; i++) {
        FzTrack *t = &project->track[i];
        if (!t->alive)
            continue;
        t->lfo.rate = r8(b);
        t->lfo.depth = r8(b);
        t->lfo.shape = r8(b);
        t->lfo.dest = r8(b);
        t->lfo.retrigger = r8(b);
        t->key_track = (int16_t)r16(b);
        seen++;
    }
    while (seen < nlfo) {
        (void)r8(b);
        (void)r8(b);
        (void)r8(b);
        (void)r8(b);
        (void)r8(b);
        (void)r16(b);
        seen++;
    }
    bpm = r32(b);
    if (b->fail)
        return FZ_ERR_FORMAT;
    fz_seq_init(seq);
    nevents = r16(b);
    if (nevents > FZ_SEQ_EVENTS)
        return FZ_ERR_FORMAT;
    for (i = 0; i < nevents; i++) {
        FzSeqEvent *ev = &seq->ev[i];
        ev->used = 1;
        ev->note = r8(b);
        ev->velocity = r8(b);
        ev->prob = r8(b);
        ev->pattern = r16(b);
        ev->duration = r16(b);
        ev->tick = r32(b);
        ev->micro = (int16_t)r16(b);
    }
    seq->count = nevents;
    song_n = r16(b);
    if (song_n > FZ_SONG_ROWS)
        return FZ_ERR_FORMAT;
    seq->song_n = song_n;
    for (i = 0; i < song_n; i++) {
        int t;
        for (t = 0; t < FZ_MAX_TRACKS; t++)
            seq->song[i].pattern[t] = r8(b);
        for (t = 0; t < FZ_MAX_TRACKS; t++)
            seq->song[i].mute[t] = r8(b);
        seq->song[i].repeats = r16(b);
    }
    npat = r16(b);
    if (npat > FZ_MAX_PATTERNS)
        return FZ_ERR_FORMAT;
    for (i = 0; i < npat; i++) {
        uint8_t index = r8(b);
        uint32_t length = r32(b);
        uint8_t baked = r8(b);
        uint8_t unsend = r8(b);
        FzPattern *pat;
        if (index >= FZ_MAX_PATTERNS)
            return FZ_ERR_FORMAT;
        pat = &project->pattern[index];
        if (!pat->alive) {
            if (pat->generation == 0)
                pat->generation = 1;
            pat->alive = 1;
        }
        pat->length_ticks = length;
        pat->baked = baked;
        pat->unsend_mask = unsend;
    }
    for (i = 0; i < FZ_MAX_TRACKS; i++) {
        uint8_t cur = r8(b);
        seq->current[i] = (cur == 255 || cur >= FZ_MAX_PATTERNS) ? -1 : (int)cur;
    }
    {
        uint8_t swing = r8(b);
        seq->swing = swing > 50 ? 50 : (int)swing;
    }
    seq->quantize_grid = (int)r16(b);
    if (b->fail)
        return FZ_ERR_FORMAT;
    fz_seq_set_tempo(seq, bpm);
    project->tempo_bpm = (double)seq->bpm_milli / 1000.0;
    return FZ_OK;
}

FzStatus fz_bundle_read(FzProject *project, const uint8_t *src, uint32_t len)
{
    FzBuf b;
    uint32_t pcm_need = 0;
    FzStatus st;
    if (!project || !src)
        return FZ_ERR_ARGS;
    st = scan(src, len, &pcm_need);
    if (st != FZ_OK)
        return st;
    if (pcm_need > project->pcm_capacity)
        return FZ_ERR_SAMPLE_STORAGE;
    st = read_project_body(project, src, len, &b);
    if (st != FZ_OK)
        return st;
    (void)r32(&b);
    return b.fail ? FZ_ERR_FORMAT : FZ_OK;
}

FzStatus fz_bundle_read_all(FzProject *project, FzSeq *seq, const uint8_t *src, uint32_t len)
{
    FzBuf b;
    uint16_t version;
    uint32_t pcm_need = 0;
    FzStatus st;
    if (!project || !seq || !src)
        return FZ_ERR_ARGS;
    if (len < 6 || memcmp(src, "FZB1", 4) != 0)
        return FZ_ERR_FORMAT;
    version = (uint16_t)(src[4] | ((uint16_t)src[5] << 8));
    if (version == 1) {
        st = fz_bundle_read(project, src, len);
        if (st != FZ_OK)
            return st;
        adopt_seq_tempo(seq, project);
        return FZ_OK;
    }
    if (version != 2)
        return FZ_ERR_FORMAT;
    st = scan_v2(src, len, &pcm_need);
    if (st != FZ_OK)
        return st;
    if (pcm_need > project->pcm_capacity)
        return FZ_ERR_SAMPLE_STORAGE;
    st = read_project_body(project, src, len, &b);
    if (st != FZ_OK)
        return st;
    st = read_trailer(project, seq, &b);
    if (st != FZ_OK)
        return st;
    if (b.fail || b.n + 4 != len)
        return FZ_ERR_FORMAT;
    (void)r32(&b);
    return b.fail ? FZ_ERR_FORMAT : FZ_OK;
}
