/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "capture.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

int16_t fz_amp_from_dbfs(int dbfs)
{
    double amp = 32767.0 * pow(10.0, (double)dbfs / 20.0);
    if (amp < 1.0)
        amp = 1.0;
    if (amp > 32767.0)
        amp = 32767.0;
    return (int16_t)(amp + 0.5);
}

int16_t fz_pcm_mono_sum(int16_t left, int16_t right)
{
    return (int16_t)(((int32_t)left + (int32_t)right) / 2);
}

static void trace(FzCapture *c, FzCapState state)
{
    c->state = state;
    if (c->trace_n < FZ_CAP_TRACE)
        c->trace[c->trace_n++] = state;
}

static void push_pre(FzCapture *c, int16_t s)
{
    uint32_t cap = c->pre_frames;
    if (cap > FZ_PRE_MAX)
        cap = FZ_PRE_MAX;
    if (cap == 0)
        return;
    c->pre[c->pre_write] = s;
    c->pre_write = (c->pre_write + 1u) % cap;
    if (c->pre_n < cap)
        c->pre_n++;
}

static void start_take_from_pre(FzCapture *c)
{
    uint32_t n = c->pre_n;
    uint32_t i;
    /* Reserve room for the triggering frame, including with no pre-trigger. */
    if (n >= c->take_cap)
        n = c->take_cap ? c->take_cap - 1u : 0;
    for (i = 0; i < n; i++)
        c->take[i] = c->pre[(c->pre_write + c->pre_frames - n + i) % c->pre_frames];
    c->take_frames = n;
    c->silence_run = 0;
    trace(c, FZ_CAP_CAPTURING);
}

static int level_of(int16_t s)
{
    int32_t a = fz_iabs32(s);
    return a > 32767 ? 32767 : (int)a;
}

static int next_empty_slot(FzCapture *c, int slot)
{
    FzInstrument *inst = fz_instrument(c->project, c->instrument);
    if (!inst) return FZ_MULTI_SLOTS;
    while (slot < FZ_MULTI_SLOTS) {
        int i, occupied = 0;
        for (i = 0; i < inst->region_count; i++) {
            FzRegion *region = fz_region(c->project, inst->region[i]);
            if (region && region->slot == slot) occupied = 1;
        }
        if (!occupied) break;
        slot++;
    }
    return slot;
}

void fz_capture_init(FzCapture *c, FzProject *project, FzId instrument, FzId track, int16_t *take,
                     uint32_t take_cap)
{
    memset(c, 0, sizeof *c);
    c->project = project;
    c->instrument = instrument;
    c->track = track;
    c->take = take;
    c->take_cap = take_cap;
    c->pre_frames = 360;
    c->start_amp = fz_amp_from_dbfs(-36);
    c->stop_amp = fz_amp_from_dbfs(-42);
    c->silence_hold = (FZ_ENGINE_RATE_HZ * 200u) / 1000u;
    c->tail_keep = (FZ_ENGINE_RATE_HZ * 10u) / 1000u;
    c->min_frames = 1;
    c->state = FZ_CAP_CONFIGURE;
    c->input_alive = 1;
    c->slot = next_empty_slot(c, 0);
    snprintf(c->message, sizeof c->message, "CONFIGURE");
}

void fz_capture_set_thresholds(FzCapture *c, int start_dbfs, int stop_dbfs, uint32_t pre_frames,
                               uint32_t silence_hold, uint32_t min_frames)
{
    c->start_amp = fz_amp_from_dbfs(start_dbfs);
    c->stop_amp = fz_amp_from_dbfs(stop_dbfs);
    c->pre_frames = pre_frames > FZ_PRE_MAX ? FZ_PRE_MAX : pre_frames;
    c->silence_hold = silence_hold;
    c->min_frames = min_frames;
    c->pre_n = 0;
    c->pre_write = 0;
}

void fz_capture_set_root(FzCapture *c, uint8_t root)
{
    c->root_set = 1;
    c->next_root = root;
}

void fz_capture_arm(FzCapture *c)
{
    FzTrack *track;
    if (!c) return;
    if (c->take_pinned) {
        snprintf(c->message, sizeof c->message, "VOLATILE TAKE PINNED");
        return;
    }
    track = fz_track(c->project, c->track);
    if (!fz_instrument(c->project, c->instrument) || !track ||
        track->instrument.index != c->instrument.index ||
        track->instrument.generation != c->instrument.generation) {
        c->auto_arm = 0; trace(c, FZ_CAP_CANCELLED);
        snprintf(c->message, sizeof c->message, "BAD HANDLE");
        return;
    }
    if (!c->take || !c->take_cap) {
        c->auto_arm = 0; trace(c, FZ_CAP_STORAGE_FULL);
        snprintf(c->message, sizeof c->message, "CAPTURE RAM FULL");
        return;
    }
    c->slot = next_empty_slot(c, c->slot);
    c->auto_arm = 1;
    c->pre_n = 0;
    c->pre_write = 0;
    c->take_frames = 0;
    c->silence_run = 0;
    c->volatile_take = 0;
    c->take_pinned = 0;
    if (c->slot >= FZ_MULTI_SLOTS) {
        trace(c, FZ_CAP_FINISHED);
        c->auto_arm = 0;
        snprintf(c->message, sizeof c->message, "FINISHED");
        return;
    }
    trace(c, FZ_CAP_WAIT_INPUT);
    snprintf(c->message, sizeof c->message, "WAIT %d", c->slot + 1);
}

