/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../sequence/fm_actions.h"
#include "fm_ui.h"
#include <math.h>
static FmPattern *pattern(FmUi *u)
{
    FmTrack *t = fm_track(u->project, u->project->selected_track);
    return t ? fm_pattern(u->project, t->patterns[t->selected_pattern]) : NULL;
}
static uint32_t selected(FmUi *u, FmPattern *p)
{
    for (unsigned i = 0; i < p->count; i++)
    {
        FmEvent *e = &p->events[i];
        if (e->kind != FM_EVENT_NOTE_ON || !e->velocity || e->note != u->step_note)
            continue;
        double start = fm_pattern_view_tick(p, e->tick);
        uint32_t off = fm_pattern_note_off(p, i);
        double end =
            off == UINT32_MAX ? p->length_ticks : fm_pattern_view_tick(p, p->events[off].tick);
        if (end <= start)
            end = start + 1;
        if (start <= u->roll_tick + .001 && end > u->roll_tick)
            return i;
    }
    return UINT32_MAX;
}
static void visible(FmUi *u)
{
    if (u->step_note < u->roll_low)
        u->roll_low = u->step_note;
    if (u->step_note >= u->roll_low + (u->roll_span ? u->roll_span : 12))
        u->roll_low = u->step_note - (u->roll_span ? u->roll_span : 12) + 1;
    if (u->roll_low > 128 - (u->roll_span ? u->roll_span : 12))
        u->roll_low = 128 - (u->roll_span ? u->roll_span : 12);
    u->page = (unsigned)(u->roll_tick / (4 * FM_PPQN));
}
void fm_ui_roll_open(FmUi *u)
{
    u->screen = FM_SCREEN_ROLL;
    u->roll_tick = 0;
    u->roll_span = 12;
    u->roll_low = u->step_note / 12 * 12;
    FmPattern *p = pattern(u);
    unsigned low = 127, high = 0;
    bool found = false;
    if (p)
        for (unsigned i = 0; i < p->count; i++)
            if (p->events[i].kind == FM_EVENT_NOTE_ON && p->events[i].velocity)
            {
                unsigned n = p->events[i].note;
                if (n < low)
                    low = n;
                if (n > high)
                    high = n;
                found = true;
            }
    if (found)
    {
        u->roll_span = ((high - low) / 12 + 1) * 12;
        if (u->roll_span > 48)
            u->roll_span = 48;
        u->roll_low = low;
        u->step_note = (uint8_t)low;
    }

    visible(u);
}
FmResult fm_ui_roll_toggle(FmUi *u)
{
    FmPattern *p = pattern(u);
    if (!p)
        return FM_ERR_NOT_FOUND;
    FmSequenceStatus st = fm_sequence_status(u->sequence);
    if (st.armed || st.recording)
        return FM_ERR_BUSY;
    uint32_t index = selected(u, p);
    if (index == UINT32_MAX)
        return fm_pattern_note_edit(u->project, p->id, 0, 0, u->roll_tick,
                                    fmin(u->step_length, p->length_ticks - u->roll_tick),
                                    u->step_note, u->step_velocity);
    FmEvent *e = &p->events[index];
    uint32_t off = fm_pattern_note_off(p, index);
    return fm_pattern_note_edit(u->project, p->id, 2, index, e->tick,
                                off == UINT32_MAX ? p->length_ticks - e->tick
                                                  : p->events[off].tick - e->tick,
                                e->note, e->velocity);
}
FmResult fm_ui_roll_edit(FmUi *u, unsigned knob, int delta)
{
    FmPattern *p = pattern(u);
    if (!p)
        return FM_ERR_NOT_FOUND;
    uint32_t at = selected(u, p), off = at == UINT32_MAX ? UINT32_MAX : fm_pattern_note_off(p, at);
    if (!knob && u->roll_move && at != UINT32_MAX)
    {
        FmSequenceStatus st = fm_sequence_status(u->sequence);
        if (st.armed || st.recording)
            return FM_ERR_BUSY;
        FmEvent *e = &p->events[at];
        double duration =
            off == UINT32_MAX ? p->length_ticks - e->tick : p->events[off].tick - e->tick;
        double next = fmax(0, fmin(p->length_ticks - duration, e->tick + delta * FM_PPQN / 4.0));
        FmResult result =
            fm_pattern_note_edit(u->project, p->id, 1, at, next, duration, e->note, e->velocity);
        if (result == FM_OK)
        {
            u->roll_tick = fm_pattern_view_tick(p, next);
            visible(u);
        }
        return result;
    }
    if (!knob)
    {
        u->roll_tick = fmax(0, fmin(p->length_ticks - 1, u->roll_tick + delta * FM_PPQN / 4.0));
        visible(u);
        return FM_OK;
    }
    FmEvent original = at == UINT32_MAX ? (FmEvent){0} : p->events[at];
    double duration = at == UINT32_MAX    ? u->step_length
                      : off == UINT32_MAX ? p->length_ticks - original.tick
                                          : p->events[off].tick - original.tick;
    if (knob == 1)
        u->step_note = (uint8_t)fmax(0, fmin(127, u->step_note + delta));
    if (knob == 2)
        u->step_length = fmax(1, fmin(p->length_ticks, duration + delta * FM_PPQN / 4.0));
    if (knob == 3)
        u->step_velocity = (uint8_t)fmax(
            1, fmin(127, (at == UINT32_MAX ? u->step_velocity : original.velocity) + delta));
    visible(u);
    if (at == UINT32_MAX)
        return FM_OK;
    FmSequenceStatus st = fm_sequence_status(u->sequence);
    if (st.armed || st.recording)
        return FM_ERR_BUSY;
    if (knob == 2)
        duration = fmin(u->step_length, p->length_ticks - original.tick);
    return fm_pattern_note_edit(u->project, p->id, 1, at, original.tick, duration,
                                knob == 1 ? u->step_note : original.note,
                                knob == 3 ? u->step_velocity : original.velocity);
}

void fm_ui_roll_next(FmUi *u, int delta)
{
    FmPattern *p = pattern(u);
    if (!p)
        return;
    unsigned total = 0, rank = 0;
    bool found = false;
    for (unsigned i = 0; i < p->count; i++)
        if (p->events[i].kind == FM_EVENT_NOTE_ON && p->events[i].velocity)
        {
            if (p->events[i].note == u->step_note &&
                fabs(fm_pattern_view_tick(p, p->events[i].tick) - u->roll_tick) < .001)
            {
                rank = total;
                found = true;
            }
            total++;
        }
    if (!total)
        return;
    int target = found ? (int)rank + delta : delta < 0 ? (int)total - 1 : 0;
    if (target < 0)
        target = 0;
    if (target >= (int)total)
        target = (int)total - 1;
    unsigned n = 0;
    for (unsigned i = 0; i < p->count; i++)
        if (p->events[i].kind == FM_EVENT_NOTE_ON && p->events[i].velocity)
        {
            if (n++ == (unsigned)target)
            {
                u->step_note = p->events[i].note;
                u->roll_tick = fm_pattern_view_tick(p, p->events[i].tick);
                visible(u);
                return;
            }
        }
}
