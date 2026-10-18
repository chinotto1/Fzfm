/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "bake.h"
#include "../audio/fm_audio.h"
#include "../sequence/fm_sequence.h"
#include <math.h>
#include <string.h>

typedef struct
{
    FmProject project;
    FmEngine engine;
    FmSequencer *sequence;
    FmTrack *track;
    FmInstrument *instrument;
    FmPattern *pattern;
} Render;
static void render_destroy(Render *r)
{
    if (r->sequence)
        fm_sequence_destroy(r->sequence);
    fm_engine_destroy(&r->engine);
    fm_project_destroy(&r->project);
}
static FmResult render_init(Render *r, FmProject *p, FmTrack *t, FmInstrument *s,
                            FmPattern *pattern)
{
    memset(r, 0, sizeof(*r));
    FmBudget budget = p->budget;
    for (unsigned i = 0; i < FM_MEM_COUNT; i++)
    {
        if (budget.used[i] > budget.limit[i])
            return FM_ERR_MEMORY;
        budget.limit[i] -= budget.used[i];
    }
    FmResult result = fm_project_init(&r->project, &budget);
    if (result != FM_OK)
        return result;
    r->track = fm_alloc(&r->project, FM_MEM_METADATA, sizeof(FmTrack));
    r->instrument = fm_alloc(&r->project, FM_MEM_METADATA, sizeof(FmInstrument));
    if (!r->track || !r->instrument)
    {
        render_destroy(r);
        return FM_ERR_MEMORY;
    }
    *r->track = *t;
    *r->instrument = *s;
    r->track->mute = false;
    r->track->solo = false;
    r->project.tracks[0] = r->track;
    r->project.track_count = 1;
    r->project.instruments[0] = r->instrument;
    r->project.instrument_count = 1;
    r->project.next_id = p->next_id;
    r->project.bpm = p->bpm;
    r->project.tempo_established = true;
    r->project.beats_per_bar = p->beats_per_bar;
    r->project.clock_source = FM_CLOCK_INTERNAL;
    /* Borrow immutable PCM. project_destroy owns only registered allocations. */
    memcpy(r->project.assets, p->assets, sizeof(p->assets));
    r->project.asset_count = p->asset_count;
    memcpy(r->project.fx, p->fx, sizeof(p->fx));
    if (pattern)
    {
        r->pattern = fm_alloc(&r->project, FM_MEM_METADATA, sizeof(FmPattern));
        if (!r->pattern)
        {
            render_destroy(r);
            return FM_ERR_MEMORY;
        }
        *r->pattern = *pattern;
        r->pattern->frozen_asset = 0;
        r->pattern->frozen_sends = 0;
        memset(r->track->patterns, 0, sizeof(r->track->patterns));
        r->track->patterns[0] = r->pattern->id;
        r->track->selected_pattern = 0;
        r->track->playing_pattern = 0;
        r->track->queued_pattern = -1;
        r->project.patterns[0] = r->pattern;
        r->project.pattern_count = 1;
    }
    result = fm_engine_init(&r->engine, &r->project, 32);
    if (result != FM_OK)
    {
        render_destroy(r);
        return result;
    }
    if (pattern)
    {
        r->sequence = fm_sequence_create(&r->project, 16, 16);
        if (!r->sequence)
        {
            render_destroy(r);
            return FM_ERR_MEMORY;
        }
        fm_sequence_seed(r->sequence, 1);
        fm_sequence_start(r->sequence);
    }
    return FM_OK;
}
static void dispatch(void *user, const FmEvent *event, uint32_t offset)
{
    Render *r = user;
    FmEvent e = *event;
    e.frame = r->engine.frame + offset;
    (void)fm_engine_event(&r->engine, &e);
}
static uint8_t active_sends(FmProject *p, FmTrack *t, FmInstrument *s, FmPattern *pattern)
{
    uint8_t mask = 0;
    for (unsigned j = 0; j < FM_FX_SLOTS; j++)
    {
        bool send = t->sends[j] > 0;
        if (pattern)
            for (unsigned k = 0; k < pattern->count; k++)
                if (pattern->events[k].kind == FM_EVENT_CONTROL &&
                    pattern->events[k].parameter == FM_PARAM_SEND1 + j &&
                    pattern->events[k].value > 0)
                    send = true;
        if (send && p->fx[j].enabled)
            for (unsigned k = 0; k < FM_ANCHORS; k++)
                if (s->regions[k].id && !(s->regions[k].baked_mask & FM_BAKED_FINAL_PATH) &&
                    s->regions[k].sends[j] > 0)
                    mask |= (uint8_t)(1u << j);
    }
    return mask;
}
static FmResult options_valid(const FmBakeOptions *o)
{
    if (!o || o->sends > 15 || o->tail_frames > FM_RATE * 30u || o->warmup_loops > 8)
        return FM_ERR_ARGUMENT;
    if (o->cancelled)
        return FM_ERR_CANCELLED;
    if (!o->isolated_confirmed)
        return FM_ERR_UNSUPPORTED;
    return FM_OK;
}
static int16_t pcm16(float v)
{
    v = fmaxf(-1, fminf(1, v));
    return (int16_t)lrintf(v * 32767);
}
static FmResult render_frames(Render *r, int16_t *pcm, uint32_t frames, bool keep)
{
    float block[128 * 2];
    for (uint32_t at = 0; at < frames;)
    {
        uint32_t n = frames - at;
        if (n > 128)
            n = 128;
        if (r->sequence)
            fm_sequence_advance(r->sequence, n, dispatch, r);
        fm_engine_render(&r->engine, block, n);
        if (r->engine.rejected_events || r->engine.stolen_voices)
            return FM_ERR_BUSY;
        for (unsigned i = 0; i < n * 2; i++)
            if (!isfinite(block[i]))
                return FM_ERR_FORMAT;
            else if (keep)
                pcm[(size_t)at * 2 + i] = pcm16(block[i]);
        at += n;
    }
    return FM_OK;
}
FmResult fm_bake_samples(FmProject *p, FmEditScope scope, const FmBakeOptions *o)
{
    FmResult result = options_valid(o);
    if (result != FM_OK)
        return result;
    FmInstrument *s = p ? fm_instrument(p, scope.instrument) : NULL;
    if (!s)
        return FM_ERR_NOT_FOUND;
    FmTrack *t = NULL;
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (p->tracks[i] && p->tracks[i]->instrument == s->id)
        {
            if (t)
                return FM_ERR_UNSUPPORTED;
            t = p->tracks[i];
        }
    if (!t)
        return FM_ERR_NOT_FOUND;
    FmRegion *target[FM_ANCHORS];
    FmRegion replacement[FM_ANCHORS];
    FmId assets[FM_ANCHORS] = {0};
    unsigned count = 0;
    for (unsigned i = 0; i < FM_ANCHORS; i++)
        if (s->regions[i].id && (scope.all || s->regions[i].id == scope.region))
        {
            FmRegion *r = &s->regions[i];
            FmAsset *a = fm_asset(p, r->asset);
            if (!a || a->cycle || !r->one_shot || r->loop_enabled ||
                (r->baked_mask & FM_BAKED_FINAL_PATH))
                return FM_ERR_UNSUPPORTED;
            FmInstrument selected = *s;
            memset(selected.regions, 0, sizeof(selected.regions));
            selected.regions[0] = *r;
            selected.count = 1;
            uint8_t active = active_sends(p, t, &selected, NULL);
            if ((o->sends & active) != active)
                return FM_ERR_UNSUPPORTED;
            target[count++] = r;
        }
    if (!count)
        return FM_ERR_NOT_FOUND;
    FmBakeRecipe *recipes[FM_ANCHORS] = {0};
    bool dirty = p->dirty, vol = p->volatile_audio;
    for (unsigned i = 0; i < count; i++)
    {
        FmRegion *region = target[i];
        FmAsset *a = fm_asset(p, region->asset);
        double step = (double)a->rate / FM_RATE * exp2(region->tuning / 1200.0);
        double frames_double =
            (region->reverse ? floor((region->end - region->start - 1) / step) + 1
                             : ceil((region->end - region->start) / step)) +
            o->tail_frames;
        if (!isfinite(frames_double) || frames_double < 1 || frames_double > UINT32_MAX)
        {
            result = FM_ERR_RANGE;
            break;
        }
        uint32_t frames = (uint32_t)frames_double;
        size_t bytes = (size_t)frames * 4;
        if (SIZE_MAX / frames < 4)
        {
            result = FM_ERR_RANGE;
            break;
        }
        int16_t *pcm = fm_alloc(p, FM_MEM_AUDIO, bytes);
        if (!pcm)
        {
            result = FM_ERR_STORAGE;
            break;
        }
        Render *render = fm_alloc(p, FM_MEM_METADATA, sizeof(*render));
        result = render ? render_init(render, p, t, s, NULL) : FM_ERR_MEMORY;
        if (result == FM_OK)
        {
            memset(render->instrument->regions, 0, sizeof(render->instrument->regions));
            render->instrument->regions[0] = *region;
            render->instrument->regions[0].low = 0;
            render->instrument->regions[0].high = 127;
            render->instrument->count = 1;
            FmEvent e = {.track = t->id,
                         .kind = FM_EVENT_NOTE_ON,
                         .note = region->root,
                         .velocity = 127,
                         .probability = 1,
                         .identity = 1};
            result = fm_engine_event(&render->engine, &e);
            if (result == FM_OK)
                result = render_frames(render, pcm, frames, true);
            render_destroy(render);
        }
        if (render)
            fm_dealloc(p, FM_MEM_METADATA, render, sizeof(*render));
        if (result == FM_OK)
            result =
                fm_asset_create(p, pcm, frames, 2, FM_RATE, false, FM_SOURCE_RENDER, &assets[i]);
        fm_dealloc(p, FM_MEM_AUDIO, pcm, bytes);
        if (result != FM_OK)
            break;
        recipes[i] = fm_alloc(p, FM_MEM_METADATA, sizeof(FmBakeRecipe));
        if (!recipes[i])
        {
            result = FM_ERR_MEMORY;
            break;
        }
        recipes[i]->region = region->id;
        recipes[i]->cleared_sends = scope.all ? o->sends : 0;
        recipes[i]->original = *region;
        memcpy(recipes[i]->track_sends, t->sends, sizeof(t->sends));
        replacement[i] = *region;
        replacement[i].start = 0;
        replacement[i].end = frames;
        replacement[i].reverse = false;
        replacement[i].tuning = 0;
        replacement[i].loop_count = 0;
        replacement[i].loop_enabled = false;
        replacement[i].baked_mask = (uint8_t)(FM_BAKED_FINAL_PATH | o->sends);
        for (unsigned j = 0; j < FM_FX_SLOTS; j++)
            replacement[i].sends[j] = 0;
    }
    if (result == FM_OK)
    {
        result = fm_edit_begin(p);
        if (result == FM_OK)
        {
            result = fm_edit_watch(p, &s->revision, sizeof(s->revision));
            if (result == FM_OK)
                result = fm_edit_watch(p, p->bake_recipes, sizeof(p->bake_recipes));
            if (result == FM_OK)
                result = fm_edit_watch(p, &p->bake_count, sizeof(p->bake_count));
            if (result == FM_OK && scope.all)
                result = fm_edit_watch(p, t->sends, sizeof(t->sends));
            if (p->bake_count + count > FM_OBJECT_LIMIT)
                result = FM_ERR_MEMORY;
            for (unsigned i = 0; i < count && result == FM_OK; i++)
                result = fm_edit_watch(p, target[i], sizeof(*target[i]));
            if (result == FM_OK)
            {
                for (unsigned i = 0; i < count; i++)
                {
                    replacement[i].asset = assets[i];
                    *target[i] = replacement[i];
                    unsigned slot = 0;
                    while (slot < FM_OBJECT_LIMIT && p->bake_recipes[slot])
                        slot++;
                    p->bake_recipes[slot] = recipes[i];
                    p->bake_count++;
                }
                if (scope.all)
                    for (unsigned j = 0; j < 4; j++)
                        if (o->sends & (1u << j))
                            t->sends[j] = 0;
                s->revision++;
                result = fm_edit_commit(p);
            }
            if (result != FM_OK)
                fm_edit_cancel(p);
        }
    }
    if (result != FM_OK)
    {
        for (unsigned i = 0; i < count; i++)
        {
            if (assets[i])
                fm_asset_discard(p, assets[i]);
            if (recipes[i])
                fm_dealloc(p, FM_MEM_METADATA, recipes[i], sizeof(FmBakeRecipe));
        }
        p->dirty = dirty;
        p->volatile_audio = vol;
    }
    return result;
}
FmResult fm_bake_pattern(FmProject *p, FmId id, const FmBakeOptions *o)
{
    FmResult result = options_valid(o);
    if (result != FM_OK)
        return result;
    if (!o->warmup_loops)
        return FM_ERR_ARGUMENT;
    FmPattern *pattern = p ? fm_pattern(p, id) : NULL;
    FmTrack *t = pattern ? fm_track(p, pattern->track) : NULL;
    FmInstrument *s = t ? fm_instrument(p, t->instrument) : NULL;
    if (!s)
        return FM_ERR_NOT_FOUND;
    if (pattern->frozen_asset)
        return FM_ERR_BUSY;
    uint8_t active = active_sends(p, t, s, pattern);
    if ((o->sends & active) != active)
        return FM_ERR_UNSUPPORTED;
    double duration = pattern->length_ticks / FM_PPQN * 60 / p->bpm / pattern->rate * FM_RATE;
    if (!isfinite(duration) || duration < 2 || duration > UINT32_MAX ||
        duration * o->warmup_loops > UINT32_MAX)
        return FM_ERR_RANGE;
    uint32_t frames = (uint32_t)llround(duration);
    if (SIZE_MAX / frames < 4 || (uint64_t)frames * o->warmup_loops > UINT32_MAX)
        return FM_ERR_RANGE;
    size_t bytes = (size_t)frames * 4;
    int16_t *pcm = fm_alloc(p, FM_MEM_AUDIO, bytes);
    if (!pcm)
        return FM_ERR_STORAGE;
    Render *render = fm_alloc(p, FM_MEM_METADATA, sizeof(*render));
    result = render ? render_init(render, p, t, s, pattern) : FM_ERR_MEMORY;
    if (result == FM_OK)
    {
        result = render_frames(render, NULL, frames * o->warmup_loops, false);
        if (result == FM_OK)
            result = render_frames(render, pcm, frames, true);
        render_destroy(render);
    }
    if (render)
        fm_dealloc(p, FM_MEM_METADATA, render, sizeof(*render));
    FmId asset = 0;
    bool dirty = p->dirty, vol = p->volatile_audio;
    if (result == FM_OK)
        result = fm_asset_create(p, pcm, frames, 2, FM_RATE, false, FM_SOURCE_RENDER, &asset);
    fm_dealloc(p, FM_MEM_AUDIO, pcm, bytes);
    if (result != FM_OK)
        return result;
    result = fm_edit_begin(p);
    if (result == FM_OK)
    {
        result = fm_edit_watch(p, pattern, sizeof(*pattern));
        if (result == FM_OK)
        {
            pattern->frozen_asset = asset;
            pattern->frozen_bpm = p->bpm;
            pattern->frozen_sends = o->sends;
            result = fm_edit_commit(p);
        }
        if (result != FM_OK)
            fm_edit_cancel(p);
    }
    if (result != FM_OK)
    {
        fm_asset_discard(p, asset);
        p->dirty = dirty;
        p->volatile_audio = vol;
    }
    return result;
}
FmResult fm_unfreeze_pattern(FmProject *p, FmId id)
{
    FmPattern *pattern = p ? fm_pattern(p, id) : NULL;
    if (!pattern)
        return FM_ERR_NOT_FOUND;
    if (!pattern->frozen_asset)
        return FM_ERR_ARGUMENT;
    FmResult result = fm_edit_begin(p);
    if (result != FM_OK)
        return result;
    result = fm_edit_watch(p, pattern, sizeof(*pattern));
    if (result != FM_OK)
    {
        fm_edit_cancel(p);
        return result;
    }
    pattern->frozen_asset = 0;
    pattern->frozen_sends = 0;
    pattern->frozen_bpm = 0;
    return fm_edit_commit(p);
}

