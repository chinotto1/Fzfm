/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "import.h"

#include <string.h>

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int free_slots(const FzProject *p, int assets)
{
    int i, n = 0;
    int max = assets ? FZ_MAX_ASSETS : FZ_MAX_REGIONS;
    for (i = 0; i < max; i++) {
        int alive = assets ? p->asset[i].alive : p->region[i].alive;
        if (!alive)
            n++;
    }
    return n;
}

static void rollback(FzProject *p, FzInstrument *it, int regions_before, uint32_t watermark)
{
    while (it->region_count > regions_before) {
        FzId id = it->region[it->region_count - 1];
        FzRegion *reg = fz_region(p, id);
        if (reg) {
            FzAsset *asset = fz_asset(p, reg->asset);
            if (asset) {
                asset->alive = 0;
                asset->generation++;
                if (asset->generation == 0)
                    asset->generation = 1;
            }
            reg->alive = 0;
            reg->generation++;
            if (reg->generation == 0)
                reg->generation = 1;
        }
        it->region_count--;
    }
    p->pcm_used = watermark;
}

FzStatus fz_import_pcm(FzProject *project, FzId instrument, const FzPcmImport *cfg, const uint8_t *data, uint32_t len)
{
    FzInstrument *it;
    uint32_t need, watermark, i;
    int before;
    if (!project || !cfg || !data)
        return FZ_ERR_ARGS;
    if (cfg->count == 0 || cfg->count > FZ_MULTI_SLOTS || cfg->frames_each == 0 || cfg->rate == 0)
        return FZ_ERR_ARGS;
    if (cfg->frames_each > len || cfg->count > len / cfg->frames_each)
        return FZ_ERR_FORMAT;
    need = cfg->count * cfg->frames_each;
    if (len != need)
        return FZ_ERR_FORMAT;
    it = fz_instrument(project, instrument);
    if (!it)
        return FZ_ERR_BAD_ID;
    if (it->region_count + (int)cfg->count > FZ_MULTI_SLOTS)
        return FZ_ERR_REGION_CAPACITY;
    if (free_slots(project, 0) < (int)cfg->count || free_slots(project, 1) < (int)cfg->count)
        return FZ_ERR_FULL;
    if (!project->pcm_store || cfg->frames_each > project->pcm_capacity ||
        project->pcm_used + cfg->count * cfg->frames_each > project->pcm_capacity)
        return FZ_ERR_SAMPLE_STORAGE;
    watermark = project->pcm_used;
    before = it->region_count;
    for (i = 0; i < cfg->count; i++) {
        int16_t pcm[1024];
        uint32_t f;
        FzId asset, region;
        FzStatus st;
        if (cfg->frames_each > 1024)
            return FZ_ERR_RANGE;
        for (f = 0; f < cfg->frames_each; f++) {
            uint8_t raw = data[i * cfg->frames_each + f];
            int32_t sample = cfg->unsigned8 ? ((int32_t)raw - 128) << 8 : ((int32_t)(int8_t)raw) << 8;
            pcm[f] = fz_sat16(sample);
        }
        st = fz_project_add_pcm(project, pcm, cfg->frames_each, cfg->rate, 0, &asset);
        if (st != FZ_OK) {
            rollback(project, it, before, watermark);
            return st;
        }
        st = fz_project_add_region(project, instrument, asset, (uint8_t)(cfg->root0 + i), (int)i + before, &region);
        if (st != FZ_OK) {
            rollback(project, it, before, watermark);
            return st;
        }
    }
    return FZ_OK;
}

FzStatus fz_import_rom(FzProject *project, FzId instrument, const char *profile, const uint8_t *data, uint32_t len)
{
    FzPcmImport cfg;
    if (!profile)
        return FZ_ERR_FORMAT;
    if (strcmp(profile, "FZTEST") != 0)
        return FZ_ERR_FORMAT;
    if (!data || len != 4u * 256u)
        return FZ_ERR_FORMAT;
    cfg.count = 4;
    cfg.frames_each = 256;
    cfg.rate = 8000;
    cfg.root0 = 36;
    cfg.unsigned8 = 1;
    return fz_import_pcm(project, instrument, &cfg, data, len);
}

FzStatus fz_import_wav_cycle(const uint8_t *wav, uint32_t len, int16_t *dst, uint32_t dst_cap, uint32_t *points,
                             uint32_t *file_rate)
{
    uint32_t at, rate = 0, data_size = 0;
    const uint8_t *data = 0;
    uint16_t format = 0, channels = 0, bits = 0;
    int have_fmt = 0;
    uint32_t frames, i, bytes_frame;
    if (!wav || !dst || !points || !file_rate)
        return FZ_ERR_ARGS;
    *points = 0;
    *file_rate = 0;
    if (len < 12)
        return FZ_ERR_FORMAT;
    if (memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0)
        return FZ_ERR_FORMAT;
    at = 12;
    while (at + 8 <= len) {
        uint32_t size;
        if (len - at < 8)
            return FZ_ERR_FORMAT;
        size = rd32(wav + at + 4);
        if (size > len - at - 8)
            return FZ_ERR_FORMAT;
        if (memcmp(wav + at, "fmt ", 4) == 0) {
            if (size < 16)
                return FZ_ERR_FORMAT;
            format = rd16(wav + at + 8);
            channels = rd16(wav + at + 10);
            rate = rd32(wav + at + 12);
            bits = rd16(wav + at + 22);
            have_fmt = 1;
        } else if (memcmp(wav + at, "data", 4) == 0) {
            data = wav + at + 8;
            data_size = size;
        }
        if (size > len - at - 8)
            return FZ_ERR_FORMAT;
        at += 8u + size;
        if ((size & 1u) && at < len)
            at++;
    }
    if (!have_fmt || !data || format != 1 || rate == 0)
        return FZ_ERR_FORMAT;
    if (!(channels == 1 || channels == 2) || !(bits == 8 || bits == 16))
        return FZ_ERR_FORMAT;
    bytes_frame = (uint32_t)channels * (bits / 8u);
    if (bytes_frame == 0 || data_size < bytes_frame || data_size % bytes_frame != 0)
        return FZ_ERR_FORMAT;
    frames = data_size / bytes_frame;
    if (frames == 0 || frames > dst_cap)
        return FZ_ERR_RANGE;
    if ((uint32_t)(data - wav) > len || data_size > len - (uint32_t)(data - wav))
        return FZ_ERR_FORMAT;
    for (i = 0; i < frames; i++) {
        const uint8_t *s = data + i * bytes_frame;
        int32_t sample;
        if (bits == 8) {
            sample = ((int32_t)s[0] - 128) << 8;
            if (channels == 2)
                sample = (sample + (((int32_t)s[1] - 128) << 8)) / 2;
        } else {
            sample = (int16_t)rd16(s);
            if (channels == 2)
                sample = (sample + (int16_t)rd16(s + 2)) / 2;
        }
        dst[i] = fz_sat16(sample);
    }
    *points = frames;
    *file_rate = rate;
    return FZ_OK;
}
