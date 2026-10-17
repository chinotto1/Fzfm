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

uint8_t fz_sample_anchor_note(int slot, int pitch_class)
{
    int note;
    if (slot < 0)
        slot = 0;
    if (slot >= FZ_SAMPLE_ANCHORS)
        slot = FZ_SAMPLE_ANCHORS - 1;
    if (pitch_class < 0)
        pitch_class = 0;
    if (pitch_class > 11)
        pitch_class %= 12;
    note = 36 + slot * 12 + pitch_class;
    if (note > 127)
        note = 127;
    return (uint8_t)note;
}

void fz_note_name(char *dst, int cap, int note)
{
    static const char *pc[12] = {"C", "CS", "D", "DS", "E", "F", "FS", "G", "GS", "A", "AS", "B"};
    int oct;
    if (!dst || cap <= 0)
        return;
    if (note < 0)
        note = 0;
    if (note > 127)
        note = 127;
    oct = note / 12 - 1;
    snprintf(dst, (size_t)cap, "%s %d", pc[note % 12], oct);
}

int fz_capture_slot_limit(const FzCapture *c)
{
    FzInstrument *inst;
    if (!c || !c->project)
        return FZ_MULTI_SLOTS;
    inst = fz_instrument(c->project, c->instrument);
    if (inst && inst->kind == FZ_INST_SAMPLE)
        return FZ_SAMPLE_ANCHORS;
    return FZ_MULTI_SLOTS;
}

int fz_capture_next_note(const FzCapture *c)
{
    FzInstrument *inst = 0;
    int slot;
    int limit;
    if (!c)
        return 60;
    if (c->root_set)
        return c->next_root;
    slot = c->slot < 0 ? 0 : c->slot;
    limit = fz_capture_slot_limit(c);
    if (slot >= limit)
        slot = limit - 1;
    if (c->project)
        inst = fz_instrument(c->project, c->instrument);
    if (inst && inst->kind == FZ_INST_MULTI) {
        int n = (int)inst->base_note + slot;
        if (n > 127)
            n = 127;
        return n;
    }
    return fz_sample_anchor_note(slot, inst ? inst->anchor_pitch_class : 0);
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
    if (c->pre_n < cap) {
        c->pre[c->pre_n++] = s;
        return;
    }
    memmove(c->pre, c->pre + 1, (cap - 1u) * sizeof(int16_t));
    c->pre[cap - 1u] = s;
}

static void start_take_from_pre(FzCapture *c)
{
    uint32_t n = c->pre_n;
    if (n > c->take_cap)
        n = c->take_cap;
    if (n && c->take)
        memcpy(c->take, c->pre + (c->pre_n - n), n * sizeof(int16_t));
    c->take_frames = n;
    c->silence_run = 0;
    trace(c, FZ_CAP_CAPTURING);
    snprintf(c->message, sizeof c->message, "REC %d", c->slot + 1);
}

static int level_of(int16_t s)
{
    int32_t a = fz_iabs32(s);
    return a > 32767 ? 32767 : (int)a;
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
    c->slot = 0;
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
}

void fz_capture_set_root(FzCapture *c, uint8_t root)
{
    c->root_set = 1;
    c->next_root = root;
}

void fz_capture_punch(FzCapture *c)
{
    if (!c)
        return;
    if (c->state == FZ_CAP_WAIT_INPUT)
        start_take_from_pre(c);
    else if (c->state == FZ_CAP_CAPTURING && c->take_frames > 0)
        trace(c, FZ_CAP_END_DETECTED);
}

void fz_capture_arm(FzCapture *c)
{
    c->auto_arm = 1;
    c->pre_n = 0;
    c->take_frames = 0;
    c->silence_run = 0;
    c->volatile_take = 0;
    c->take_pinned = 0;
    if (c->slot >= fz_capture_slot_limit(c)) {
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
    if (c->silence_hold > 0 && c->silence_run >= c->silence_hold)
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
            push_pre(c, s);
            if (lvl >= c->start_amp)
                start_take_from_pre(c);
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
        root = fz_sample_anchor_note(c->slot, inst->anchor_pitch_class);
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
    c->slot++;
    c->take_frames = 0;
    c->pre_n = 0;
    c->silence_run = 0;
    if (!c->auto_arm || c->slot >= fz_capture_slot_limit(c)) {
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
    snprintf(c->message, sizeof c->message, "HOME");
    if (c->project && c->track.index < FZ_MAX_TRACKS)
        c->project->selected_track = (int)c->track.index;
}

void fz_capture_undo_last(FzCapture *c)
{
    FzInstrument *inst;
    int slot;
    if (!c)
        return;
    slot = c->slot - 1;
    if (c->state == FZ_CAP_CAPTURING || c->state == FZ_CAP_END_DETECTED) {
        c->take_frames = 0;
        c->silence_run = 0;
        c->pre_n = 0;
        trace(c, FZ_CAP_WAIT_INPUT);
        return;
    }
    if (slot < 0 || !c->committed[slot])
        return;
    inst = fz_instrument(c->project, c->instrument);
    if (inst && inst->region_count > 0) {
        FzId id = inst->region[inst->region_count - 1];
        FzRegion *r = fz_region(c->project, id);
        if (r) {
            r->alive = 0;
            r->generation++;
        }
        inst->region_count--;
    }
    {
        FzAsset *a = fz_asset(c->project, c->slot_asset[slot]);
        if (a) {
            a->alive = 0;
            a->generation++;
        }
    }
    c->project->pcm_used = c->slot_watermark[slot];
    c->committed[slot] = 0;
    c->slot = slot;
    c->auto_arm = 1;
    trace(c, FZ_CAP_WAIT_INPUT);
    snprintf(c->message, sizeof c->message, "UNDO %d", slot + 1);
}

void fz_capture_skip(FzCapture *c)
{
    if (!c || c->slot >= fz_capture_slot_limit(c))
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
    if (c->state == FZ_CAP_CAPTURING && c->take_frames >= c->min_frames)
        commit_take(c);
    trace(c, FZ_CAP_INPUT_LOST);
    snprintf(c->message, sizeof c->message, "INPUT LOST");
}

void fz_capture_cancel(FzCapture *c)
{
    if (!c)
        return;
    c->auto_arm = 0;
    c->take_frames = 0;
    trace(c, FZ_CAP_CANCELLED);
}

int fz_capture_oct_flash(const FzCapture *c)
{
    if (!c || !c->auto_arm)
        return 0;
    return c->state == FZ_CAP_WAIT_INPUT || c->state == FZ_CAP_CAPTURING || c->state == FZ_CAP_END_DETECTED;
}
