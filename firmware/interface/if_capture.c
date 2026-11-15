/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* Retrospective MIDI capture.
 * The free-take tempo function follows Melodee nightly
 * 3fe570d285d1d8da5438d93963c6048734e9e4d2 (capture_free_place /
 * capture_take): 16, 32 or 64 steps, nearest BPM in 40..240, keep the
 * current tempo when it is within 3 percent, and leave the tempo alone
 * when another track already has notes or the phrase is a single note.
 * Undo restores the pattern and its length. It does not restore BPM.
 * This file is original; the Melodee sources were not copied. */
#include "if_internal.h"

#include <string.h>

_Static_assert(IF_EQ_BANDS == 0, "the interface does not allocate an equalizer");
_Static_assert(sizeof(IfCapEv) * IF_CAP_N == 1024u, "capture ring stays a fixed 1 KB");

static void say(IfMachine *m, const char *text)
{
    unsigned i = 0;
    if (!text)
        text = "";
    while (text[i] && i + 1u < sizeof m->message) {
        m->message[i] = text[i];
        i++;
    }
    m->message[i] = 0;
}

static unsigned append(char *dst, unsigned used, unsigned cap, const char *src)
{
    while (src && *src && used + 1u < cap)
        dst[used++] = *src++;
    dst[used] = 0;
    return used;
}

static unsigned append_u(char *dst, unsigned used, unsigned cap, unsigned n)
{
    char tmp[10];
    unsigned i = 0;
    if (!n)
        tmp[i++] = '0';
    while (n && i < sizeof tmp) {
        tmp[i++] = (char)('0' + (n % 10u));
        n /= 10u;
    }
    while (i && used + 1u < cap)
        dst[used++] = tmp[--i];
    dst[used] = 0;
    return used;
}

int if_track_empty(const IfMachine *m, unsigned track)
{
    unsigned i;
    const IfTrack *t;
    if (!m || track >= IF_TRACKS)
        return 1;
    t = &m->track[track];
    if (t->rec_n)
        return 0;
    for (i = 0; i < IF_STEPS; i++)
        if (t->step[i].n)
            return 0;
    return 1;
}

static uint32_t cap_frac(const IfTrack *t)
{
    uint32_t span = if_step_units(t, t->seq_idx);
    uint32_t on = 0;
    if (t->seq_pos >= 0x7FFFFFFFu || !span)
        return 0;
    on = (uint32_t)(((uint64_t)t->seq_pos * 65536u) / span);
    return on > 65535u ? 65535u : on;
}

static uint32_t cap_now(const IfTrack *t)
{
    uint32_t st = t->seq_pos >= 0x7FFFFFFFu ? (uint16_t)(t->cap_step + 1u) : t->cap_step;
    return (st << 8) + (cap_frac(t) >> 8);
}

static uint32_t cap_len(const IfCapEv *e, uint32_t now)
{
    uint32_t d = (e->tr & 128u) ? (now - ((uint32_t)e->step << 8) - e->on) & 0xFFFFFFu : e->dur;
    if (d < 1u)
        return 1u;
    return d > 65535u ? 65535u : d;
}

void if_cap_on(IfMachine *m, unsigned track, unsigned note, unsigned vel)
{
    IfTrack *t;
    IfCapEv *e;
    if (!m || track >= IF_TRACKS || note > 127u || !vel || vel > 127u)
        return;
    t = &m->track[track];
    if (track != m->sel || (m->rec && m->playing))
        return;
    if (!m->playing) {
        if (m->rec || m->clock_ext || !if_track_empty(m, track))
            return;
    }
    if (m->cap_track != track) {
        m->cap_from = m->cap_w;
        m->cap_track = (uint8_t)track;
        m->cap_free = 0;
    }
    e = &m->cap[m->cap_w % IF_CAP_N];
    if (m->playing) {
        uint32_t st = t->seq_pos >= 0x7FFFFFFFu ? (uint16_t)(t->cap_step + 1u) : t->cap_step;
        e->step = (uint16_t)st;
        e->on = (uint8_t)(cap_frac(t) >> 8);
    } else {
        uint32_t ms = m->ms;
        if (!m->cap_free || ms - m->cap_free_last > IF_FREE_REST_MS || ms - m->cap_free_t0 > 65535u) {
            m->cap_free = 1;
            m->cap_free_t0 = ms;
            m->cap_from = m->cap_w;
        }
        m->cap_free_last = ms;
        e->step = (uint16_t)(ms - m->cap_free_t0);
        e->on = 0;
    }
    e->dur = 0;
    e->note = (uint8_t)note;
    e->vel = (uint8_t)vel;
    e->tr = (uint8_t)(track | 128u);
    m->cap_w++;
}

