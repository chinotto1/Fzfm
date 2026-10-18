/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fm_actions.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static int order(const void *a, const void *b)
{
    const FmEvent *x = a, *y = b;
    if (x->tick < y->tick)
        return -1;
    if (x->tick > y->tick)
        return 1;
    unsigned xp = x->kind == FM_EVENT_NOTE_OFF ? 0 : x->kind == FM_EVENT_CONTROL ? 1 : 2;
    unsigned yp = y->kind == FM_EVENT_NOTE_OFF ? 0 : y->kind == FM_EVENT_CONTROL ? 1 : 2;
    if (xp != yp)
        return xp < yp ? -1 : 1;
    return x->identity < y->identity ? -1 : x->identity > y->identity ? 1 : 0;
}
FmResult fm_pattern_copy(FmProject *p, FmId source, FmId destination, bool overwrite)
{
    FmPattern *a = fm_pattern(p, source), *b = fm_pattern(p, destination);
    if (!a || !b || a == b)
        return FM_ERR_ARGUMENT;
    if (b->count && !overwrite)
        return FM_ERR_BUSY;
    FmEvent *events = NULL;
    if (a->count)
    {
        events = fm_alloc(p, FM_MEM_EVENTS, a->count * sizeof(*events));
        if (!events)
            return FM_ERR_MEMORY;
        memcpy(events, a->events, a->count * sizeof(*events));
        for (unsigned i = 0; i < a->count; i++)
            events[i].track = b->track;
    }
    FmResult r = fm_edit_begin(p);
    if (r == FM_OK)
        r = fm_edit_watch(p, b, sizeof(*b));
    if (r != FM_OK)
    {
        fm_edit_cancel(p);
        if (events)
            fm_dealloc(p, FM_MEM_EVENTS, events, a->count * sizeof(*events));
        return r;
    }
    FmId id = b->id, track = b->track;
    *b = *a;
    b->id = id;
    b->track = track;
    b->events = events;
    b->capacity = b->count;
    return fm_edit_commit(p);
}
FmResult fm_pattern_clear(FmProject *p, FmId id)
{
    FmPattern *pat = fm_pattern(p, id);
    if (!pat)
        return FM_ERR_NOT_FOUND;
    FmResult r = fm_edit_begin(p);
    if (r == FM_OK)
        r = fm_edit_watch(p, pat, sizeof(*pat));
    if (r != FM_OK)
    {
        fm_edit_cancel(p);
        return r;
    }
    pat->events = NULL;
    pat->count = pat->capacity = 0;
    pat->frozen_asset = 0;
    pat->frozen_sends = 0;
    pat->frozen_bpm = 0;
    return fm_edit_commit(p);
}
FmResult fm_step_properties(FmProject *p, FmId id, uint32_t step, uint8_t note, float probability,
                            double micro)
{
    FmPattern *pat = fm_pattern(p, id);
    if (!pat)
        return FM_ERR_NOT_FOUND;
    if (pat->frozen_asset)
        return FM_ERR_BUSY;
    if (!isfinite(probability) || probability < 0 || probability > 1 || !isfinite(micro) ||
        fabs(micro) > FM_PPQN / 8.0)
        return FM_ERR_RANGE;
    double base = (double)step * FM_PPQN / 4;
    uint32_t found = UINT32_MAX;
    double best = FM_PPQN / 8.0 + 1;
    for (unsigned i = 0; i < pat->count; i++)
        if (pat->events[i].kind == FM_EVENT_NOTE_ON && pat->events[i].note == note)
        {
            double d = fabs(pat->events[i].tick - base);
            if (d < best)
            {
                best = d;
                found = i;
            }
        }
    if (found == UINT32_MAX)
        return FM_ERR_NOT_FOUND;
    FmEvent *copy = fm_alloc(p, FM_MEM_EVENTS, pat->count * sizeof(*copy));
    if (!copy)
        return FM_ERR_MEMORY;
    memcpy(copy, pat->events, pat->count * sizeof(*copy));
    uint32_t identity = copy[found].identity;
    double shift = fmax(0, base + micro) - copy[found].tick;
    copy[found].probability = probability;
    for (unsigned i = 0; i < pat->count; i++)
        if (i == found || (copy[i].kind == FM_EVENT_NOTE_OFF && copy[i].identity == identity))
            copy[i].tick = fmin(pat->length_ticks, fmax(0, copy[i].tick + shift));
    qsort(copy, pat->count, sizeof(*copy), order);
    FmResult r = fm_edit_begin(p);
    if (r == FM_OK)
        r = fm_edit_watch(p, pat, sizeof(*pat));
    if (r != FM_OK)
    {
        fm_edit_cancel(p);
        fm_dealloc(p, FM_MEM_EVENTS, copy, pat->count * sizeof(*copy));
        return r;
    }
    pat->events = copy;
    pat->capacity = pat->count;
    return fm_edit_commit(p);
}
FmResult fm_step_lock(FmProject *p, FmId id, uint32_t step, FmParameter param, float value)
{
    FmPattern *pat = fm_pattern(p, id);
    if (!pat)
        return FM_ERR_NOT_FOUND;
    if (pat->frozen_asset)
        return FM_ERR_BUSY;
    if (param > FM_PARAM_SEND4 || !isfinite(value) || value < (param == FM_PARAM_PAN ? -1 : 0) ||
        value > (param == FM_PARAM_VOLUME ? 4 : 1))
        return FM_ERR_RANGE;
    double tick = (double)step * FM_PPQN / 4;
    if (tick >= pat->length_ticks)
        return FM_ERR_RANGE;
    FmEvent *copy = fm_alloc(p, FM_MEM_EVENTS, (pat->count + 1) * sizeof(*copy));
    if (!copy)
        return FM_ERR_MEMORY;
    uint32_t n = 0;
    for (unsigned i = 0; i < pat->count; i++)
        if (!(pat->events[i].kind == FM_EVENT_CONTROL && pat->events[i].parameter == param &&
              fabs(pat->events[i].tick - tick) < .001))
            copy[n++] = pat->events[i];
    copy[n++] = (FmEvent){.tick = tick,
                          .track = pat->track,
                          .kind = FM_EVENT_CONTROL,
                          .origin = FM_ORIGIN_SEQUENCER,
                          .parameter = (uint8_t)param,
                          .value = value,
                          .probability = 1,
                          .identity = 0x80000000u | param};
    qsort(copy, n, sizeof(*copy), order);
    FmResult r = fm_edit_begin(p);
    if (r == FM_OK)
        r = fm_edit_watch(p, pat, sizeof(*pat));
    if (r != FM_OK)
    {
        fm_edit_cancel(p);
        fm_dealloc(p, FM_MEM_EVENTS, copy, (pat->count + 1) * sizeof(*copy));
        return r;
    }
    pat->events = copy;
    pat->count = n;
    pat->capacity = n;
    return fm_edit_commit(p);
}
/* A tie keeps the preceding note identity and removes the current retrigger.
 * It fills any gate gap and extends through at least the selected step. */