FmResult fm_unbake_sample(FmProject *p, FmId id)
{
    FmRegion *r = fm_region(p, id);
    if (!r)
        return FM_ERR_NOT_FOUND;
    unsigned slot = 0;
    while (slot < FM_OBJECT_LIMIT &&
           (!p->bake_recipes[slot] || p->bake_recipes[slot]->region != id))
        slot++;
    if (slot == FM_OBJECT_LIMIT)
        return FM_ERR_NOT_FOUND;
    FmBakeRecipe *recipe = p->bake_recipes[slot];
    FmResult result = fm_edit_begin(p);
    if (result == FM_OK)
        result = fm_edit_watch(p, r, sizeof(*r));
    if (result == FM_OK)
        result = fm_edit_watch(p, &p->bake_recipes[slot], sizeof(recipe));
    if (result == FM_OK)
        result = fm_edit_watch(p, &p->bake_count, sizeof(p->bake_count));
    FmTrack *t = NULL;
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (p->tracks[i])
        {
            FmInstrument *in = fm_instrument(p, p->tracks[i]->instrument);
            if (in)
                for (unsigned j = 0; j < 16; j++)
                    if (in->regions[j].id == id)
                        t = p->tracks[i];
        }
    if (result == FM_OK && t)
        result = fm_edit_watch(p, t->sends, sizeof(t->sends));
    if (result != FM_OK)
    {
        fm_edit_cancel(p);
        return result;
    }
    *r = recipe->original;
    if (t)
        for (unsigned j = 0; j < 4; j++)
            if (recipe->cleared_sends & (1u << j))
                t->sends[j] = recipe->track_sends[j];
    p->bake_recipes[slot] = NULL;
    p->bake_count--;
    return fm_edit_commit(p);
}
