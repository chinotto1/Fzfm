/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "arp.h"

#include <string.h>

static const uint64_t k_den = 36000ull * 60ull * 1000ull;
static const uint16_t k_rate_ticks[FZ_ARP_RATE_COUNT] = {96, 48, 32, 24, 16, 12};

static int clamp_i(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static int rate_ticks(const FzArp *arp)
{
    int rate = arp->rate;
    if (rate < 0 || rate >= FZ_ARP_RATE_COUNT)
        rate = FZ_ARP_1_16;
    return (int)k_rate_ticks[rate];
}

static int down_count(const FzArp *arp)
{
    int i, n = 0;
    for (i = 0; i < arp->chord_n; i++)
        if (arp->down[i])
            n++;
    return n;
}

static void sort_notes(int *notes, int n)
{
    int i, j;
    for (i = 1; i < n; i++) {
        int v = notes[i];
        j = i;
        while (j > 0 && notes[j - 1] > v) {
            notes[j] = notes[j - 1];
            j--;
        }
        notes[j] = v;
    }
}

static void reverse_notes(int *notes, int n)
{
    int i;
    for (i = 0; i < n / 2; i++) {
        int t = notes[i];
        notes[i] = notes[n - 1 - i];
        notes[n - 1 - i] = t;
    }
}

/* Pitch order, then octaves. ORD + PLAY keeps the order the keys went down. */
static int forward_notes(const FzArp *arp, int *dst, int cap)
{
    int base[FZ_ARP_NOTES];
    int n, i, o, w, oct, sort;
    if (!arp || arp->chord_n <= 0 || cap <= 0)
        return 0;
    n = arp->chord_n;
    if (n > FZ_ARP_NOTES)
        n = FZ_ARP_NOTES;
    for (i = 0; i < n; i++)
        base[i] = arp->chord[i];
    sort = !(arp->mode == FZ_ARP_ORD && arp->order_play);
    if (sort)
        sort_notes(base, n);
    oct = clamp_i(arp->octaves, 1, 4);
    w = 0;
    for (o = 0; o < oct; o++) {
        for (i = 0; i < n && w < cap; i++) {
            int note = base[i] + 12 * o;
            if (note > 127)
                continue;
            dst[w++] = note;
        }
    }
    return w;
}

static int cycle_notes(const FzArp *arp, int *dst, int cap)
{
    int list[32];
    int n, i, w;
    n = forward_notes(arp, list, 32);
    if (n <= 0 || cap <= 0)
        return 0;
    if (arp->mode == FZ_ARP_DN) {
        reverse_notes(list, n);
        w = n < cap ? n : cap;
        for (i = 0; i < w; i++)
            dst[i] = list[i];
        return w;
    }
    if (arp->mode == FZ_ARP_UPDN && n > 1) {
        w = 0;
        for (i = 0; i < n && w < cap; i++)
            dst[w++] = list[i];
        for (i = n - 2; i > 0 && w < cap; i--)
            dst[w++] = list[i];
        return w;
    }
    w = n < cap ? n : cap;
    for (i = 0; i < w; i++)
        dst[i] = list[i];
    return w;
}

static void arm_now(FzArp *arp)
{
    arp->origin = arp->tick;
    arp->step = 0;
    arp->next_on = arp->tick;
    arp->armed = 1;
    if (arp->live >= 0) {
        arp->gate_off = arp->tick;
        arp->gate_pending = 1;
    }
}

static uint32_t xorshift(FzArp *arp)
{
    uint32_t x = arp->rng ? arp->rng : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    arp->rng = x ? x : 1u;
    return arp->rng;
}

void fz_arp_init(FzArp *arp)
{
    if (!arp)
        return;
    memset(arp, 0, sizeof *arp);
    arp->rate = FZ_ARP_1_16;
    arp->octaves = 1;
    arp->gate = 80;
    arp->prob = 100;
    arp->live = -1;
    arp->cursor = -1;
    arp->rng = 1u;
}

void fz_arp_key(FzArp *arp, int note, int down)
{
    int i;
    if (!arp || note < 0 || note > 127)
        return;
    if (down) {
        if (arp->hold && down_count(arp) == 0) {
            arp->chord_n = 0;
            memset(arp->down, 0, sizeof arp->down);
        }
        for (i = 0; i < arp->chord_n; i++) {
            if (arp->chord[i] == (uint8_t)note) {
                arp->down[i] = 1;
                arm_now(arp);
                return;
            }
        }
        if (arp->chord_n >= FZ_ARP_NOTES)
            return;
        arp->chord[arp->chord_n] = (uint8_t)note;
        arp->down[arp->chord_n] = 1;
        arp->chord_n++;
        arm_now(arp);
        return;
    }
    for (i = 0; i < arp->chord_n; i++) {
        if (arp->chord[i] != (uint8_t)note || !arp->down[i])
            continue;
        arp->down[i] = 0;
        if (!arp->hold) {
            int j;
            for (j = i; j < arp->chord_n - 1; j++) {
                arp->chord[j] = arp->chord[j + 1];
                arp->down[j] = arp->down[j + 1];
            }
            arp->chord_n--;
            if (arp->chord_n > 0)
                arm_now(arp);
            else {
                arp->armed = 0;
                if (arp->live >= 0) {
                    arp->gate_off = arp->tick;
                    arp->gate_pending = 1;
                }
            }
        }
        return;
    }
}

void fz_arp_turn(FzArp *arp, int page, int knob, int detents)
{
    int sign, was;
    if (!arp || detents == 0 || knob < 0 || knob > 3)
        return;
    sign = detents > 0 ? 1 : -1;
    was = arp->mode;
    if (page) {
        if (knob == 0)
            arp->swing = clamp_i(arp->swing + detents * 5, 0, 100);
        else if (knob == 1)
            arp->prob = clamp_i(arp->prob + detents * 5, 0, 100);
        else if (knob == 2)
            arp->hold = sign > 0 ? 1 : 0;
        else
            arp->order_play = sign > 0 ? 1 : 0;
        return;
    }
    if (knob == 0)
        arp->mode = clamp_i(arp->mode + sign, 0, FZ_ARP_MODE_COUNT - 1);
    else if (knob == 1)
        arp->rate = clamp_i(arp->rate + sign, 0, FZ_ARP_RATE_COUNT - 1);
    else if (knob == 2)
        arp->octaves = clamp_i(arp->octaves + sign, 1, 4);
    else
        arp->gate = clamp_i(arp->gate + detents * 5, 20, 100);
    if (was == FZ_ARP_OFF && arp->mode != FZ_ARP_OFF && arp->chord_n > 0)
        arm_now(arp);
    if (arp->mode == FZ_ARP_OFF && arp->live >= 0) {
        arp->gate_off = arp->tick;
        arp->gate_pending = 1;
    }
}

int fz_arp_pattern(const FzArp *arp, int *dst, int cap, int *cursor)
{
    if (cursor)
        *cursor = arp ? arp->cursor : -1;
    if (!arp || !dst || cap <= 0)
        return 0;
    return cycle_notes(arp, dst, cap);
}

const char *fz_arp_mode_name(int mode)
{
    static const char *names[FZ_ARP_MODE_COUNT] = {"OFF", "UP", "DN", "UPDN", "RND", "ORD"};
    if (mode < 0 || mode >= FZ_ARP_MODE_COUNT)
        return "OFF";
    return names[mode];
}

const char *fz_arp_rate_name(int rate)
{
    static const char *names[FZ_ARP_RATE_COUNT] = {"1/4", "1/8", "1/8T", "1/16", "16T", "1/32"};
    if (rate < 0 || rate >= FZ_ARP_RATE_COUNT)
        return "1/16";
    return names[rate];
}

static uint32_t step_time(uint32_t origin, int step, int ticks, int swing)
{
    uint32_t t = origin + (uint32_t)step * (uint32_t)ticks;
    if (step & 1)
        t += (uint32_t)swing * (uint32_t)ticks / 200u;
    return t;
}

int fz_arp_advance(FzArp *arp, uint32_t frames, uint32_t bpm_milli, FzArpEvent *out, int cap)
{
    uint32_t t1;
    int n = 0;
    int guard = 0;
    if (!arp)
        return 0;
    if (bpm_milli < 10000)
        bpm_milli = 10000;
    if (bpm_milli > 400000)
        bpm_milli = 400000;
    if (frames > 0) {
        uint64_t sum = arp->acc + (uint64_t)frames * (uint64_t)bpm_milli * 96ull;
        arp->tick += (uint32_t)(sum / k_den);
        arp->acc = sum % k_den;
    }
    t1 = arp->tick;
    if (arp->mode == FZ_ARP_OFF || arp->chord_n <= 0) {
        arp->armed = 0;
        arp->cursor = -1;
        if (arp->live >= 0 && out && n < cap) {
            out[n].note = arp->live;
            out[n].on = 0;
            n++;
            arp->live = -1;
            arp->gate_off = 0;
            arp->gate_pending = 0;
        }
        return n;
    }
    while (out && n < cap && guard++ < 48) {
        int due_off = arp->live >= 0 && arp->gate_pending && arp->gate_off <= t1;
        int due_on = arp->armed && arp->next_on <= t1;
        int ticks, span, idx, note, gate, hit;
        int cycle[32];
        uint32_t at;
        if (!due_off && !due_on)
            break;
        if (due_off && (!due_on || arp->gate_off <= arp->next_on)) {
            out[n].note = arp->live;
            out[n].on = 0;
            n++;
            arp->live = -1;
            arp->gate_off = 0;
            arp->gate_pending = 0;
            continue;
        }
        at = arp->next_on;
        span = cycle_notes(arp, cycle, 32);
        if (span <= 0) {
            arp->armed = 0;
            break;
        }
        if (arp->mode == FZ_ARP_RND)
            idx = (int)(xorshift(arp) % (uint32_t)span);
        else
            idx = arp->step % span;
        note = cycle[idx];
        arp->cursor = idx;
        arp->step++;
        ticks = rate_ticks(arp);
        arp->next_on = step_time(arp->origin, arp->step, ticks, arp->swing);
        if (arp->next_on <= at)
            arp->next_on = at + 1u;
        hit = arp->prob >= 100 || (arp->prob > 0 && (int)(xorshift(arp) % 100u) < arp->prob);
        if (!hit)
            continue;
        if (arp->live >= 0) {
            out[n].note = arp->live;
            out[n].on = 0;
            n++;
            arp->live = -1;
            arp->gate_off = 0;
            arp->gate_pending = 0;
            if (n >= cap)
                break;
        }
        out[n].note = note;
        out[n].on = 1;
        n++;
        arp->live = note;
        gate = ticks * arp->gate / 100;
        if (gate < 1)
            gate = 1;
        arp->gate_off = at + (uint32_t)gate;
        if (arp->gate_off > arp->next_on)
            arp->gate_off = arp->next_on;
        arp->gate_pending = 1;
    }
    return n;
}
