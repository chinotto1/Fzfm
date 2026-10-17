/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "edit.h"

#include "dsp/additive.h"
#include "dsp/mangle.h"

#include <stdio.h>
#include <string.h>

enum {
    F_NONE = 0,
    F_START,
    F_END,
    F_GAIN,
    F_REVERSE,
    F_TUNE,
    F_NUDGE,
    F_CUT,
    F_RES,
    F_SEND,
    F_ATTACK,
    F_DECAY,
    F_SUSTAIN,
    F_RELEASE,
    F_LOOP_START,
    F_LOOP_END,
    F_XFADE,
    F_REPEATS,
    F_ROOT,
    F_ENVAMT,
    F_CHOKE,
    F_LRATE,
    F_LDEPTH,
    F_LDEST,
    F_LSHAPE,
    F_LRETRIG,
    F_HARM,
    F_FATTACK,
    F_FDECAY,
    F_FSUSTAIN,
    F_FRELEASE,
    F_DRAW,
    F_NORM,
    F_SMOOTH
};

typedef struct {
    const char *name;
    int numeric;
    int field;
} FzRowDef;

static const char *k_tabs[FZ_TAB_COUNT] = {"SAMPLE", "LOOP", "MIX", "AMP", "FILTER", "MOD", "WAVE", "MAP", "RENDER", "DRAW"};

static const FzRowDef k_sample[] = {{"START", 1, F_START}, {"END", 1, F_END},     {"GAIN", 1, F_GAIN},
                                    {"REVERSE", 0, F_REVERSE}, {"TUNE", 1, F_TUNE}, {"NUDGE", 0, F_NUDGE}};
static const FzRowDef k_loop[] = {{"LOOP START", 1, F_LOOP_START},
                                  {"LOOP END", 1, F_LOOP_END},
                                  {"XFADE", 0, F_XFADE},
                                  {"REPEATS", 1, F_REPEATS}};
static const FzRowDef k_amp[] = {{"ATTACK", 1, F_ATTACK},
                                 {"DECAY", 1, F_DECAY},
                                 {"SUSTAIN", 1, F_SUSTAIN},
                                 {"RELEASE", 1, F_RELEASE},
                                 {"FLT ATK", 1, F_FATTACK},
                                 {"FLT DEC", 1, F_FDECAY},
                                 {"FLT SUS", 1, F_FSUSTAIN},
                                 {"FLT REL", 1, F_FRELEASE},
                                 {"FLT AMT", 1, F_ENVAMT}};
static const FzRowDef k_filter[] = {{"CUT OFFSET", 0, F_CUT}, {"RES OFFSET", 0, F_RES}};
static const FzRowDef k_mod[] = {{"RATE", 1, F_LRATE}, {"DEPTH", 1, F_LDEPTH}, {"TARGET", 0, F_LDEST},
                                 {"SHAPE", 0, F_LSHAPE}, {"RETRIG", 0, F_LRETRIG}};
static const FzRowDef k_map[] = {{"ROOT", 1, F_ROOT}, {"SEND", 0, F_SEND}, {"GAIN", 1, F_GAIN}, {"CHOKE", 0, F_CHOKE}};
static const FzRowDef k_mix[] = {{"MIX WRITE", 0, F_NONE}};
static const FzRowDef k_wave[] = {{"HARMONIC", 0, F_HARM}};
static const FzRowDef k_render[] = {{"BAKE", 0, F_NONE}};
static const FzRowDef k_draw[] = {{"POINT", 0, F_DRAW}, {"NORMALIZE", 0, F_NORM}, {"SMOOTH", 0, F_SMOOTH}};

static const int k_sample_tabs[] = {FZ_TAB_SAMPLE, FZ_TAB_LOOP, FZ_TAB_MIX, FZ_TAB_FILTER,
                                    FZ_TAB_WAVE,   FZ_TAB_MAP,  FZ_TAB_RENDER};
static const int k_cycle_tabs[] = {FZ_TAB_WAVE, FZ_TAB_DRAW, FZ_TAB_FILTER, FZ_TAB_MAP};

static const FzRowDef *rows_for(int tab, int *count)
{
    switch (tab) {
    case FZ_TAB_SAMPLE:
        *count = 6;
        return k_sample;
    case FZ_TAB_LOOP:
        *count = 4;
        return k_loop;
    case FZ_TAB_AMP:
        *count = 9;
        return k_amp;
    case FZ_TAB_FILTER:
        *count = 2;
        return k_filter;
    case FZ_TAB_MOD:
        *count = 5;
        return k_mod;
    case FZ_TAB_MAP:
        *count = 4;
        return k_map;
    case FZ_TAB_MIX:
        *count = 1;
        return k_mix;
    case FZ_TAB_WAVE:
        *count = 1;
        return k_wave;
    case FZ_TAB_RENDER:
        *count = 1;
        return k_render;
    case FZ_TAB_DRAW:
        *count = 3;
        return k_draw;
    default:
        *count = 1;
        return k_mix;
    }
}