void if_cap_off(IfMachine *m, unsigned track, unsigned note)
{
    uint32_t i;
    if (!m || track >= IF_TRACKS || track != m->cap_track)
        return;
    for (i = 0; i < m->cap_w - m->cap_from && i < IF_CAP_N; i++) {
        IfCapEv *e = &m->cap[(m->cap_w - 1u - i) % IF_CAP_N];
        if (e->tr == (uint8_t)(track | 128u) && e->note == note) {
            if (m->cap_free) {
                uint32_t ms = m->ms - m->cap_free_t0 - e->step;
                e->dur = (uint16_t)(ms > 65535u ? 65535u : ms);
            } else
                e->dur = (uint16_t)cap_len(e, cap_now(&m->track[track]));
            e->tr = (uint8_t)track;
        }
    }
}

/* Returns the loop length in steps, or 0 when the press does not contain
 * a note. Rewrites events into 1/256-step positions. */
static uint32_t free_place(IfMachine *m, IfTrack *t, IfCapEv *ev, uint32_t from, uint32_t until, uint32_t span,
                           uint32_t *bpm)
{
    uint32_t cur = m->bpm, div = if_div_units(t->division);
    uint32_t w = 0, best = 0, gap = 0xFFFFFFFFu, last = 0, n = 0, i, press, k;
    int keep = 0;
    if (span - 1u >= 0x7FFFFFFFu)
        return 0;
    press = span = span > 65535u ? 65535u : span;
    for (k = 0; k < IF_TRACKS; k++)
        if (&m->track[k] != t && !if_track_empty(m, k))
            keep = 1;
    for (i = from; i != until; i++)
        if (ev[i % IF_CAP_N].step < press) {
            n++;
            if (ev[i % IF_CAP_N].step > last)
                last = ev[i % IF_CAP_N].step;
        }
    if (!n || press - last > IF_FREE_REST_MS)
        return 0;
    if (!keep && n > 1u && press - last <= press / 4u * 3u)
        for (uint32_t c = 16u; c <= 64u; c *= 2u) {
            uint32_t b = (if_cap_loop_bpm_ms(c, div) + press / 2u) / press;
            uint32_t d = b > cur ? b - cur : cur - b;
            if (b >= IF_BPM_MIN && b <= IF_BPM_MAX && d < gap) {
                gap = d;
                best = b;
                w = c;
            }
        }
    if (w)
        *bpm = gap * 100u <= cur * 3u ? cur : best;
    else {
        for (w = 16u; w < 64u && if_cap_loop_bpm_ms(w, div) / cur <= last; w *= 2u)
            ;
        span = if_cap_loop_bpm_ms(w, div) / cur;
        *bpm = cur;
    }
    for (i = from; i != until; i++) {
        IfCapEv *e = &ev[i % IF_CAP_N];
        uint32_t at = e->step;
        uint32_t pos = span ? at * w * 256u / span : 0;
        uint32_t held = (e->tr & 128u) ? (press > at ? press - at : 1u) : e->dur;
        uint32_t len = span ? held * w * 256u / span : 1u;
        e->step = (uint16_t)(pos >> 8);
        e->on = (uint8_t)pos;
        e->dur = (uint16_t)(len > 65535u ? 65535u : len);
        e->tr = at < press && at < span ? (uint8_t)(t - m->track) : 255u;
    }
    return w;
}