static void append_take(FzCapture *c, int16_t s)
{
    if (c->take_frames >= c->take_cap) {
        trace(c, FZ_CAP_END_DETECTED);
        snprintf(c->message, sizeof c->message, "MAX LENGTH");
        return;
    }
    c->take[c->take_frames++] = s;
    if (level_of(s) < c->stop_amp)
        c->silence_run++;
    else
        c->silence_run = 0;
    if (c->take_frames == c->take_cap ||
        (c->silence_hold > 0 && c->silence_run >= c->silence_hold))
        trace(c, FZ_CAP_END_DETECTED);
}

void fz_capture_accept(FzCapture *c, const int16_t *mono, uint32_t frames)
{
    uint32_t i;
    if (!c || !mono || !c->input_alive)
        return;
    if (c->state != FZ_CAP_WAIT_INPUT && c->state != FZ_CAP_CAPTURING)
        return;
    for (i = 0; i < frames; i++) {
        int16_t s = mono[i];
        int lvl = level_of(s);
        if (lvl > c->peak)
            c->peak = (uint16_t)lvl;
        if (lvl >= 32767)
            c->clipped = 1, c->clip_count++;
        if (c->state == FZ_CAP_WAIT_INPUT) {
            if (lvl >= c->start_amp) {
                start_take_from_pre(c);
                append_take(c, s);
                if (c->state != FZ_CAP_CAPTURING) break;
            } else {
                push_pre(c, s);
            }
        } else if (c->state == FZ_CAP_CAPTURING) {
            append_take(c, s);
            if (c->state != FZ_CAP_CAPTURING)
                break;
        }
    }
}

static void trim_tail(FzCapture *c)
{
    uint32_t trim;
    if (c->silence_run == 0 || c->silence_hold == 0)
        return;
    if (c->silence_run <= c->tail_keep)
        return;
    trim = c->silence_run - c->tail_keep;
    if (trim < c->take_frames)
        c->take_frames -= trim;
}

static FzStatus commit_take(FzCapture *c)
{
    FzInstrument *inst;
    FzId asset, region;
    FzStatus st;
    uint8_t root;
    uint32_t mark;

    trace(c, FZ_CAP_FINALIZE);
    trim_tail(c);
    if (c->take_frames < c->min_frames || c->take_frames == 0) {
        c->take_frames = 0;
        c->pre_n = c->pre_write = c->silence_run = 0;
        snprintf(c->message, sizeof c->message, "TOO SHORT");
        return FZ_ERR_RANGE;
    }
    inst = fz_instrument(c->project, c->instrument);
    if (!inst)
        return FZ_ERR_BAD_ID;
    mark = c->project->pcm_used;
    trace(c, FZ_CAP_COMMIT);
    st = fz_project_add_pcm(c->project, c->take, c->take_frames, FZ_ENGINE_RATE_HZ, 0, &asset);
    if (st != FZ_OK) {
        trace(c, FZ_CAP_STORAGE_FULL);
        c->volatile_take = 1;
        c->take_pinned = 1;
        snprintf(c->message, sizeof c->message, "SAMPLE STORAGE FULL");
        return st;
    }
    if (c->root_set)
        root = c->next_root;
    else if (inst->kind == FZ_INST_MULTI)
        root = (uint8_t)(inst->base_note + c->slot);
    else
        root = (uint8_t)((60 + inst->anchor_pitch_class) & 127);
    st = fz_project_add_region(c->project, c->instrument, asset, root, c->slot, &region);
    if (st != FZ_OK) {
        FzAsset *failed = fz_asset(c->project, asset);
        if (failed) {
            failed->alive = 0;
            failed->generation++;
        }
        c->project->pcm_used = mark;
        trace(c, FZ_CAP_STORAGE_FULL);
        c->volatile_take = 1;
        c->take_pinned = 1;
        return st;
    }
    c->committed[c->slot] = 1;
    c->slot_region[c->slot] = region;
    c->slot_asset[c->slot] = asset;
    c->slot_watermark[c->slot] = mark;
    c->volatile_take = 0;
    c->take_pinned = 0;
    return FZ_OK;
}

static void advance_slot(FzCapture *c)
{
    trace(c, FZ_CAP_ADVANCE);
    c->slot = next_empty_slot(c, c->slot + 1);
    c->take_frames = 0;
    c->pre_n = 0;
    c->pre_write = 0;
    c->silence_run = 0;
    if (!c->auto_arm || c->slot >= FZ_MULTI_SLOTS) {
        c->auto_arm = 0;
        trace(c, FZ_CAP_FINISHED);
        snprintf(c->message, sizeof c->message, "FINISHED");
        return;
    }
    trace(c, FZ_CAP_WAIT_INPUT);
    snprintf(c->message, sizeof c->message, "WAIT %d", c->slot + 1);
}