FmResult fm_step_tie(FmProject *p, FmId id, uint32_t step, uint8_t note)
{
    FmPattern *pat = fm_pattern(p, id);
    if (!pat)
        return FM_ERR_NOT_FOUND;
    if (pat->frozen_asset)
        return FM_ERR_BUSY;
    if (!step || note > 127)
        return FM_ERR_RANGE;
    double base = (double)step * FM_PPQN / 4, previous = base - FM_PPQN / 4;
    if (base >= pat->length_ticks)
        return FM_ERR_RANGE;
    int before = -1, current = -1;
    double distance = FM_PPQN / 8.0 + 1;
    for (unsigned i = 0; i < pat->count; i++)
        if (pat->events[i].kind == FM_EVENT_NOTE_ON && pat->events[i].note == note)
        {
            if (fabs(pat->events[i].tick - previous) < distance)
            {
                distance = fabs(pat->events[i].tick - previous);
                before = (int)i;
            }
            if (fabs(pat->events[i].tick - base) <= FM_PPQN / 8.0)
                current = (int)i;
        }
    if (before < 0)
        return FM_ERR_NOT_FOUND;
    if (current == before)
        current = -1;
    uint32_t identity = pat->events[before].identity;
    if (!identity || (current >= 0 && !pat->events[current].identity))
        return FM_ERR_UNSUPPORTED;
    uint32_t removed = current < 0 ? 0 : pat->events[current].identity;
    double end = fmin(pat->length_ticks, base + FM_PPQN / 4);
    int off = -1;
    for (unsigned i = 0; i < pat->count; i++)
        if (pat->events[i].kind == FM_EVENT_NOTE_OFF)
        {
            if (pat->events[i].identity == identity)
                off = (int)i;
            if (current >= 0 && pat->events[i].identity == removed)
                end = fmax(end, pat->events[i].tick);
        }
    if (off < 0)
        return FM_ERR_NOT_FOUND;
    FmEvent *copy = fm_alloc(p, FM_MEM_EVENTS, pat->count * sizeof(*copy));
    if (!copy)
        return FM_ERR_MEMORY;
    uint32_t count = 0;
    for (unsigned i = 0; i < pat->count; i++)
    {
        if (current >= 0 && (i == (unsigned)current || (pat->events[i].kind == FM_EVENT_NOTE_OFF &&
                                                        pat->events[i].identity == removed)))
            continue;
        copy[count] = pat->events[i];
        if (i == (unsigned)off)
            copy[count].tick = fmax(copy[count].tick, end);
        count++;
    }
    qsort(copy, count, sizeof(*copy), order);
    FmResult r = fm_edit_begin(p);
    if (r == FM_OK)
        r = fm_edit_watch(p, pat, sizeof(*pat));
    if (r != FM_OK)
    {
        fm_edit_cancel(p);
        fm_dealloc(p, FM_MEM_EVENTS, copy, pat->count * sizeof(*copy));
        return r;
    }
    pat->capacity = pat->count;
    pat->count = count;
    pat->events = copy;
    return fm_edit_commit(p);
}