static void merge_step(IfStep *st, uint8_t note, uint8_t vel)
{
    uint8_t q;
    for (q = 0; q < st->n; q++)
        if (st->note[q] == note)
            break;
    if (q == st->n && st->n < 4u)
        st->note[st->n++] = note;
    if (vel > st->vel)
        st->vel = vel;
}

static int same_rec(const IfTrack *t, uint8_t note, uint16_t step, uint8_t on)
{
    uint8_t i;
    for (i = 0; i < t->rec_n; i++)
        if (t->rec[i].used && t->rec[i].note == note && t->rec[i].step == step && t->rec[i].on == on)
            return 1;
    return 0;
}

static void save_undo(IfMachine *m, const IfTrack *t, unsigned track)
{
    m->undo.valid = 1;
    m->undo.redo = 0;
    m->undo.track = (uint8_t)track;
    m->undo.length = t->length;
    m->undo.rec_n = t->rec_n;
    memcpy(m->undo.step, t->step, sizeof t->step);
    memcpy(m->undo.rec, t->rec, sizeof t->rec);
}

static void swap_undo(IfMachine *m)
{
    IfTrack *t = &m->track[m->undo.track];
    IfStep steps[IF_STEPS];
    IfRec rec[IF_REC_MAX];
    uint8_t length = t->length, rec_n = t->rec_n;
    memcpy(steps, t->step, sizeof steps);
    memcpy(rec, t->rec, sizeof rec);
    memcpy(t->step, m->undo.step, sizeof t->step);
    memcpy(t->rec, m->undo.rec, sizeof t->rec);
    t->length = m->undo.length;
    t->rec_n = m->undo.rec_n;
    memcpy(m->undo.step, steps, sizeof steps);
    memcpy(m->undo.rec, rec, sizeof rec);
    m->undo.length = length;
    m->undo.rec_n = rec_n;
}

static void format_take(IfMachine *m, IfCaptureInfo *info, int full, int free_take, unsigned w, unsigned bpm)
{
    unsigned used = 0;
    unsigned shown = (w % 16u) ? w : w / 16u;
    const char *unit = (w % 16u) ? " STEPS" : (w > 16u ? " BARS" : " BAR");
    info->text[0] = 0;
    used = append(info->text, used, sizeof info->text, full ? "FULL: " : "CAPTURED ");
    used = append_u(info->text, used, sizeof info->text, shown);
    used = append(info->text, used, sizeof info->text, unit);
    if (free_take) {
        used = append(info->text, used, sizeof info->text, " ");
        used = append_u(info->text, used, sizeof info->text, bpm);
        append(info->text, used, sizeof info->text, " BPM");
    }
    say(m, info->text);
}