FzStatus fz_capture_service(FzCapture *c)
{
    FzStatus st;
    if (!c)
        return FZ_ERR_ARGS;
    if (c->state != FZ_CAP_END_DETECTED)
        return FZ_OK;
    st = commit_take(c);
    if (st == FZ_ERR_RANGE) {
        if (c->auto_arm)
            trace(c, FZ_CAP_WAIT_INPUT);
        return FZ_OK;
    }
    if (st != FZ_OK)
        return st;
    advance_slot(c);
    return FZ_OK;
}

void fz_capture_oct_plus(FzCapture *c)
{
    if (!c)
        return;
    c->auto_arm = 0;
    if (c->state == FZ_CAP_CAPTURING || c->state == FZ_CAP_END_DETECTED) {
        if (c->state == FZ_CAP_CAPTURING)
            trace(c, FZ_CAP_END_DETECTED);
        if (commit_take(c) == FZ_OK)
            c->slot++;
    }
    c->take_frames = c->take_pinned ? c->take_frames : 0;
    trace(c, FZ_CAP_PERFORMANCE);
    snprintf(c->message, sizeof c->message, c->take_pinned ? "HOME / VOLATILE TAKE" : "HOME");
    if (c->project && c->track.index < FZ_MAX_TRACKS)
        c->project->selected_track = (int)c->track.index;
}

void fz_capture_undo_last(FzCapture *c)
{
    FzInstrument *inst;
    int slot;
    if (!c)
        return;
    slot = c->slot < FZ_MULTI_SLOTS ? c->slot - 1 : FZ_MULTI_SLOTS - 1;
    if (c->state == FZ_CAP_CAPTURING || c->state == FZ_CAP_END_DETECTED) {
        c->take_frames = c->silence_run = c->pre_n = c->pre_write = 0;
        trace(c, FZ_CAP_WAIT_INPUT);
        return;
    }
    while (slot >= 0 && !c->committed[slot]) slot--;
    if (slot < 0) return;
    inst = fz_instrument(c->project, c->instrument);
    if (inst) {
        int i;
        for (i = 0; i < inst->region_count; i++) {
            FzId id = inst->region[i];
            if (id.index == c->slot_region[slot].index &&
                id.generation == c->slot_region[slot].generation) {
                FzRegion *r = fz_region(c->project, id);
                if (r) {r->alive = 0; if (++r->generation == 0) r->generation = 1;}
                memmove(inst->region + i, inst->region + i + 1,
                        (size_t)(inst->region_count - i - 1) * sizeof inst->region[0]);
                inst->region[--inst->region_count] = fz_id_none();
                break;
            }
        }
        fz_project_recompute_zones(c->project, c->instrument);
    }
    /* Without an engine ownership/retirement fence, neither active voices nor
     * shared regions can be proved done with this immutable PCM. Retain it and
     * its asset descriptor; do not rewind a global arena behind newer assets. */
    c->committed[slot] = 0;
    c->slot = slot;
    c->auto_arm = 1;
    trace(c, FZ_CAP_WAIT_INPUT);
    snprintf(c->message, sizeof c->message, "UNDO %d", slot + 1);
}

void fz_capture_skip(FzCapture *c)
{
    if (!c || c->slot >= FZ_MULTI_SLOTS || c->take_pinned)
        return;
    c->take_frames = 0;
    advance_slot(c);
}

void fz_capture_pause(FzCapture *c)
{
    if (!c)
        return;
    c->auto_arm = 0;
    trace(c, FZ_CAP_PAUSED);
}

void fz_capture_input_lost(FzCapture *c)
{
    if (!c)
        return;
    c->input_alive = 0;
    c->auto_arm = 0;
    if ((c->state == FZ_CAP_CAPTURING || c->state == FZ_CAP_END_DETECTED) &&
        c->take_frames >= c->min_frames && commit_take(c) == FZ_OK) {
        c->slot = next_empty_slot(c, c->slot + 1);
        c->take_frames = 0;
    }
    trace(c, FZ_CAP_INPUT_LOST);
    snprintf(c->message, sizeof c->message, "INPUT LOST");
}

void fz_capture_cancel(FzCapture *c)
{
    if (!c)
        return;
    c->auto_arm = 0;
    c->take_frames = 0;
    c->take_pinned = 0;
    c->volatile_take = 0;
    c->pre_n = c->pre_write = c->silence_run = 0;
    trace(c, FZ_CAP_CANCELLED);
}

int fz_capture_oct_flash(const FzCapture *c)
{
    if (!c || !c->auto_arm)
        return 0;
    return c->state == FZ_CAP_WAIT_INPUT || c->state == FZ_CAP_CAPTURING || c->state == FZ_CAP_END_DETECTED;
}
