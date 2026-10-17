/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* Selected-track performance. Volume and pan are the track mix state.
 * fz_performance_render is what turns the FZ INSPIRED filter on. */
#include "play.h"

static int any_solo(const FzProject *project)
{
    int i;
    for (i = 0; i < FZ_MAX_TRACKS; i++) {
        if (project->track[i].alive && project->track[i].solo)
            return 1;
    }
    return 0;
}

static void route_sends(FzEngine *engine, int slot, const FzProject *project, const FzTrack *tr, const FzRegion *region,
                        int wet)
{
    const FzPattern *pat = 0;
    uint8_t track_send[4];
    uint8_t trim[4];
    int i;
    if (tr->pattern.index < FZ_MAX_PATTERNS) {
        const FzPattern *cand = &project->pattern[tr->pattern.index];
        if (cand->alive && cand->generation == tr->pattern.generation)
            pat = cand;
    }
    for (i = 0; i < 4; i++) {
        int send = tr->send[i];
        if (send < 0)
            send = 0;
        if (send > 127)
            send = 127;
        track_send[i] = (uint8_t)send;
        trim[i] = region->send_trim[i];
        if (wet || (pat && pat->baked && (pat->unsend_mask & (uint8_t)(1u << i))))
            trim[i] = 0;
    }
    fz_engine_voice_send(engine, slot, track_send, trim);
}

static void configure_voice(FzEngine *engine, int slot, const FzTrack *tr, const FzRegion *region, int note)
{
    int cut = region->cut_offset;
    if (tr->key_track)
        cut += ((note - (int)region->root) * tr->key_track) / 12;
    fz_engine_voice_region(engine, slot, region->gain_q15, cut, region->res_offset, region->choke);
    if (region->amp_on)
        fz_engine_voice_amp(engine, slot, &region->amp);
    if (region->filt_on)
        fz_engine_voice_filter_env(engine, slot, &region->filt, region->env_amount);
}

static const int16_t *span_pcm(const FzAsset *asset, const FzRegion *region, uint32_t *frames)
{
    uint32_t start = region->start_frame;
    uint32_t end = region->end_frame ? region->end_frame : asset->frames;
    if (start >= asset->frames)
        return 0;
    if (end > asset->frames)
        end = asset->frames;
    if (end <= start)
        return 0;
    *frames = end - start;
    return asset->pcm + start;
}

int fz_performance_note(FzEngine *engine, FzProject *project, int note, int velocity, int down)
{
    FzTrack *tr;
    FzInstrument *inst;
    FzRegion *region;
    FzAsset *asset;
    FzId id;
    const int16_t *pcm;
    uint32_t frames;
    int slot;

    if (!engine || !project)
        return -1;
    if (!down) {
        fz_engine_note_off(engine, note);
        return 0;
    }
    if (project->selected_track < 0)
        return -1;
    id = fz_track_id(project, project->selected_track);
    tr = fz_track(project, id);
    if (!tr || tr->mute)
        return -1;
    if (any_solo(project) && !tr->solo)
        return -1;
    inst = fz_instrument(project, tr->instrument);
    if (!inst)
        return -1;
    region = fz_instrument_region_for_note(project, tr->instrument, note);
    if (!region)
        return -1;
    asset = fz_asset(project, region->asset);
    if (!asset || !asset->pcm)
        return -1;
    pcm = span_pcm(asset, region, &frames);
    if (!pcm)
        return -1;
    slot = fz_engine_note_on_pcm(engine, pcm, frames, asset->native_rate, region->root, note, region->tune_cents,
                                 velocity, tr->volume, tr->pan, region->one_shot, asset->cycle, asset->tag);
    if (slot < 0)
        return -1;
    if (region->reverse)
        fz_reader_set_reverse(&engine->voice[slot].reader, 1);
    route_sends(engine, slot, project, tr, region, 0);
    /* Section 0 is the voice loop when it sits inside this span. Later
     * sections and the crossfade are the fz_loop_sample preview. */
    if (region->start_frame == 0 && region->loop.count > 0) {
        uint32_t loop_end = region->loop.sec[0].end;
        uint32_t loop_start = region->loop.sec[0].start;
        if (loop_end > loop_start && loop_end <= frames)
            fz_reader_set_loop(&engine->voice[slot].reader, loop_start, loop_end);
    }
    configure_voice(engine, slot, tr, region, note);
    if (region->wet_asset.generation) {
        FzAsset *wet = fz_asset(project, region->wet_asset);
        int wet_slot;
        if (wet && wet->pcm) {
            wet_slot = fz_engine_note_on_pcm(engine, wet->pcm, wet->frames, wet->native_rate, region->root, note,
                                             region->tune_cents, velocity, tr->volume, tr->pan, 1, wet->cycle, wet->tag);
            if (wet_slot >= 0) {
                fz_engine_voice_region(engine, wet_slot, 32767, 0, 0, 0);
                engine->voice[wet_slot].no_send = 1;
                route_sends(engine, wet_slot, project, tr, region, 1);
            }
        }
    }
    return slot;
}

void fz_performance_render(FzEngine *engine, const FzProject *project, int16_t *interleaved_lr, uint32_t frames)
{
    const FzTrack *tr = 0;
    if (engine && project && project->selected_track >= 0 && project->selected_track < FZ_MAX_TRACKS &&
        project->track[project->selected_track].alive)
        tr = &project->track[project->selected_track];
    if (tr) {
        fz_engine_set_filter(engine, 1, tr->cutoff, tr->resonance, 0);
        fz_engine_set_lfo(engine, tr->lfo.rate, tr->lfo.depth, tr->lfo.shape, tr->lfo.dest, tr->lfo.retrigger);
    } else if (engine) {
        fz_engine_set_filter(engine, 0, 0, 0, 0);
        fz_engine_set_lfo(engine, 0, 0, 0, 0, 0);
    }
    fz_engine_render(engine, interleaved_lr, frames);
}

void fz_performance_render_sends(FzEngine *engine, const FzProject *project, struct FzFxRack *fx, int16_t *interleaved_lr,
                                 uint32_t frames)
{
    fz_engine_set_fx(engine, fx);
    fz_performance_render(engine, project, interleaved_lr, frames);
    fz_engine_set_fx(engine, 0);
}