IfCaptureInfo if_capture(IfMachine *m, uint32_t now_ms, uint32_t hold_age_ms)
{
    IfCaptureInfo info;
    IfTrack *t;
    IfCapEv captured[IF_CAP_N];
    uint32_t k, n = 0, i, now, first = 0xFFFFu, last = 0, end, w, start, from, until, bpm, t0;
    int empty, free_take, put = 0, full = 0;
    memset(&info, 0, sizeof info);
    if (!m) {
        info.status = IF_CAP_EMPTY;
        return info;
    }
    t = &m->track[m->sel];
    k = m->sel;
    m->ms = now_ms;
    if ((m->rec && m->playing) || ((m->rec >> k) & 1u)) {
        info.status = IF_CAP_RECORDING;
        say(m, "RECORDING");
        memcpy(info.text, m->message, sizeof info.text);
        return info;
    }
    if (m->cap_track != k) {
        m->cap_from = m->cap_w;
        m->cap_track = (uint8_t)k;
        m->cap_free = 0;
    }
    now = cap_now(t);
    from = m->cap_w - m->cap_from > IF_CAP_N ? m->cap_w - IF_CAP_N : m->cap_from;
    until = m->cap_w;
    memcpy(captured, m->cap, sizeof captured);
    free_take = m->cap_free;
    t0 = m->cap_free_t0;
    empty = if_track_empty(m, k);
    if (free_take) {
        uint32_t span = now_ms - hold_age_ms - t0;
        w = empty ? free_place(m, t, captured, from, until, span, &bpm) : 0u;
        if (!w) {
            info.status = IF_CAP_EMPTY;
            say(m, "NOTHING TO CAPTURE");
            memcpy(info.text, m->message, sizeof info.text);
            return info;
        }
        start = 0;
        now = w << 8;
    } else {
        bpm = m->bpm;
        for (i = from; i != until; i++) {
            const IfCapEv *e = &captured[i % IF_CAP_N];
            uint32_t back = (uint16_t)((now >> 8) - e->step);
            if ((e->tr & 127u) != k || back >= 64u)
                continue;
            n++;
            if (back > last)
                last = back;
            if (back < first)
                first = back;
        }
        if (!n) {
            info.status = IF_CAP_EMPTY;
            say(m, "NOTHING TO CAPTURE");
            memcpy(info.text, m->message, sizeof info.text);
            return info;
        }
        {
            uint32_t cur = (now >> 8) & 0xFFFFu;
            uint32_t pos = cur & 15u;
            uint32_t span;
            end = (uint16_t)(cur - pos + (first <= pos ? 16u : 0u));
            span = (uint16_t)(end - (uint16_t)(cur - last));
            w = empty ? (span <= 16u ? 16u : span <= 32u ? 32u : 64u) : t->length;
            if (!w)
                w = 1;
            if (w > IF_STEPS)
                w = IF_STEPS;
            start = (uint16_t)(end - w);
        }
    }
    for (i = from; i != until; i++) {
        const IfCapEv *e = &captured[i % IF_CAP_N];
        uint32_t actual = (uint16_t)(e->step - start);
        uint32_t on = (uint32_t)e->on << 8;
        uint32_t view = on >= 32768u ? (actual + 1u) % w : actual;
        uint32_t dur;
        if ((e->tr & 127u) != k || actual >= w)
            continue;
        if (same_rec(t, e->note, (uint16_t)actual, e->on))
            continue;
        if (t->rec_n >= IF_REC_MAX) {
            full = 1;
            break;
        }
        if (!put)
            save_undo(m, t, k);
        if (!put && empty)
            t->length = (uint8_t)w;
        dur = cap_len(e, now);
        t->rec[t->rec_n].step = (uint16_t)actual;
        t->rec[t->rec_n].dur = (uint16_t)dur;
        t->rec[t->rec_n].on = e->on;
        t->rec[t->rec_n].note = e->note;
        t->rec[t->rec_n].vel = e->vel ? e->vel : 1u;
        t->rec[t->rec_n].used = 1;
        t->rec_n++;
        merge_step(&t->step[view], e->note, e->vel ? e->vel : 1u);
        put++;
    }
    if (!put) {
        info.status = IF_CAP_EMPTY;
        say(m, "NOTHING TO CAPTURE");
        memcpy(info.text, m->message, sizeof info.text);
        return info;
    }
    if (free_take) {
        m->bpm = (uint16_t)bpm;
        if_play(m, 1);
    }
    info.status = full ? IF_CAP_FULL : IF_CAP_OK;
    info.free_take = free_take;
    info.steps = w;
    info.bpm = free_take ? bpm : m->bpm;
    format_take(m, &info, full, free_take, w, info.bpm);
    return info;
}

int if_undo(IfMachine *m)
{
    if (!m || !m->undo.valid || m->undo.redo)
        return 0;
    if (m->playing && ((m->rec >> m->undo.track) & 1u)) {
        say(m, "FINISH RECORDING");
        return 0;
    }
    swap_undo(m);
    m->undo.redo = 1;
    say(m, "UNDO RECORDING");
    return 1;
}

int if_redo(IfMachine *m)
{
    if (!m || !m->undo.valid || !m->undo.redo)
        return 0;
    swap_undo(m);
    m->undo.redo = 0;
    say(m, "REDO RECORDING");
    return 1;
}