int fz_edit_tab_on_cycle(int tab)
{
    int i;
    for (i = 0; i < (int)(sizeof k_cycle_tabs / sizeof k_cycle_tabs[0]); i++) {
        if (k_cycle_tabs[i] == tab)
            return 1;
    }
    return 0;
}

int fz_edit_next_tab(int cycle, int tab)
{
    const int *tabs = cycle ? k_cycle_tabs : k_sample_tabs;
    int n = cycle ? (int)(sizeof k_cycle_tabs / sizeof k_cycle_tabs[0])
                  : (int)(sizeof k_sample_tabs / sizeof k_sample_tabs[0]);
    int i;
    for (i = 0; i < n; i++) {
        if (tabs[i] == tab)
            return tabs[(i + 1) % n];
    }
    return tabs[0];
}

const char *fz_edit_tab_name(int tab)
{
    if (tab < 0 || tab >= FZ_TAB_COUNT)
        return "";
    return k_tabs[tab];
}

int fz_edit_row_count(int tab)
{
    int n = 0;
    rows_for(tab, &n);
    return n;
}

const char *fz_edit_row_name(int tab, int row)
{
    int n = 0;
    const FzRowDef *rows = rows_for(tab, &n);
    if (row < 0 || row >= n)
        return "";
    return rows[row].name;
}

int fz_edit_row_numeric(int tab, int row)
{
    int n = 0;
    const FzRowDef *rows = rows_for(tab, &n);
    if (row < 0 || row >= n)
        return 0;
    return rows[row].numeric;
}

const char *fz_edit_knob_label(int tab, int row, int knob)
{
    if (knob == 0)
        return "ROW";
    if (knob == 1)
        return fz_edit_row_name(tab, row);
    return "-";
}

static FzInstrument *selected_inst(const FzProject *project)
{
    if (!project || project->selected_track < 0 || project->selected_track >= FZ_MAX_TRACKS)
        return 0;
    if (!project->track[project->selected_track].alive)
        return 0;
    return fz_instrument((FzProject *)project, project->track[project->selected_track].instrument);
}

static const FzRegion *shown_region(const FzSession *session, const FzProject *project)
{
    const FzInstrument *it = selected_inst(project);
    int sel;
    if (!session || !it || it->region_count <= 0)
        return 0;
    sel = session->region_sel;
    if (sel < 0 || sel >= it->region_count)
        sel = 0;
    return fz_region((FzProject *)project, it->region[sel]);
}

static int shown_frames(const FzEnvelope *env, int stage, int fallback)
{
    if (env && env->n_stages >= 4)
        return (int)env->frames[stage];
    return fallback;
}

static int shown_sustain(const FzEnvelope *env)
{
    if (env && env->n_stages >= 4)
        return env->level[1];
    return 20000;
}

void fz_edit_shown_env(const FzSession *session, const FzProject *project, int filter, int *attack, int *decay,
                       int *sustain, int *release)
{
    const FzRegion *region = shown_region(session, project);
    const FzEnvelope *env = 0;
    if (region)
        env = filter ? &region->filt : &region->amp;
    if (attack)
        *attack = shown_frames(env, 0, 10);
    if (decay)
        *decay = shown_frames(env, 1, 200);
    if (sustain)
        *sustain = shown_sustain(env);
    if (release)
        *release = shown_frames(env, 3, 400);
}

static int draw_sample(const FzSession *session, const FzProject *project)
{
    const FzRegion *region = shown_region(session, project);
    const FzAsset *asset;
    int pt;
    if (!region)
        return 0;
    asset = fz_asset((FzProject *)project, region->asset);
    if (!asset || !asset->pcm || asset->frames == 0)
        return 0;
    pt = session && session->draw_pt > 0 ? session->draw_pt : 0;
    if ((uint32_t)pt >= asset->frames)
        pt = (int)asset->frames - 1;
    return asset->pcm[pt];
}

