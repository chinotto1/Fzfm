/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Single-owner, non-RT host views/commands for the sequencer and sampling page. */
#include "../../firmware/workstation/sequence/fm_actions.h"
static FmPattern *workflow_pattern(Simulator *s)
{
    FmTrack *t = fm_track(&s->project, s->project.selected_track);
    return t ? fm_pattern(&s->project, t->patterns[t->selected_pattern]) : NULL;
}
static void workflow_editor(Simulator *s)
{
    FmPattern *p = workflow_pattern(s);
    FmTrack *t = fm_track(&s->project, s->project.selected_track);
    FmInstrument *in = t ? fm_instrument(&s->project, t->instrument) : NULL;
    editor_write(",\"pattern\":");
    if (p)
        editor_write(
            "{\"id\":%u,\"slot\":%u,\"length\":%.9g,\"quantize\":%.9g,\"swing\":%.9g,\"events\":%u,"
            "\"frozen\":%s,\"revision\":%llu,\"playhead\":%.9g,\"queued\":%d}",
            p->id, t->selected_pattern, p->length_ticks, p->quantize, p->swing, p->count,
            p->frozen_asset ? "true" : "false", (unsigned long long)fm_edit_serial(&s->project),
            fm_sequence_pattern_tick(s->sequence, p->id), t->queued_pattern);
    else
        editor_write("null");
    FmSequenceStatus status = fm_sequence_status(s->sequence);
    editor_write(",\"timing\":[");
    for (unsigned i = 0; i < status.candidate_count; i++)
        editor_write("%s{\"bpm\":%.6g,\"beats\":%.6g,\"confidence\":%.4g}", i ? "," : "",
                     status.candidates[i].bpm, status.candidates[i].beats,
                     status.candidates[i].confidence);
    editor_write("],\"sampleSlots\":[");
    /* Cache envelopes by checksum; don't rescan large PCM every UI refresh. */
    static struct
    {
        FmId id;
        uint32_t checksum;
        int16_t peaks[48];
    } cache[FM_ANCHORS];
    for (unsigned i = 0; i < FM_ANCHORS; i++)
    {
        FmRegion *r = in ? &in->regions[i] : NULL;
        FmAsset *a = r && r->id ? fm_asset(&s->project, r->asset) : NULL;
        editor_write(
            "%s{\"slot\":%u,\"id\":%u,\"asset\":%u,\"note\":%u,\"seconds\":%.6g,\"peaks\":[",
            i ? "," : "", i, r ? r->id : 0, a ? a->id : 0,
            in && in->type == FM_MULTI_SAMPLE ? in->base_note + i
            : r && r->id                      ? r->root
                                              : s->ui.step_note,
            a ? (double)(r->end - r->start) / a->rate : 0);
        if (a)
        {
            if (cache[i].id != a->id || cache[i].checksum != a->checksum)
            {
                cache[i].id = a->id;
                cache[i].checksum = a->checksum;
                for (unsigned bucket = 0; bucket < 48; bucket++)
                {
                    uint32_t from = (uint32_t)((uint64_t)bucket * a->frames / 48),
                             to = (uint32_t)((uint64_t)(bucket + 1) * a->frames / 48);
                    int peak = 0;
                    for (uint32_t at = from; at < to; at++)
                        for (unsigned c = 0; c < a->channels; c++)
                        {
                            int v = abs(a->pcm[(size_t)at * a->channels + c]);
                            if (v > peak)
                                peak = v;
                        }
                    cache[i].peaks[bucket] = (int16_t)(peak > 32767 ? 32767 : peak);
                }
            }
            for (unsigned j = 0; j < 48; j++)
                editor_write("%s%d", j ? "," : "", cache[i].peaks[j]);
        }
        editor_write("]}");
    }
    editor_write("]");
    editor_write(",\"mixer\":[");
    unsigned channel = 0;
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (s->project.tracks[i])
        {
            FmTrack *track = s->project.tracks[i];
            FmInstrument *sound = fm_instrument(&s->project, track->instrument);
            double peak = 0;
            for (unsigned j = 0; j < s->engine.max_voices; j++)
            {
                FmVoice *v = &s->engine.voices[j];
                if (v->active && v->track == track)
                    peak += fmax(fabs(v->last[0]), fabs(v->last[1]));
            }
            editor_write("%s{\"id\":%u,\"name\":", channel++ ? "," : "", track->id);
            editor_string(sound ? sound->name : "Missing");
            editor_write(
                ",\"level\":%.6g,\"pan\":%.6g,\"mute\":%s,\"solo\":%s,\"peak\":%.6g,\"sends\":[",
                (double)track->volume, (double)track->pan, track->mute ? "true" : "false",
                track->solo ? "true" : "false", fmin(1, peak));
            for (unsigned j = 0; j < 4; j++)
                editor_write("%s%.6g", j ? "," : "", (double)track->sends[j]);
            editor_write("]}");
        }
    editor_write("],\"arp\":");
    if (t)
        editor_write("{\"mode\":%u,\"division\":%u,\"octaves\":%u,\"gate\":%.6g,\"swing\":%.6g,"
                     "\"probability\":%.6g,\"latch\":%s,\"order\":%s}",
                     t->arp.mode, t->arp.division, t->arp.octaves, (double)t->arp.gate,
                     (double)t->arp.swing, (double)t->arp.probability,
                     t->arp.latch ? "true" : "false", t->arp.play_order ? "true" : "false");
    else
        editor_write("null");
}
int fm_sim_sequence(void *handle, unsigned operation, unsigned index, double a, double b,
                    unsigned note, unsigned velocity, double revision, unsigned expected)
{
    Simulator *s = handle;
    FmPattern *p = workflow_pattern(s);
    FmTrack *t = fm_track(&s->project, s->project.selected_track);
    if (!p || !t)
        return FM_ERR_NOT_FOUND;
    if (s->ui.picker || !isfinite(a) || !isfinite(b) || !isfinite(revision))
        return FM_ERR_ARGUMENT;
    if (operation != 14 && expected && p->id != expected)
        return FM_ERR_BUSY;
    if (operation >= 1 && operation <= 3 && revision != (double)fm_edit_serial(&s->project))
        return FM_ERR_BUSY;
    FmResult r = FM_OK;
    if (operation == 0)
        r = fm_pattern_quantize(&s->project, p->id, a);
    else if (operation >= 1 && operation <= 3)
    {
        if (note > 127 || velocity > 127)
            return FM_ERR_RANGE;
        r = fm_pattern_note_edit(&s->project, p->id, operation - 1, index, a, b, (uint8_t)note,
                                 (uint8_t)velocity);
    }
    else if (operation == 4 || operation == 5 || operation == 12)
    {
        if (p->frozen_asset)
            return FM_ERR_BUSY;
        if (operation == 4 && (a < 20 || a > 300 || s->project.clock_source != FM_CLOCK_INTERNAL))
            return FM_ERR_RANGE;
        if (operation == 5)
        {
            if (a < FM_PPQN / 4 || a > FM_PPQN * 256)
                return FM_ERR_RANGE;
            for (unsigned i = 0; i < p->count; i++)
                if (p->events[i].tick > a || (p->events[i].kind == FM_EVENT_NOTE_ON &&
                                              p->events[i].velocity && p->events[i].tick >= a))
                    return FM_ERR_RANGE;
        }
        if (operation == 12 && (a < 0 || a > .49))
            return FM_ERR_RANGE;
        r = fm_edit_begin(&s->project);
        if (r == FM_OK)
            r = fm_edit_watch(&s->project, operation == 4 ? (void *)&s->project.bpm : (void *)p,
                              operation == 4 ? sizeof(s->project.bpm) : sizeof(*p));
        if (r == FM_OK && operation == 4)
            r = fm_edit_watch(&s->project, &s->project.tempo_established,
                              sizeof(s->project.tempo_established));
        if (r == FM_OK)
        {
            if (operation == 4)
            {
                s->project.bpm = a;
                s->project.tempo_established = true;
            }
            else if (operation == 5)
                p->length_ticks = a;
            else
                p->swing = a;
            r = fm_edit_commit(&s->project);
        }
        else
            fm_edit_cancel(&s->project);
    }
    else if (operation == 6)
    {
        if (index >= FM_PATTERN_SLOTS)
            return FM_ERR_RANGE;
        FmId id;
        if (!t->patterns[index])
            r = fm_pattern_create(&s->project, t->id, (uint8_t)index, &id);
        if (r == FM_OK)
            r = fm_sequence_queue_pattern(s->sequence, t->id, (uint8_t)index);
    }
    else if (operation == 7)
    {
        FmSequenceStatus st = fm_sequence_status(s->sequence);
        r = st.armed || st.recording
                ? fm_sequence_record_close(s->sequence, fm_sequence_frame(s->sequence))
                : fm_sequence_record_arm(s->sequence, t->id);
    }
    else if (operation == 8)
    {
        if (a < 1 || a > 64)
            return FM_ERR_RANGE;
        FmFrame lookback = (FmFrame)(a * (s->project.beats_per_bar ? s->project.beats_per_bar : 4) *
                                     60 / s->project.bpm * FM_RATE);
        r = fm_sequence_retrospective(s->sequence, t->id, fm_sequence_frame(s->sequence), lookback);
    }
    else if (operation == 9)
        r = index > 255 ? FM_ERR_RANGE : fm_sequence_candidate(s->sequence, (uint8_t)index);
    else if (operation == 10)
    {
        if (fm_sequence_playing(s->sequence))
            fm_sequence_stop(s->sequence, fm_ui_sequence_dispatch, &s->engine);
        else
            fm_sequence_start(s->sequence);
    }
    else if (operation == 11)
        r = fm_ui_history(&s->ui, false);
    else if (operation == 15)
        fm_sequence_stop(s->sequence, fm_ui_sequence_dispatch, &s->engine);
    else if (operation == 13 || operation == 14)
    {
        if (note > 127)
            return FM_ERR_RANGE;
        FmEvent *key = &s->performed[note];
        if (operation == 13)
        {
            if (key->velocity)
                return FM_OK;
            *key = (FmEvent){.kind = FM_EVENT_NOTE_ON,
                             .origin = FM_ORIGIN_LOCAL,
                             .track = t->id,
                             .identity = ++s->ui.next_identity,
                             .note = (uint8_t)note,
                             .velocity = 100,
                             .probability = 1};
        }
        else
        {
            if (!key->velocity)
                return FM_OK;
            key->kind = FM_EVENT_NOTE_OFF;
            key->velocity = 0;
        }
        key->frame = s->engine.frame;
        bool consumed = false;
        r = fm_sequence_arp_input(s->sequence, key, &consumed);
        if (r == FM_OK && !consumed)
        {
            r = fm_engine_event(&s->engine, key);
            if (r == FM_OK)
                r = fm_sequence_live(s->sequence, key);
        }
    }
    else
        return FM_ERR_ARGUMENT;
    if (operation != 13 && operation != 14 && operation != 15)
        s->ui.screen = FM_SCREEN_ROLL;
    s->ui.last_result = r;
    snprintf(s->ui.status, sizeof(s->ui.status), "%s",
             r == FM_OK ? "PATTERN READY - QUANTIZE IS REVERSIBLE" : fm_result_string(r));
    return r;
}
typedef struct
{
    uint64_t key;
    uint32_t head, tail;
} NoteBucket;
const char *fm_sim_notes(void *handle, double from, double to)
{
    Simulator *s = handle;
    FmPattern *p = workflow_pattern(s);
    if (!p || !isfinite(from) || !isfinite(to) || from < 0 || to <= from)
        return NULL;
    unsigned cap = 1;
    while (cap < p->count * 2 + 1)
        cap *= 2;
    NoteBucket *buckets = calloc(cap, sizeof(*buckets));
    uint32_t *next = calloc(p->count + 1, sizeof(*next)),
             *offs = malloc((p->count + 1) * sizeof(*offs));
    if (!buckets || !next || !offs)
    {
        free(buckets);
        free(next);
        free(offs);
        return NULL;
    }
    for (unsigned i = 0; i < p->count; i++)
        offs[i] = UINT32_MAX;
    for (unsigned i = 0; i < p->count; i++)
    {
        FmEvent *e = &p->events[i];
        bool on = e->kind == FM_EVENT_NOTE_ON && e->velocity;
        bool off = e->kind == FM_EVENT_NOTE_OFF || (e->kind == FM_EVENT_NOTE_ON && !e->velocity);
        if (!on && !off)
            continue;
        uint64_t key = (((uint64_t)e->identity << 16) | ((uint64_t)e->origin << 12) |
                        ((uint64_t)e->channel << 8) | (e->identity ? 0 : e->note)) +
                       1;
        unsigned bucket = (unsigned)((key ^ (key >> 27)) * 2654435761u) & (cap - 1);
        while (buckets[bucket].key && buckets[bucket].key != key)
            bucket = (bucket + 1) & (cap - 1);
        NoteBucket *q = &buckets[bucket];
        q->key = key;
        if (on)
        {
            if (q->tail)
                next[q->tail - 1] = i + 1;
            else
                q->head = i + 1;
            q->tail = i + 1;
        }
        else if (q->head)
        {
            unsigned start = q->head - 1;
            offs[start] = i;
            q->head = next[start];
            if (!q->head)
                q->tail = 0;
        }
    }
    editor_at = 0;
    editor_write("{\"pattern\":%u,\"revision\":%llu,\"notes\":[", p->id,
                 (unsigned long long)fm_edit_serial(&s->project));
    unsigned count = 0, total = 0, visible = 0;
    for (unsigned i = 0; i < p->count; i++)
    {
        FmEvent *e = &p->events[i];
        if (e->kind != FM_EVENT_NOTE_ON || !e->velocity)
            continue;
        total++;
        double rawend = offs[i] == UINT32_MAX ? p->length_ticks : p->events[offs[i]].tick;
        double tick = fm_pattern_view_tick(p, e->tick), end = fm_pattern_view_tick(p, rawend);
        if (end <= tick)
            end = fmin(p->length_ticks, tick + fmax(1, rawend - e->tick));
        if (end < from || tick >= to)
            continue;
        visible++;
        if (count >= 4096)
            continue;
        editor_write("%s{\"index\":%u,\"id\":%u,\"note\":%u,\"velocity\":%u,\"tick\":%.9g,"
                     "\"duration\":%.9g,\"rawTick\":%.9g,\"rawDuration\":%.9g}",
                     count++ ? "," : "", i, e->identity, e->note, e->velocity, tick, end - tick,
                     e->tick, fmax(1, rawend - e->tick));
    }
    editor_write("],\"total\":%u,\"truncated\":%s}", total, visible > 4096 ? "true" : "false");
    free(buckets);
    free(next);
    free(offs);
    return editor_json;
}
int fm_sim_sampling(void *handle, unsigned operation, unsigned slot, unsigned value)
{
    Simulator *s = handle;
    FmTrack *t = fm_track(&s->project, s->project.selected_track);
    FmInstrument *in = t ? fm_instrument(&s->project, t->instrument) : NULL;
    if (!in || in->type == FM_SYNTH || slot >= FM_ANCHORS || s->ui.picker)
        return FM_ERR_ARGUMENT;
    FmRegion *r = &in->regions[slot];
    FmResult result = FM_OK;
    if (operation == 0)
    {
        s->ui.scope = slot + 1;
        s->capture_target = slot;
        s->capture_targeted = true;
        s->capture_replace = false;
    }
    else if (operation == 1 || operation == 2)
    {
        if (value > 127 || s->capture.state == FM_CAPTURE_CAPTURING)
            return FM_ERR_RANGE;
        fm_capture_destroy(&s->capture);
        result = fm_capture_begin_slot(&s->capture, &s->project, t->id, (uint8_t)value,
                                       &s->ui.capture_config, (uint8_t)slot, operation == 2);
        if (result == FM_OK)
        {
            s->capture_target = slot;
            s->capture_targeted = true;
            s->capture_replace = operation == 2;
            s->ui.step_note = s->capture.root;
            s->ui.scope = slot + 1;
        }
    }
    else if (operation == 3)
    {
        result = r->id ? fm_region_remove(&s->project, r->id) : FM_ERR_NOT_FOUND;
        s->capture_targeted = s->capture_replace = false;
    }
    else if (operation == 4)
    {
        if (!r->id)
            return FM_ERR_NOT_FOUND;
        s->audition =
            (FmEvent){.kind = FM_EVENT_NOTE_ON,
                      .origin = FM_ORIGIN_PICKER,
                      .track = t->id,
                      .identity = 0x7ffffffeu,
                      .note = in->type == FM_MULTI_SAMPLE ? in->base_note + slot : r->root,
                      .velocity = 100,
                      .probability = 1,
                      .frame = s->engine.frame};
        result = fm_engine_event(&s->engine, &s->audition);
    }
    else if (operation == 5)
    {
        s->audition.kind = FM_EVENT_NOTE_OFF;
        s->audition.velocity = 0;
        s->audition.frame = s->engine.frame;
        result = fm_engine_event(&s->engine, &s->audition);
    }
    else
        return FM_ERR_ARGUMENT;
    s->ui.screen = FM_SCREEN_CAPTURE;
    s->ui.last_result = result;
    return result;
}

