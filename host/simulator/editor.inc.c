/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Accessible browser controls over the same C instrument. Included by bridge.c.
 * All mutations run on its single non-real-time owner. No JS DSP/state model. */
#include "../../firmware/workstation/dsp/fm_synthesis.h"
#include <stdarg.h>
static void workflow_editor(Simulator *s);
static char editor_json[1048576];
static size_t editor_at;
static void editor_write(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void editor_write(const char *format, ...)
{
    if (editor_at >= sizeof(editor_json) - 1)
        return;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(editor_json + editor_at, sizeof(editor_json) - editor_at, format, args);
    va_end(args);
    if (n > 0)
        editor_at += (size_t)n < sizeof(editor_json) - editor_at
                         ? (size_t)n
                         : sizeof(editor_json) - editor_at - 1;
}
static void editor_string(const char *value)
{
    editor_write("\"");
    for (const unsigned char *p = (const unsigned char *)value; *p; p++)
        if (*p == '"' || *p == '\\')
            editor_write("\\%c", *p);
        else if (*p < 32 || *p > 126)
            editor_write("\\u%04x", *p);
        else
            editor_write("%c", *p);
    editor_write("\"");
}
static FmRegion *editor_region(Simulator *s)
{
    FmTrack *t = fm_track(&s->project, s->project.selected_track);
    FmInstrument *in = t ? fm_instrument(&s->project, t->instrument) : NULL;
    if (!in)
        return NULL;
    if (s->ui.scope && s->ui.scope <= FM_ANCHORS && in->regions[s->ui.scope - 1].id)
        return &in->regions[s->ui.scope - 1];
    for (unsigned i = 0; i < FM_ANCHORS; i++)
        if (in->regions[i].id)
            return &in->regions[i];
    return NULL;
}
const char *fm_sim_editor(void *handle)
{
    Simulator *s = handle;
    FmTrack *t = fm_track(&s->project, s->project.selected_track);
    FmInstrument *in = t ? fm_instrument(&s->project, t->instrument) : NULL;
    FmRegion *r = editor_region(s);
    FmAsset *a = r ? fm_asset(&s->project, r->asset) : NULL;
    editor_at = 0;
    editor_write(
        "{\"selected\":%u,\"stage\":%u,\"harmonic\":%u,\"source\":%u,\"additive\":%s,\"tracks\":[",
        s->project.selected_track, s->ui.stage, s->ui.harmonic, s->ui.source,
        in && in->additive ? "true" : "false");
    unsigned count = 0;
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (s->project.tracks[i])
        {
            FmTrack *item = s->project.tracks[i];
            FmInstrument *instrument = fm_instrument(&s->project, item->instrument);
            editor_write("%s{\"id\":%u,\"name\":", count++ ? "," : "", item->id);
            editor_string(instrument ? instrument->name : "Missing instrument");
            editor_write(",\"type\":%u}", instrument ? (unsigned)instrument->type : 0);
        }
    editor_write("],\"slots\":[");
    count = 0;
    if (in)
        for (unsigned i = 0; i < FM_ANCHORS; i++)
            if (in->regions[i].id)
                editor_write("%s%u", count++ ? "," : "", i + 1);
    editor_write("],\"harmonics\":[");
    for (unsigned i = 0; i < FM_HARMONICS; i++)
        editor_write("%s%u", i ? "," : "", in ? in->harmonics[i] : 0);
    editor_write("],\"wave\":[");
    /* A complete editable cycle; ordinary long samples use an overview. */
    unsigned points = a && r && r->end > r->start ? (a->cycle ? r->end - r->start : 256) : 0;
    if (points > 1024)
        points = 1024;
    for (unsigned i = 0; i < points; i++)
    {
        uint32_t at = r->start + (uint32_t)((uint64_t)i * (r->end - r->start - 1) /
                                            (points > 1 ? points - 1 : 1));
        editor_write("%s%d", i ? "," : "", a->pcm[(size_t)at * a->channels]);
    }
    editor_write("],\"cycle\":%s,\"amp\":", a && a->cycle ? "true" : "false");
    for (unsigned which = 0; which < 2; which++)
    {
        const FmEnvelope *e = r ? (which ? &r->filter : &r->amp) : NULL;
        if (which)
            editor_write(",\"filter\":");
        if (!e)
        {
            editor_write("null");
            continue;
        }
        editor_write("{\"count\":%u,\"sustain\":%u,\"release\":%u,\"stages\":[", e->count,
                     e->sustain, e->release);
        for (unsigned i = 0; i < e->count && i < FM_ENV_STAGES; i++)
            editor_write("%s[%g,%g]", i ? "," : "", (double)e->stage[i].milliseconds,
                         (double)e->stage[i].level);
        editor_write("]}");
    }
    editor_write(",\"lfo\":");
    if (r)
    {
        editor_write(
            "{\"rate\":%g,\"depth\":%g,\"shape\":%u,\"destination\":%u,\"free\":%s,\"samples\":[",
            (double)r->lfo_rate, (double)r->lfo_depth, r->lfo_shape & 0x7fu, r->lfo_destination,
            r->lfo_shape & 128 ? "true" : "false");
        for (unsigned i = 0; i < 224; i++)
        {
            double cycles = (double)i / 223 * r->lfo_rate;
            editor_write(
                "%s%d", i ? "," : "",
                (int)(32767 * r->lfo_depth * fm_lfo_sample(cycles, r->lfo_shape & 0x7fu, r->id)));
        }
        editor_write("]}");
    }
    else
        editor_write("null");
    workflow_editor(s);
    editor_write("}");
    return editor_json;
}
int fm_sim_action(void *handle, unsigned action, unsigned value)
{
    Simulator *s = handle;
    if (!s || s->ui.picker != FM_PICK_NONE)
        return FM_ERR_BUSY;
    if ((action == 0 || action == 2 || action == 3) && s->capture.state == FM_CAPTURE_CAPTURING)
        return FM_ERR_BUSY;
    FmResult result = FM_OK;
    FmTrack *t = fm_track(&s->project, s->project.selected_track);
    FmInstrument *in = t ? fm_instrument(&s->project, t->instrument) : NULL;
    if (action == 0 && value <= 5)
    {
        s->capture_targeted = s->capture_replace = false;
        if (fm_sequence_playing(s->sequence))
            return FM_ERR_BUSY;
        FmId previous = s->project.selected_track, id;
        result = fm_instrument_create(&s->project,
                                      value == 0   ? FM_MULTI_SAMPLE
                                      : value == 1 ? FM_SAMPLE
                                                   : FM_SYNTH,
                                      &id);
        if (result == FM_OK)
        {
            fm_ui_sync_view(&s->ui);
            if (value == 5)
                s->ui.wave_shape = FM_WAVE_SINE;
            result = fm_ui_use_source(&s->ui, value == 2                 ? 1
                                              : value == 3 || value == 5 ? 2
                                              : value == 4               ? 4
                                                                         : 0);
            if (result != FM_OK)
            {
                FmTrack *created = fm_track(&s->project, id);
                if (created && fm_remove_empty_instrument(&s->project, id) == FM_OK)
                    s->project.selected_track = previous;
                fm_ui_sync_view(&s->ui);
            }
            else if (value >= 2)
            {
                s->ui.screen = FM_SCREEN_EDIT;
                s->ui.tab = 6;
                s->ui.rows[6] = value == 2 ? 1 : 4;
            }
        }
    }
    else if (action == 1 && (value == 3 || value == 4 || value == 5 || value == 6))
    {
        if (!in)
            return FM_ERR_NOT_FOUND;
        if (value == 6 && in->type != FM_SYNTH)
            return FM_ERR_ARGUMENT;
        s->ui.screen = FM_SCREEN_EDIT;
        s->ui.tab = value;
    }
    else if (action == 2)
    {
        if (!fm_track(&s->project, value))
            return FM_ERR_NOT_FOUND;
        s->capture_targeted = s->capture_replace = false;
        s->project.selected_track = value;
        fm_ui_sync_view(&s->ui);
        s->ui.screen = FM_SCREEN_HOME;
    }
    else if (action == 3)
    {
        if (!in)
            return FM_ERR_NOT_FOUND;
        if (s->capture.buffer && s->capture.config.manual != s->ui.capture_config.manual)
            fm_capture_destroy(&s->capture);
        result = fm_ui_use_source(&s->ui, 0);
    }
    else if (action == 4)
    {
        if (!s->capture.buffer)
        {
            s->ui.screen = FM_SCREEN_HOME;
            return FM_OK;
        }
        result = fm_capture_finish(&s->capture);
        s->ui.screen = FM_SCREEN_HOME;
    }
    else if ((action == 5 || action == 6) && value <= 3)
    {
        if (!in || in->type != FM_SYNTH)
            return FM_ERR_ARGUMENT;
        FmId id;
        if (action == 5)
            result = fm_synthesis_preset(&s->project, in->id, (FmWaveShape)value, 1024, &id);
        else
        {
            uint16_t levels[FM_HARMONICS] = {0};
            for (unsigned h = 0; h < FM_HARMONICS; h++)
                levels[h] = value == 0   ? (h == 0 ? 256 : 0)
                            : value == 1 ? 256 / (h + 1)
                            : value == 2 ? (h % 2 ? 0 : 256 / (h + 1))
                                         : 256;
            result = fm_synthesis_additive(&s->project, in->id, levels, 1024, &id);
        }
        s->ui.screen = FM_SCREEN_EDIT;
        s->ui.tab = 6;
        s->ui.rows[6] = action == 6 ? 1 : 4;
    }
    else if (action == 7)
    {
        result = fm_ui_history(&s->ui, false);
    }
    else if (action == 9)
        fm_ui_roll_open(&s->ui);
    else if (action == 10)
        s->ui.screen = FM_SCREEN_MIXER;
    else if (action == 11)
        s->ui.screen = FM_SCREEN_ARP;
    else if (action == 8 && value <= FM_ANCHORS)
        s->ui.scope = value;
    else
        return FM_ERR_ARGUMENT;
    s->ui.last_result = result;
    snprintf(s->ui.status, sizeof(s->ui.status), "%s",
             result == FM_OK ? "READY - PLAY KEYS / SAVE TO KEEP" : fm_result_string(result));
    return result;
}
int fm_sim_parameter(void *handle, unsigned tab, unsigned row, unsigned field, double value)
{
    Simulator *s = handle;
    return fm_ui_set_parameter(&s->ui, tab, row, field, value);
}
int fm_sim_draw(void *handle, const int16_t *pcm, unsigned count)
{
    Simulator *s = handle;
    FmTrack *t = fm_track(&s->project, s->project.selected_track);
    FmInstrument *in = t ? fm_instrument(&s->project, t->instrument) : NULL;
    if (!in || in->type != FM_SYNTH || s->ui.picker != FM_PICK_NONE || count != 1024)
        return FM_ERR_ARGUMENT;
    FmId id;
    FmResult result = fm_synthesis_draw(&s->project, in->id, pcm, count, 60, &id);
    s->ui.last_result = result;
    if (result == FM_OK)
    {
        s->ui.screen = FM_SCREEN_EDIT;
        s->ui.tab = 6;
        s->ui.rows[6] = 4;
    }
    return result;
}
int fm_sim_input(void *handle, const int16_t *pcm, unsigned count)
{
    Simulator *s = handle;
    if (!s || !pcm || count < 36 || count > FM_RATE * 10 || s->ui.screen != FM_SCREEN_CAPTURE ||
        s->ui.picker != FM_PICK_NONE)
        return FM_ERR_ARGUMENT;
    /* A recorded/file take is deliberate, so retain its internal silences.
     * The original capture state machine owns allocation, slot mapping and commit. */
    FmCaptureConfig cfg = s->ui.capture_config;
    cfg.manual = true;
    cfg.maximum_frames = FM_RATE * 10;
    fm_capture_destroy(&s->capture);
    FmResult result = s->capture_targeted
                          ? fm_capture_begin_slot(&s->capture, &s->project,
                                                  s->project.selected_track, s->ui.step_note, &cfg,
                                                  (uint8_t)s->capture_target, s->capture_replace)
                          : fm_capture_begin(&s->capture, &s->project, s->project.selected_track,
                                             s->ui.step_note, &cfg);
    if (result == FM_OK)
        result = fm_capture_manual_start(&s->capture);
    for (unsigned at = 0; result == FM_OK && at < count; at += 128)
    {
        unsigned n = count - at < 128 ? count - at : 128;
        fm_capture_accept(&s->capture, pcm + at, n, s->frames + at);
        result = fm_capture_service(&s->capture);
    }
    if (result == FM_OK)
        result = fm_capture_finish(&s->capture);
    s->ui.last_result = result;
    if (result == FM_OK)
        s->capture_targeted = s->capture_replace = false;
    fm_ui_tick(&s->ui, s->frames / 36);
    snprintf(s->ui.status, sizeof(s->ui.status), "%s",
             result == FM_OK ? "TAKE CAPTURED - FINISH TO PLAY" : fm_result_string(result));
    return result;
}
int fm_sim_harmonics(void *handle, const uint16_t *levels, unsigned count)
{
    Simulator *s = handle;
    FmTrack *t = fm_track(&s->project, s->project.selected_track);
    FmInstrument *in = t ? fm_instrument(&s->project, t->instrument) : NULL;
    if (!in || in->type != FM_SYNTH || s->ui.picker != FM_PICK_NONE || count != FM_HARMONICS)
        return FM_ERR_ARGUMENT;
    FmId id;
    FmResult result = fm_synthesis_additive(&s->project, in->id, levels, 1024, &id);
    if (result == FM_OK)
    {
        s->ui.screen = FM_SCREEN_EDIT;
        s->ui.tab = 6;
        s->ui.rows[6] = 1;
    }
    s->ui.last_result = result;
    return result;
}
