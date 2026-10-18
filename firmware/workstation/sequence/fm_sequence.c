/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fm_sequence.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef FM_SEQUENCE_HELD_LIMIT
#define FM_SEQUENCE_HELD_LIMIT 256u
#endif
#define HELD_LIMIT FM_SEQUENCE_HELD_LIMIT
#define SONG_LIMIT 256u
typedef struct
{
    FmEvent on;
    bool released;
} Held;
typedef struct
{
    FmId track;
    uint8_t origin, channel;
    bool down;
} Pedal;
typedef struct
{
    FmTrack *track;
    FmPattern *pattern;
    double start;
    uint32_t cursor;
    uint8_t chain[FM_SEQUENCE_CHAIN_MAX], count, position;
    bool launched, frozen_active;
    const FmEvent *events_seen;
    FmId frozen_seen;
} TrackRun;
typedef struct
{
    FmEvent source;
    uint32_t identity;
} PlayingNote;
struct FmSequencer
{
    FmProject *project;
    FmEvent *history, *take;
    uint32_t history_cap, take_cap, head, history_count, take_count;
    Held boundary[HELD_LIMIT];
    uint16_t boundary_count;
    Pedal pedals[HELD_LIMIT];
    uint16_t pedal_count;
    PlayingNote playing_notes[HELD_LIMIT];
    uint16_t playing_count;
    TrackRun tracks[FM_OBJECT_LIMIT];
    uint16_t track_count;
    FmSequenceStatus status;
    FmId record_track, record_pattern;
    bool running, overdub, song_running, song_loop, take_adopt;
    FmSongEntry *arrangement;
    uint16_t arrangement_count;
    double arrangement_start;
    bool arranging, arrangement_overflow;
    double tick, compensation, next_bar, song_end, record_tick;
    FmFrame frame, take_start, take_end, last_clock;
    double external_bpm;
    uint32_t rng, identity;
    uint16_t song_index, song_repeat;
    int queued_song;
    FmSequenceFrozen frozen_callback;
    void *frozen_user;
};

static bool live_origin(FmOrigin o)
{
    return o == FM_ORIGIN_LOCAL || o == FM_ORIGIN_USB || o == FM_ORIGIN_TRS;
}
static bool note_on(const FmEvent *e)
{
    return e->kind == FM_EVENT_NOTE_ON && e->velocity != 0;
}
static bool note_off(const FmEvent *e)
{
    return e->kind == FM_EVENT_NOTE_OFF || (e->kind == FM_EVENT_NOTE_ON && e->velocity == 0);
}
static bool same_note(const FmEvent *on, const FmEvent *off)
{
    if (on->track != off->track || on->origin != off->origin || on->channel != off->channel)
        return false;
    if (off->identity != 0)
        return on->identity == off->identity;
    return on->note == off->note;
}
static bool pedal_down(const Pedal *p, uint16_t n, const FmEvent *e)
{
    for (uint16_t i = 0; i < n; i++)
        if (p[i].track == e->track && p[i].origin == e->origin && p[i].channel == e->channel)
            return p[i].down;
    return false;
}
static void remove_held(Held *h, uint16_t *n, uint16_t at)
{
    memmove(h + at, h + at + 1, (size_t)(--*n - at) * sizeof(*h));
}
/* The state immediately preceding the oldest retained ring event reconstructs
 * held/sustained notes when dense input evicts their original note-on. */