static bool is_on(const FmEvent *e)
{
    return e->kind == FM_EVENT_NOTE_ON && e->velocity;
}
static bool is_off(const FmEvent *e)
{
    return e->kind == FM_EVENT_NOTE_OFF || (e->kind == FM_EVENT_NOTE_ON && !e->velocity);
}
static bool note_matches(const FmEvent *a, const FmEvent *b)
{
    return a->origin == b->origin && a->channel == b->channel &&
           (a->identity ? a->identity == b->identity : b->identity == 0 && a->note == b->note);
}
uint32_t fm_pattern_note_off(const FmPattern *p, uint32_t on)
{
    if (!p || on >= p->count || !is_on(&p->events[on]))
        return UINT32_MAX;
    const FmEvent *source = &p->events[on];
    unsigned earlier = 0;
    for (uint32_t i = 0; i < on; i++)
        if (note_matches(source, &p->events[i]))
        {
            if (is_on(&p->events[i]))
                earlier++;
            else if (is_off(&p->events[i]) && earlier)
                earlier--;
        }
    for (uint32_t i = on + 1; i < p->count; i++)
        if (is_off(&p->events[i]) && note_matches(source, &p->events[i]))
        {
            if (!earlier)
                return i;
            earlier--;
        }
    return UINT32_MAX;
}
FmResult fm_pattern_quantize(FmProject *p, FmId id, double grid)
{
    FmPattern *pat = fm_pattern(p, id);
    if (!pat)
        return FM_ERR_NOT_FOUND;
    if (pat->frozen_asset)
        return FM_ERR_BUSY;
    if (!isfinite(grid) || grid < 0 || grid > FM_PPQN * 4)
        return FM_ERR_RANGE;
    if (pat->quantize == grid)
        return FM_OK;
    FmResult r = fm_edit_begin(p);
    if (r == FM_OK)
        r = fm_edit_watch(p, pat, sizeof(*pat));
    if (r != FM_OK)
    {
        fm_edit_cancel(p);
        return r;
    }
    pat->quantize = grid;
    return fm_edit_commit(p);
}
FmResult fm_pattern_note_edit(FmProject *p, FmId id, unsigned operation, uint32_t on, double tick,
                              double duration, uint8_t note, uint8_t velocity)
{
    FmPattern *pat = fm_pattern(p, id);
    if (!pat)
        return FM_ERR_NOT_FOUND;
    if (pat->frozen_asset)
        return FM_ERR_BUSY;
    if (operation > 2 || note > 127 || !velocity || velocity > 127 || !isfinite(tick) ||
        !isfinite(duration) || tick < 0 || duration < 1 || tick >= pat->length_ticks ||
        tick + duration > pat->length_ticks + 1e-7)
        return FM_ERR_RANGE;
    if (operation && (on >= pat->count || !is_on(&pat->events[on])))
        return FM_ERR_NOT_FOUND;
    uint32_t off = operation ? fm_pattern_note_off(pat, on) : UINT32_MAX;
    if (pat->count > 65534 && operation != 2)
        return FM_ERR_MEMORY;
    unsigned capacity = pat->count + 2;
    FmEvent *events = fm_alloc(p, FM_MEM_EVENTS, capacity * sizeof(*events));
    if (!events)
        return FM_ERR_MEMORY;
    unsigned count = 0;
    uint32_t identity = 0;
    for (unsigned i = 0; i < pat->count; i++)
    {
        if (pat->events[i].identity > identity)
            identity = pat->events[i].identity;
        if (!operation || (i != on && i != off))
            events[count++] = pat->events[i];
    }
    if (operation != 2)
    {
        if (!operation && identity == UINT32_MAX)
        {
            fm_dealloc(p, FM_MEM_EVENTS, events, capacity * sizeof(*events));
            return FM_ERR_RANGE;
        }
        FmEvent start = operation ? pat->events[on]
                                  : (FmEvent){.track = pat->track,
                                              .origin = FM_ORIGIN_LOCAL,
                                              .probability = 1,
                                              .identity = identity + 1};
        start.kind = FM_EVENT_NOTE_ON;
        start.tick = tick;
        start.note = note;
        start.velocity = velocity;
        FmEvent end = off != UINT32_MAX ? pat->events[off] : start;
        end.kind = FM_EVENT_NOTE_OFF;
        end.tick = tick + duration;
        end.note = note;
        end.velocity = 0;
        end.identity = start.identity;
        end.origin = start.origin;
        end.channel = start.channel;
        events[count++] = start;
        events[count++] = end;
    }
    qsort(events, count, sizeof(*events), order);
    FmResult r = fm_edit_begin(p);
    if (r == FM_OK)
        r = fm_edit_watch(p, pat, sizeof(*pat));
    if (r != FM_OK)
    {
        fm_edit_cancel(p);
        fm_dealloc(p, FM_MEM_EVENTS, events, capacity * sizeof(*events));
        return r;
    }
    pat->events = events;
    pat->count = count;
    pat->capacity = capacity;
    return fm_edit_commit(p);
}