int fm_sim_performance(void *handle, unsigned operation, unsigned track, unsigned field,
                       double value)
{
    Simulator *s = handle;
    FmTrack *t = fm_track(&s->project, track);
    if (!t || !isfinite(value) || s->ui.picker)
        return FM_ERR_ARGUMENT;
    if (!operation)
        return fm_arp_set(&s->project, track, field, value);
    if ((operation != 1 && operation != 2) || field > 7)
        return FM_ERR_ARGUMENT;
    if (value < (field == 1 ? -1 : 0) || value > (field == 0 ? 2 : 1))
        return FM_ERR_RANGE;
    if (field >= 6 && value != 0 && value != 1)
        return FM_ERR_RANGE;
    float current = field == 0   ? t->volume
                    : field == 1 ? t->pan
                    : field < 6  ? t->sends[field - 2]
                    : field == 6 ? t->mute
                                 : t->solo;
    if (current == (float)value)
        return FM_OK;
    uint64_t before = fm_edit_serial(&s->project);
    FmResult r = fm_edit_begin(&s->project);
    if (r == FM_OK)
        r = fm_edit_watch(&s->project, t, sizeof(*t));
    if (r == FM_OK)
    {
        if (field == 0)
            t->volume = (float)value;
        else if (field == 1)
            t->pan = (float)value;
        else if (field < 6)
            t->sends[field - 2] = (float)value;
        else if (field == 6)
            t->mute = value != 0;
        else
            t->solo = value != 0;
        r = fm_edit_commit(&s->project);
    }
    else
        fm_edit_cancel(&s->project);
    if (r == FM_OK && field < 6)
    {
        FmParameter parameter = field == 0   ? FM_PARAM_VOLUME
                                : field == 1 ? FM_PARAM_PAN
                                             : (FmParameter)(FM_PARAM_SEND1 + field - 2);
        FmEvent event = {.kind = FM_EVENT_CONTROL,
                         .origin = FM_ORIGIN_LOCAL,
                         .track = track,
                         .parameter = (uint8_t)parameter,
                         .value = (float)value,
                         .frame = s->engine.frame,
                         .probability = 1};
        r = fm_engine_event(&s->engine, &event);
        if (r == FM_OK)
            r = fm_sequence_motion(s->sequence, track, parameter, (float)value, s->engine.frame);
    }
    if (r == FM_OK && operation == 2)
    {
        uint64_t now = s->engine.frame / 36, after = fm_edit_serial(&s->project);
        unsigned key = 0x30000000u ^ (track * 8 + field);
        if (s->ui.gesture_serial == before && s->ui.gesture_key == key && now >= s->ui.gesture_ms &&
            now - s->ui.gesture_ms < 250)
            (void)fm_edit_coalesce(&s->project);
        s->ui.gesture_serial = after;
        s->ui.gesture_key = key;
        s->ui.gesture_ms = now;
    }
    return r;
}
