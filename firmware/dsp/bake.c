/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "bake.h"

int fz_bake_shared_sum_is_exact(void)
{
    return 0;
}

static int partial_mask(const FzTrack *track, const FzRegion *region, const FzFxRack *fx, int mask)
{
    int i, partial = 0;
    for (i = 0; i < FZ_FX_SLOTS; i++) {
        int active = track->send[i] > 0 && region->send_trim[i] > 0 && fx->slot[i].enabled && fx->slot[i].kind != FZ_FX_OFF;
        int selected = (mask & (1 << i)) != 0;
        if (active && !selected)
            partial = 1;
    }
    return partial;
}

static FzStatus render_asset(FzProject *project, FzFxRack *fx, const FzTrack *track, const FzRegion *region,
                             const FzAsset *asset, int mask, int partial, int16_t *scratch, uint32_t scratch_cap,
                             FzId *out_asset)
{
    uint32_t i, frames;
    if (!asset->pcm || asset->frames == 0)
        return FZ_ERR_RANGE;
    frames = asset->frames;
    if (region->end_frame > region->start_frame && region->end_frame <= asset->frames)
        frames = region->end_frame - region->start_frame;
    else if (region->start_frame >= asset->frames)
        return FZ_ERR_RANGE;
    else if (region->start_frame > 0)
        frames = asset->frames - region->start_frame;
    if (frames == 0 || frames > scratch_cap)
        return FZ_ERR_RANGE;
    if (!project->pcm_store || project->pcm_used + frames > project->pcm_capacity)
        return FZ_ERR_SAMPLE_STORAGE;
    for (i = 0; i < frames; i++) {
        int32_t sample = asset->pcm[region->start_frame + i];
        int32_t send[FZ_FX_SLOTS];
        int32_t wet_l, wet_r;
        int s;
        for (s = 0; s < FZ_FX_SLOTS; s++) {
            send[s] = 0;
            if (mask & (1 << s))
                send[s] = fz_send_amount(sample, track->send[s], region->send_trim[s]);
        }
        fz_fx_process(fx, send, &wet_l, &wet_r);
        if (partial)
            scratch[i] = fz_sat16(wet_l);
        else
            scratch[i] = fz_sat16(sample + wet_l);
    }
    return fz_project_add_pcm(project, scratch, frames, asset->native_rate, 0, out_asset);
}

FzStatus fz_bake_region(FzProject *project, FzFxRack *fx, FzId track, FzId region, int mask, int16_t *scratch,
                        uint32_t scratch_cap)
{
    FzTrack *tr = fz_track(project, track);
    FzRegion *reg = fz_region(project, region);
    FzAsset *asset;
    FzId baked;
    int partial, group, i;
    FzStatus st;
    if (!project || !fx || !scratch || !tr || !reg || mask == 0)
        return FZ_ERR_ARGS;
    asset = fz_asset(project, reg->asset);
    if (!asset)
        return FZ_ERR_BAD_ID;
    partial = partial_mask(tr, reg, fx, mask);
    st = render_asset(project, fx, tr, reg, asset, mask, partial, scratch, scratch_cap, &baked);
    if (st != FZ_OK)
        return st;
    group = fz_project_undo_begin(project);
    fz_project_undo_mark(project, region, group);
    if (partial) {
        reg->wet_asset = baked;
        for (i = 0; i < FZ_FX_SLOTS; i++) {
            if (mask & (1 << i))
                reg->send_trim[i] = 0;
        }
    } else {
        reg->asset = baked;
        reg->wet_asset = fz_id_none();
        for (i = 0; i < FZ_FX_SLOTS; i++) {
            if (mask & (1 << i))
                reg->send_trim[i] = 0;
        }
    }
    return FZ_OK;
}

FzStatus fz_bake_pattern(FzProject *project, FzFxRack *fx, FzId track, FzId pattern, FzId region, int mask,
                         int16_t *scratch, uint32_t scratch_cap)
{
    FzTrack *tr = fz_track(project, track);
    FzRegion *reg = fz_region(project, region);
    FzPattern *pat;
    FzAsset *asset;
    FzId baked;
    int group;
    FzStatus st;
    if (!project || !fx || !scratch || !tr || !reg || pattern.generation == 0 || pattern.index >= FZ_MAX_PATTERNS)
        return FZ_ERR_ARGS;
    pat = &project->pattern[pattern.index];
    if (!pat->alive || pat->generation != pattern.generation)
        return FZ_ERR_BAD_ID;
    asset = fz_asset(project, reg->asset);
    if (!asset)
        return FZ_ERR_BAD_ID;
    st = render_asset(project, fx, tr, reg, asset, mask, 0, scratch, scratch_cap, &baked);
    if (st != FZ_OK)
        return st;
    group = fz_project_undo_begin(project);
    fz_project_undo_mark_pattern(project, (int)pattern.index, group);
    pat->baked = 1;
    pat->unsend_mask = (uint8_t)(mask & 0x0F);
    pat->baked_asset = baked;
    return FZ_OK;
}
