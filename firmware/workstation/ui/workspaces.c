/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../sequence/fm_actions.h"
#include "../storage/bake.h"
#include "fm_ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static const char *const commands[] = {"NEW INSTRUMENT",
                                       "REDO",
                                       "PANIC",
                                       "PICK STEP NOTE",
                                       "MUTE TRACK",
                                       "SOLO TRACK",
                                       "SONG INSERT",
                                       "SONG DELETE",
                                       "SONG DUPLICATE",
                                       "SONG MOVE UP",
                                       "SONG MOVE DOWN",
                                       "SONG TRACK MUTE",
                                       "CHAIN APPEND",
                                       "CHAIN PLAY",
                                       "CHAIN CLEAR",
                                       "COPY PATTERN",
                                       "PASTE PATTERN",
                                       "CLEAR PATTERN",
                                       "RECORD ARRANGEMENT",
                                       "FINISH ARRANGEMENT",
                                       "TAKE TIMING",
                                       "STEP DETAIL",
                                       "PATTERN SETTINGS",
                                       "LOAD PROJECT",
                                       "CAPTURE / OSCILLATOR",
                                       "UNBAKE SAMPLE",
                                       "CANCEL EMPTY TRACK",
                                       "RELEASE UNUSED FX"};
const char *fm_ui_command_label(unsigned i)
{
    return i < FM_UI_COMMAND_COUNT ? commands[i] : "UNKNOWN";
}
static FmTrack *track(FmUi *u)
{
    return fm_track(u->project, u->project->selected_track);
}
static FmPattern *pattern(FmUi *u)
{
    FmTrack *t = track(u);
    return t ? fm_pattern(u->project, t->patterns[t->selected_pattern]) : NULL;
}
static unsigned change(unsigned old, int d, unsigned max)
{
    int64_t n = (int64_t)old + d;
    return n < 0 ? 0 : n > max ? max : (unsigned)n;
}
static float bound(float x, float lo, float hi)
{
    return fminf(hi, fmaxf(lo, x));
}
static FmResult watch(FmUi *u, void *p, size_t n)
{
    FmResult r = fm_edit_begin(u->project);
    if (r == FM_OK)
        r = fm_edit_watch(u->project, p, n);
    if (r != FM_OK)
        fm_edit_cancel(u->project);
    return r;
}
void fm_ui_sync_view(FmUi *u)
{
    FmTrack *t = track(u);
    FmId id = t ? t->instrument : 0;
    FmInstrument *in = fm_instrument(u->project, id);
    if (u->screen == FM_SCREEN_EDIT && in && in->type == FM_SYNTH && u->source != 1 &&
        u->source != 2 && u->source != 4)
        u->source = 2;
    if (id == u->view_instrument)
    {
        if (!fm_ui_edit_tab_allowed(u, u->tab))
            u->tab = in && in->type == FM_SYNTH ? 6 : 0;
        if (u->rows[u->tab] >= fm_ui_edit_row_count(u, u->tab))
            u->rows[u->tab] = 0;
        return;
    }
    if (u->view_instrument)
    {
        unsigned slot = 0;
        while (slot < FM_OBJECT_LIMIT && u->views[slot].id &&
               u->views[slot].id != u->view_instrument)
            slot++;
        if (slot < FM_OBJECT_LIMIT)
        {
            u->views[slot].id = u->view_instrument;
            u->views[slot].tab = u->tab;
            u->views[slot].scope = u->scope;
            memcpy(u->views[slot].rows, u->rows, sizeof(u->rows));
        }
    }
    u->tab = u->scope = 0;
    memset(u->rows, 0, sizeof(u->rows));
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (u->views[i].id == id)
        {
            u->tab = u->views[i].tab;
            u->scope = u->views[i].scope;
            memcpy(u->rows, u->views[i].rows, sizeof(u->rows));
            break;
        }
    u->view_instrument = id;
    if (!fm_ui_edit_tab_allowed(u, u->tab))
        u->tab = in && in->type == FM_SYNTH ? 6 : 0;
}
static FmResult confirm_needed(FmUi *u, unsigned command, const char *notice)
{
    u->pending_action = (int)command;
    u->picker = FM_PICK_CONFIRM;
    snprintf(u->status, sizeof(u->status), "%s", notice);
    return FM_OK;
}
FmResult fm_ui_workspace_action(FmUi *u, unsigned command, bool confirmed)
{
    FmTrack *t = track(u);
    FmPattern *p = pattern(u);
    if (!t && command != 23)
        return FM_ERR_NOT_FOUND;
    if (command == 9)
        return u->song_row ? fm_sequence_song_move(u->sequence, (uint16_t)u->song_row,
                                                   (uint16_t)(u->song_row - 1))
                           : FM_ERR_RANGE;
    if (command == 10)
        return fm_sequence_song_move(u->sequence, (uint16_t)u->song_row,
                                     (uint16_t)(u->song_row + 1));
    if (command == 11)
    {
        if (u->song_row >= u->project->song_count)
            return FM_ERR_NOT_FOUND;
        FmSongEntry *e = &u->project->song[u->song_row];
        FmResult r = watch(u, e, sizeof(*e));
        if (r != FM_OK)
            return r;
        for (unsigned i = 0; i < e->count; i++)
            if (e->tracks[i] == t->id)
                e->muted[i] = !e->muted[i];
        return fm_edit_commit(u->project);
    }
    if (command == 12)
    {
        if (t->chain_count == 64)
            return FM_ERR_MEMORY;
        FmResult r = watch(u, t, sizeof(*t));
        if (r != FM_OK)
            return r;
        t->chain[t->chain_count++] = t->selected_pattern;
        return fm_edit_commit(u->project);
    }
    if (command == 13)
        return fm_sequence_chain(u->sequence, t->id, t->chain, t->chain_count);
    if (command == 14)
        return fm_sequence_chain(u->sequence, t->id, NULL, 0);
    if (command == 15)
    {
        if (!p)
            return FM_ERR_NOT_FOUND;
        u->clipboard_pattern = p->id;
        snprintf(u->status, sizeof(u->status), "PATTERN COPIED; SELECT DESTINATION");
        return FM_OK;
    }
    if (command == 16)
    {
        if (!p || !u->clipboard_pattern)
            return FM_ERR_NOT_FOUND;
        if (p->count && !confirmed)
            return confirm_needed(u, command, "REPLACE DESTINATION PATTERN?");
        return fm_pattern_copy(u->project, u->clipboard_pattern, p->id, confirmed);
    }
    if (command == 17)
    {
        if (!p)
            return FM_ERR_NOT_FOUND;
        if (!confirmed)
            return confirm_needed(u, command, "CLEAR SELECTED PATTERN? UNDO KEPT");
        return fm_pattern_clear(u->project, p->id);
    }
    if (command == 18 || command == 19)
    {
        if (command == 18 && u->project->song_count && !confirmed)
            return confirm_needed(u, command, "RECORD NEW SONG? OLD SONG KEPT");
        return fm_sequence_arrange(u->sequence, command == 18);
    }
    if (command >= 20 && command <= 22)
    {
        u->return_screen = command == 20   ? FM_SCREEN_HOME
                           : command == 21 ? FM_SCREEN_STEP
                                           : FM_SCREEN_PATTERNS;
        u->screen = command == 20   ? FM_SCREEN_TIMING
                    : command == 21 ? FM_SCREEN_STEP_DETAIL
                                    : FM_SCREEN_PATTERN_SETTINGS;
        u->options_row = 0;
        return FM_OK;
    }
    if (command == 23)
    {
        if (!confirmed)
            return confirm_needed(u, command, "REPLACE PROJECT FROM SAVED BACKUP?");
        return u->load ? u->load(u->user, u->project) : FM_ERR_UNSUPPORTED;
    }
    if (command == 24)
    {
        FmInstrument *in = fm_instrument(u->project, t->instrument);
        if (!in)
            return FM_ERR_NOT_FOUND;
        if (in->type != FM_SYNTH)
            return fm_ui_use_source(u, 0);
        u->screen = FM_SCREEN_SOURCE;
        u->source = 2;
        return FM_OK;
    }
    if (command == 25)
    {
        FmInstrument *in = fm_instrument(u->project, t->instrument);
        if (!in || !u->scope || !in->regions[u->scope - 1].id)
            return FM_ERR_ARGUMENT;
        return fm_unbake_sample(u->project, in->regions[u->scope - 1].id);
    }
    if (command == 26)
    {
        if (!confirmed)
            return confirm_needed(u, command, "REMOVE EMPTY TRACK? UNDO KEPT");
        FmResult r = fm_remove_empty_instrument(u->project, t->id);
        if (r == FM_OK)
            u->screen = FM_SCREEN_HOME;
        return r;
    }
    if (command == 27)
    {
        uint8_t released = 0;
        FmResult r = fm_engine_fx_reclaim_unused(u->engine, 15, &released);
        snprintf(u->status, sizeof(u->status), "RELEASED FX MASK %u / %s", released,
                 fm_result_string(r));
        return r;
    }
    return FM_ERR_UNSUPPORTED;
}
FmResult fm_ui_workspace_confirm(FmUi *u)
{
    FmPattern *p = pattern(u);
    if (u->screen == FM_SCREEN_STEP_DETAIL)
    {
        if (!p)
            return FM_ERR_NOT_FOUND;
        if (u->options_row == 2)
            return fm_step_lock(u->project, p->id, u->page * 16 + u->selected_step,
                                u->lock_parameter, u->lock_value);
        return fm_step_properties(u->project, p->id, u->page * 16 + u->selected_step, u->step_note,
                                  u->step_probability, u->step_micro);
    }
    if (u->screen == FM_SCREEN_TIMING)
        return fm_sequence_candidate(u->sequence, (uint8_t)u->options_row);
    if (u->screen == FM_SCREEN_LIBRARY)
    {
        FmTrack *t = track(u);
        FmInstrument *in = t ? fm_instrument(u->project, t->instrument) : NULL;
        if (!in || u->library_item >= u->project->asset_count)
            return FM_ERR_NOT_FOUND;
        FmAsset *a = NULL;
        unsigned n = 0;
        for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
            if (u->project->assets[i])
            {
                if (n++ == u->library_item)
                {
                    a = u->project->assets[i];
                    break;
                }
            }
        if (!a)
            return FM_ERR_NOT_FOUND;
        FmResult r = fm_edit_begin(u->project);
        if (r == FM_OK)
            r = fm_edit_watch(u->project, in, sizeof(*in));
        if (r != FM_OK)
        {
            fm_edit_cancel(u->project);
            return r;
        }
        FmId region = 0;
        FmRegion *replace = NULL;
        if (u->scope && in->regions[u->scope - 1].id)
            replace = &in->regions[u->scope - 1];
        else if (in->type == FM_SAMPLE)
            for (unsigned i = 0; i < 16; i++)
                if (in->regions[i].id && in->regions[i].root == u->step_note)
                    replace = &in->regions[i];
        if (replace)
        {
            if (replace->baked_mask & 0x80)
                r = FM_ERR_BUSY;
            else
            {
                replace->asset = a->id;
                replace->start = 0;
                replace->end = a->frames;
                replace->loop_enabled = false;
                replace->loop_count = 0;
                in->revision++;
                r = FM_OK;
            }
        }
        else
            r = fm_region_attach(u->project, in->id, a->id, u->step_note, -1, &region);
        if (r == FM_OK)
        {
            r = fm_edit_commit(u->project);
            u->screen = FM_SCREEN_HOME;
        }
        else
            fm_edit_cancel(u->project);
        return r;
    }
    return FM_OK;
}
bool fm_ui_workspace_encoder(FmUi *u, unsigned knob, int delta)
{
    if (u->screen < FM_SCREEN_STEP_DETAIL || u->screen > FM_SCREEN_LIBRARY)
        return false;
    FmResult r = FM_OK;
    FmPattern *p = pattern(u);
    if (u->screen == FM_SCREEN_LIBRARY)
    {
        if (!knob)
            u->library_item = change(u->library_item, delta,
                                     u->project->asset_count ? u->project->asset_count - 1 : 0);
        return true;
    }
    if (!knob)
    {
        unsigned count = u->screen == FM_SCREEN_STEP_DETAIL ? 3
                         : u->screen == FM_SCREEN_PATTERN_SETTINGS
                             ? 6
                             : fm_sequence_status(u->sequence).candidate_count;
        u->options_row = change(u->options_row, delta, count ? count - 1 : 0);
        return true;
    }
    if (u->screen == FM_SCREEN_STEP_DETAIL)
    {
        if (u->options_row == 0)
            u->step_probability = bound(u->step_probability + delta * .01f, 0, 1);
        if (u->options_row == 1)
            u->step_micro = fmax(-FM_PPQN / 8.0, fmin(FM_PPQN / 8.0, u->step_micro + delta));
        if (u->options_row == 2)
        {
            if (knob == 1)
                u->lock_parameter = (FmParameter)change(u->lock_parameter, delta, FM_PARAM_SEND4);
            else
                u->lock_value =
                    bound(u->lock_value + delta * .01f, u->lock_parameter == FM_PARAM_PAN ? -1 : 0,
                          u->lock_parameter == FM_PARAM_VOLUME ? 4 : 1);
        }
        return true;
    }
    if (u->screen == FM_SCREEN_PATTERN_SETTINGS && p)
    {
        if (p->frozen_asset)
            r = FM_ERR_BUSY;
        else if (u->options_row == 5)
        {
            r = watch(u, &u->project->bpm, sizeof(u->project->bpm));
            if (r == FM_OK)
            {
                if (u->project->clock_source != FM_CLOCK_INTERNAL)
                {
                    fm_edit_cancel(u->project);
                    r = FM_ERR_BUSY;
                }
                else
                {
                    u->project->bpm =
                        fmax(20, fmin(300, u->project->bpm + delta * (knob == 2 ? .1 : 1)));
                    r = fm_edit_commit(u->project);
                }
            }
        }
        else
        {
            r = watch(u, p, sizeof(*p));
            if (r == FM_OK)
            {
                if (u->options_row == 0)
                    p->length_ticks = fmax(
                        FM_PPQN / 4, fmin(FM_PPQN * 256, p->length_ticks + delta * FM_PPQN / 4));
                if (u->options_row == 1)
                    p->rate = fmax(.125, fmin(8, p->rate * pow(2, delta / 12.0)));
                if (u->options_row == 2)
                    p->swing = fmax(0, fmin(.49, p->swing + delta * .01));
                if (u->options_row == 3)
                    p->quantize = fmax(0, fmin(FM_PPQN, p->quantize + delta * FM_PPQN / 16));
                if (u->options_row == 4)
                {
                    double snap = FM_PPQN / 4.0;
                    for (unsigned i = 0; i < p->count; i++)
                        if (p->events[i].tick >= p->length_ticks)
                            p->length_ticks = ceil((p->events[i].tick + 1) / snap) * snap;
                }
                r = fm_edit_commit(u->project);
            }
        }
    }
    u->last_result = r;
    if (r != FM_OK)
        snprintf(u->status, sizeof(u->status), "%s", fm_result_string(r));
    return true;
}
FmResult fm_ui_capture_setting(FmUi *u, int delta)
{
    FmCaptureConfig before = u->capture_config, *c = &u->capture_config;
    switch (u->capture_row)
    {
    case 0:
        u->step_note = (uint8_t)change(u->step_note, delta, 127);
        break;
    case 1:
        c->source = (FmInputSource)change(c->source, delta, 2);
        break;
    case 2:
    {
        float db = 20 * log10f(c->threshold);
        c->threshold = powf(10, bound(db + delta, -72, 0) / 20);
        if (c->stop_threshold >= c->threshold)
            c->stop_threshold = c->threshold * .5f;
        break;
    }
    case 3:
    {
        float db = 20 * log10f(c->stop_threshold);
        c->stop_threshold = powf(10, bound(db + delta, -96, 20 * log10f(c->threshold) - 1) / 20);
        break;
    }
    case 4:
        c->silence_frames =
            (uint32_t)change(c->silence_frames, (int)((int64_t)delta * 36), FM_RATE * 4);
        break;
    case 5:
        c->pretrigger_frames = change(c->pretrigger_frames, delta * 36, FM_PRETRIGGER_MAX);
        break;
    case 6:
        c->maximum_frames = (uint32_t)fmax(
            FM_RATE / 10, fmin(FM_RATE * 16, c->maximum_frames + delta * (double)FM_RATE / 10));
        break;
    case 7:
        c->minimum_frames =
            (uint32_t)fmax(1, fmin(c->maximum_frames, c->minimum_frames + (double)delta * 36));
        break;
    case 8:
        c->manual = delta > 0;
        break;
    default:
        return FM_OK;
    }
    FmTrack *t = track(u);
    FmResult r = u->capture && t ? u->capture(u->user, 6, t->id, u->step_note, u->now_ms)
                                 : FM_ERR_UNSUPPORTED;
    if (r != FM_OK)
        u->capture_config = before;
    return r;
}