void fz_edit_row_value(const FzSession *session, const FzProject *project, int row, char *dst, uint32_t cap)
{
    int n = 0;
    const FzRowDef *rows;
    const FzRegion *region;
    const FzTrack *track = 0;
    int field;
    if (!dst || cap == 0)
        return;
    dst[0] = 0;
    if (!session)
        return;
    rows = rows_for(session->edit_tab, &n);
    if (row < 0 || row >= n)
        return;
    field = rows[row].field;
    region = shown_region(session, project);
    if (project && project->selected_track >= 0 && project->selected_track < FZ_MAX_TRACKS &&
        project->track[project->selected_track].alive)
        track = &project->track[project->selected_track];
    switch (field) {
    case F_ATTACK:
        snprintf(dst, cap, "%d", shown_frames(region ? &region->amp : 0, 0, 10));
        break;
    case F_DECAY:
        snprintf(dst, cap, "%d", shown_frames(region ? &region->amp : 0, 1, 200));
        break;
    case F_SUSTAIN:
        snprintf(dst, cap, "%d", shown_sustain(region ? &region->amp : 0));
        break;
    case F_RELEASE:
        snprintf(dst, cap, "%d", shown_frames(region ? &region->amp : 0, 3, 400));
        break;
    case F_FATTACK:
        snprintf(dst, cap, "%d", shown_frames(region ? &region->filt : 0, 0, 10));
        break;
    case F_FDECAY:
        snprintf(dst, cap, "%d", shown_frames(region ? &region->filt : 0, 1, 200));
        break;
    case F_FSUSTAIN:
        snprintf(dst, cap, "%d", shown_sustain(region ? &region->filt : 0));
        break;
    case F_FRELEASE:
        snprintf(dst, cap, "%d", shown_frames(region ? &region->filt : 0, 3, 400));
        break;
    case F_ENVAMT:
        snprintf(dst, cap, "%d", region ? (int)region->env_amount : 0);
        break;
    case F_LRATE:
        snprintf(dst, cap, "%d", track ? (int)track->lfo.rate : 0);
        break;
    case F_LDEPTH:
        snprintf(dst, cap, "%d", track ? (int)track->lfo.depth : 0);
        break;
    case F_LDEST:
        snprintf(dst, cap, "%s", fz_lfo_target_name(track ? (int)track->lfo.dest : 0));
        break;
    case F_LSHAPE:
        snprintf(dst, cap, "%s", fz_lfo_shape_name(track ? (int)track->lfo.shape : 0));
        break;
    case F_LRETRIG:
        snprintf(dst, cap, "%s", track && track->lfo.retrigger ? "ON" : "OFF");
        break;
    case F_DRAW:
        snprintf(dst, cap, "%d", draw_sample(session, project));
        break;
    default:
        break;
    }
}

void fz_edit_header(const FzSession *session, const FzProject *project, char *dst, uint32_t cap)
{
    const FzInstrument *it = selected_inst(project);
    const char *name = it ? it->name : "-";
    const char *label;
    if (!dst || cap == 0)
        return;
    if (!session) {
        dst[0] = 0;
        return;
    }
    if (session->edit_tab == FZ_TAB_AMP)
        label = "ENV";
    else if (session->edit_tab == FZ_TAB_MOD)
        label = "LFO";
    else if (session->edit_tab == FZ_TAB_DRAW)
        label = "DRAW";
    else
        label = fz_edit_tab_name(session->edit_tab);
    if (session->scope_all)
        snprintf(dst, cap, "%s / %s / ALL", name, label);
    else
        snprintf(dst, cap, "%s / %s / ONE %d", name, label, session->region_sel + 1);
}

int fz_edit_affected(const FzSession *session, const FzProject *project)
{
    const FzInstrument *it = selected_inst(project);
    if (!session || !it || it->region_count <= 0)
        return 0;
    if (session->scope_all)
        return it->region_count;
    if (session->region_sel < 0 || session->region_sel >= it->region_count)
        return 0;
    return 1;
}

static int collect(FzSession *session, FzProject *project, FzId *out, int max)
{
    FzInstrument *it = selected_inst(project);
    int i, n = 0;
    if (!session || !it)
        return 0;
    if (!session->scope_all) {
        if (session->region_sel < 0 || session->region_sel >= it->region_count || max < 1)
            return 0;
        out[0] = it->region[session->region_sel];
        return 1;
    }
    for (i = 0; i < it->region_count && n < max; i++)
        out[n++] = it->region[i];
    return n;
}

static int field_of(const FzSession *session)
{
    int n = 0;
    const FzRowDef *rows = rows_for(session->edit_tab, &n);
    int row = session->row[session->edit_tab];
    if (row < 0 || row >= n)
        return F_NONE;
    return rows[row].field;
}

static int clamp_span(int v, int max)
{
    if (v < 0)
        return 0;
    if (max > 0 && v >= max)
        return max - 1;
    return v;
}

static void ensure_gesture(FzSession *session, FzProject *project, FzId id)
{
    if (session->gesture == 0)
        session->gesture = fz_project_undo_begin(project);
    fz_project_undo_mark(project, id, session->gesture);
}

