/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* Step sequencer and arpeggiator for the audio interface. Musical time
 * uses 24576 units per quarter so capture tempo matches the nightly
 * function. The monitor is monophonic per track; chords stay in the grid. */
#include "if_internal.h"

#include <string.h>

static uint32_t rng(IfMachine *m)
{
    uint32_t x = m->rng ? m->rng : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    m->rng = x ? x : 1u;
    return m->rng;
}

static void log_arp(IfMachine *m, unsigned track, unsigned note, int down)
{
    IfArpEvent *e;
    if (m->arp_log_n >= IF_ARP_LOG)
        return;
    e = &m->arp_log[m->arp_log_n++];
    e->track = (uint8_t)track;
    e->note = (uint8_t)note;
    e->vel = down ? 100u : 0u;
    e->down = (uint8_t)(down ? 1u : 0u);
}

static void arp_silence(IfMachine *m, IfTrack *t, unsigned track)
{
    if (!t->arp_note)
        return;
    log_arp(m, track, t->arp_note, 0);
    if (t->sound_note == t->arp_note)
        t->sound_on = 0;
    t->arp_note = 0;
}

static void sort_notes(uint32_t *list, uint32_t n)
{
    uint32_t i, j;
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && list[j - 1] > list[j]; j--) {
            uint32_t x = list[j];
            list[j] = list[j - 1];
            list[j - 1] = x;
        }
}

static void arp_step(IfMachine *m, IfTrack *t, unsigned track, uint32_t n)
{
    uint32_t period, list[64], len, cnt, i, o, j;
    if (t->arp_note) {
        if (t->arp_off <= n)
            arp_silence(m, t, track);
        else
            t->arp_off -= n;
    }
    if (!t->arp_mode || !t->nheld) {
        if (!t->nheld)
            arp_silence(m, t, track);
        return;
    }
    period = if_div_units(t->arp_rate);
    if (!period)
        period = 1;
    t->arp_pos += n;
    for (;;) {
        int32_t sw = (int32_t)t->arp_swing * (int32_t)period / 250;
        int32_t delta = (t->arp_idx & 1u) ? sw : -sw;
        int32_t edge_s = (int32_t)period + delta;
        uint32_t edge = edge_s < 1 ? 1u : (uint32_t)edge_s;
        if (t->arp_pos < edge && t->arp_pos != 0x0FFFFFFFu + n)
            return;
        if (t->arp_pos >= 0x0FFFFFFFu)
            t->arp_pos = 0;
        else
            t->arp_pos -= edge;
        cnt = t->nheld;
        {
            uint32_t src[IF_HELD];
            for (i = 0; i < cnt; i++)
                src[i] = t->held[i];
            if (t->arp_mode == IF_ARP_UP || t->arp_mode == IF_ARP_DOWN || t->arp_mode == IF_ARP_UPDOWN)
                sort_notes(src, cnt);
            len = 0;
            if (t->arp_mode == IF_ARP_REPEAT) {
                list[0] = t->held[t->nheld - 1u];
                len = 1;
            } else {
                uint32_t octs = t->arp_oct ? t->arp_oct : 1u;
                for (o = 0; o < octs; o++)
                    for (i = 0; i < cnt && len < 64u; i++) {
                        int32_t note = (int32_t)src[i] + 12 * (int32_t)o;
                        if (note < 0)
                            note = 0;
                        if (note > 127)
                            note = 127;
                        list[len++] = (uint32_t)note;
                    }
            }
        }
        if (!len)
            return;
        t->arp_idx++;
        if (t->arp_mode == IF_ARP_DOWN)
            j = len - 1u - (t->arp_idx % len);
        else if (t->arp_mode == IF_ARP_UPDOWN) {
            uint32_t cyc = len > 1u ? 2u * len - 2u : 1u;
            uint32_t k = t->arp_idx % cyc;
            j = k < len ? k : cyc - k;
        } else if (t->arp_mode == IF_ARP_RANDOM)
            j = rng(m) % len;
        else
            j = t->arp_idx % len;
        if (t->arp_note)
            arp_silence(m, t, track);
        if ((rng(m) & 127u) <= t->arp_prob) {
            t->arp_note = (uint8_t)list[j];
            t->arp_off = period * (t->arp_gate ? t->arp_gate : 1u) / 128u;
            t->sound_note = t->arp_note;
            t->sound_on = 1;
            log_arp(m, track, t->arp_note, 1);
        }
        if (t->arp_note) {
            if (t->arp_off <= t->arp_pos)
                arp_silence(m, t, track);
            else
                t->arp_off -= t->arp_pos;
        }
    }
}

