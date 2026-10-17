/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "project.h"

#include <stdio.h>
#include <string.h>

static void set_error(FzProject *p, const char *msg)
{
    snprintf(p->last_error, sizeof p->last_error, "%s", msg);
}

void fz_project_init(FzProject *p, int16_t *pcm_store, uint32_t pcm_capacity_frames)
{
    memset(p, 0, sizeof *p);
    p->pcm_store = pcm_store;
    p->pcm_capacity = pcm_capacity_frames;
    p->selected_track = -1;
    p->tempo_bpm = 120.0;
}

static FzId make_id(uint16_t index, uint16_t generation)
{
    FzId id;
    id.index = index;
    id.generation = generation;
    return id;
}

int fz_project_undo_begin(FzProject *p)
{
    if (!p)
        return 0;
    p->undo_serial++;
    if (p->undo_serial <= 0)
        p->undo_serial = 1;
    return p->undo_serial;
}

void fz_project_undo_mark(FzProject *p, FzId id, int group)
{
    FzRegion *region;
    int i;
    if (!p)
        return;
    region = fz_region(p, id);
    if (!region || p->undo_count >= 24)
        return;
    for (i = 0; i < p->undo_count; i++) {
        if (p->undo_item[i].valid && p->undo_item[i].group == group &&
            p->undo_item[i].id.index == id.index && p->undo_item[i].id.generation == id.generation)
            return;
    }
    p->undo_item[p->undo_count].valid = 1;
    p->undo_item[p->undo_count].group = group;
    p->undo_item[p->undo_count].is_pattern = 0;
    p->undo_item[p->undo_count].id = id;
    p->undo_item[p->undo_count].snap = *region;
    p->undo_count++;
    p->redo_count = 0;
}

void fz_project_undo_mark_pattern(FzProject *p, int pattern_index, int group)
{
    int i;
    if (!p || pattern_index < 0 || pattern_index >= FZ_MAX_PATTERNS)
        return;
    if (!p->pattern[pattern_index].alive || p->undo_count >= 24)
        return;
    for (i = 0; i < p->undo_count; i++) {
        if (p->undo_item[i].valid && p->undo_item[i].is_pattern && p->undo_item[i].group == group &&
            p->undo_item[i].pattern_index == pattern_index)
            return;
    }
    p->undo_item[p->undo_count].valid = 1;
    p->undo_item[p->undo_count].group = group;
    p->undo_item[p->undo_count].is_pattern = 1;
    p->undo_item[p->undo_count].id = fz_id_none();
    p->undo_item[p->undo_count].pattern_index = pattern_index;
    p->undo_item[p->undo_count].pattern_snap = p->pattern[pattern_index];
    p->undo_count++;
    p->redo_count = 0;
}

static int group_start(const FzUndoItem *row, int count, int group)
{
    int start = count - 1;
    while (start > 0 && row[start - 1].valid && row[start - 1].group == group)
        start--;
    return start;
}

FzStatus fz_project_undo(FzProject *p)
{
    int group, start, i;
    if (!p || p->undo_count <= 0)
        return FZ_ERR_STATE;
    group = p->undo_item[p->undo_count - 1].group;
    start = group_start(p->undo_item, p->undo_count, group);
    for (i = start; i < p->undo_count; i++) {
        FzUndoItem *item = &p->undo_item[i];
        if (p->redo_count < 24) {
            FzUndoItem *redo = &p->redo_item[p->redo_count];
            redo->valid = 1;
            redo->group = group;
            redo->is_pattern = item->is_pattern;
            redo->id = item->id;
            redo->pattern_index = item->pattern_index;
            if (item->is_pattern && item->pattern_index >= 0 && item->pattern_index < FZ_MAX_PATTERNS)
                redo->pattern_snap = p->pattern[item->pattern_index];
            else if (!item->is_pattern) {
                FzRegion *region = fz_region(p, item->id);
                if (region)
                    redo->snap = *region;
            }
            p->redo_count++;
        }
        if (item->is_pattern) {
            if (item->pattern_index >= 0 && item->pattern_index < FZ_MAX_PATTERNS)
                p->pattern[item->pattern_index] = item->pattern_snap;
        } else {
            FzRegion *region = fz_region(p, item->id);
            if (region)
                *region = item->snap;
        }
        item->valid = 0;
    }
    p->undo_count = start;
    return FZ_OK;
}