static int clamp_i(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static void ensure_amp(FzRegion *region)
{
    if (region->amp.n_stages < 4)
        fz_env_adsr(&region->amp, 10, 200, 20000, 400);
    region->amp_on = 1;
}

static void ensure_filt(FzRegion *region)
{
    if (region->filt.n_stages < 4)
        fz_env_adsr(&region->filt, 10, 200, 20000, 400);
    region->filt_on = 1;
}

static void set_amp_frames(FzRegion *region, int stage, int frames)
{
    ensure_amp(region);
    if (frames < 1)
        frames = 1;
    region->amp.frames[stage] = (uint32_t)frames;
}

static void set_filt_frames(FzRegion *region, int stage, int frames)
{
    ensure_filt(region);
    if (frames < 1)
        frames = 1;
    region->filt.frames[stage] = (uint32_t)frames;
}

static FzLoopSection *ensure_loop(FzRegion *region, uint32_t frames)
{
    FzLoopSection *s;
    if (region->loop.count < 1) {
        memset(&region->loop, 0, sizeof region->loop);
        region->loop.count = 1;
        region->loop.sec[0].next = 255;
        region->loop.sec[0].end = frames;
    }
    s = &region->loop.sec[0];
    if (frames > 0 && (s->end == 0 || s->end > frames))
        s->end = frames;
    if (s->end > 0 && s->start >= s->end)
        s->start = s->end - 1u;
    if (s->end > s->start && s->crossfade >= s->end - s->start)
        s->crossfade = (uint16_t)(s->end - s->start - 1u);
    return s;
}

static void apply_field(FzProject *project, FzRegion *region, int field, int detents, int absolute, int value)
{
    FzAsset *asset = fz_asset(project, region->asset);
    uint32_t frames = asset ? asset->frames : 0;
    if (absolute) {
        if (field == F_START)
            region->start_frame = (uint32_t)clamp_span(value, (int)frames);
        else if (field == F_END && frames > 0) {
            uint32_t end = (uint32_t)value;
            if (end > frames)
                end = frames;
            region->end_frame = end;
        } else if (field == F_GAIN)
            region->gain_q15 = (int16_t)clamp_i(value, 0, 32767);
        else if (field == F_TUNE)
            region->tune_cents = (int16_t)clamp_i(value, -1200, 1200);
        else if (field == F_ATTACK)
            set_amp_frames(region, 0, value);
        else if (field == F_DECAY)
            set_amp_frames(region, 1, value);
        else if (field == F_RELEASE)
            set_amp_frames(region, 3, value);
        else if (field == F_SUSTAIN) {
            int16_t level = (int16_t)clamp_i(value, 0, 32767);
            ensure_amp(region);
            region->amp.level[1] = level;
            region->amp.level[2] = level;
        } else if (field == F_FATTACK)
            set_filt_frames(region, 0, value);
        else if (field == F_FDECAY)
            set_filt_frames(region, 1, value);
        else if (field == F_FRELEASE)
            set_filt_frames(region, 3, value);
        else if (field == F_FSUSTAIN) {
            int16_t level = (int16_t)clamp_i(value, 0, 32767);
            ensure_filt(region);
            region->filt.level[1] = level;
            region->filt.level[2] = level;
        } else if (field == F_LOOP_START || field == F_LOOP_END || field == F_XFADE || field == F_REPEATS) {
            FzLoopSection *s = ensure_loop(region, frames);
            if (field == F_LOOP_START)
                s->start = (uint32_t)clamp_span(value, s->end > 0 ? (int)s->end : (int)frames);
            else if (field == F_LOOP_END && frames > 0) {
                uint32_t end = (uint32_t)clamp_i(value, 1, (int)frames);
                if (end <= s->start && s->start + 1u <= frames)
                    end = s->start + 1u;
                s->end = end;
            } else if (field == F_XFADE) {
                uint32_t span = s->end > s->start ? s->end - s->start : 0;
                s->crossfade = (uint16_t)clamp_i(value, 0, span > 0 ? (int)span - 1 : 0);
            } else
                s->repeats = (uint16_t)clamp_i(value, 0, 64);
        } else if (field == F_ROOT)
            region->root = (uint8_t)clamp_i(value, 0, 127);
        else if (field == F_ENVAMT)
            region->env_amount = (int16_t)clamp_i(value, -127, 127);
        else if (field == F_CHOKE)
            region->choke = (uint8_t)clamp_i(value, 0, 16);
        return;
    }
    if (field == F_GAIN)
        region->gain_q15 = (int16_t)clamp_i(region->gain_q15 + detents * 256, 0, 32767);
    else if (field == F_REVERSE)
        region->reverse = detents > 0 ? 1 : (detents < 0 ? 0 : region->reverse);
    else if (field == F_TUNE)
        region->tune_cents = (int16_t)clamp_i(region->tune_cents + detents, -1200, 1200);
    else if (field == F_NUDGE)
        region->start_frame = (uint32_t)clamp_span((int)region->start_frame + detents, (int)frames);
    else if (field == F_CUT)
        region->cut_offset = (int16_t)clamp_i(region->cut_offset + detents, -64, 64);
    else if (field == F_RES)
        region->res_offset = (int16_t)clamp_i(region->res_offset + detents, -64, 64);
    else if (field == F_SEND)
        region->send_trim[0] = (uint8_t)clamp_i((int)region->send_trim[0] + detents, 0, 127);
    else if (field == F_ATTACK)
        set_amp_frames(region, 0, (int)(region->amp.n_stages >= 4 ? region->amp.frames[0] : 10) + detents);
    else if (field == F_DECAY)
        set_amp_frames(region, 1, (int)(region->amp.n_stages >= 4 ? region->amp.frames[1] : 200) + detents);
    else if (field == F_RELEASE)
        set_amp_frames(region, 3, (int)(region->amp.n_stages >= 4 ? region->amp.frames[3] : 400) + detents);
    else if (field == F_SUSTAIN) {
        int level = region->amp.n_stages >= 4 ? region->amp.level[1] : 20000;
        level = clamp_i(level + detents * 256, 0, 32767);
        ensure_amp(region);
        region->amp.level[1] = (int16_t)level;
        region->amp.level[2] = (int16_t)level;
    } else if (field == F_FATTACK)
        set_filt_frames(region, 0, (int)(region->filt.n_stages >= 4 ? region->filt.frames[0] : 10) + detents);
    else if (field == F_FDECAY)
        set_filt_frames(region, 1, (int)(region->filt.n_stages >= 4 ? region->filt.frames[1] : 200) + detents);
    else if (field == F_FRELEASE)
        set_filt_frames(region, 3, (int)(region->filt.n_stages >= 4 ? region->filt.frames[3] : 400) + detents);
    else if (field == F_FSUSTAIN) {
        int level = region->filt.n_stages >= 4 ? region->filt.level[1] : 20000;
        level = clamp_i(level + detents * 256, 0, 32767);
        ensure_filt(region);
        region->filt.level[1] = (int16_t)level;
        region->filt.level[2] = (int16_t)level;
    } else if (field == F_XFADE || field == F_REPEATS || field == F_LOOP_START || field == F_LOOP_END) {
        FzLoopSection *s = ensure_loop(region, frames);
        if (field == F_XFADE) {
            uint32_t span = s->end > s->start ? s->end - s->start : 0;
            s->crossfade = (uint16_t)clamp_i((int)s->crossfade + detents, 0, span > 0 ? (int)span - 1 : 0);
        } else if (field == F_REPEATS)
            s->repeats = (uint16_t)clamp_i((int)s->repeats + detents, 0, 64);
        else if (field == F_LOOP_START)
            s->start = (uint32_t)clamp_span((int)s->start + detents, s->end > 0 ? (int)s->end : (int)frames);
        else if (frames > 0) {
            int end = (int)s->end + detents;
            if (end < (int)s->start + 1)
                end = (int)s->start + 1;
            if (end > (int)frames)
                end = (int)frames;
            s->end = (uint32_t)end;
        }
    } else if (field == F_ROOT)
        region->root = (uint8_t)clamp_i((int)region->root + detents, 0, 127);
    else if (field == F_ENVAMT)
        region->env_amount = (int16_t)clamp_i(region->env_amount + detents, -127, 127);
    else if (field == F_CHOKE)
        region->choke = (uint8_t)clamp_i((int)region->choke + detents, 0, 16);
}

static int frame_field(int field)
{
    return field == F_START || field == F_END || field == F_LOOP_START || field == F_LOOP_END;
}

static FzStatus edit_lfo(FzSession *session, FzProject *project, int detents, int absolute, int value)
{
    FzTrack *track;
    FzLfo *lfo;
    int field = field_of(session);
    if (!project || project->selected_track < 0 || project->selected_track >= FZ_MAX_TRACKS)
        return FZ_ERR_BAD_ID;
    track = &project->track[project->selected_track];
    if (!track->alive)
        return FZ_ERR_BAD_ID;
    lfo = &track->lfo;
    if (absolute) {
        if (field == F_LRATE)
            lfo->rate = (uint8_t)clamp_i(value, 0, 127);
        else if (field == F_LDEPTH)
            lfo->depth = (uint8_t)clamp_i(value, 0, 127);
        return FZ_OK;
    }
    if (field == F_LRATE)
        lfo->rate = (uint8_t)clamp_i((int)lfo->rate + detents, 0, 127);
    else if (field == F_LDEPTH)
        lfo->depth = (uint8_t)clamp_i((int)lfo->depth + detents, 0, 127);
    else if (field == F_LDEST)
        lfo->dest = (uint8_t)clamp_i((int)lfo->dest + (detents > 0 ? 1 : (detents < 0 ? -1 : 0)), 0, FZ_LFO_DEST_COUNT - 1);
    else if (field == F_LSHAPE)
        lfo->shape = (uint8_t)clamp_i((int)lfo->shape + (detents > 0 ? 1 : (detents < 0 ? -1 : 0)), 0,
                                      FZ_LFO_SHAPE_COUNT - 1);
    else if (field == F_LRETRIG)
        lfo->retrigger = detents > 0 ? 1 : (detents < 0 ? 0 : lfo->retrigger);
    return FZ_OK;
}

static FzStatus edit_lfo_target(FzProject *project, int detents)
{
    FzTrack *track;
    int dest;
    if (!project || project->selected_track < 0 || project->selected_track >= FZ_MAX_TRACKS)
        return FZ_ERR_BAD_ID;
    track = &project->track[project->selected_track];
    if (!track->alive)
        return FZ_ERR_BAD_ID;
    dest = (int)track->lfo.dest + (detents > 0 ? 1 : (detents < 0 ? -1 : 0));
    track->lfo.dest = (uint8_t)clamp_i(dest, 0, FZ_LFO_DEST_COUNT - 1);
    return FZ_OK;
}

static FzStatus edit_lfo_shape(FzProject *project, int detents)
{
    FzTrack *track;
    int shape;
    if (!project || project->selected_track < 0 || project->selected_track >= FZ_MAX_TRACKS)
        return FZ_ERR_BAD_ID;
    track = &project->track[project->selected_track];
    if (!track->alive)
        return FZ_ERR_BAD_ID;
    shape = (int)track->lfo.shape + (detents > 0 ? 1 : (detents < 0 ? -1 : 0));
    track->lfo.shape = (uint8_t)clamp_i(shape, 0, FZ_LFO_SHAPE_COUNT - 1);
    return FZ_OK;
}

static FzStatus install_cycle(FzProject *project, FzRegion *region, const int16_t *pcm, uint32_t frames)
{
    FzId asset;
    if (fz_project_overwrite_pcm(project, region->asset, pcm, frames) == FZ_OK)
        return FZ_OK;
    if (fz_project_add_pcm(project, pcm, frames, FZ_ENGINE_RATE_HZ, 1, &asset) != FZ_OK)
        return FZ_ERR_SAMPLE_STORAGE;
    region->asset = asset;
    region->start_frame = 0;
    region->end_frame = 0;
    return FZ_OK;
}

static FzStatus edit_harmonic(FzSession *session, FzProject *project, int knob, int detents)
{
    FzInstrument *inst;
    FzRegion *region;
    int16_t table[FZ_ADDITIVE_POINTS];
    int partial, level;
    if (!project || project->selected_track < 0 || project->selected_track >= FZ_MAX_TRACKS)
        return FZ_ERR_BAD_ID;
    inst = fz_instrument(project, project->track[project->selected_track].instrument);
    if (!inst || inst->region_count < 1)
        return FZ_ERR_BAD_ID;
    if (session->partial < 0)
        session->partial = 0;
    if (session->partial >= FZ_HARMONICS)
        session->partial = FZ_HARMONICS - 1;
    if (knob == 1) {
        session->partial = clamp_i(session->partial + detents, 0, FZ_HARMONICS - 1);
        return FZ_OK;
    }
    partial = session->partial;
    level = (int)inst->additive.level[partial] + detents * 8;
    inst->additive.level[partial] = (uint16_t)clamp_i(level, 0, 256);
    inst->additive.points = FZ_ADDITIVE_POINTS;
    if (fz_additive_render(&inst->additive, table, FZ_ADDITIVE_POINTS) != FZ_OK)
        return FZ_ERR_ARGS;
    region = fz_region(project, inst->region[0]);
    if (!region)
        return FZ_ERR_BAD_ID;
    return install_cycle(project, region, table, FZ_ADDITIVE_POINTS);
}

static FzStatus edit_draw(FzSession *session, FzProject *project, int knob, int detents)
{
    FzInstrument *inst;
    FzRegion *region;
    FzAsset *asset;
    int16_t table[FZ_ADDITIVE_POINTS];
    int field, pt, sample, frames;
    if (!project || project->selected_track < 0 || project->selected_track >= FZ_MAX_TRACKS)
        return FZ_ERR_BAD_ID;
    inst = fz_instrument(project, project->track[project->selected_track].instrument);
    if (!inst || inst->region_count < 1)
        return FZ_ERR_BAD_ID;
    region = fz_region(project, inst->region[0]);
    asset = region ? fz_asset(project, region->asset) : 0;
    if (!region || !asset || !asset->pcm || asset->frames < 2 || !asset->cycle)
        return FZ_ERR_STATE;
    if (asset->frames > FZ_ADDITIVE_POINTS) {
        snprintf(project->last_error, sizeof project->last_error, "TOO LONG");
        return FZ_ERR_RANGE;
    }
    frames = (int)asset->frames;
    if (session->draw_pt < 0)
        session->draw_pt = 0;
    if (session->draw_pt >= frames)
        session->draw_pt = frames - 1;
    field = field_of(session);
    if (field == F_DRAW && knob == 1) {
        session->draw_pt = clamp_i(session->draw_pt + detents * 8, 0, frames - 1);
        return FZ_OK;
    }
    memcpy(table, asset->pcm, (size_t)frames * sizeof(int16_t));
    pt = session->draw_pt;
    if (field == F_DRAW && knob == 2) {
        sample = clamp_i((int)table[pt] + detents * 1024, -32767, 32767);
        fz_draw_set(table, (uint32_t)frames, (uint32_t)pt, (int16_t)sample);
    } else if (field == F_NORM && knob == 1 && detents > 0)
        fz_draw_normalize(table, (uint32_t)frames);
    else if (field == F_SMOOTH && knob == 1 && detents > 0)
        fz_draw_smooth(table, (uint32_t)frames);
    else
        return FZ_OK;
    return install_cycle(project, region, table, (uint32_t)frames);
}

FzStatus fz_edit_knob(FzSession *session, FzProject *project, int knob, int detents)
{
    int field, n, i;
    FzId ids[FZ_MULTI_SLOTS];
    if (!session || !project)
        return FZ_ERR_ARGS;
    if (knob == 0) {
        int count = fz_edit_row_count(session->edit_tab);
        int row = session->row[session->edit_tab] + detents;
        if (count < 1)
            count = 1;
        if (row < 0)
            row = 0;
        if (row >= count)
            row = count - 1;
        session->row[session->edit_tab] = row;
        return FZ_OK;
    }
    if (session->edit_tab == FZ_TAB_MOD && knob == 3)
        return edit_lfo_shape(project, detents);
    if (knob < 1 || knob > 2 || detents == 0)
        return FZ_OK;
    if (session->edit_tab == FZ_TAB_MOD && knob == 2)
        return edit_lfo_target(project, detents);
    if (session->edit_tab == FZ_TAB_DRAW)
        return edit_draw(session, project, knob, detents);
    field = field_of(session);
    if (field == F_HARM)
        return edit_harmonic(session, project, knob, detents);
    if (knob != 1)
        return FZ_OK;
    if (field == F_NONE)
        return FZ_OK;
    if (field == F_LRATE || field == F_LDEPTH || field == F_LDEST || field == F_LSHAPE || field == F_LRETRIG)
        return edit_lfo(session, project, detents, 0, 0);
    if (frame_field(field) && session->scope_all) {
        snprintf(project->last_error, sizeof project->last_error, "ONE SAMPLE");
        return FZ_ERR_STATE;
    }
    n = collect(session, project, ids, FZ_MULTI_SLOTS);
    session->undo_kind = FZ_UNDO_REGION;
    for (i = 0; i < n; i++) {
        FzRegion *region = fz_region(project, ids[i]);
        if (!region)
            continue;
        ensure_gesture(session, project, ids[i]);
        apply_field(project, region, field, detents, 0, 0);
    }
    if (field == F_ROOT && n > 0)
        fz_project_recompute_zones(project, project->track[project->selected_track].instrument);
    return FZ_OK;
}

FzStatus fz_edit_apply_number(FzSession *session, FzProject *project, int value)
{
    int field, n, i;
    FzId ids[FZ_MULTI_SLOTS];
    if (!session || !project)
        return FZ_ERR_ARGS;
    field = field_of(session);
    if (field == F_LRATE || field == F_LDEPTH)
        return edit_lfo(session, project, 0, 1, value);
    if ((frame_field(field) || field == F_ROOT) && session->scope_all) {
        snprintf(project->last_error, sizeof project->last_error, "ONE SAMPLE");
        return FZ_ERR_STATE;
    }
    n = collect(session, project, ids, FZ_MULTI_SLOTS);
    session->undo_kind = FZ_UNDO_REGION;
    for (i = 0; i < n; i++) {
        FzRegion *region = fz_region(project, ids[i]);
        if (!region)
            continue;
        ensure_gesture(session, project, ids[i]);
        apply_field(project, region, field, 0, 1, value);
    }
    if (field == F_ROOT && n > 0)
        fz_project_recompute_zones(project, project->track[project->selected_track].instrument);
    return FZ_OK;
}

static FzAsset *region_asset(FzProject *project, FzId region, FzRegion **out_region)
{
    FzRegion *reg = fz_region(project, region);
    if (out_region)
        *out_region = reg;
    if (!reg)
        return 0;
    return fz_asset(project, reg->asset);
}

FzStatus fz_edit_mix(FzProject *project, FzId region_a, FzId region_b, int gain_a_q15, int gain_b_q15, int32_t offset,
                     int16_t *dst, uint32_t dst_cap, FzId *out_asset)
{
    FzAsset *a = region_asset(project, region_a, 0);
    FzAsset *b = region_asset(project, region_b, 0);
    uint32_t frames = 0;
    FzStatus st;
    if (!a || !b || !dst || !out_asset)
        return FZ_ERR_ARGS;
    st = fz_render_mix(a->pcm, a->frames, b->pcm, b->frames, gain_a_q15, gain_b_q15, offset, dst, dst_cap, &frames);
    if (st != FZ_OK)
        return st;
    return fz_project_store_pcm(project, dst, frames, 0, out_asset);
}

FzStatus fz_edit_cross(FzProject *project, FzId region_a, FzId region_b, uint32_t overlap, int16_t *dst,
                       uint32_t dst_cap, FzId *out_asset)
{
    FzAsset *a = region_asset(project, region_a, 0);
    FzAsset *b = region_asset(project, region_b, 0);
    uint32_t frames = 0;
    FzStatus st;
    if (!a || !b || !dst || !out_asset)
        return FZ_ERR_ARGS;
    st = fz_render_cross(a->pcm, a->frames, b->pcm, b->frames, overlap, dst, dst_cap, &frames);
    if (st != FZ_OK)
        return st;
    return fz_project_store_pcm(project, dst, frames, 0, out_asset);
}

FzStatus fz_edit_truncate(FzProject *project, FzId region, uint32_t start, uint32_t end)
{
    FzRegion *reg = 0;
    FzAsset *asset = region_asset(project, region, &reg);
    FzId neu;
    int group;
    uint32_t before;
    if (!project || !reg || !asset)
        return FZ_ERR_BAD_ID;
    if (end <= start || end > asset->frames)
        return FZ_ERR_RANGE;
    before = project->pcm_used;
    if (fz_project_add_pcm(project, asset->pcm + start, end - start, asset->native_rate, asset->cycle, &neu) != FZ_OK)
        return FZ_ERR_SAMPLE_STORAGE;
    group = fz_project_undo_begin(project);
    fz_project_undo_mark(project, region, group);
    reg->asset = neu;
    reg->start_frame = 0;
    reg->end_frame = 0;
    (void)before;
    return FZ_OK;
}

FzStatus fz_edit_reverse_write(FzProject *project, FzId region, int16_t *dst, uint32_t dst_cap)
{
    FzRegion *reg = 0;
    FzAsset *asset = region_asset(project, region, &reg);
    FzId neu;
    int group;
    if (!project || !reg || !asset || !dst)
        return FZ_ERR_ARGS;
    if (asset->frames > dst_cap)
        return FZ_ERR_RANGE;
    if (fz_render_reverse(asset->pcm, asset->frames, dst) != FZ_OK)
        return FZ_ERR_ARGS;
    if (fz_project_store_pcm(project, dst, asset->frames, asset->cycle, &neu) != FZ_OK)
        return FZ_ERR_SAMPLE_STORAGE;
    group = fz_project_undo_begin(project);
    fz_project_undo_mark(project, region, group);
    reg->asset = neu;
    reg->reverse = 0;
    return FZ_OK;
}

FzStatus fz_edit_cut_cycle(FzProject *project, FzId region, uint32_t start, uint32_t length, int16_t *dst, FzId *out_asset)
{
    FzAsset *asset = region_asset(project, region, 0);
    int16_t probe;
    if (!project || !asset || !dst || !out_asset)
        return FZ_ERR_ARGS;
    probe = asset->pcm[0];
    if (fz_cut_cycle(asset->pcm, asset->frames, start, length, dst) != FZ_OK)
        return FZ_ERR_RANGE;
    if (asset->pcm[0] != probe)
        return FZ_ERR_STATE;
    return fz_project_store_pcm(project, dst, length, 1, out_asset);
}

FzStatus fz_edit_preset(FzProject *project, FzId region, int square, int16_t *dst, uint32_t points)
{
    FzRegion *reg = 0;
    FzAsset *asset = region_asset(project, region, &reg);
    FzId neu;
    int group;
    if (!project || !reg || !asset || !dst || points == 0)
        return FZ_ERR_ARGS;
    if (square)
        fz_preset_square(dst, points);
    else
        fz_preset_sine(dst, points);
    if (fz_project_store_pcm(project, dst, points, 1, &neu) != FZ_OK)
        return FZ_ERR_SAMPLE_STORAGE;
    group = fz_project_undo_begin(project);
    fz_project_undo_mark(project, region, group);
    reg->asset = neu;
    return FZ_OK;
}