static void arp_add(IfTrack *t, unsigned note)
{
    unsigned i;
    if (t->arp_hold && t->arp_phys == 0)
        t->nheld = 0;
    if (t->arp_phys < 255u)
        t->arp_phys++;
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] == note)
            return;
    if (t->nheld < IF_HELD)
        t->held[t->nheld++] = (uint8_t)note;
    if (t->nheld == 1u) {
        t->arp_pos = 0x0FFFFFFFu;
        t->arp_idx = 0xFFFFFFFFu;
    }
}

static void arp_remove(IfMachine *m, IfTrack *t, unsigned track, unsigned note)
{
    unsigned i, k = 0;
    if (t->arp_phys)
        t->arp_phys--;
    if (t->arp_hold)
        return;
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] != note)
            t->held[k++] = t->held[i];
    t->nheld = (uint8_t)k;
    if (!t->nheld)
        arp_silence(m, t, track);
}

static void grid_tick(IfTrack *t, uint32_t n)
{
    uint32_t len = t->length ? t->length : 1u;
    t->seq_pos += n;
    for (;;) {
        uint32_t cur = if_step_units(t, t->seq_idx);
        int starting;
        if (t->seq_pos < cur && t->seq_pos != 0x7FFFFFFFu + n)
            break;
        starting = t->seq_pos >= 0x7FFFFFFFu;
        t->seq_pos = starting ? 0u : t->seq_pos - cur;
        t->seq_idx = (uint16_t)((t->seq_idx + 1u) % len);
        t->cap_step++;
        t->steps_entered++;
        if (t->step[t->seq_idx].n && (!t->arp_mode || !t->nheld)) {
            t->sound_note = t->step[t->seq_idx].note[0];
            t->sound_on = 1;
            t->sound_left = if_step_units(t, t->seq_idx) * (t->gate ? t->gate : 64u) / 128u;
        }
    }
}

static uint32_t units_from_frames(IfMachine *m, uint32_t frames)
{
    uint64_t acc;
    uint32_t per = IF_RATE * 60u;
    if (!frames || !m->bpm)
        return 0;
    acc = (uint64_t)frames * IF_BEAT_UNITS * m->bpm + m->clock_rem;
    m->clock_rem = (uint32_t)(acc % per);
    return (uint32_t)(acc / per);
}

void if_init(IfMachine *m)
{
    unsigned i;
    memset(m, 0, sizeof *m);
    m->bpm = 120;
    m->rng = 0xA5A5A5A5u;
    m->cap_track = 255u;
    m->ceiling = 24000;
    m->lim_env = 24000;
    m->usb_level = 127;
    m->out_mode = IF_OUT_MIX;
    m->page = IF_PAGE_MIX;
    for (i = 0; i < IF_TRACKS; i++) {
        IfTrack *t = &m->track[i];
        t->length = 16;
        t->division = IF_DIV_1_16;
        t->gate = 64;
        t->arp_oct = 1;
        t->arp_rate = IF_DIV_1_16;
        t->arp_gate = 64;
        t->arp_prob = 127;
        t->level = 96;
        t->cap_step = 0xFFFFu;
        t->seq_pos = 0x7FFFFFFFu;
    }
}