FzStatus fz_project_redo(FzProject *p)
{
    int group, start, i;
    if (!p || p->redo_count <= 0)
        return FZ_ERR_STATE;
    group = p->redo_item[p->redo_count - 1].group;
    start = group_start(p->redo_item, p->redo_count, group);
    for (i = start; i < p->redo_count; i++) {
        FzUndoItem *item = &p->redo_item[i];
        if (p->undo_count < 24) {
            FzUndoItem *undo = &p->undo_item[p->undo_count];
            undo->valid = 1;
            undo->group = group;
            undo->is_pattern = item->is_pattern;
            undo->id = item->id;
            undo->pattern_index = item->pattern_index;
            if (item->is_pattern && item->pattern_index >= 0 && item->pattern_index < FZ_MAX_PATTERNS)
                undo->pattern_snap = p->pattern[item->pattern_index];
            else if (!item->is_pattern) {
                FzRegion *region = fz_region(p, item->id);
                if (region)
                    undo->snap = *region;
            }
            p->undo_count++;
        }
        if (item->is_pattern) {
            if (item->pattern_index >= 0 && item->pattern_index < FZ_MAX_PATTERNS)
                p->pattern[item->pattern_index] = item->pattern_snap;
        } else {
            FzRegion *region = fz_region(p, item->id);
            if (region)
                *region = item->snap;
        }
        item->valid = 0;
    }
    p->redo_count = start;
    return FZ_OK;
}