static void state_event(Held *h, uint16_t *n, Pedal *p, uint16_t *pn, const FmEvent *e,
                        bool *overflow)
{
    if (note_on(e))
    {
        if (*n == HELD_LIMIT)
        {
            *overflow = true;
            return;
        }
        h[*n].on = *e;
        h[*n].released = false;
        ++*n;
    }
    else if (note_off(e))
    {
        for (uint16_t i = 0; i < *n; i++)
            if (!h[i].released && same_note(&h[i].on, e))
            {
                if (pedal_down(p, *pn, e))
                    h[i].released = true;
                else
                    remove_held(h, n, i);
                break;
            }
    }
    else if (e->kind == FM_EVENT_SUSTAIN)
    {
        uint16_t i = 0;
        while (i < *pn &&
               !(p[i].track == e->track && p[i].origin == e->origin && p[i].channel == e->channel))
            ++i;
        if (i == *pn && *pn < HELD_LIMIT)
        {
            p[i] = (Pedal){e->track, (uint8_t)e->origin, e->channel, false};
            ++*pn;
        }
        if (i < *pn)
            p[i].down = e->value >= 0.5f;
        else
            *overflow = true;
        if (e->value < 0.5f)
            for (uint16_t j = *n; j > 0; j--)
                if (h[j - 1].released && h[j - 1].on.track == e->track &&
                    h[j - 1].on.origin == e->origin && h[j - 1].on.channel == e->channel)
                    remove_held(h, n, (uint16_t)(j - 1));
    }
}
static TrackRun *run_track(FmSequencer *s, FmId track)
{
    for (uint16_t i = 0; i < s->track_count; i++)
        if (s->tracks[i].track->id == track)
            return &s->tracks[i];
    FmTrack *t = fm_track(s->project, track);
    if (!t || s->track_count == FM_OBJECT_LIMIT)
        return NULL;
    TrackRun *r = &s->tracks[s->track_count++];
    memset(r, 0, sizeof(*r));
    r->track = t;
    r->count = t->chain_count;
    memcpy(r->chain, t->chain, t->chain_count);
    r->pattern = fm_pattern(s->project, t->patterns[t->playing_pattern]);
    if (r->pattern)
    {
        r->events_seen = r->pattern->events;
        r->frozen_seen = r->pattern->frozen_asset;
    }
    return r;
}
static void sync_tracks(FmSequencer *s)
{
    for (uint16_t i = 0; i < s->track_count;)
    {
        if (!fm_track(s->project, s->tracks[i].track->id))
        {
            memmove(s->tracks + i, s->tracks + i + 1,
                    (size_t)(--s->track_count - i) * sizeof(TrackRun));
        }
        else
            i++;
    }
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (s->project->tracks[i])
            run_track(s, s->project->tracks[i]->id);
}
FmSequencer *fm_sequence_create(FmProject *p, uint32_t history, uint32_t take)
{
    if (!p || history < 2 || take < 2 || history > 65536u || take > 65536u)
        return NULL;
    FmSequencer *s = fm_alloc(p, FM_MEM_EVENTS, sizeof(*s));
    if (!s)
        return NULL;
    memset(s, 0, sizeof(*s));
    s->project = p;
    s->history_cap = history;
    s->take_cap = take;
    s->history = fm_alloc(p, FM_MEM_EVENTS, (size_t)history * sizeof(FmEvent));
    s->take = fm_alloc(p, FM_MEM_EVENTS, (size_t)take * sizeof(FmEvent));
    if (!s->history || !s->take)
    {
        fm_sequence_destroy(s);
        return NULL;
    }
    s->rng = 1;
    s->identity = 1;
    s->queued_song = -1;
    sync_tracks(s);
    return s;
}
void fm_sequence_destroy(FmSequencer *s)
{
    if (!s)
        return;
    FmProject *p = s->project;
    if (s->arrangement)
        fm_dealloc(p, FM_MEM_METADATA, s->arrangement, 64 * sizeof(FmSongEntry));
    fm_dealloc(p, FM_MEM_EVENTS, s->history, (size_t)s->history_cap * sizeof(FmEvent));
    fm_dealloc(p, FM_MEM_EVENTS, s->take, (size_t)s->take_cap * sizeof(FmEvent));
    fm_dealloc(p, FM_MEM_EVENTS, s, sizeof(*s));
}
static double bar_ticks(const FmSequencer *s)
{
    return (s->project->beats_per_bar ? s->project->beats_per_bar : 4) * FM_PPQN;
}
void fm_sequence_start(FmSequencer *s)
{
    if (!s)
        return;
    sync_tracks(s);
    s->running = true;
    s->tick = 0;
    s->compensation = 0;
    s->next_bar = bar_ticks(s);
    s->playing_count = 0;
    for (uint16_t i = 0; i < s->track_count; i++)
    {
        TrackRun *r = &s->tracks[i];
        r->start = 0;
        r->cursor = 0;
        r->position = 0;
        r->launched = false;
        r->pattern = fm_pattern(s->project, r->track->patterns[r->track->playing_pattern]);
        r->events_seen = r->pattern ? r->pattern->events : NULL;
        r->frozen_seen = r->pattern ? r->pattern->frozen_asset : 0;
    }
}
static void send(FmSequenceDispatch cb, void *user, FmEvent *e, uint32_t offset)
{
    if (cb)
        cb(user, e, offset);
}
static void release_track(FmSequencer *s, FmId track, uint32_t offset, FmSequenceDispatch cb,
                          void *user)
{
    for (uint16_t i = 0; i < s->track_count; i++)
    {
        TrackRun *r = &s->tracks[i];
        if ((!track || r->track->id == track) && r->frozen_active)
        {
            if (s->frozen_callback)
                s->frozen_callback(s->frozen_user, r->track->id, 0, s->frame, 0, offset);
            r->frozen_active = false;
        }
    }
    for (uint16_t i = s->playing_count; i > 0; i--)
    {
        PlayingNote *n = &s->playing_notes[i - 1];
        if (track && n->source.track != track)
            continue;
        FmEvent e = n->source;
        e.kind = FM_EVENT_NOTE_OFF;
        e.velocity = 0;
        e.origin = FM_ORIGIN_SEQUENCER;
        e.identity = n->identity;
        e.frame = s->frame;
        send(cb, user, &e, offset);
        memmove(n, n + 1, (size_t)(--s->playing_count - (i - 1)) * sizeof(*n));
    }
    /* Sustain is scoped to the sequencer origin, so live keyboard gates survive. */
    for (uint16_t t = 0; t < s->track_count; t++)
        if (!track || s->tracks[t].track->id == track)
        {
            FmEvent e = {0};
            e.kind = FM_EVENT_SUSTAIN;
            e.track = s->tracks[t].track->id;
            e.origin = FM_ORIGIN_SEQUENCER;
            e.frame = s->frame;
            send(cb, user, &e, offset);
        }
}
void fm_sequence_stop(FmSequencer *s, FmSequenceDispatch cb, void *user)
{
    if (!s)
        return;
    release_track(s, 0, 0, cb, user);
    s->running = false;
    s->song_running = false;
}
bool fm_sequence_playing(const FmSequencer *s)
{
    return s && s->running;
}
double fm_sequence_tick(const FmSequencer *s)
{
    return s ? s->tick : 0;
}
FmFrame fm_sequence_frame(const FmSequencer *s)
{
    return s ? s->frame : 0;
}
void fm_sequence_seed(FmSequencer *s, uint32_t seed)
{
    if (s)
        s->rng = seed ? seed : 1;
}
void fm_sequence_set_frozen_callback(FmSequencer *s, FmSequenceFrozen cb, void *user)
{
    if (s)
    {
        s->frozen_callback = cb;
        s->frozen_user = user;
    }
}
static uint32_t random_next(FmSequencer *s)
{
    uint32_t x = s->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return s->rng = x;
}
static void dispatch_event(FmSequencer *s, const FmEvent *source, uint32_t offset,
                           FmSequenceDispatch cb, void *user)
{
    FmEvent e = *source;
    e.origin = FM_ORIGIN_SEQUENCER;
    e.frame = s->frame;
    if (note_on(source))
    {
        if (source->probability < 1 &&
            (double)random_next(s) / 4294967296.0 >= fmax(0, source->probability))
            return;
        if (s->playing_count == HELD_LIMIT)
        {
            s->status.overflow = true;
            return;
        }
        e.identity = ++s->identity;
        if (!e.identity)
            e.identity = ++s->identity;
        s->playing_notes[s->playing_count++] = (PlayingNote){*source, e.identity};
    }
    else if (note_off(source))
    {
        uint16_t i = 0;
        while (i < s->playing_count && !same_note(&s->playing_notes[i].source, source))
            ++i;
        if (i == s->playing_count)
            return;
        e.identity = s->playing_notes[i].identity;
        memmove(s->playing_notes + i, s->playing_notes + i + 1,
                (size_t)(--s->playing_count - i) * sizeof(PlayingNote));
    }
    send(cb, user, &e, offset);
}
static void select_pattern(FmSequencer *s, TrackRun *r, uint8_t slot, double at, uint32_t offset,
                           FmSequenceDispatch cb, void *user)
{
    release_track(s, r->track->id, offset, cb, user);
    r->track->playing_pattern = slot;
    r->track->selected_pattern = slot;
    r->track->queued_pattern = -1;
    r->pattern = fm_pattern(s->project, r->track->patterns[slot]);
    r->cursor = 0;
    r->start = at;
    r->launched = false;
    r->events_seen = r->pattern ? r->pattern->events : NULL;
    r->frozen_seen = r->pattern ? r->pattern->frozen_asset : 0;
}
static void apply_song(FmSequencer *s, uint16_t index, double at, uint32_t offset,
                       FmSequenceDispatch cb, void *user)
{
    const FmSongEntry *e = &s->project->song[index];
    s->song_index = index;
    s->song_repeat = 0;
    for (uint16_t i = 0; i < e->count; i++)
    {
        TrackRun *r = run_track(s, e->tracks[i]);
        if (!r)
            continue;
        select_pattern(s, r, e->patterns[i], at, offset, cb, user);
        r->track->mute = e->muted[i];
    }
    s->song_end = at + e->bars * bar_ticks(s);
}
static void arrangement_observe(FmSequencer *s);
static double view_tick(const FmPattern *p, double tick)
{
    if (p->quantize > 0 && isfinite(p->quantize))
        tick = round(tick / p->quantize) * p->quantize;
    double pair = FM_PPQN / 2.0, x = fmod(fmax(tick, 0), pair);
    double swing = fmax(-0.49, fmin(0.49, p->swing));
    /* A monotonic piecewise warp retains event order and the raw tick/frame. */
    double shifted = x < pair / 2 ? x * (1 + 2 * swing)
                                  : pair / 2 * (1 + 2 * swing) + (x - pair / 2) * (1 - 2 * swing);
    return tick - x + shifted;
}
void fm_sequence_advance(FmSequencer *s, uint32_t frames, FmSequenceDispatch cb, void *user)
{
    if (!s)
        return;
    sync_tracks(s);
    for (uint32_t f = 0; f < frames; f++, s->frame++)
    {
        if (!s->running)
            continue;
        double bpm = s->project->bpm;
        if (s->project->clock_source != FM_CLOCK_INTERNAL)
        {
            s->status.sync_lost = s->last_clock == 0 || s->frame > s->last_clock + FM_RATE / 2;
            if (s->status.sync_lost)
            {
                if (s->playing_count)
                    release_track(s, 0, f, cb, user);
                continue;
            }
            bpm = s->external_bpm;
        }
        if (!(bpm >= 20 && bpm <= 400) || !isfinite(bpm))
            continue;
        if (s->tick + 1e-8 >= s->next_bar)
        {
            double boundary = s->next_bar;
            s->next_bar += bar_ticks(s);
            if (s->queued_song >= 0)
            {
                apply_song(s, (uint16_t)s->queued_song, boundary, f, cb, user);
                s->queued_song = -1;
                s->song_running = true;
            }
            for (uint16_t i = 0; i < s->track_count; i++)
            {
                TrackRun *r = &s->tracks[i];
                if (r->track->queued_pattern >= 0)
                    select_pattern(s, r, (uint8_t)r->track->queued_pattern, boundary, f, cb, user);
            }
        }
        if (s->song_running && s->tick + 1e-8 >= s->song_end)
        {
            FmSongEntry *e = &s->project->song[s->song_index];
            if (++s->song_repeat < (e->repeats ? e->repeats : 1))
            {
                s->song_end += e->bars * bar_ticks(s);
            }
            else
            {
                uint16_t next = (uint16_t)(s->song_index + 1);
                if (next >= s->project->song_count)
                {
                    if (s->song_loop)
                        next = 0;
                    else
                    {
                        release_track(s, 0, f, cb, user);
                        s->running = false;
                        s->song_running = false;
                        continue;
                    }
                }
                apply_song(s, next, s->song_end, f, cb, user);
            }
        }
        if (s->arranging)
            arrangement_observe(s);
        bool any_solo = false;
        for (uint16_t i = 0; i < s->track_count; i++)
            any_solo |= s->tracks[i].track->solo;
        for (uint16_t i = 0; i < s->track_count; i++)
        {
            TrackRun *r = &s->tracks[i];
            FmPattern *p = r->pattern;
            if (!p || p->length_ticks < 1 || !isfinite(p->length_ticks))
                continue;
            if (p->events != r->events_seen || p->frozen_asset != r->frozen_seen)
            {
                release_track(s, r->track->id, f, cb, user);
                r->events_seen = p->events;
                r->frozen_seen = p->frozen_asset;
                r->cursor = 0;
                r->start = s->tick;
                r->launched = false;
            }
            double rate = (p->rate >= 0.125 && p->rate <= 8) ? p->rate : 1;
            double local = (s->tick - r->start) * rate;
            /* At 400 BPM, 8x rate, length >=1 tick, at most two wraps/frame. */
            for (unsigned wrap = 0; wrap < 4 && local + 1e-8 >= p->length_ticks; wrap++)
            {
                while (r->cursor < p->count &&
                       view_tick(p, p->events[r->cursor].tick) <= p->length_ticks + 1e-8)
                {
                    const FmEvent *e = &p->events[r->cursor++];
                    if (note_off(e) || e->kind == FM_EVENT_SUSTAIN)
                        dispatch_event(s, e, f, cb, user);
                }
                double at = r->start + p->length_ticks / rate;
                release_track(s, r->track->id, f, cb, user);
                if (r->count)
                {
                    r->position = (uint8_t)((r->position + 1) % r->count);
                    select_pattern(s, r, r->chain[r->position], at, f, cb, user);
                    p = r->pattern;
                    if (!p || p->length_ticks < 1)
                        break;
                    rate = (p->rate >= 0.125 && p->rate <= 8) ? p->rate : 1;
                }
                else
                {
                    r->start = at;
                    r->cursor = 0;
                    r->launched = false;
                }
                local = (s->tick - r->start) * rate;
            }
            if (!p)
                continue;
            if (p->frozen_asset)
            {
                if (!r->launched && !r->track->mute && (!any_solo || r->track->solo) &&
                    s->frozen_callback)
                {
                    s->frozen_callback(s->frozen_user, r->track->id, p->id, s->frame, ++s->identity,
                                       f);
                    r->frozen_active = true;
                }
                r->launched = true;
                continue;
            }
            r->launched = true;
            while (r->cursor < p->count && view_tick(p, p->events[r->cursor].tick) <= local + 1e-8)
            {
                const FmEvent *e = &p->events[r->cursor++];
                if (!r->track->mute && (!any_solo || r->track->solo))
                    dispatch_event(s, e, f, cb, user);
                else if (note_off(e))
                    dispatch_event(s, e, f, cb, user);
            }
        }
        /* Compensated musical accumulation avoids integer ticks/block drift. */
        double add = bpm * FM_PPQN / (60.0 * FM_RATE) - s->compensation;
        double next = s->tick + add;
        s->compensation = (next - s->tick) - add;
        s->tick = next;
    }
}

