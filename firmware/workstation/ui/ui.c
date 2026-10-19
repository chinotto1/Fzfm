/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../dsp/fm_synthesis.h"
#include "../sampler/edit.h"
#include "../sequence/fm_actions.h"
#include "../storage/bake.h"
#include "fm_ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const uint8_t fm_ui_white_keys[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const unsigned row_counts[] = {10, 9, 9, 9, 12, 5, 8, 12, 4};
static float bound(float x, float lo, float hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}
static unsigned index_delta(unsigned v, int delta, unsigned max)
{
    int64_t n = (int64_t)v + delta;
    return n < 0 ? 0 : n > (int64_t)max ? max : (unsigned)n;
}
static FmTrack *track(FmUi *u)
{
    return fm_track(u->project, u->project->selected_track);
}
static FmInstrument *instrument(FmUi *u)
{
    FmTrack *t = track(u);
    return t ? fm_instrument(u->project, t->instrument) : NULL;
}
bool fm_ui_edit_tab_allowed(FmUi *u, unsigned tab)
{
    FmInstrument *in = instrument(u);
    return tab < 9 && (!in || (in->type == FM_SYNTH ? tab == 6 || tab == 3 || tab == 4 || tab == 5
                                                    : tab != 6));
}
unsigned fm_ui_edit_row_count(FmUi *u, unsigned tab)
{
    FmInstrument *in = instrument(u);
    return tab >= 9 ? 0 : tab == 6 && in && in->type == FM_SYNTH ? 7 : row_counts[tab];
}
static FmRegion *region(FmUi *u)
{
    FmInstrument *in = instrument(u);
    if (!in)
        return NULL;
    if (u->scope && in->regions[u->scope - 1].id)
        return &in->regions[u->scope - 1];
    for (unsigned i = 0; i < FM_ANCHORS; i++)
        if (in->regions[i].id)
            return &in->regions[i];
    return NULL;
}
static void result(FmUi *u, FmResult r)
{
    u->last_result = r;
    snprintf(u->status, sizeof(u->status), "%s", r == FM_OK ? "" : fm_result_string(r));
}
static FmFrame audio_time(uint64_t ms)
{
    return ms > UINT64_MAX / (FM_RATE / 1000u) ? UINT64_MAX : ms * (FM_RATE / 1000u);
}
void fm_ui_sequence_dispatch(void *engine, const FmEvent *event, uint32_t offset)
{
    (void)offset;
    (void)fm_engine_event(engine, event);
}
const char *fm_ui_screen_name(FmScreen s)
{
    static const char *const names[] = {
        "HOME",        "EDIT",        "MIXER",         "FX",          "STEP",
        "PATTERNS",    "SONG",        "CAPTURE",       "COMMANDS",    "NEW INSTRUMENT",
        "SOURCE",      "STEP DETAIL", "PATTERN SETUP", "TAKE TIMING", "ASSET LIBRARY",
        "ARPEGGIATOR", "PIANO ROLL"};
    return (unsigned)s < sizeof(names) / sizeof(names[0]) ? names[s] : "INVALID";
}
static void frozen_dispatch(void *engine, FmId track_id, FmId pattern_id, FmFrame frame,
                            uint32_t identity, uint32_t offset)
{
    (void)offset;
    (void)fm_engine_frozen_event(engine, track_id, pattern_id, frame, identity);
}
void fm_ui_init(FmUi *u, FmProject *p, FmEngine *e, FmSequencer *s)
{
    memset(u, 0, sizeof(*u));
    u->project = p;
    u->engine = e;
    u->sequence = s;
    u->screen = FM_SCREEN_HOME;
    u->step_note = 60;
    u->step_velocity = 100;
    u->step_length = FM_PPQN / 4;
    u->mix_gain_a = u->mix_gain_b = 1;
    u->mix_overlap = 64;
    u->last_result = FM_OK;
    u->pending_action = -1;
    u->capture_config = fm_capture_defaults();
    u->wave_points = 1024;
    u->wave_shape = FM_WAVE_SAW;
    u->step_probability = 1;
    snprintf(u->status, sizeof(u->status), "%s",
             p && p->track_count ? "HOLD HOME: COMMANDS / ARP: SETUP"
                                 : "SCL: NEW / HOLD HOME: COMMANDS");
    if (s && e)
        fm_sequence_set_frozen_callback(s, frozen_dispatch, e);
}
void fm_ui_callbacks(FmUi *u, FmUiSave save, FmUiCaptureCommand capture, void *user)
{
    u->save = save;
    u->capture = capture;
    u->user = user;
}
static void select_track(FmUi *u, int delta)
{
    unsigned selected = 0, n = 0;
    FmTrack *list[FM_OBJECT_LIMIT];
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (u->project->tracks[i])
        {
            if (u->project->tracks[i]->id == u->project->selected_track)
                selected = n;
            list[n++] = u->project->tracks[i];
        }
    if (n)
        u->project->selected_track = list[index_delta(selected, delta, n - 1)]->id;
}
static FmResult watch_scope(FmUi *u)
{
    FmInstrument *in = instrument(u);
    if (!in || !in->count || (u->scope && !in->regions[u->scope - 1].id))
        return FM_ERR_NOT_FOUND;
    FmResult r = fm_edit_begin(u->project);
    if (r != FM_OK)
        return r;
    for (unsigned i = 0; i < FM_ANCHORS; i++)
        if (in->regions[i].id && (!u->scope || i == u->scope - 1))
        {
            r = fm_edit_watch(u->project, &in->regions[i], sizeof(FmRegion));
            if (r != FM_OK)
            {
                fm_edit_cancel(u->project);
                return r;
            }
        }
    return FM_OK;
}
static FmResult finish_scope(FmUi *u)
{
    FmInstrument *in = instrument(u);
    if (in)
        fm_map_automatic(in);
    return fm_edit_commit(u->project);
}
static FmResult capture(FmUi *u, unsigned command, uint64_t ms)
{
    FmTrack *t = track(u);
    return u->capture && t ? u->capture(u->user, command, t->id, u->step_note, ms)
                           : FM_ERR_UNSUPPORTED;
}
static void emit(FmUi *u, FmKeyState *k, bool down, uint64_t ms)
{
    FmEvent e = {0};
    e.frame = audio_time(ms);
    e.track = k->track;
    e.identity = k->identity;
    e.kind = down ? FM_EVENT_NOTE_ON : FM_EVENT_NOTE_OFF;
    e.origin = FM_ORIGIN_LOCAL;
    e.note = k->note;
    e.velocity = down ? 100 : 0;
    e.probability = 1;
    bool consumed = false;
    FmResult filtered = u->sequence ? fm_sequence_arp_input(u->sequence, &e, &consumed) : FM_OK;
    if (filtered != FM_OK)
    {
        result(u, filtered);
        return;
    }
    if (consumed)
        return;
    if (u->engine)
    {
        FmResult r = fm_engine_event(u->engine, &e);
        if (r != FM_OK)
            result(u, r);
    }
    if (u->sequence)
    {
        FmResult r = fm_sequence_live(u->sequence, &e);
        if (r != FM_OK)
            result(u, r);
    }
}
static void picker(FmUi *u, FmPicker p)
{
    u->picker = p;
    u->number[0] = 0;
    u->number_length = 0;
    u->pending_action = -1;
}
static bool action_row(FmUi *u)
{
    unsigned r = u->rows[u->tab];
    return (u->tab == 0 && r >= 7) || (u->tab == 2 && r >= 7) || (u->tab == 3 && r == 8) ||
           (u->tab == 4 && r == 11) || (u->tab == 6 && (r == 2 || r >= 3)) ||
           (u->tab == 7 && r >= 9) || u->tab == 8;
}
static FmResult scope_value(FmUi *u, double value, bool relative, unsigned field)
{
    FmInstrument *in = instrument(u);
    if (!in || !isfinite(value))
        return FM_ERR_ARGUMENT;
    if (u->tab == 2)
    {
        switch (u->rows[2])
        {
        case 0:
            u->mix_source = index_delta(u->mix_source, (int)value, 15);
            break;
        case 1:
            u->mix_gain_a = bound(relative ? u->mix_gain_a + (float)value : (float)value, 0, 4);
            break;
        case 2:
            u->mix_gain_b = bound(relative ? u->mix_gain_b + (float)value : (float)value, 0, 4);
            break;
        case 3:
            u->mix_offset = (int32_t)bound(relative ? u->mix_offset + (float)value : (float)value,
                                           -360000, 360000);
            break;
        case 4:
            u->mix_tuning =
                bound(relative ? u->mix_tuning + (float)value : (float)value, -4800, 4800);
            break;
        case 5:
            u->mix_overlap =
                (uint32_t)bound(relative ? u->mix_overlap + (float)value : (float)value, 0, 360000);
            break;
        case 6:
            u->mix_equal_power = relative ? !u->mix_equal_power : value != 0;
            break;
        default:
            return FM_ERR_UNSUPPORTED;
        }
        return FM_OK;
    }
    if (u->tab == 6 && u->rows[6] == 0)
    {
        if (in->type == FM_SYNTH)
        {
            if (!relative && value != 1 && value != 2 && value != 4)
                return FM_ERR_UNSUPPORTED;
            static const unsigned sources[] = {1, 2, 4};
            unsigned at = u->source == 4 ? 2 : u->source == 2 ? 1 : 0;
            u->source = relative ? sources[index_delta(at, (int)value, 2)] : (unsigned)value;
        }
        else
            u->source = relative ? index_delta(u->source, (int)value, 5)
                                 : (unsigned)bound((float)value, 0, 5);
        return FM_OK;
    }
    if (u->tab == 6 && u->rows[6] == 1 && field == 0)
    {
        u->harmonic = relative ? index_delta(u->harmonic, (int)value, 47)
                               : (unsigned)bound((float)value, 0, 47);
        return FM_OK;
    }
    if (u->tab == 3 && u->rows[3] == 0)
    {
        u->stage =
            relative ? index_delta(u->stage, (int)value, 7) : (unsigned)bound((float)value, 0, 7);
        return FM_OK;
    }
    if (u->tab == 4 && u->rows[4] == 5)
    {
        u->stage =
            relative ? index_delta(u->stage, (int)value, 7) : (unsigned)bound((float)value, 0, 7);
        return FM_OK;
    }
    if (u->tab == 1 && u->rows[1] == 1)
    {
        u->loop_section = relative ? index_delta(u->loop_section, (int)value, 7)
                                   : (unsigned)bound((float)value, 0, 7);
        return FM_OK;
    }
    if (u->tab == 6 && u->rows[6] == 1)
    {
        uint16_t levels[FM_HARMONICS];
        memcpy(levels, in->harmonics, sizeof(levels));
        levels[u->harmonic] =
            (uint16_t)bound(relative ? levels[u->harmonic] + (float)value : (float)value, 0, 256);
        FmId id;
        return fm_synthesis_additive(u->project, in->id, levels, 1024, &id);
    }
    if (u->tab == 6)
        return FM_ERR_UNSUPPORTED;
    if (u->tab == 7 && u->rows[7] == 2)
    {
        FmResult r = fm_edit_begin(u->project);
        if (r != FM_OK)
            return r;
        r = fm_edit_watch(u->project, in, sizeof(*in));
        if (r != FM_OK)
        {
            fm_edit_cancel(u->project);
            return r;
        }
        in->base_note =
            (uint8_t)bound(relative ? in->base_note + (float)value : (float)value, 0, 112);
        fm_map_automatic(in);
        return fm_edit_commit(u->project);
    }
    FmResult r = watch_scope(u);
    if (r != FM_OK)
        return r;
    unsigned row = u->rows[u->tab];
    for (unsigned i = 0; i < FM_ANCHORS; i++)
        if (in->regions[i].id && (!u->scope || i == u->scope - 1))
        {
            FmRegion *a = &in->regions[i];
            FmAsset *asset = fm_asset(u->project, a->asset);
            if (!asset)
                continue;
            float v = (float)value;
#define SET(vr, lo, hi) ((vr) = bound(relative ? (vr) + v : v, lo, hi))
            if (u->tab == 0)
            {
                switch (row)
                {
                case 0:
                case 1:
                {
                    uint32_t *marker = row ? &a->end : &a->start;
                    float n = u->scope ? (float)*marker : (float)*marker / asset->frames * 100;
                    float candidate = relative ? n + v : v;
                    if (!u->scope)
                        candidate = candidate * asset->frames / 100;
                    uint32_t target =
                        (uint32_t)bound(candidate, row ? (float)a->start + 1 : 0,
                                        row ? (float)asset->frames : (float)a->end - 1);
                    *marker = target;
                    break;
                }
                case 2:
                    SET(a->gain, 0, 4);
                    break;
                case 3:
                    a->reverse = relative ? !a->reverse : v != 0;
                    break;
                case 4:
                    a->root = (uint8_t)bound(relative ? a->root + v : v, 0, 127);
                    break;
                case 5:
                    SET(a->tuning, -4800, 4800);
                    break;
                case 6:
                    a->one_shot = relative ? !a->one_shot : v != 0;
                    break;
                default:
                    break;
                }
            }
            else if (u->tab == 1)
            {
                FmLoopSection *l = &a->loops[u->loop_section];
                if (!l->end)
                {
                    l->start = a->start;
                    l->end = a->end;
                    l->repeats = 1;
                }
                if (a->loop_count <= u->loop_section)
                    a->loop_count = (uint8_t)(u->loop_section + 1);
                switch (row)
                {
                case 0:
                    a->loop_enabled = relative ? !a->loop_enabled : v != 0;
                    break;
                case 2:
                {
                    float n = u->scope ? (float)l->start : 100.0f * l->start / asset->frames;
                    float target = relative ? n + v : v;
                    if (!u->scope)
                        target = target * asset->frames / 100;
                    l->start = (uint32_t)bound(target, (float)a->start, (float)l->end - 1);
                    break;
                }
                case 3:
                {
                    float n = u->scope ? (float)l->end : 100.0f * l->end / asset->frames;
                    float target = relative ? n + v : v;
                    if (!u->scope)
                        target = target * asset->frames / 100;
                    l->end = (uint32_t)bound(target, (float)l->start + 1, (float)a->end);
                    break;
                }
                case 4:
                {
                    float n =
                        u->scope ? (float)l->crossfade : 100.0f * l->crossfade / asset->frames;
                    float target = relative ? n + v : v;
                    if (!u->scope)
                        target = target * asset->frames / 100;
                    l->crossfade = (uint32_t)bound(target, 0, (float)(l->end - l->start) / 2);
                    break;
                }
                case 5:
                    l->repeats = (uint16_t)bound(relative ? l->repeats + v : v, 0, 65535);
                    break;
                case 6:
                    l->action = (uint8_t)bound(relative ? l->action + v : v, 0, FM_LOOP_END);
                    break;
                case 7:
                    l->jump =
                        (uint8_t)bound(relative ? l->jump + v : v, 0, (float)a->loop_count - 1);
                    break;
                case 8:
                    l->until_release = relative ? !l->until_release : v != 0;
                    break;
                default:
                    break;
                }
            }
            else if (u->tab == 3 || (u->tab == 4 && row >= 5))
            {
                FmEnvelope *e = u->tab == 3 ? &a->amp : &a->filter;
                unsigned er = u->tab == 3 ? row : row - 5;
                if (e->count <= u->stage)
                {
                    for (unsigned j = e->count; j <= u->stage; j++)
                        e->stage[j] = (FmEnvStage){10, 0};
                    e->count = (uint8_t)(u->stage + 1);
                }
                switch (er)
                {
                case 1:
                    SET(e->stage[u->stage].milliseconds, 0, 60000);
                    break;
                case 2:
                    SET(e->stage[u->stage].level, 0, 1);
                    break;
                case 3:
                    e->sustain =
                        (uint8_t)bound(relative ? e->sustain + v : v, 0, (float)e->count - 1);
                    break;
                case 4:
                    e->release =
                        (uint8_t)bound(relative ? e->release + v : v, 0, (float)e->count - 1);
                    break;
                case 5:
                    e->count = (uint8_t)bound(relative ? e->count + v : v, 1, 8);
                    if (e->sustain >= e->count)
                        e->sustain = e->count - 1;
                    if (e->release >= e->count)
                        e->release = e->count - 1;
                    break;
                case 6:
                    SET(e->key_rate, -4, 4);
                    break;
                case 7:
                    SET(e->velocity, 0, 1);
                    break;
                default:
                    break;
                }
            }
            else if (u->tab == 4)
            {
                switch (row)
                {
                case 0:
                    SET(a->cutoff, 0, 1);
                    break;
                case 1:
                    SET(a->resonance, 0, 0.98f);
                    break;
                case 2:
                    SET(a->drive, 0, 4);
                    break;
                case 3:
                    SET(a->filter_env, -8, 8);
                    break;
                case 4:
                    SET(a->keytrack, -4, 4);
                    break;
                default:
                    break;
                }
            }
            else if (u->tab == 5)
            {
                switch (row)
                {
                case 0:
                    SET(a->lfo_rate, 0, 50);
                    break;
                case 1:
                    SET(a->lfo_depth, 0, 1);
                    break;
                case 2:
                    a->lfo_shape = (a->lfo_shape & 0x80u) |
                                   (uint8_t)bound(relative ? (a->lfo_shape & 0x7fu) + v : v, 0,
                                                  FM_LFO_SAMPLE_HOLD);
                    break;
                case 3:
                    a->lfo_destination =
                        (uint8_t)bound(relative ? a->lfo_destination + v : v, 0, 3);
                    break;
                case 4:
                    a->lfo_shape = (a->lfo_shape & 0x7fu) | (v > 0 ? 0x80u : 0);
                    break;
                default:
                    break;
                }
            }
            else if (u->tab == 7)
            {
                switch (row)
                {
                case 0:
                    a->root = (uint8_t)bound(relative ? a->root + v : v, 0, 127);
                    break;
                case 2:
                    if (i == 0 || u->scope)
                        in->base_note = (uint8_t)bound(relative ? in->base_note + v : v, 0, 112);
                    break;
                case 3:
                    a->choke = (uint8_t)bound(relative ? a->choke + v : v, 0, 15);
                    break;
                case 4:
                    SET(a->pan, -1, 1);
                    break;
                default:
                    if (row >= 5 && row <= 8)
                        SET(a->sends[row - 5], 0, 2);
                    break;
                }
            }
#undef SET
        }
    if ((u->tab == 3 && row == 5) || (u->tab == 4 && row == 10))
    {
        FmRegion *current = region(u);
        if (current)
        {
            const FmEnvelope *e = u->tab == 3 ? &current->amp : &current->filter;
            if (u->stage >= e->count)
                u->stage = e->count - 1;
        }
    }
    return finish_scope(u);
}
FmResult fm_ui_set_parameter(FmUi *u, unsigned tab, unsigned row, unsigned field, double value)
{
    if (!u || !isfinite(value) || fabs(value) > 1000000 || tab >= 9 || row >= row_counts[tab] ||
        field > 1 || u->picker != FM_PICK_NONE)
        return FM_ERR_ARGUMENT;
    fm_ui_sync_view(u);
    if (!fm_ui_edit_tab_allowed(u, tab) || row >= fm_ui_edit_row_count(u, tab))
        return FM_ERR_UNSUPPORTED;
    unsigned previous_tab = u->tab, previous_row = u->rows[tab];
    u->tab = tab;
    u->rows[tab] = row;
    if (action_row(u))
    {
        u->tab = previous_tab;
        u->rows[tab] = previous_row;
        return FM_ERR_UNSUPPORTED;
    }
    FmResult r = scope_value(u, value, false, field);
    u->screen = FM_SCREEN_EDIT;
    result(u, r);
    return r;
}
static FmResult source_apply(FmUi *u)
{
    FmInstrument *in = instrument(u);
    if (!in)
        return FM_ERR_NOT_FOUND;
    if (in->type == FM_SYNTH ? u->source != 1 && u->source != 2 && u->source != 4 : u->source != 0)
        return FM_ERR_UNSUPPORTED;
    FmId id = 0;
    FmResult r = FM_ERR_UNSUPPORTED;
    switch (u->source)
    {
    case 0:
        /* A source can be reopened on an existing pitched instrument. Select
         * an unused octave anchor rather than failing on its existing C4. */
        if (in->type == FM_SAMPLE)
        {
            bool free_root = false;
            for (unsigned attempt = 0; attempt < FM_ANCHORS; attempt++)
            {
                free_root = true;
                for (unsigned i = 0; i < FM_ANCHORS; i++)
                    if (in->regions[i].id && in->regions[i].root == u->step_note)
                        free_root = false;
                if (free_root)
                    break;
                u->step_note = u->step_note <= 115 ? u->step_note + 12 : u->step_note % 12;
            }
            if (!free_root)
                return FM_ERR_RANGE;
        }
        u->screen = FM_SCREEN_CAPTURE;
        return capture(u, 0, u->now_ms);
    case 1:
    {
        uint16_t levels[FM_HARMONICS] = {256};
        if (in->additive)
            memcpy(levels, in->harmonics, sizeof(levels));
        r = fm_synthesis_additive(u->project, in->id, levels, 1024, &id);
        break;
    }
    case 2:
        r = fm_synthesis_preset(u->project, in->id, (FmWaveShape)u->wave_shape, u->wave_points,
                                &id);
        break;
    case 3:
    {
        FmRegion *a = region(u);
        if (a)
            r = fm_synthesis_extract(u->project, in->id, a->asset, a->start, a->end - a->start,
                                     a->root, &id);
        else
            r = FM_ERR_NOT_FOUND;
        break;
    }
    case 4:
    {
        int16_t points[1024] = {0};
        r = fm_synthesis_draw(u->project, in->id, points, 1024, 60, &id);
        break;
    }
    case 5:
        u->library_item = 0;
        u->screen = FM_SCREEN_LIBRARY;
        return FM_OK;
    default:
        break;
    }
    if (r == FM_OK)
    {
        u->screen = u->source == 4 || in->type == FM_SYNTH ? FM_SCREEN_EDIT : FM_SCREEN_HOME;
        if (u->source == 4 || in->type == FM_SYNTH)
        {
            u->tab = 6;
            u->rows[6] = u->source == 1 ? 1 : 4;
        }
        snprintf(u->status, sizeof(u->status), "SOURCE READY - VOLATILE");
    }
    return r;
}
FmResult fm_ui_use_source(FmUi *u, unsigned source)
{
    if (!u || source > 5 || u->picker != FM_PICK_NONE)
        return FM_ERR_ARGUMENT;
    FmInstrument *in = instrument(u);
    if (in && (in->type == FM_SYNTH ? source != 1 && source != 2 && source != 4 : source != 0))
        return FM_ERR_UNSUPPORTED;
    u->source = source;
    FmResult r = source_apply(u);
    result(u, r);
    return r;
}
static FmResult edit_action(FmUi *u)
{
    FmInstrument *in = instrument(u);
    FmRegion *a = region(u);
    if (!in)
        return FM_ERR_NOT_FOUND;
    unsigned row = u->rows[u->tab];
    FmEditScope scope = {in->id, a ? a->id : 0, u->scope == 0};
    if (u->tab == 0 && row >= 7)
    {
        FmSampleEdit edit = {0};
        edit.operation = row == 7   ? FM_EDIT_NORMALIZE
                         : row == 8 ? FM_EDIT_REVERSE_WRITE
                                    : FM_EDIT_TRUNCATE;
        edit.to = 1;
        edit.level = .95f;
        return fm_sample_edit(u->project, scope, &edit);
    }
    if (u->tab == 2 && row >= 7)
    {
        if (!a || !in->regions[u->mix_source].id)
            return FM_ERR_NOT_FOUND;
        if (!u->scope)
            return FM_ERR_ARGUMENT;
        FmMixWrite mix = {a->id,         in->regions[u->mix_source].id,
                          u->mix_gain_a, u->mix_gain_b,
                          u->mix_offset, u->mix_tuning,
                          row == 8,      u->mix_equal_power,
                          u->mix_overlap};
        return fm_mix_write(u->project, a->id, &mix);
    }
    if ((u->tab == 3 && row == 8) || (u->tab == 4 && row == 11))
    {
        FmResult r = watch_scope(u);
        if (r != FM_OK)
            return r;
        for (unsigned i = 0; i < FM_ANCHORS; i++)
            if (in->regions[i].id && (!u->scope || i == u->scope - 1))
            {
                if (u->tab == 3)
                    in->regions[i].filter = in->regions[i].amp;
                else
                    in->regions[i].amp = in->regions[i].filter;
            }
        return finish_scope(u);
    }
    if (u->tab == 6)
    {
        if (row == 2)
        {
            unsigned sum = 0;
            for (unsigned i = 0; i < 48; i++)
                sum += in->harmonics[i];
            if (!sum)
                return FM_ERR_RANGE;
            uint16_t levels[FM_HARMONICS];
            for (unsigned i = 0; i < FM_HARMONICS; i++)
                levels[i] = (uint16_t)((unsigned)in->harmonics[i] * 256u / sum);
            FmId id;
            return fm_synthesis_additive(u->project, in->id, levels, 1024, &id);
        }
        if (row == 3)
        {
            FmId id;
            return fm_synthesis_additive(u->project, in->id, in->harmonics, 1024, &id);
        }
        if (row == 4)
        {
            picker(u, FM_PICK_NUMBER);
            return FM_OK;
        }
        if (row == 5 || row == 6)
        {
            FmSampleEdit edit = {0};
            edit.operation = row == 5 ? FM_EDIT_CLEAR : FM_EDIT_SMOOTH;
            edit.to = 1;
            return fm_sample_edit(u->project, scope, &edit);
        }
        if (row == 7)
        {
            FmId id;
            return a ? fm_synthesis_extract(u->project, in->id, a->asset, a->start,
                                            a->end - a->start, a->root, &id)
                     : FM_ERR_NOT_FOUND;
        }
    }
    if (u->tab == 7 && row == 9)
    {
        FmResult r = watch_scope(u);
        if (r != FM_OK)
            return r;
        for (unsigned i = 0; i < FM_ANCHORS; i++)
            if (in->regions[i].id && (!u->scope || i == u->scope - 1))
                in->regions[i].manual_zone = false;
        return finish_scope(u);
    }
    if (u->tab == 7 && row == 10)
    {
        picker(u, FM_PICK_SAMPLE);
        return FM_OK;
    }
    if (u->tab == 7 && row == 11)
        return u->scope && a ? fm_region_remove(u->project, a->id) : FM_ERR_ARGUMENT;
    if (u->tab == 8)
    {
        FmTrack *t = track(u);
        if (!t)
            return FM_ERR_NOT_FOUND;
        FmBakeOptions options = {15, FM_RATE, 2, true, false};
        if (row == 0)
            return fm_bake_samples(u->project, scope, &options);
        if (row == 1)
            return fm_bake_pattern(u->project, t->patterns[t->selected_pattern], &options);
        if (row == 2)
            return fm_unfreeze_pattern(u->project, t->patterns[t->selected_pattern]);
        if (row == 3 && a)
            return fm_unbake_sample(u->project, a->id);
    }
    return FM_ERR_UNSUPPORTED;
}
static void numeric_confirm(FmUi *u)
{
    if (!u->number_length)
    {
        result(u, FM_ERR_ARGUMENT);
        return;
    }
    char *end;
    double n = strtod(u->number, &end);
    if (*end || !isfinite(n))
    {
        result(u, FM_ERR_RANGE);
        return;
    } /* Reject invalid values before applying; relative controls have independent clamps. */
    unsigned row = u->rows[u->tab];
    double lo = 0, hi = 1;
    if (u->tab == 0)
    {
        if (row == 0 || row == 1)
        {
            FmRegion *a = region(u);
            FmAsset *as = a ? fm_asset(u->project, a->asset) : NULL;
            hi = u->scope && as ? as->frames : 100;
        }
        else if (row == 2)
            hi = 4;
        else if (row == 4)
            hi = 127;
        else if (row == 5)
        {
            lo = -4800;
            hi = 4800;
        }
    }
    else if (u->tab == 1)
    {
        hi = !u->scope && row >= 2 && row <= 4 ? 100
             : row == 5                        ? 65535
             : row == 6                        ? 2
             : row == 7                        ? 7
                                               : 360000;
    }
    else if (u->tab == 2)
    {
        lo = row == 3 ? -360000 : row == 4 ? -4800 : 0;
        hi = row == 3 || row == 5 ? 360000 : row == 4 ? 4800 : row == 0 ? 15 : 4;
    }
    else if (u->tab == 3)
    {
        lo = row == 6 ? -4 : 0;
        hi = row == 1                           ? 60000
             : row == 0 || row == 3 || row == 4 ? 7
             : row == 5                         ? 8
             : row == 6                         ? 4
                                                : 1;
    }
    else if (u->tab == 4)
    {
        lo = row == 3 ? -8 : row == 4 ? -4 : 0;
        hi = row == 2                           ? 4
             : row == 3                         ? 8
             : row == 4                         ? 4
             : row == 6                         ? 60000
             : row == 5 || row == 8 || row == 9 ? 7
             : row == 10                        ? 8
                                                : 1;
    }
    else if (u->tab == 5)
    {
        hi = row == 0 ? 50 : row == 2 ? FM_LFO_SAMPLE_HOLD : row == 3 ? 3 : 1;
    }
    else if (u->tab == 6)
    {
        hi = row == 1 ? 256 : row == 4 ? 32767 : 5;
        lo = row == 4 ? -32768 : 0;
    }
    else if (u->tab == 7)
    {
        lo = row == 4 ? -1 : 0;
        hi = row == 0 ? 127 : row == 2 ? 112 : row == 3 ? 15 : row >= 5 ? 2 : 1;
    }
    if (n < lo || n > hi)
    {
        result(u, FM_ERR_RANGE);
        return;
    }
    FmResult r;
    if (u->tab == 6 && row == 4)
    {
        FmInstrument *in = instrument(u);
        FmRegion *a = region(u);
        FmSampleEdit edit = {0};
        edit.operation = FM_EDIT_DRAW_POINT;
        edit.point = u->point;
        edit.from = a && a->end - a->start > 1 ? (double)u->point / (a->end - a->start - 1) : 0;
        edit.level = (float)(n / 32768);
        edit.to = 1;
        FmEditScope scope = {in ? in->id : 0, a ? a->id : 0, u->scope == 0};
        r = fm_sample_edit(u->project, scope, &edit);
    }
    else
        r = scope_value(u, n, false, 1);
    result(u, r);
    if (r == FM_OK)
        u->picker = FM_PICK_NONE;
}
FmResult fm_ui_history(FmUi *u, bool redo)
{
    FmFxConfig before[FM_FX_SLOTS];
    memcpy(before, u->project->fx, sizeof(before));
    FmResult r = redo ? fm_redo(u->project) : fm_undo(u->project);
    if (r != FM_OK || !u->engine)
        return r;
    uint8_t mask = 0;
    for (unsigned i = 0; i < FM_FX_SLOTS; i++)
        if (memcmp(&before[i], &u->project->fx[i], sizeof(before[i])))
            mask |= 1u << i;
    r = fm_engine_fx_sync(u->engine, mask);
    if (r != FM_OK)
    {
        if (redo)
            (void)fm_undo(u->project);
        else
            (void)fm_redo(u->project);
    }
    return r;
}
static void confirm(FmUi *u)
{
    if (u->picker == FM_PICK_NUMBER)
    {
        numeric_confirm(u);
        return;
    }
    if (u->picker == FM_PICK_CONFIRM)
    {
        u->picker = FM_PICK_NONE;
        if (u->pending_action >= 0)
        {
            unsigned action = (unsigned)u->pending_action;
            u->pending_action = -1;
            result(u, fm_ui_workspace_action(u, action, true));
        }
        else
            result(u, edit_action(u));
        return;
    }
    if (u->picker != FM_PICK_NONE)
        return;
    if (u->screen == FM_SCREEN_ROLL)
    {
        result(u, fm_ui_roll_toggle(u));
        return;
    }
    if ((u->screen >= FM_SCREEN_STEP_DETAIL && u->screen <= FM_SCREEN_LIBRARY))
    {
        result(u, fm_ui_workspace_confirm(u));
        return;
    }
    if (u->screen == FM_SCREEN_COMMANDS && u->command >= 9)
    {
        result(u, fm_ui_workspace_action(u, u->command, false));
        return;
    }
    if (u->screen == FM_SCREEN_HOME)
    {
        u->screen = u->project->track_count ? FM_SCREEN_COMMANDS : FM_SCREEN_NEW;
        u->source = u->command = 0;
    }
    else if (u->screen == FM_SCREEN_COMMANDS)
    {
        if (u->command == 0)
        {
            u->screen = FM_SCREEN_NEW;
            u->source = 0;
        }
        else if (u->command == 1)
            result(u, fm_ui_history(u, true));
        else if (u->command == 2)
        {
            if (u->engine)
                fm_engine_panic(u->engine);
            snprintf(u->status, sizeof(u->status), "PANIC: VOICES RELEASED");
        }
        else if (u->command == 3)
        {
            picker(u, FM_PICK_NOTE);
            u->screen = FM_SCREEN_STEP;
        }
        else if (u->command == 4)
        {
            FmTrack *t = track(u);
            if (t)
            {
                FmResult r = fm_edit_begin(u->project);
                if (r == FM_OK)
                    r = fm_edit_watch(u->project, t, sizeof(*t));
                if (r == FM_OK)
                {
                    t->mute = !t->mute;
                    r = fm_edit_commit(u->project);
                }
                else
                    fm_edit_cancel(u->project);
                result(u, r);
            }
        }
        else if (u->command == 5)
        {
            FmTrack *t = track(u);
            if (t)
            {
                FmResult r = fm_edit_begin(u->project);
                if (r == FM_OK)
                    r = fm_edit_watch(u->project, t, sizeof(*t));
                if (r == FM_OK)
                {
                    t->solo = !t->solo;
                    r = fm_edit_commit(u->project);
                }
                else
                    fm_edit_cancel(u->project);
                result(u, r);
            }
        }
        else if (u->command >= 6 && u->sequence)
        {
            FmSongEntry entry = {0};
            if (u->command == 7)
                result(u, fm_sequence_song_delete(u->sequence, (uint16_t)u->song_row));
            else
            {
                if (u->command == 8 && u->project->song_count)
                    entry = u->project->song[u->song_row];
                else
                {
                    entry.repeats = 1;
                    entry.bars = 1;
                    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
                        if (u->project->tracks[i])
                        {
                            FmTrack *t = u->project->tracks[i];
                            entry.tracks[entry.count] = t->id;
                            entry.patterns[entry.count] = t->selected_pattern;
                            entry.muted[entry.count++] = t->mute;
                        }
                }
                result(u, fm_sequence_song_insert(
                              u->sequence,
                              (uint16_t)(u->song_row + (u->project->song_count ? 1 : 0)), &entry));
            }
        }
        else
            result(u, FM_ERR_UNSUPPORTED);
    }
    else if (u->screen == FM_SCREEN_NEW)
    {
        FmId t, previous = u->project->selected_track;
        bool synth = u->source == 2;
        FmResult r = fm_instrument_create(u->project, (FmInstrumentType)u->source, &t);
        result(u, r);
        if (r == FM_OK)
        {
            unsigned choice = u->source;
            u->scope = 0;
            u->wave_shape = FM_WAVE_SINE;
            fm_ui_sync_view(u);
            r = fm_ui_use_source(u, synth ? 2 : 0);
            if (r != FM_OK)
            {
                (void)fm_remove_empty_instrument(u->project, t);
                u->project->selected_track = previous;
                fm_ui_sync_view(u);
                u->screen = FM_SCREEN_NEW;
                u->source = choice;
            }
            u->pending_instrument = 0;
            result(u, r);
        }
    }
    else if (u->screen == FM_SCREEN_SOURCE)
        result(u, source_apply(u));
    else if (u->screen == FM_SCREEN_STEP)
        picker(u, FM_PICK_NOTE);
    else if (u->screen == FM_SCREEN_CAPTURE)
    {
        if (u->capture_row >= 9)
            result(u, capture(u,
                              u->capture_row == 9    ? 4
                              : u->capture_row == 10 ? 5
                                                     : 0,
                              u->now_ms));
        else
            result(u, capture(u, 3, u->now_ms));
    }
    else if (u->screen == FM_SCREEN_EDIT)
    {
        unsigned row = u->rows[u->tab];
        if ((u->tab == 0 && row == 4) || (u->tab == 7 && row == 0))
            picker(u, FM_PICK_NOTE);
        else if (u->tab == 7 && row == 1)
            picker(u, FM_PICK_LOW);
        else if (u->tab == 7 && row == 10)
            picker(u, FM_PICK_SAMPLE);
        else if (u->tab == 6 && row == 0)
        {
            u->screen = FM_SCREEN_SOURCE;
            FmInstrument *in = instrument(u);
            if (in && in->type == FM_SYNTH && u->source != 1 && u->source != 2 && u->source != 4)
                u->source = 2;
        }
        else if (action_row(u))
        {
            if ((u->tab == 0 && row >= 8) || (u->tab == 2 && row >= 7) ||
                (u->tab == 6 && (row == 5 || row == 6)) || (u->tab == 8 && row < 2) ||
                (u->tab == 7 && row == 11))
            {
                picker(u, FM_PICK_CONFIRM);
                if (u->tab == 8)
                    snprintf(u->status, sizeof(u->status), "ISOLATED BAKE; ALL SENDS; TAIL 1S");
            }
            else
                result(u, edit_action(u));
        }
        else
            picker(u, FM_PICK_NUMBER);
    }
    else if (u->screen == FM_SCREEN_MIXER)
    {
        FmTrack *t = track(u);
        if (t)
        {
            FmResult r = fm_edit_begin(u->project);
            if (r == FM_OK)
                r = fm_edit_watch(u->project, t, sizeof(*t));
            if (r == FM_OK)
            {
                t->mute = !t->mute;
                r = fm_edit_commit(u->project);
            }
            else
                fm_edit_cancel(u->project);
            result(u, r);
        }
    }
    else if (u->screen == FM_SCREEN_SONG && u->sequence)
    {
        if (!u->project->song_count)
        {
            FmSongEntry entry = {0};
            entry.repeats = 1;
            entry.bars = 1;
            for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
                if (u->project->tracks[i])
                {
                    FmTrack *t = u->project->tracks[i];
                    entry.tracks[entry.count] = t->id;
                    entry.patterns[entry.count] = t->selected_pattern;
                    entry.muted[entry.count++] = t->mute;
                }
            result(u, fm_sequence_song_insert(u->sequence, 0, &entry));
        }
        else
            result(u, fm_sequence_song_queue(u->sequence, (uint16_t)u->song_row));
    }
}
static void tap(FmUi *u, FmButton button, const FmButtonState *state)
{
    uint64_t ms = state->down_ms;
    if (button == FM_BUTTON_HOME)
    {
        if (u->picker != FM_PICK_NONE)
        {
            u->picker = FM_PICK_NONE;
            return;
        }
        if (u->screen == FM_SCREEN_SOURCE && u->pending_instrument)
        {
            FmResult r = fm_remove_empty_instrument(u->project, u->pending_instrument);
            if (r == FM_OK)
                u->pending_instrument = 0;
        }
        if ((u->screen >= FM_SCREEN_STEP_DETAIL && u->screen <= FM_SCREEN_LIBRARY))
        {
            u->screen = u->return_screen;
            return;
        }
        u->screen = FM_SCREEN_HOME;
        return;
    }
    if (button == FM_BUTTON_SCL)
    {
        confirm(u);
        return;
    }
    if (button == FM_BUTTON_OCT_PLUS || button == FM_BUTTON_OCT_MINUS)
    {
        int d = button == FM_BUTTON_OCT_PLUS ? 1 : -1;
        if (state->screen == FM_SCREEN_CAPTURE && button == FM_BUTTON_OCT_PLUS)
        {
            result(u, capture(u, 1, ms));
            u->screen = FM_SCREEN_HOME;
            return;
        }
        if (u->screen == FM_SCREEN_ROLL)
        {
            int next = (int)index_delta((unsigned)(u->octave + 3), d, 7) - 3;
            d = next - u->octave;
            u->octave = next;
            u->step_note = (uint8_t)bound(u->step_note + d * 12, 0, 127);
            u->roll_low = (unsigned)bound((float)u->roll_low + d * 12, 0,
                                          128 - (u->roll_span ? u->roll_span : 12));
            return;
        }
        if (u->screen == FM_SCREEN_STEP || u->screen == FM_SCREEN_PATTERNS)
        {
            u->page = index_delta(u->page, d, u->screen == FM_SCREEN_PATTERNS ? 1 : 15);
            return;
        }
        u->octave = (int)index_delta((unsigned)(u->octave + 3), d, 7) - 3;
        return;
    }
    if (u->picker != FM_PICK_NONE)
        return;
    switch (button)
    {
    case FM_BUTTON_EDIT:
        if (u->screen == FM_SCREEN_ROLL)
        {
            u->roll_move = !u->roll_move;
            break;
        }
        if (u->screen == FM_SCREEN_EDIT)
        {
            do
                u->tab = (u->tab + 1) % 9;
            while (!fm_ui_edit_tab_allowed(u, u->tab));
        }
        else
            u->screen = FM_SCREEN_EDIT;
        break;
    case FM_BUTTON_ENV:
        u->screen = FM_SCREEN_EDIT;
        u->tab = 3;
        break;
    case FM_BUTTON_LFO:
        u->screen = FM_SCREEN_EDIT;
        u->tab = 5;
        break;
    case FM_BUTTON_GLO:
        if (u->screen == FM_SCREEN_MIXER)
            u->mixer_send = (u->mixer_send + 1) % 4;
        else
            u->screen = FM_SCREEN_MIXER;
        snprintf(u->status, sizeof(u->status), "SCL MUTE / HOLD GLO SOLO");
        break;
    case FM_BUTTON_FX:
        if (u->screen == FM_SCREEN_FX)
            u->fx_slot = (u->fx_slot + 1) % 4;
        else
        {
            u->screen = FM_SCREEN_FX;
            u->command = 0;
        }
        break;
    case FM_BUTTON_SEQ:
        if (u->screen == FM_SCREEN_ROLL)
            u->screen = FM_SCREEN_STEP;
        else if (u->screen == FM_SCREEN_STEP)
            u->screen = FM_SCREEN_PATTERNS;
        else if (u->screen == FM_SCREEN_PATTERNS)
            u->screen = FM_SCREEN_SONG;
        else
            fm_ui_roll_open(u);
        break;
    case FM_BUTTON_PLAY:
        if (u->sequence)
        {
            if (fm_sequence_playing(u->sequence))
            {
                if (fm_sequence_arranging(u->sequence))
                    result(u, fm_sequence_arrange(u->sequence, false));
                fm_sequence_stop(u->sequence, fm_ui_sequence_dispatch, u->engine);
            }
            else if (u->screen == FM_SCREEN_SONG && u->project->song_count)
                result(u, fm_sequence_song_start(u->sequence, (uint16_t)u->song_row, true));
            else
                fm_sequence_start(u->sequence);
        }
        else
            result(u, FM_ERR_UNSUPPORTED);
        break;
    case FM_BUTTON_SAVE:
        if (u->sequence && fm_sequence_playing(u->sequence))
        {
            u->save_pending = true;
            snprintf(u->status, sizeof(u->status), "SAVE QUEUED - STOP PLAY");
        }
        else
            result(u, u->save ? u->save(u->user, u->project) : FM_ERR_UNSUPPORTED);
        break;
    case FM_BUTTON_REC:
        if (state->screen == FM_SCREEN_CAPTURE)
            result(u, capture(u, 3, ms));
        else if (u->sequence)
        {
            FmSequenceStatus s = fm_sequence_status(u->sequence);
            result(u, s.armed || s.recording ? fm_sequence_record_close(u->sequence, audio_time(ms))
                                             : fm_sequence_record_arm(u->sequence, state->track));
        }
        else
            result(u, FM_ERR_UNSUPPORTED);
        break;
    case FM_BUTTON_ARP:
        if (u->screen == FM_SCREEN_ARP)
            u->arp_page ^= 1;
        else
        {
            u->screen = FM_SCREEN_ARP;
            u->arp_page = 0;
        }
        break;
    default:
        break;
    }
}
static void hold(FmUi *u, FmButton button, const FmButtonState *state)
{
    if (button == FM_BUTTON_GLO && u->screen == FM_SCREEN_MIXER)
    {
        FmTrack *t = track(u);
        if (!t)
            return;
        FmResult r = fm_edit_begin(u->project);
        if (r == FM_OK)
            r = fm_edit_watch(u->project, t, sizeof(*t));
        if (r == FM_OK)
        {
            t->solo = !t->solo;
            r = fm_edit_commit(u->project);
        }
        else
            fm_edit_cancel(u->project);
        result(u, r);
        return;
    }
    if (button == FM_BUTTON_HOME)
    {
        if (u->picker != FM_PICK_NONE)
        {
            u->picker = FM_PICK_NONE;
            snprintf(u->status, sizeof(u->status), "CANCELLED");
            return;
        }
        u->return_screen = u->screen;
        u->screen = FM_SCREEN_COMMANDS;
        u->command = 0;
        return;
    }
    if (button == FM_BUTTON_EDIT)
    {
        picker(u, FM_PICK_NAME);
        return;
    }
    if (button == FM_BUTTON_SCL)
    {
        if (state->picker != FM_PICK_NONE || u->picker != FM_PICK_NONE)
        {
            u->picker = FM_PICK_NONE;
            snprintf(u->status, sizeof(u->status), "CANCELLED");
            return;
        }
        FmResult r = state->screen == FM_SCREEN_CAPTURE ? capture(u, 2, state->down_ms)
                                                        : fm_ui_history(u, false);
        result(u, r);
        return;
    }
    if (button == FM_BUTTON_REC)
    {
        if (state->screen == FM_SCREEN_CAPTURE)
        {
            result(u, capture(u, 4, state->down_ms));
            return;
        }
        if (u->sequence)
            result(u, fm_sequence_retrospective(u->sequence, state->track,
                                                audio_time(state->down_ms), FM_RATE * 60u));
        else
            result(u, FM_ERR_UNSUPPORTED);
    }
}
void fm_ui_tick(FmUi *u, uint64_t ms)
{
    if (ms < u->now_ms)
        return;
    u->now_ms = ms;
    fm_ui_sync_view(u);
    if (u->engine)
        (void)fm_engine_collect_cycles(u->engine);
    if (u->capture_monitor && u->capture_monitor->last_region &&
        u->capture_monitor->last_region != u->last_capture)
    {
        u->last_capture = u->capture_monitor->last_region;
        FmInstrument *in = instrument(u);
        if (in && in->type == FM_SAMPLE)
            u->step_note = (uint8_t)fmin(127, u->capture_monitor->root + 12);
    }
    for (unsigned i = 0; i < FM_UI_BUTTONS; i++)
    {
        FmButtonState *b = &u->buttons[i];
        if (b->down && !b->held && ms - b->down_ms >= FM_UI_HOLD_MS)
        {
            b->held = true;
            hold(u, (FmButton)i, b);
        }
    }
    if (u->save_pending && (!u->sequence || !fm_sequence_playing(u->sequence)))
    {
        u->save_pending = false;
        result(u, u->save ? u->save(u->user, u->project) : FM_ERR_UNSUPPORTED);
    }
}
void fm_ui_button(FmUi *u, FmButton button, bool down, uint64_t ms)
{
    if ((unsigned)button >= FM_UI_BUTTONS || ms < u->now_ms)
        return;
    fm_ui_tick(u, ms);
    FmButtonState *b = &u->buttons[button];
    if (down == b->down)
        return;
    if (down)
    {
        *b = (FmButtonState){true, false, ms, u->screen, u->picker, u->project->selected_track};
        if (button == FM_BUTTON_OCT_PLUS && u->screen == FM_SCREEN_CAPTURE)
        {
            b->held = true;
            tap(u, button, b);
        }
    }
    else
    {
        b->down = false;
        if (!b->held && ms - b->down_ms >= 5)
            tap(u, button, b);
    }
}
static int white_index(unsigned key)
{
    for (unsigned i = 0; i < 16; i++)
        if (fm_ui_white_keys[i] == key)
            return (int)i;
    return -1;
}
static uint8_t key_note(FmUi *u, unsigned index)
{
    FmInstrument *in = instrument(u);
    int note =
        (in && in->type == FM_MULTI_SAMPLE ? in->base_note : 53) + (int)index + u->octave * 12;
    return (uint8_t)bound((float)note, 0, 127);
}
void fm_ui_key(FmUi *u, unsigned index, bool down, uint64_t ms)
{
    if (index >= FM_UI_KEYS || ms < u->now_ms)
        return;
    fm_ui_tick(u, ms);
    FmKeyState *key = &u->keys[index];
    if (key->down == down)
        return;
    key->down = down;
    int white = white_index(index);
    if (!down && key->sounding)
    {
        emit(u, key, false, ms);
        key->sounding = false;
        return;
    }
    if (u->picker != FM_PICK_NONE)
    {
        if (!down)
            return;
        if (u->picker == FM_PICK_NAME)
        {
            if (white >= 0 && white < 9)
            {
                FmInstrument *in = instrument(u);
                if (in)
                {
                    FmResult r = fm_edit_begin(u->project);
                    if (r == FM_OK)
                        r = fm_edit_watch(u->project, in, sizeof(*in));
                    if (r == FM_OK)
                        r = fm_name_category(u->project, in->id, (uint8_t)white);
                    if (r == FM_OK)
                        r = fm_edit_commit(u->project);
                    else
                        fm_edit_cancel(u->project);
                    result(u, r);
                    if (r == FM_OK)
                        u->picker = FM_PICK_NONE;
                }
            }
            return;
        }
        if (u->picker == FM_PICK_CONFIRM)
        {
            return;
        }
        if (u->picker == FM_PICK_NUMBER)
        {
            if (white >= 0 && white < 10)
            {
                if (u->number_length + 1 < sizeof(u->number))
                {
                    u->number[u->number_length++] = (char)('0' + white);
                    u->number[u->number_length] = 0;
                }
                else
                    result(u, FM_ERR_RANGE);
            }
            else if (white == 10 && u->number_length)
                u->number[--u->number_length] = 0;
            else if (white == 11 && !u->number_length)
            {
                u->number[u->number_length++] = '-';
                u->number[u->number_length] = 0;
            }
            else if (white == 12 && !strchr(u->number, '.') &&
                     u->number_length + 1 < sizeof(u->number))
            {
                u->number[u->number_length++] = '.';
                u->number[u->number_length] = 0;
            }
            else if (white == 13)
                u->picker = FM_PICK_NONE;
            else if (white == 14)
                numeric_confirm(u);
            return;
        }
        uint8_t note = key_note(u, index);
        if (u->picker == FM_PICK_NOTE)
        {
            if (u->screen == FM_SCREEN_STEP)
            {
                u->step_note = note;
                u->picker = FM_PICK_NONE;
                return;
            }
            result(u, scope_value(u, note, false, 0));
            if (u->last_result == FM_OK)
                u->picker = FM_PICK_NONE;
            return;
        }
        if (u->picker == FM_PICK_LOW)
        {
            u->pick_low = note;
            u->picker = FM_PICK_HIGH;
            return;
        }
        if (u->picker == FM_PICK_HIGH)
        {
            FmResult r = watch_scope(u);
            FmInstrument *in = instrument(u);
            if (r == FM_OK && in)
            {
                for (unsigned i = 0; i < 16; i++)
                    if (in->regions[i].id && (!u->scope || i == u->scope - 1))
                    {
                        in->regions[i].low = note < u->pick_low ? note : u->pick_low;
                        in->regions[i].high = note > u->pick_low ? note : u->pick_low;
                        in->regions[i].manual_zone = true;
                    }
                r = finish_scope(u);
            }
            result(u, r);
            if (r == FM_OK)
                u->picker = FM_PICK_NONE;
            return;
        }
        if (u->picker == FM_PICK_SAMPLE)
        {
            FmInstrument *in = instrument(u);
            FmRegion *a = fm_map_note(in, note);
            if (a)
            {
                u->scope = (unsigned)(a - in->regions) + 1;
                u->picker = FM_PICK_NONE;
                result(u, FM_OK);
            }
            else
                result(u, FM_ERR_NOT_FOUND);
            return;
        }
    }
    if (u->screen == FM_SCREEN_PATTERNS && white >= 0 && u->sequence)
    {
        unsigned bit = 1u << (unsigned)white;
        FmTrack *t = track(u);
        if (!t)
            return;
        if (down)
        {
            if (u->pattern_keys)
            {
                unsigned first = 0;
                while (!(u->pattern_keys & (1u << first)))
                    first++;
                unsigned a = u->page * 16 + first, b = u->page * 16 + (unsigned)white;
                FmId made;
                for (unsigned j = a < b ? a : b; j <= (a > b ? a : b); j++)
                    fm_pattern_create(u->project, t->id, (uint8_t)j, &made);
                result(u,
                       fm_sequence_chain_range(u->sequence, t->id, (uint8_t)(u->page * 16 + first),
                                               (uint8_t)(u->page * 16 + (unsigned)white)));
                u->pattern_chained = true;
            }
            u->pattern_keys |= bit;
        }
        else
        {
            u->pattern_keys &= ~bit;
            if (!u->pattern_chained)
            {
                FmId created;
                if (u->scene_scope)
                {
                    for (unsigned j = 0; j < FM_OBJECT_LIMIT; j++)
                        if (u->project->tracks[j])
                            fm_pattern_create(u->project, u->project->tracks[j]->id,
                                              (uint8_t)(u->page * 16 + (unsigned)white), &created);
                }
                else
                    fm_pattern_create(u->project, t->id, (uint8_t)(u->page * 16 + (unsigned)white),
                                      &created);
                result(u, u->scene_scope
                              ? fm_sequence_scene(u->sequence,
                                                  (uint8_t)(u->page * 16 + (unsigned)white))
                              : fm_sequence_queue_pattern(
                                    u->sequence, t->id, (uint8_t)(u->page * 16 + (unsigned)white)));
            }
            if (!u->pattern_keys)
                u->pattern_chained = false;
        }
        return;
    }
    if (!down)
        return;
    if (u->screen == FM_SCREEN_NEW && white >= 0 && white < 3)
    {
        u->source = (unsigned)white;
        confirm(u);
        return;
    }
    if (u->screen == FM_SCREEN_STEP && u->sequence)
    {
        FmTrack *t = track(u);
        if (!t)
            return;
        FmPattern *p = fm_pattern(u->project, t->patterns[t->selected_pattern]);
        if (!p)
            return;
        if (white >= 0)
        {
            u->selected_step = (unsigned)white;
            double tick = (u->page * 16 + (unsigned)white) * (FM_PPQN / 4.0);
            bool enabled = true;
            for (unsigned i = 0; i < p->count; i++)
                if (p->events[i].kind == FM_EVENT_NOTE_ON &&
                    fabs(p->events[i].tick - tick) < .001 && p->events[i].note == u->step_note)
                    enabled = false;
            result(u, fm_sequence_step(u->sequence, p->id, u->page * 16 + (unsigned)white,
                                       u->step_note, u->step_velocity, u->step_length, enabled));
        }
        else if (index == 1)
            result(u, fm_sequence_step(u->sequence, p->id, u->page * 16 + u->selected_step,
                                       u->step_note, 0, u->step_length, false));
        else if (index == 3)
        {
            result(u,
                   fm_step_tie(u->project, p->id, u->page * 16 + u->selected_step, u->step_note));
        }
        return;
    }
    if (u->screen == FM_SCREEN_SOURCE || u->screen == FM_SCREEN_COMMANDS ||
        u->screen == FM_SCREEN_SONG || u->screen == FM_SCREEN_CAPTURE ||
        (u->screen >= FM_SCREEN_STEP_DETAIL && u->screen <= FM_SCREEN_LIBRARY))
        return;
    if (u->screen == FM_SCREEN_ROLL)
    {
        u->step_note = key_note(u, index);
        if (u->step_note < u->roll_low ||
            u->step_note >= u->roll_low + (u->roll_span ? u->roll_span : 12))
            u->roll_low = u->step_note > 5 ? u->step_note - 5 : 0;
        if (u->roll_low > 128 - (u->roll_span ? u->roll_span : 12))
            u->roll_low = 128 - (u->roll_span ? u->roll_span : 12);
    }
    key->track = u->project->selected_track;
    key->note = key_note(u, index);
    key->identity = ++u->next_identity;
    key->sounding = true;
    emit(u, key, true, ms);
}
static FmResult track_control(FmUi *u, FmParameter parameter, float delta)
{
    FmTrack *t = track(u);
    if (!t)
        return FM_ERR_NOT_FOUND;
    float *value = NULL;
    switch (parameter)
    {
    case FM_PARAM_CUTOFF:
        value = &t->cutoff;
        break;
    case FM_PARAM_RESONANCE:
        value = &t->resonance;
        break;
    case FM_PARAM_VOLUME:
        value = &t->volume;
        break;
    case FM_PARAM_PAN:
        value = &t->pan;
        break;
    default:
        if (parameter >= FM_PARAM_SEND1 && parameter <= FM_PARAM_SEND4)
            value = &t->sends[parameter - FM_PARAM_SEND1];
        break;
    }
    if (!value)
        return FM_ERR_ARGUMENT;
    FmResult r = fm_edit_begin(u->project);
    if (r != FM_OK)
        return r;
    r = fm_edit_watch(u->project, value, sizeof(*value));
    if (r != FM_OK)
    {
        fm_edit_cancel(u->project);
        return r;
    }
    *value = bound(*value + delta, parameter == FM_PARAM_PAN ? -1 : 0,
                   parameter == FM_PARAM_RESONANCE ? .98f
                   : parameter == FM_PARAM_VOLUME  ? 2
                                                   : 1);
    r = fm_edit_commit(u->project);
    if (r == FM_OK && u->engine)
    {
        FmEvent e = {.kind = FM_EVENT_CONTROL,
                     .origin = FM_ORIGIN_LOCAL,
                     .track = t->id,
                     .parameter = (uint8_t)parameter,
                     .value = *value,
                     .frame = u->engine->frame,
                     .probability = 1};
        r = fm_engine_event(u->engine, &e);
    }
    if (r == FM_OK && u->sequence)
        r = fm_sequence_motion(u->sequence, t->id, parameter, *value, audio_time(u->now_ms));
    return r;
}
static float edit_scale(FmUi *u)
{
    unsigned row = u->rows[u->tab];
    if (u->tab == 0)
        return row == 2                           ? .01f
               : row == 3 || row == 4 || row == 6 ? 1
               : row == 5                         ? 1
               : u->scope                         ? 1
                                                  : .1f;
    if (u->tab == 1)
        return 1;
    if (u->tab == 2)
        return row == 1 || row == 2 ? .01f : 1;
    if (u->tab == 3)
        return row == 1 ? 1 : row == 2 || row == 6 || row == 7 ? .01f : 1;
    if (u->tab == 4)
        return row <= 4 || row == 7 ? .01f : 1;
    if (u->tab == 5)
        return row == 0 ? .1f : row == 1 ? .01f : 1;
    if (u->tab == 6)
        return 1;
    if (u->tab == 7)
        return row >= 4 ? .01f : 1;
    return 1;
}
static void encoder_inner(FmUi *u, unsigned index, int delta, uint64_t ms)
{
    if (index > 6 || !delta || ms < u->now_ms)
        return;
    fm_ui_tick(u, ms);
    if (u->picker == FM_PICK_NUMBER || u->picker == FM_PICK_CONFIRM || u->picker == FM_PICK_NAME)
        return;
    if (index == 0 && u->screen == FM_SCREEN_EDIT)
    {
        u->scope = index_delta(u->scope, delta, 16);
        return;
    }
    if (index == 6 && u->screen == FM_SCREEN_ROLL)
    {
        fm_ui_roll_next(u, delta);
        return;
    }
    if (index == 1 &&
        (u->screen == FM_SCREEN_MIXER || u->screen == FM_SCREEN_ARP || u->screen == FM_SCREEN_ROLL))
    {
        select_track(u, delta);
        if (u->screen == FM_SCREEN_ROLL)
            fm_ui_roll_open(u);
        return;
    }
    if (index == 0 && u->screen == FM_SCREEN_ROLL)
    {
        u->roll_low = (unsigned)bound((float)u->roll_low + delta, 0,
                                      128 - (u->roll_span ? u->roll_span : 12));
        return;
    }
    if (index == 1 && u->screen == FM_SCREEN_HOME)
    {
        select_track(u, delta);
        return;
    }
    if (index == 6 && u->screen == FM_SCREEN_HOME)
    {
        u->screen = FM_SCREEN_COMMANDS;
        u->command = 0;
        return;
    }
    if (index < 2 || index > 5)
        return;
    unsigned knob = index - 2;
    if (u->screen == FM_SCREEN_ROLL)
    {
        result(u, fm_ui_roll_edit(u, knob, delta));
        return;
    }
    if (u->screen == FM_SCREEN_ARP)
    {
        FmTrack *t = track(u);
        if (!t)
            return;
        unsigned field = u->arp_page * 4 + knob;
        double values[] = {t->arp.mode,  t->arp.division,    t->arp.octaves, t->arp.gate,
                           t->arp.swing, t->arp.probability, t->arp.latch,   t->arp.play_order};
        double maximum[] = {FM_ARP_MODE_COUNT - 1, 7, 4, 1, .49, 1, 1, 1},
               minimum[] = {0, 0, 1, .05, 0, 0, 0, 0};
        double step = field == 3 || field == 4 || field == 5 ? .01 : 1;
        result(u, fm_arp_set(
                      u->project, t->id, field,
                      fmax(minimum[field], fmin(maximum[field], values[field] + delta * step))));
        return;
    }
    if (fm_ui_workspace_encoder(u, knob, delta))
        return;
    if (u->screen == FM_SCREEN_HOME)
    {
        static const FmParameter p[] = {FM_PARAM_CUTOFF, FM_PARAM_RESONANCE, FM_PARAM_VOLUME,
                                        FM_PARAM_PAN};
        result(u, track_control(u, p[knob], delta * .01f));
    }
    else if (u->screen == FM_SCREEN_MIXER)
    {
        if (!knob)
            select_track(u, delta);
        else
            result(u, track_control(u,
                                    knob == 1   ? FM_PARAM_VOLUME
                                    : knob == 2 ? FM_PARAM_PAN
                                                : (FmParameter)(FM_PARAM_SEND1 + u->mixer_send),
                                    delta * .01f));
    }
    else if (u->screen == FM_SCREEN_EDIT)
    {
        if (!knob)
            u->rows[u->tab] =
                index_delta(u->rows[u->tab], delta, fm_ui_edit_row_count(u, u->tab) - 1);
        else if (!action_row(u))
        {
            unsigned field = knob - 1;
            float scale = edit_scale(u);
            if (knob == 2 && u->tab != 6)
                scale *= .1f;
            if (knob == 3 && u->tab != 6)
                scale *= 10;
            if (u->tab == 6 && u->rows[6] == 1)
            {
                if (knob == 3)
                    return;
                scale = 1;
            }
            result(u, scope_value(u, delta * scale, true, field));
        }
        else if (u->tab == 6 && u->rows[6] == 4)
        {
            if (knob == 1)
            {
                FmRegion *a = region(u);
                unsigned maximum = a && a->end > a->start ? a->end - a->start - 1 : 0;
                u->point = index_delta(u->point, delta, maximum);
            }
            else if (knob == 2)
            {
                FmInstrument *in = instrument(u);
                FmRegion *a = region(u);
                FmAsset *as = a ? fm_asset(u->project, a->asset) : NULL;
                if (in && a && as && u->point < a->end - a->start)
                {
                    FmSampleEdit edit = {
                        FM_EDIT_DRAW_POINT, 0, 1,
                        bound(as->pcm[(size_t)(a->start + u->point) * as->channels] / 32768.0f +
                                  delta / 128.0f,
                              -1, 1),
                        u->point};
                    edit.from =
                        a->end - a->start > 1 ? (double)u->point / (a->end - a->start - 1) : 0;
                    FmEditScope scope = {in->id, a->id, u->scope == 0};
                    result(u, fm_sample_edit(u->project, scope, &edit));
                }
            }
        }
    }
    else if (u->screen == FM_SCREEN_COMMANDS)
    {
        if (!knob)
            u->command = index_delta(u->command, delta, FM_UI_COMMAND_COUNT - 1);
    }
    else if (u->screen == FM_SCREEN_NEW)
    {
        if (!knob)
            u->source = index_delta(u->source, delta, 2);
    }
    else if (u->screen == FM_SCREEN_SOURCE)
    {
        if (!knob)
        {
            FmInstrument *in = instrument(u);
            if (in && in->type == FM_SYNTH)
            {
                static const unsigned sources[] = {1, 2, 4};
                unsigned at = u->source == 4 ? 2 : u->source == 2 ? 1 : 0;
                u->source = sources[index_delta(at, delta, 2)];
            }
            else
                u->source = 0;
        }
        else if (knob == 1)
            u->wave_shape = index_delta(u->wave_shape, delta, 3);
        else if (knob == 2)
            u->wave_points = delta < 0 ? 600 : 1024;
    }
    else if (u->screen == FM_SCREEN_CAPTURE)
    {
        if (!knob)
            u->capture_row = index_delta(u->capture_row, delta, 11);
        else
            result(u, fm_ui_capture_setting(u, delta));
    }
    else if (u->screen == FM_SCREEN_FX)
    {
        if (!knob)
        {
            u->command = index_delta(u->command, delta, 6);
            return;
        }
        FmFxConfig c = u->project->fx[u->fx_slot];
        if (knob == 3)
            c.pan = bound(c.pan + delta * .01f, -1, 1);
        else
        {
            unsigned row = u->command;
            float amount = delta * (knob == 2 ? .001f : .01f);
            if (row == 0)
                c.type = (FmFxType)index_delta((unsigned)c.type, delta, 3);
            else if (row == 1)
                c.enabled = !c.enabled;
            else if (row <= 4)
                c.parameter[row - 2] = bound(
                    c.parameter[row - 2] + amount, row == 2 && c.type == FM_FX_DELAY ? .001f : 0,
                    row == 2 && c.type == FM_FX_DELAY ? fm_fx_max_delay() : 1);
            else if (row == 5)
                c.gain = bound(c.gain + amount, 0, 2);
            else
                c.return_muted = delta > 0;
        }
        FmResult r = fm_edit_begin(u->project);
        if (r == FM_OK)
            r = fm_edit_watch(u->project, &u->project->fx[u->fx_slot], sizeof(c));
        if (r == FM_OK)
            r = u->engine ? fm_engine_fx_prepare(u->engine, u->fx_slot, &c) : FM_ERR_UNSUPPORTED;
        if (r == FM_OK)
        {
            u->project->fx[u->fx_slot] = c;
            r = fm_edit_commit(u->project);
        }
        else
            fm_edit_cancel(u->project);
        result(u, r);
    }
    else if (u->screen == FM_SCREEN_STEP)
    {
        if (!knob)
            u->selected_step = index_delta(u->selected_step, delta, 15);
        else if (knob == 1)
            u->step_note = (uint8_t)index_delta(u->step_note, delta, 127);
        else if (knob == 2)
            u->step_velocity = (uint8_t)(index_delta(u->step_velocity - 1, delta, 126) + 1);
        else
            u->step_length = bound((float)u->step_length + delta * 60, 1, FM_PPQN * 64);
    }
    else if (u->screen == FM_SCREEN_PATTERNS)
    {
        if (!knob)
            select_track(u, delta);
        else if (knob == 1)
            u->scene_scope = delta > 0;
    }
    else if (u->screen == FM_SCREEN_SONG)
    {
        if (!knob)
            u->song_row = index_delta(u->song_row, delta,
                                      u->project->song_count ? u->project->song_count - 1 : 0);
        else if (u->project->song_count)
        {
            FmSongEntry *s = &u->project->song[u->song_row];
            FmResult r = fm_edit_begin(u->project);
            if (r == FM_OK)
                r = fm_edit_watch(u->project, s, sizeof(*s));
            if (r == FM_OK)
            {
                if (knob == 1)
                    s->bars = bound((float)s->bars + delta, 1, 256);
                else if (knob == 2)
                    s->repeats = (uint16_t)(index_delta(s->repeats - 1, delta, 255) + 1);
                else
                {
                    for (unsigned i = 0; i < s->count; i++)
                        if (s->tracks[i] == u->project->selected_track)
                            s->patterns[i] = (uint8_t)index_delta(s->patterns[i], delta, 31);
                }
                r = fm_edit_commit(u->project);
            }
            else
                fm_edit_cancel(u->project);
            result(u, r);
        }
    }
}

void fm_ui_encoder(FmUi *u, unsigned index, int delta, uint64_t ms)
{
    uint64_t before = fm_edit_serial(u->project);
    unsigned key = (unsigned)u->screen * 100000u + u->tab * 1000u + u->rows[u->tab % 9] * 20u +
                   index + (u->screen == FM_SCREEN_ARP ? u->arp_page * 10000u : 0);
    encoder_inner(u, index, delta, ms);
    uint64_t after = fm_edit_serial(u->project);
    if (after != before)
    {
        if (u->gesture_serial == before && key == u->gesture_key && ms >= u->gesture_ms &&
            ms - u->gesture_ms < 250)
            (void)fm_edit_coalesce(u->project);
        u->gesture_serial = after;
        u->gesture_key = key;
        u->gesture_ms = ms;
    }
    if (after == before && u->screen == FM_SCREEN_ROLL)
        u->gesture_serial = 0;
    if (u->engine)
        (void)fm_engine_collect_cycles(u->engine);
}