uint32_t fz_pcm_checksum(const int16_t *pcm, uint32_t frames)
{
    uint32_t h = 2166136261u;
    uint32_t i;
    const uint8_t *b = (const uint8_t *)pcm;
    uint32_t n = frames * 2u;
    for (i = 0; i < n; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}

static int alloc_slot(int alive_query_n, int (*alive)(void *, int), void *ctx, int n)
{
    int i;
    (void)alive_query_n;
    for (i = 0; i < n; i++) {
        if (!alive(ctx, i))
            return i;
    }
    return -1;
}

static int inst_alive(void *ctx, int i) { return ((FzProject *)ctx)->instrument[i].alive; }
static int track_alive(void *ctx, int i) { return ((FzProject *)ctx)->track[i].alive; }
static int pat_alive(void *ctx, int i) { return ((FzProject *)ctx)->pattern[i].alive; }
static int reg_alive(void *ctx, int i) { return ((FzProject *)ctx)->region[i].alive; }
static int asset_alive(void *ctx, int i) { return ((FzProject *)ctx)->asset[i].alive; }

static int name_taken(const FzProject *p, const char *name)
{
    int i;
    for (i = 0; i < FZ_MAX_INSTRUMENTS; i++) {
        if (p->instrument[i].alive && strcmp(p->instrument[i].name, name) == 0)
            return 1;
    }
    return 0;
}

static void next_name(const FzProject *p, FzInstKind kind, char *dst, uint32_t n)
{
    const char *prefix = kind == FZ_INST_MULTI ? "DRUMS" : "SYNTH";
    char candidate[FZ_NAME_LEN];
    int number;
    /* Build the candidate aside from `dst`. The new instrument is already
     * alive, so writing the trial name into it would make it look taken. */
    for (number = 1; number < 1000; number++) {
        snprintf(candidate, sizeof candidate, "%s %d", prefix, number);
        if (!name_taken(p, candidate)) {
            snprintf(dst, n, "%s", candidate);
            return;
        }
    }
    snprintf(dst, n, "%s", prefix);
}

FzStatus fz_project_rename_category(FzProject *p, FzId inst, const char *category)
{
    FzInstrument *it = fz_instrument(p, inst);
    char candidate[FZ_NAME_LEN];
    int number;
    size_t len;
    if (!it || !category)
        return FZ_ERR_ARGS;
    len = strlen(category);
    if (len == 0 || len > 12)
        return FZ_ERR_ARGS;
    for (number = 1; number < 1000; number++) {
        snprintf(candidate, sizeof candidate, "%s %d", category, number);
        if (!name_taken(p, candidate) || strcmp(it->name, candidate) == 0) {
            snprintf(it->name, sizeof it->name, "%s", candidate);
            return FZ_OK;
        }
    }
    return FZ_ERR_FULL;
}

FzInstrument *fz_instrument(FzProject *p, FzId id)
{
    FzInstrument *it;
    if (!p || id.generation == 0 || id.index >= FZ_MAX_INSTRUMENTS)
        return 0;
    it = &p->instrument[id.index];
    if (!it->alive || it->generation != id.generation)
        return 0;
    return it;
}

FzTrack *fz_track(FzProject *p, FzId id)
{
    FzTrack *t;
    if (!p || id.generation == 0 || id.index >= FZ_MAX_TRACKS)
        return 0;
    t = &p->track[id.index];
    if (!t->alive || t->generation != id.generation)
        return 0;
    return t;
}

FzRegion *fz_region(FzProject *p, FzId id)
{
    FzRegion *r;
    if (!p || id.generation == 0 || id.index >= FZ_MAX_REGIONS)
        return 0;
    r = &p->region[id.index];
    if (!r->alive || r->generation != id.generation)
        return 0;
    return r;
}

FzAsset *fz_asset(FzProject *p, FzId id)
{
    FzAsset *a;
    if (!p || id.generation == 0 || id.index >= FZ_MAX_ASSETS)
        return 0;
    a = &p->asset[id.index];
    if (!a->alive || a->generation != id.generation)
        return 0;
    return a;
}

FzId fz_track_id(const FzProject *p, int index)
{
    if (!p || index < 0 || index >= FZ_MAX_TRACKS || !p->track[index].alive)
        return fz_id_none();
    return make_id((uint16_t)index, p->track[index].generation);
}

FzStatus fz_project_new_instrument(FzProject *p, FzInstKind kind, FzId *out_inst, FzId *out_track)
{
    int ii, ti, pi;
    FzInstrument *inst;
    FzTrack *tr;
    FzPattern *pat;

    if (!p || (kind != FZ_INST_MULTI && kind != FZ_INST_SAMPLE) || !out_inst || !out_track)
        return FZ_ERR_ARGS;
    ii = alloc_slot(0, inst_alive, p, FZ_MAX_INSTRUMENTS);
    ti = alloc_slot(0, track_alive, p, FZ_MAX_TRACKS);
    pi = alloc_slot(0, pat_alive, p, FZ_MAX_PATTERNS);
    if (ii < 0 || ti < 0 || pi < 0) {
        set_error(p, ii < 0 ? "INSTRUMENT CAPACITY FULL" : ti < 0 ? "TRACK CAPACITY FULL" : "PATTERN CAPACITY FULL");
        if (ii < 0)
            return FZ_ERR_FULL;
        if (ti < 0)
            return FZ_ERR_TRACK_CAPACITY;
        return FZ_ERR_PATTERN_CAPACITY;
    }

    inst = &p->instrument[ii];
    {
        uint16_t gen = (uint16_t)(inst->generation + 1u);
        if (gen == 0)
            gen = 1;
        memset(inst, 0, sizeof *inst);
        inst->generation = gen;
    }
    inst->alive = 1;
    inst->kind = kind;
    inst->base_note = 36;
    inst->anchor_pitch_class = 0;
    next_name(p, kind, inst->name, sizeof inst->name);

    pat = &p->pattern[pi];
    {
        uint16_t gen = (uint16_t)(pat->generation + 1u);
        if (gen == 0)
            gen = 1;
        memset(pat, 0, sizeof *pat);
        pat->generation = gen;
    }
    pat->alive = 1;
    pat->length_ticks = 16;

    tr = &p->track[ti];
    {
        uint16_t gen = (uint16_t)(tr->generation + 1u);
        if (gen == 0)
            gen = 1;
        memset(tr, 0, sizeof *tr);
        tr->generation = gen;
    }
    tr->alive = 1;
    tr->instrument = make_id((uint16_t)ii, inst->generation);
    tr->pattern = make_id((uint16_t)pi, pat->generation);
    tr->cutoff = 127;
    tr->resonance = 0;
    tr->volume = 100;
    tr->pan = 64;

    *out_inst = tr->instrument;
    *out_track = make_id((uint16_t)ti, tr->generation);
    p->selected_track = ti;
    return FZ_OK;
}

static void release_region_slot(FzProject *p, FzId id)
{
    FzRegion *r = fz_region(p, id);
    if (!r)
        return;
    r->alive = 0;
    r->generation++;
    if (r->generation == 0)
        r->generation = 1;
}

FzStatus fz_project_delete_instrument(FzProject *p, FzId inst)
{
    FzInstrument *it = fz_instrument(p, inst);
    uint16_t generation;
    int i;
    if (!it)
        return FZ_ERR_BAD_ID;
    generation = it->generation;
    for (i = 0; i < it->region_count; i++)
        release_region_slot(p, it->region[i]);
    it->alive = 0;
    it->generation++;
    for (i = 0; i < FZ_MAX_TRACKS; i++) {
        if (!p->track[i].alive)
            continue;
        if (p->track[i].instrument.index == inst.index &&
            p->track[i].instrument.generation == generation) {
            FzId pat = p->track[i].pattern;
            if (pat.generation && pat.index < FZ_MAX_PATTERNS &&
                p->pattern[pat.index].generation == pat.generation) {
                p->pattern[pat.index].alive = 0;
                p->pattern[pat.index].generation++;
            }
            p->track[i].alive = 0;
            p->track[i].generation++;
            if (p->selected_track == i)
                p->selected_track = -1;
        }
    }
    return FZ_OK;
}

int fz_project_asset_refs(const FzProject *p, FzId asset)
{
    int i, n = 0;
    for (i = 0; i < FZ_MAX_REGIONS; i++) {
        if (!p->region[i].alive)
            continue;
        if (p->region[i].asset.index == asset.index && p->region[i].asset.generation == asset.generation)
            n++;
    }
    return n;
}

FzStatus fz_project_add_pcm(FzProject *p, const int16_t *pcm, uint32_t frames, uint32_t native_rate, int cycle,
                            FzId *out_asset)
{
    int slot;
    FzAsset *a;
    if (!p || !pcm || frames == 0 || !out_asset)
        return FZ_ERR_ARGS;
    if (!p->pcm_store || p->pcm_used + frames > p->pcm_capacity) {
        set_error(p, "SAMPLE STORAGE FULL");
        return FZ_ERR_SAMPLE_STORAGE;
    }
    slot = alloc_slot(0, asset_alive, p, FZ_MAX_ASSETS);
    if (slot < 0) {
        set_error(p, "ASSET CAPACITY FULL");
        return FZ_ERR_ASSET_CAPACITY;
    }
    memmove(p->pcm_store + p->pcm_used, pcm, frames * sizeof(int16_t));
    a = &p->asset[slot];
    {
        uint16_t gen = (uint16_t)(a->generation + 1u);
        if (gen == 0)
            gen = 1;
        memset(a, 0, sizeof *a);
        a->generation = gen;
    }
    a->alive = 1;
    a->pcm = p->pcm_store + p->pcm_used;
    a->frames = frames;
    a->native_rate = native_rate ? native_rate : FZ_ENGINE_RATE_HZ;
    a->cycle = cycle ? 1 : 0;
    a->checksum = fz_pcm_checksum(a->pcm, frames);
    p->asset_tags++;
    if (p->asset_tags == 0)
        p->asset_tags = 1;
    a->tag = p->asset_tags;
    p->pcm_used += frames;
    *out_asset = make_id((uint16_t)slot, a->generation);
    return FZ_OK;
}

FzStatus fz_project_add_region(FzProject *p, FzId inst, FzId asset, uint8_t root, int slot, FzId *out_region)
{
    FzInstrument *it = fz_instrument(p, inst);
    FzAsset *a = fz_asset(p, asset);
    int ri;
    FzRegion *r;
    if (!it || !a || !out_region)
        return FZ_ERR_BAD_ID;
    if (it->region_count >= FZ_MULTI_SLOTS)
        return FZ_ERR_REGION_CAPACITY;
    ri = alloc_slot(0, reg_alive, p, FZ_MAX_REGIONS);
    if (ri < 0)
        return FZ_ERR_REGION_CAPACITY;
    r = &p->region[ri];
    {
        uint16_t gen = (uint16_t)(r->generation + 1u);
        if (gen == 0)
            gen = 1;
        memset(r, 0, sizeof *r);
        r->generation = gen;
    }
    r->alive = 1;
    r->asset = asset;
    r->root = root;
    r->one_shot = it->kind == FZ_INST_MULTI ? 1 : 0;
    r->slot = (uint8_t)slot;
    r->gain_q15 = 32767;
    r->send_trim[0] = r->send_trim[1] = r->send_trim[2] = r->send_trim[3] = 127;
    if (it->kind == FZ_INST_MULTI) {
        r->key_low = root;
        r->key_high = root;
    }
    it->region[it->region_count++] = make_id((uint16_t)ri, r->generation);
    *out_region = it->region[it->region_count - 1];
    if (it->kind == FZ_INST_SAMPLE)
        fz_project_recompute_zones(p, inst);
    return FZ_OK;
}

int fz_zone_root_for_note(const uint8_t *roots, int n_roots, int note)
{
    int i, best = -1, best_dist = 1000;
    if (!roots || n_roots <= 0 || note < 0 || note > 127)
        return -1;
    for (i = 0; i < n_roots; i++) {
        int dist = note - (int)roots[i];
        if (dist < 0)
            dist = -dist;
        if (dist < best_dist || (dist == best_dist && roots[i] < (best >= 0 ? roots[best] : 255))) {
            best = i;
            best_dist = dist;
        }
    }
    return best;
}

void fz_project_recompute_zones(FzProject *p, FzId inst)
{
    FzInstrument *it = fz_instrument(p, inst);
    uint8_t roots[FZ_MULTI_SLOTS];
    int root_i[FZ_MULTI_SLOTS];
    int n = 0;
    int note, i;
    int assigned[128];

    if (!it || it->kind != FZ_INST_SAMPLE)
        return;
    memset(assigned, 0, sizeof assigned);
    for (i = 0; i < it->region_count; i++) {
        FzRegion *r = fz_region(p, it->region[i]);
        int k;
        if (!r)
            continue;
        if (r->manual_zone) {
            for (k = r->key_low; k <= r->key_high && k < 128; k++)
                assigned[k] = 1;
            continue;
        }
        roots[n] = r->root;
        root_i[n] = i;
        n++;
        r->key_low = 255;
        r->key_high = 0;
    }
    for (note = 0; note < 128; note++) {
        int pick, ri;
        FzRegion *r;
        if (assigned[note] || n == 0)
            continue;
        pick = fz_zone_root_for_note(roots, n, note);
        if (pick < 0)
            continue;
        ri = root_i[pick];
        r = fz_region(p, it->region[ri]);
        if (!r)
            continue;
        if (r->key_low == 255 || note < r->key_low)
            r->key_low = (uint8_t)note;
        if (note > r->key_high || r->key_high == 0)
            r->key_high = (uint8_t)note;
    }
}

FzRegion *fz_instrument_region_for_note(FzProject *p, FzId inst, int note)
{
    FzInstrument *it = fz_instrument(p, inst);
    int i;
    if (!it || note < 0 || note > 127)
        return 0;
    for (i = 0; i < it->region_count; i++) {
        FzRegion *r = fz_region(p, it->region[i]);
        if (!r)
            continue;
        if (note >= r->key_low && note <= r->key_high)
            return r;
    }
    return 0;
}

static int clamp7(int v)
{
    if (v < 0)
        return 0;
    if (v > 127)
        return 127;
    return v;
}

static FzTrack *selected(FzProject *p)
{
    if (!p || p->selected_track < 0 || p->selected_track >= FZ_MAX_TRACKS)
        return 0;
    if (!p->track[p->selected_track].alive)
        return 0;
    return &p->track[p->selected_track];
}

void fz_track_set_volume(FzProject *p, FzId track, int volume)
{
    FzTrack *t = fz_track(p, track);
    if (t)
        t->volume = (int16_t)clamp7(volume);
}

void fz_track_set_pan(FzProject *p, FzId track, int pan)
{
    FzTrack *t = fz_track(p, track);
    if (t)
        t->pan = (int16_t)clamp7(pan);
}

void fz_track_set_cutoff(FzProject *p, FzId track, int cutoff)
{
    FzTrack *t = fz_track(p, track);
    if (t)
        t->cutoff = (int16_t)clamp7(cutoff);
}

void fz_track_set_resonance(FzProject *p, FzId track, int resonance)
{
    FzTrack *t = fz_track(p, track);
    if (t)
        t->resonance = (int16_t)clamp7(resonance);
}

void fz_home_turn(FzProject *p, int knob, int detents)
{
    FzTrack *t = selected(p);
    if (!t)
        return;
    if (knob == 0)
        t->cutoff = (int16_t)clamp7(t->cutoff + detents);
    else if (knob == 1)
        t->resonance = (int16_t)clamp7(t->resonance + detents);
    else if (knob == 2)
        t->volume = (int16_t)clamp7(t->volume + detents);
    else if (knob == 3)
        t->pan = (int16_t)clamp7(t->pan + detents);
}

void fz_home_select_track(FzProject *p, int detents)
{
    int n = 0, i, cur, idx = 0;
    int map[FZ_MAX_TRACKS];
    if (!p)
        return;
    for (i = 0; i < FZ_MAX_TRACKS; i++) {
        if (p->track[i].alive)
            map[n++] = i;
    }
    if (n == 0) {
        p->selected_track = -1;
        return;
    }
    cur = 0;
    for (i = 0; i < n; i++) {
        if (map[i] == p->selected_track)
            cur = i;
    }
    idx = cur + detents;
    if (idx < 0)
        idx = 0;
    if (idx >= n)
        idx = n - 1;
    p->selected_track = map[idx];
}