FmResult fm_sequence_live(FmSequencer *s, const FmEvent *input)
{
    if (!s || !input || !fm_track(s->project, input->track) || input->note > 127 ||
        input->channel > 15 || !isfinite(input->value))
        return FM_ERR_ARGUMENT;
    if (!live_origin(input->origin))
        return FM_OK;
    FmEvent e = *input;
    if (note_off(&e))
        e.kind = FM_EVENT_NOTE_OFF;
    e.probability = 1;
    if (s->history_count)
    {
        uint32_t previous = (s->head + s->history_count - 1) % s->history_cap;
        if (e.frame < s->history[previous].frame)
            return FM_ERR_RANGE;
    }
    bool coalesced = false;
    if (e.kind == FM_EVENT_CONTROL && s->history_count)
    {
        FmEvent *last = &s->history[(s->head + s->history_count - 1) % s->history_cap];
        if (last->kind == e.kind && last->parameter == e.parameter && last->track == e.track &&
            last->origin == e.origin && last->channel == e.channel &&
            last->frame / (FM_RATE / 100) == e.frame / (FM_RATE / 100))
        {
            *last = e;
            coalesced = true;
        }
    }
    if (!coalesced && s->history_count == s->history_cap)
    {
        state_event(s->boundary, &s->boundary_count, s->pedals, &s->pedal_count,
                    &s->history[s->head], &s->status.overflow);
        s->head = (s->head + 1) % s->history_cap;
        --s->history_count;
        ++s->status.dropped_events;
    }
    if (!coalesced)
        s->history[(s->head + s->history_count++) % s->history_cap] = e;
    if (s->status.armed && e.track == s->record_track)
    {
        if (!s->status.recording && note_on(&e))
        {
            s->status.recording = true;
            s->take_start = e.frame;
        }
        if (s->status.recording)
        {
            if (e.kind == FM_EVENT_CONTROL && s->take_count)
            {
                FmEvent *last = &s->take[s->take_count - 1];
                if (last->kind == e.kind && last->parameter == e.parameter &&
                    last->origin == e.origin && last->channel == e.channel &&
                    last->frame / (FM_RATE / 100) == e.frame / (FM_RATE / 100))
                {
                    *last = e;
                    return FM_OK;
                }
            }
            if (s->take_count == s->take_cap)
            {
                s->status.overflow = true;
                return FM_ERR_MEMORY;
            }
            s->take[s->take_count++] = e;
        }
    }
    return FM_OK;
}
void fm_sequence_record_cancel(FmSequencer *s)
{
    if (!s)
        return;
    s->status.armed = false;
    s->status.recording = false;
    s->take_count = 0;
}
FmResult fm_sequence_record_arm(FmSequencer *s, FmId track)
{
    if (!s)
        return FM_ERR_ARGUMENT;
    FmTrack *t = fm_track(s->project, track);
    if (!t)
        return FM_ERR_NOT_FOUND;
    if (!fm_pattern(s->project, t->patterns[t->selected_pattern]))
        return FM_ERR_NOT_FOUND;
    s->record_track = track;
    s->record_pattern = t->patterns[t->selected_pattern];
    s->take_count = 0;
    s->status.armed = true;
    s->status.recording = false;
    s->status.overflow = false;
    s->overdub = s->running;
    s->record_tick = s->tick;
    if (s->overdub)
    {
        s->take_start = s->frame;
        s->status.recording = true;
    }
    sync_tracks(s);
    return FM_OK;
}
static int event_compare(const void *a, const void *b)
{
    const FmEvent *x = a, *y = b;
    if (x->tick < y->tick)
        return -1;
    if (x->tick > y->tick)
        return 1;
    /* Release before retrigger at a shared boundary; identities preserve chords. */
    /* Pedal-down precedes reconstructed notes. On precedes its zero-length
     * release; generated identities avoid confusing simultaneous retriggers. */
    unsigned xr = x->kind == FM_EVENT_SUSTAIN ? (x->value >= 0.5f ? 0 : 3) : (note_off(x) ? 2 : 1);
    unsigned yr = y->kind == FM_EVENT_SUSTAIN ? (y->value >= 0.5f ? 0 : 3) : (note_off(y) ? 2 : 1);
    if (xr != yr)
        return xr < yr ? -1 : 1;
    return 0;
}
static int frame_compare(const void *a, const void *b)
{
    const FmEvent *x = a, *y = b;
    if (x->frame < y->frame)
        return -1;
    if (x->frame > y->frame)
        return 1;
    FmEvent xx = *x, yy = *y;
    xx.tick = 0;
    yy.tick = 0;
    return event_compare(&xx, &yy);
}
static void infer(FmSequencer *s)
{
    double seconds = (double)(s->take_end - s->take_start) / FM_RATE;
    double prior = s->project->bpm >= 20 ? s->project->bpm : 120;
    s->status.candidate_count = 0;
    unsigned onsets = 0;
    for (uint32_t i = 0; i < s->take_count; i++)
        onsets += s->take[i].frame <= s->take_end && note_on(&s->take[i]);
    const double bars[] = {0.25, 0.5, 1, 2, 4, 8, 16, 32};
    for (unsigned j = 0; j < sizeof(bars) / sizeof(bars[0]); j++)
    {
        double beats = bars[j] * (s->project->beats_per_bar ? s->project->beats_per_bar : 4);
        double bpm = 60 * beats / seconds;
        if (bpm < 30 || bpm > 300)
            continue;
        double error = 0;
        unsigned used = 0;
        for (uint32_t i = 0; i < s->take_count && used < 128; i++)
            if (s->take[i].frame <= s->take_end && note_on(&s->take[i]))
            {
                double t = (double)(s->take[i].frame - s->take_start) / FM_RATE * bpm / 60;
                double d = fabs(t * 4 - round(t * 4)) / 4;
                error += d;
                ++used;
            }
        double score = exp(-8 * (used ? error / used : 1)) * exp(-0.22 * fabs(log2(bpm / prior)));
        if (onsets < 3)
            score *= 0.25;
        FmTempoCandidate c = {bpm, beats, score};
        uint8_t pos = s->status.candidate_count++;
        s->status.candidates[pos] = c;
        while (pos &&
               s->status.candidates[pos].confidence > s->status.candidates[pos - 1].confidence)
        {
            FmTempoCandidate temp = s->status.candidates[pos - 1];
            s->status.candidates[pos - 1] = s->status.candidates[pos];
            s->status.candidates[pos--] = temp;
        }
    }
    if (!s->status.candidate_count)
    {
        s->status.candidates[0] = (FmTempoCandidate){prior, seconds * prior / 60, 0};
        s->status.candidate_count = 1;
    }
    s->status.ambiguous =
        onsets < 3 || s->status.candidates[0].confidence < 0.65 ||
        (s->status.candidate_count > 1 &&
         s->status.candidates[1].confidence > s->status.candidates[0].confidence * 0.85);
}
static FmResult watch(FmProject *p, void *ptr, size_t n)
{
    FmResult r = fm_edit_watch(p, ptr, n);
    if (r != FM_OK)
        fm_edit_cancel(p);
    return r;
}
static FmResult publish(FmSequencer *s, uint8_t candidate)
{
    FmProject *p = s->project;
    FmPattern *pat = fm_pattern(p, s->record_pattern);
    if (!pat || candidate >= s->status.candidate_count)
        return FM_ERR_ARGUMENT;
    FmTempoCandidate c = s->status.candidates[candidate];
    bool adopt = s->take_adopt && p->clock_source == FM_CLOCK_INTERNAL && !s->overdub;
    double bpm = adopt ? c.bpm : p->bpm;
    if (!isfinite(bpm) || bpm < 20 || bpm > 400)
        return FM_ERR_RANGE;
    double duration = (double)(s->take_end - s->take_start) * bpm * FM_PPQN / (60.0 * FM_RATE);
    double length = s->overdub ? pat->length_ticks : (adopt ? c.beats * FM_PPQN : duration);
    if (length < 1)
        return FM_ERR_RANGE;
    uint32_t old = s->overdub ? pat->count : 0;
    uint32_t capacity = old + s->take_count + HELD_LIMIT + HELD_LIMIT;
    FmEvent *events = fm_alloc(p, FM_MEM_EVENTS, (size_t)capacity * sizeof(*events));
    if (!events)
        return FM_ERR_MEMORY;
    uint32_t n = old;
    if (old)
        memcpy(events, pat->events, (size_t)old * sizeof(*events));
    Held held[HELD_LIMIT];
    Pedal pedals[HELD_LIMIT];
    uint16_t hn = 0, pn = 0;
    bool overflow = false;
    qsort(s->take, s->take_count, sizeof(*s->take), frame_compare);
    for (uint32_t i = 0; i < s->take_count; i++)
    {
        FmEvent e = s->take[i];
        if (e.frame < s->take_start || e.frame > s->take_end)
            continue;
        state_event(held, &hn, pedals, &pn, &e, &overflow);
        e.tick = (double)(e.frame - s->take_start) * bpm * FM_PPQN / (60.0 * FM_RATE);
        if (s->overdub)
            e.tick = fmod(e.tick + s->record_tick, pat->length_ticks);
        e.track = s->record_track;
        e.probability = 1;
        events[n++] = e;
    }
    for (uint16_t i = 0; i < hn; i++)
    {
        FmEvent e = held[i].on;
        e.kind = FM_EVENT_NOTE_OFF;
        e.velocity = 0;
        e.frame = s->take_end;
        e.tick = length;
        events[n++] = e;
    }
    for (uint16_t i = 0; i < pn; i++)
        if (pedals[i].down)
        {
            FmEvent e = {0};
            e.kind = FM_EVENT_SUSTAIN;
            e.origin = (FmOrigin)pedals[i].origin;
            e.track = pedals[i].track;
            e.channel = pedals[i].channel;
            e.frame = s->take_end;
            e.tick = length;
            e.probability = 1;
            events[n++] = e;
        }
    if (overflow)
    {
        fm_dealloc(p, FM_MEM_EVENTS, events, (size_t)capacity * sizeof(*events));
        return FM_ERR_MEMORY;
    }
    qsort(events, n, sizeof(*events), event_compare);
    FmResult r = fm_edit_begin(p);
    if (r == FM_OK)
        r = watch(p, pat, sizeof(*pat));
    if (r == FM_OK)
        r = watch(p, &p->bpm, sizeof(p->bpm));
    if (r == FM_OK)
        r = watch(p, &p->tempo_established, sizeof(p->tempo_established));
    if (r != FM_OK)
    {
        fm_dealloc(p, FM_MEM_EVENTS, events, (size_t)capacity * sizeof(*events));
        return r;
    }
    pat->events = events;
    pat->count = n;
    pat->capacity = capacity;
    pat->length_ticks = length;
    pat->quantize = 0;
    if (adopt)
        p->bpm = bpm;
    p->tempo_established = true;
    r = fm_edit_commit(p);
    if (r != FM_OK)
    {
        fm_edit_cancel(p);
        return r;
    }
    s->status.captured_events = n - old;
    s->status.first_frame = s->take_start;
    s->status.end_frame = s->take_end;
    s->status.armed = false;
    s->status.recording = false;
    TrackRun *run = run_track(s, s->record_track);
    if (run && !s->overdub)
    {
        run->pattern = pat;
        run->cursor = 0;
        run->start = s->tick;
        run->track->playing_pattern = run->track->selected_pattern;
    }
    if (!s->running)
        fm_sequence_start(s);
    return FM_OK;
}
FmResult fm_sequence_record_close(FmSequencer *s, FmFrame down)
{
    if (!s || !s->status.armed)
        return FM_ERR_ARGUMENT;
    if (!s->status.recording || !s->take_count)
    {
        fm_sequence_record_cancel(s);
        return FM_ERR_NOT_FOUND;
    }
    if (down <= s->take_start)
        return FM_ERR_RANGE;
    if (s->status.overflow)
        return FM_ERR_MEMORY;
    s->take_end = down;
    s->take_adopt = !s->project->tempo_established;
    infer(s);
    return publish(s, 0);
}
FmResult fm_sequence_candidate(FmSequencer *s, uint8_t candidate)
{
    if (!s || !s->take_count || s->take_end <= s->take_start)
        return FM_ERR_ARGUMENT;
    return publish(s, candidate);
}
static FmResult retrospective_build(FmSequencer *s, FmId track, FmFrame down, FmFrame lookback)
{
    if (!s || !lookback || !s->history_count)
        return FM_ERR_NOT_FOUND;
    FmTrack *t = fm_track(s->project, track);
    if (!t)
        return FM_ERR_NOT_FOUND;
    FmFrame start = down > lookback ? down - lookback : 0;
    FmFrame oldest = s->history[s->head].frame;
    if (start < oldest)
        start = oldest;
    if (start >= down)
        return FM_ERR_NOT_FOUND;
    Held held[HELD_LIMIT];
    Pedal pedals[HELD_LIMIT];
    uint16_t hn = s->boundary_count, pn = s->pedal_count;
    memcpy(held, s->boundary, (size_t)hn * sizeof(*held));
    memcpy(pedals, s->pedals, (size_t)pn * sizeof(*pedals));
    bool overflow = false;
    for (uint32_t i = 0; i < s->history_count; i++)
    {
        FmEvent *e = &s->history[(s->head + i) % s->history_cap];
        if (e->frame >= start)
            break;
        state_event(held, &hn, pedals, &pn, e, &overflow);
    }
    uint32_t n = 0;
    bool sounded = false;
    for (uint16_t i = 0; i < hn; i++)
        if (held[i].on.track == track)
        {
            if (n == s->take_cap)
                return FM_ERR_MEMORY;
            FmEvent e = held[i].on;
            e.frame = start;
            s->take[n++] = e;
            sounded = true;
            if (held[i].released)
            {
                if (n == s->take_cap)
                    return FM_ERR_MEMORY;
                e.kind = FM_EVENT_NOTE_OFF;
                e.velocity = 0;
                s->take[n++] = e;
            }
        }
    for (uint16_t i = 0; i < pn; i++)
        if (pedals[i].track == track && pedals[i].down)
        {
            if (n == s->take_cap)
                return FM_ERR_MEMORY;
            FmEvent e = {0};
            e.track = track;
            e.origin = (FmOrigin)pedals[i].origin;
            e.kind = FM_EVENT_SUSTAIN;
            e.value = 1;
            e.channel = pedals[i].channel;
            e.frame = start;
            s->take[n++] = e;
        }
    if (!sounded)
    {
        /* A ring window is a maximum search range, not automatically a phrase.
         * Favor the last onset group after a one-bar gap; ambiguous rests stay
         * visible through confidence/correction rather than silent quantizing. */
        FmFrame phrase = 0, last = 0;
        bool found = false;
        double prior = s->project->bpm >= 20 ? s->project->bpm : 120;
        FmFrame gap =
            (FmFrame)(60.0 * FM_RATE * (s->project->beats_per_bar ? s->project->beats_per_bar : 4) /
                      prior);
        for (uint32_t i = 0; i < s->history_count; i++)
        {
            const FmEvent *e = &s->history[(s->head + i) % s->history_cap];
            if (e->frame < start || e->frame > down || e->track != track || !note_on(e))
                continue;
            if (!found || e->frame > last + gap)
                phrase = e->frame;
            last = e->frame;
            found = true;
        }
        if (found)
        {
            start = phrase;
            for (uint32_t i = 0; i < n; i++)
                s->take[i].frame = start;
        }
    }
    for (uint32_t i = 0; i < s->history_count; i++)
    {
        FmEvent e = s->history[(s->head + i) % s->history_cap];
        if (e.frame < start || e.frame > down || e.track != track)
            continue;
        if (n == s->take_cap)
            return FM_ERR_MEMORY;
        s->take[n++] = e;
        sounded |= note_on(&e);
    }
    if (!sounded || overflow)
        return overflow ? FM_ERR_MEMORY : FM_ERR_NOT_FOUND;
    s->record_track = track;
    s->record_pattern = t->patterns[t->selected_pattern];
    s->take_count = n;
    s->take_start = start;
    s->take_end = down;
    s->overdub = false;
    s->take_adopt = !s->project->tempo_established;
    s->status.overflow = false;
    infer(s);
    return publish(s, 0);
}
FmResult fm_sequence_retrospective(FmSequencer *s, FmId track, FmFrame down, FmFrame lookback)
{
    if (!s)
        return FM_ERR_ARGUMENT;
    /* Build the hold candidate separately: a failed interpretation never
     * destroys an active conventional take or accidentally fires REC tap. */
    FmEvent *candidate =
        fm_alloc(s->project, FM_MEM_EVENTS, (size_t)s->take_cap * sizeof(*candidate));
    if (!candidate)
        return FM_ERR_MEMORY;
    FmEvent *previous = s->take;
    uint32_t old_count = s->take_count;
    FmFrame old_start = s->take_start, old_end = s->take_end;
    FmId old_track = s->record_track, old_pattern = s->record_pattern;
    bool old_overdub = s->overdub, old_adopt = s->take_adopt;
    FmSequenceStatus old_status = s->status;
    s->take = candidate;
    FmResult r = retrospective_build(s, track, down, lookback);
    if (r != FM_OK)
    {
        s->take = previous;
        s->take_count = old_count;
        s->take_start = old_start;
        s->take_end = old_end;
        s->record_track = old_track;
        s->record_pattern = old_pattern;
        s->overdub = old_overdub;
        s->take_adopt = old_adopt;
        s->status = old_status;
        fm_dealloc(s->project, FM_MEM_EVENTS, candidate, (size_t)s->take_cap * sizeof(*candidate));
    }
    else
        fm_dealloc(s->project, FM_MEM_EVENTS, previous, (size_t)s->take_cap * sizeof(*previous));
    return r;
}
FmSequenceStatus fm_sequence_status(const FmSequencer *s)
{
    FmSequenceStatus z = {0};
    if (!s)
        return z;
    z = s->status;
    z.retained_events = s->history_count;
    if (s->history_count)
        z.retained_frames = s->history[(s->head + s->history_count - 1) % s->history_cap].frame -
                            s->history[s->head].frame;
    return z;
}
FmResult fm_sequence_queue_pattern(FmSequencer *s, FmId track, uint8_t slot)
{
    if (!s || slot >= FM_PATTERN_SLOTS)
        return FM_ERR_ARGUMENT;
    TrackRun *r = run_track(s, track);
    if (!r || !r->track->patterns[slot])
        return FM_ERR_NOT_FOUND;
    r->count = 0;
    if (s->running)
        r->track->queued_pattern = (int8_t)slot;
    else
        select_pattern(s, r, slot, s->tick, 0, NULL, NULL);
    return FM_OK;
}
FmResult fm_sequence_scene(FmSequencer *s, uint8_t slot)
{
    if (!s || slot >= FM_PATTERN_SLOTS)
        return FM_ERR_ARGUMENT;
    sync_tracks(s);
    for (uint16_t i = 0; i < s->track_count; i++)
        if (s->tracks[i].track->patterns[slot])
            (void)fm_sequence_queue_pattern(s, s->tracks[i].track->id, slot);
    return FM_OK;
}
FmResult fm_sequence_chain(FmSequencer *s, FmId track, const uint8_t *slots, uint8_t count)
{
    if (!s || (count && !slots) || count > FM_SEQUENCE_CHAIN_MAX)
        return FM_ERR_ARGUMENT;
    TrackRun *r = run_track(s, track);
    if (!r)
        return FM_ERR_NOT_FOUND;
    for (uint8_t i = 0; i < count; i++)
        if (slots[i] >= FM_PATTERN_SLOTS || !r->track->patterns[slots[i]])
            return FM_ERR_NOT_FOUND;
    FmResult saved = fm_edit_begin(s->project);
    if (saved != FM_OK)
        return saved;
    saved = fm_edit_watch(s->project, r->track, sizeof(*r->track));
    if (saved != FM_OK)
    {
        fm_edit_cancel(s->project);
        return saved;
    }
    if (count)
        memcpy(r->track->chain, slots, count);
    r->track->chain_count = count;
    saved = fm_edit_commit(s->project);
    if (saved != FM_OK)
        return saved;
    if (count)
        memcpy(r->chain, slots, count);
    r->count = count;
    r->position = 0;
    if (!count)
        return FM_OK;
    if (s->running)
        r->track->queued_pattern = (int8_t)slots[0];
    else
        select_pattern(s, r, slots[0], s->tick, 0, NULL, NULL);
    return FM_OK;
}
FmResult fm_sequence_chain_range(FmSequencer *s, FmId track, uint8_t a, uint8_t b)
{
    if (a >= FM_PATTERN_SLOTS || b >= FM_PATTERN_SLOTS)
        return FM_ERR_ARGUMENT;
    if (a > b)
    {
        uint8_t t = a;
        a = b;
        b = t;
    }
    uint8_t slots[FM_PATTERN_SLOTS], n = 0;
    for (unsigned i = a; i <= b; i++)
        slots[n++] = (uint8_t)i;
    return fm_sequence_chain(s, track, slots, n);
}
FmResult fm_sequence_step(FmSequencer *s, FmId id, uint32_t step, uint8_t note, uint8_t velocity,
                          double length, bool enabled)
{
    if (!s || note > 127 || (enabled && !velocity) || velocity > 127 || !isfinite(length) ||
        length <= 0)
        return FM_ERR_ARGUMENT;
    FmPattern *p = fm_pattern(s->project, id);
    if (!p)
        return FM_ERR_NOT_FOUND;
    if (p->frozen_asset)
        return FM_ERR_BUSY;
    double tick = (double)step * FM_PPQN / 4;
    if (tick >= p->length_ticks)
        return FM_ERR_RANGE;
    uint32_t cap = p->count + 2;
    FmEvent *events = fm_alloc(s->project, FM_MEM_EVENTS, (size_t)cap * sizeof(*events));
    if (!events)
        return FM_ERR_MEMORY;
    uint32_t n = 0, identity = 0;
    for (uint32_t i = 0; i < p->count; i++)
        if (note_on(&p->events[i]) && p->events[i].note == note &&
            fabs(p->events[i].tick - tick) < 1e-7)
            identity = p->events[i].identity;
    for (uint32_t i = 0; i < p->count; i++)
    {
        FmEvent *e = &p->events[i];
        bool remove = e->note == note && ((note_on(e) && fabs(e->tick - tick) < 1e-7) ||
                                          (note_off(e) && identity && e->identity == identity));
        if (!remove)
            events[n++] = *e;
    }
    if (enabled)
    {
        FmEvent e = {0};
        e.kind = FM_EVENT_NOTE_ON;
        e.origin = FM_ORIGIN_LOCAL;
        e.track = p->track;
        e.note = note;
        e.velocity = velocity;
        e.tick = tick;
        e.probability = 1;
        e.identity = ++s->identity;
        events[n++] = e;
        e.kind = FM_EVENT_NOTE_OFF;
        e.velocity = 0;
        e.tick = fmin(tick + length, p->length_ticks);
        events[n++] = e;
    }
    qsort(events, n, sizeof(*events), event_compare);
    FmResult r = fm_edit_begin(s->project);
    if (r == FM_OK)
        r = watch(s->project, p, sizeof(*p));
    if (r != FM_OK)
    {
        fm_dealloc(s->project, FM_MEM_EVENTS, events, (size_t)cap * sizeof(*events));
        return r;
    }
    p->events = events;
    p->count = n;
    p->capacity = cap;
    r = fm_edit_commit(s->project);
    if (r != FM_OK)
        fm_edit_cancel(s->project);
    return r;
}
FmResult fm_sequence_motion(FmSequencer *s, FmId track, FmParameter parameter, float value,
                            FmFrame frame)
{
    if (parameter > FM_PARAM_SEND4 || !isfinite(value))
        return FM_ERR_ARGUMENT;
    FmEvent e = {0};
    e.track = track;
    e.kind = FM_EVENT_CONTROL;
    e.origin = FM_ORIGIN_LOCAL;
    e.parameter = (uint8_t)parameter;
    e.value = value;
    e.frame = frame;
    e.probability = 1;
    return fm_sequence_live(s, &e);
}
static bool valid_entry(FmSequencer *s, const FmSongEntry *e)
{
    if (!e || !e->count || e->count > FM_OBJECT_LIMIT || !isfinite(e->bars) || e->bars < 0.25 ||
        e->bars > 1024 || e->repeats > 1024)
        return false;
    for (uint16_t i = 0; i < e->count; i++)
    {
        FmTrack *t = fm_track(s->project, e->tracks[i]);
        if (!t || e->patterns[i] >= FM_PATTERN_SLOTS || !t->patterns[e->patterns[i]])
            return false;
        for (uint16_t j = 0; j < i; j++)
            if (e->tracks[j] == e->tracks[i])
                return false;
    }
    return true;
}
static FmResult song_replace(FmSequencer *s, FmSongEntry *entries, uint16_t count)
{
    FmProject *p = s->project;
    FmResult r = fm_edit_begin(p);
    if (r == FM_OK)
        r = watch(p, &p->song, sizeof(p->song));
    if (r == FM_OK)
        r = watch(p, &p->song_count, sizeof(p->song_count));
    if (r != FM_OK)
    {
        fm_dealloc(p, FM_MEM_METADATA, entries, (size_t)count * sizeof(*entries));
        return r;
    }
    p->song = entries;
    p->song_count = count;
    r = fm_edit_commit(p);
    if (r != FM_OK)
        fm_edit_cancel(p);
    return r;
}
FmResult fm_sequence_song_insert(FmSequencer *s, uint16_t index, const FmSongEntry *entry)
{
    if (!s || index > s->project->song_count || !valid_entry(s, entry))
        return FM_ERR_ARGUMENT;
    if (s->song_running)
        return FM_ERR_BUSY;
    uint16_t old = s->project->song_count;
    if (old == SONG_LIMIT)
        return FM_ERR_MEMORY;
    FmSongEntry *out = fm_alloc(s->project, FM_MEM_METADATA, (size_t)(old + 1) * sizeof(*out));
    if (!out)
        return FM_ERR_MEMORY;
    if (index)
        memcpy(out, s->project->song, (size_t)index * sizeof(*out));
    out[index] = *entry;
    if (index < old)
        memcpy(out + index + 1, s->project->song + index, (size_t)(old - index) * sizeof(*out));
    return song_replace(s, out, (uint16_t)(old + 1));
}
FmResult fm_sequence_song_delete(FmSequencer *s, uint16_t index)
{
    if (!s || index >= s->project->song_count)
        return FM_ERR_ARGUMENT;
    if (s->song_running)
        return FM_ERR_BUSY;
    uint16_t count = (uint16_t)(s->project->song_count - 1);
    FmSongEntry *out =
        count ? fm_alloc(s->project, FM_MEM_METADATA, (size_t)count * sizeof(*out)) : NULL;
    if (count && !out)
        return FM_ERR_MEMORY;
    if (index)
        memcpy(out, s->project->song, (size_t)index * sizeof(*out));
    if (index < count)
        memcpy(out + index, s->project->song + index + 1, (size_t)(count - index) * sizeof(*out));
    return song_replace(s, out, count);
}
FmResult fm_sequence_song_move(FmSequencer *s, uint16_t from, uint16_t to)
{
    if (!s || from >= s->project->song_count || to >= s->project->song_count)
        return FM_ERR_ARGUMENT;
    if (s->song_running)
        return FM_ERR_BUSY;
    uint16_t n = s->project->song_count;
    FmSongEntry *out = fm_alloc(s->project, FM_MEM_METADATA, (size_t)n * sizeof(*out));
    if (!out)
        return FM_ERR_MEMORY;
    memcpy(out, s->project->song, (size_t)n * sizeof(*out));
    FmSongEntry entry = out[from];
    if (from < to)
        memmove(out + from, out + from + 1, (size_t)(to - from) * sizeof(*out));
    else if (from > to)
        memmove(out + to + 1, out + to, (size_t)(from - to) * sizeof(*out));
    out[to] = entry;
    return song_replace(s, out, n);
}
FmResult fm_sequence_song_start(FmSequencer *s, uint16_t index, bool loop)
{
    if (!s || index >= s->project->song_count)
        return FM_ERR_ARGUMENT;
    fm_sequence_start(s);
    s->song_running = true;
    s->song_loop = loop;
    apply_song(s, index, 0, 0, NULL, NULL);
    return FM_OK;
}
FmResult fm_sequence_song_queue(FmSequencer *s, uint16_t index)
{
    if (!s || index >= s->project->song_count)
        return FM_ERR_ARGUMENT;
    s->queued_song = index;
    return FM_OK;
}
FmResult fm_sequence_clock_pulse(FmSequencer *s, FmClockSource source, FmFrame frame)
{
    if (!s || source == FM_CLOCK_INTERNAL || source != s->project->clock_source)
        return FM_ERR_ARGUMENT;
    if (s->last_clock && frame <= s->last_clock)
        return FM_ERR_RANGE;
    if (s->last_clock)
    {
        double estimate = 60.0 * FM_RATE / (24.0 * (double)(frame - s->last_clock));
        if (estimate < 20 || estimate > 400)
        {
            s->last_clock = frame;
            return FM_ERR_RANGE;
        }
        s->external_bpm = s->external_bpm ? s->external_bpm * 0.8 + estimate * 0.2 : estimate;
    }
    else
        s->external_bpm = s->project->bpm;
    s->last_clock = frame;
    s->status.sync_lost = false;
    return FM_OK;
}