void if_select(IfMachine *m, unsigned track)
{
    if (!m || track >= IF_TRACKS)
        return;
    if (m->cap_track != track) {
        m->cap_from = m->cap_w;
        m->cap_track = (uint8_t)track;
        m->cap_free = 0;
    }
    m->sel = (uint8_t)track;
}

void if_set_bpm(IfMachine *m, unsigned bpm)
{
    if (!m)
        return;
    if (bpm < IF_BPM_MIN)
        bpm = IF_BPM_MIN;
    if (bpm > IF_BPM_MAX)
        bpm = IF_BPM_MAX;
    m->bpm = (uint16_t)bpm;
}

void if_clock_external(IfMachine *m, int on)
{
    if (m)
        m->clock_ext = (uint8_t)(on ? 1u : 0u);
}

void if_arm(IfMachine *m, int on)
{
    uint8_t bit;
    if (!m)
        return;
    bit = (uint8_t)(1u << m->sel);
    if (on)
        m->rec = (uint8_t)(m->rec | bit);
    else
        m->rec = (uint8_t)(m->rec & (uint8_t)~bit);
}

void if_play(IfMachine *m, int on)
{
    unsigned i;
    if (!m)
        return;
    if (!on) {
        m->playing = 0;
        return;
    }
    if (m->playing)
        return;
    m->clock_rem = 0;
    for (i = 0; i < IF_TRACKS; i++) {
        IfTrack *t = &m->track[i];
        t->seq_idx = (uint16_t)(t->length ? t->length - 1u : 0u);
        t->seq_pos = 0x7FFFFFFFu;
        t->cap_step = 0xFFFFu;
        t->steps_entered = 0;
    }
    m->cap_from = m->cap_w;
    m->cap_free = 0;
    m->cap_track = m->sel;
    m->playing = 1;
}

int if_set_step(IfMachine *m, unsigned track, unsigned step, const uint8_t *notes, unsigned n, unsigned vel)
{
    IfStep *s;
    unsigned i;
    if (!m || track >= IF_TRACKS || step >= IF_STEPS)
        return 0;
    if (n > 4u)
        n = 4u;
    s = &m->track[track].step[step];
    memset(s, 0, sizeof *s);
    s->n = (uint8_t)n;
    s->vel = (uint8_t)(vel > 127u ? 127u : vel);
    for (i = 0; i < n; i++)
        s->note[i] = notes ? notes[i] : 0;
    return 1;
}

void if_note(IfMachine *m, unsigned track, unsigned note, unsigned vel, uint32_t ms)
{
    IfTrack *t;
    if (!m || track >= IF_TRACKS || note > 127u)
        return;
    m->ms = ms;
    t = &m->track[track];
    if (!vel) {
        if_cap_off(m, track, note);
        if (t->arp_mode)
            arp_remove(m, t, track, note);
        else if (t->sound_on && t->sound_note == note)
            t->sound_on = 0;
        return;
    }
    if (vel > 127u)
        vel = 127u;
    if_cap_on(m, track, note, vel);
    if (t->arp_mode)
        arp_add(t, note);
    else {
        t->sound_note = (uint8_t)note;
        t->sound_on = 1;
        t->sound_left = 0;
    }
}

void if_advance(IfMachine *m, uint32_t frames, const int16_t *usb, int16_t *speaker, int16_t *usb_out)
{
    uint32_t units, i;
    if (!m)
        return;
    m->arp_log_n = 0;
    units = m->clock_ext ? 0u : units_from_frames(m, frames);
    if (m->playing && units)
        for (i = 0; i < IF_TRACKS; i++) {
            IfTrack *t = &m->track[i];
            if (t->sound_on && !t->arp_mode && t->sound_left) {
                if (t->sound_left <= units)
                    t->sound_on = 0;
                else
                    t->sound_left -= units;
            }
            grid_tick(t, units);
        }
    if (units)
        for (i = 0; i < IF_TRACKS; i++)
            arp_step(m, &m->track[i], i, units);
    if_route_process(m, usb, frames, speaker, usb_out);
}