void fm_sequence_continue(FmSequencer *s)
{
    if (s)
    {
        sync_tracks(s);
        s->running = true;
    }
}
static FmSongEntry arrangement_snapshot(FmSequencer *s)
{
    FmSongEntry out = {0};
    out.bars = 1;
    out.repeats = 1;
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (s->project->tracks[i])
        {
            FmTrack *t = s->project->tracks[i];
            if (!t->patterns[t->playing_pattern])
                continue;
            out.tracks[out.count] = t->id;
            out.patterns[out.count] = t->playing_pattern;
            out.muted[out.count++] = t->mute;
        }
    return out;
}
static void arrangement_observe(FmSequencer *s)
{
    FmSongEntry now = arrangement_snapshot(s);
    if (!now.count)
        return;
    if (!s->arrangement_count)
    {
        s->arrangement[0] = now;
        s->arrangement_count = 1;
        s->arrangement_start = s->tick;
        return;
    }
    FmSongEntry *old = &s->arrangement[s->arrangement_count - 1];
    bool same = old->count == now.count;
    for (unsigned i = 0; same && i < now.count; i++)
        same = old->tracks[i] == now.tracks[i] && old->patterns[i] == now.patterns[i] &&
               old->muted[i] == now.muted[i];
    if (same)
        return;
    if (s->arrangement_count == 64)
    {
        s->arrangement_overflow = true;
        return;
    }
    old->bars = fmax(1.0 / 960.0, (s->tick - s->arrangement_start) / bar_ticks(s));
    s->arrangement[s->arrangement_count++] = now;
    s->arrangement_start = s->tick;
}
FmResult fm_sequence_arrange(FmSequencer *s, bool begin)
{
    if (!s)
        return FM_ERR_ARGUMENT;
    if (begin)
    {
        if (s->arranging || s->song_running)
            return FM_ERR_BUSY;
        if (s->arrangement)
            fm_dealloc(s->project, FM_MEM_METADATA, s->arrangement, 64 * sizeof(FmSongEntry));
        s->arrangement = fm_alloc(s->project, FM_MEM_METADATA, 64 * sizeof(FmSongEntry));
        if (!s->arrangement)
            return FM_ERR_MEMORY;
        s->arranging = true;
        s->arrangement_count = 0;
        s->arrangement_overflow = false;
        if (!s->running)
            fm_sequence_start(s);
        arrangement_observe(s);
        return FM_OK;
    }
    if (!s->arranging)
        return FM_ERR_NOT_FOUND;
    s->arranging = false;
    if (s->arrangement_overflow)
        return FM_ERR_MEMORY;
    if (!s->arrangement_count)
        return FM_ERR_NOT_FOUND;
    s->arrangement[s->arrangement_count - 1].bars =
        fmax(1.0 / 960.0, (s->tick - s->arrangement_start) / bar_ticks(s));
    size_t bytes = s->arrangement_count * sizeof(FmSongEntry);
    FmSongEntry *copy = fm_alloc(s->project, FM_MEM_METADATA, bytes);
    if (!copy)
        return FM_ERR_MEMORY;
    memcpy(copy, s->arrangement, bytes);
    return song_replace(s, copy, s->arrangement_count);
}
bool fm_sequence_arranging(const FmSequencer *s)
{
    return s && s->arranging;
}

void fm_sequence_rebind(FmSequencer *s, FmProject *p)
{
    if (s)
        s->project = p;
}
