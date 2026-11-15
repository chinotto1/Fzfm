/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* Stereo router: USB in, per-track monitor, peak limiter, USB out.
 * There is no equalizer stage and no filter history. An impulse under
 * the ceiling stays an impulse. */
#include "if_internal.h"

static int32_t gain_of(unsigned level)
{
    if (level > 127u)
        level = 127u;
    return (int32_t)((level * 32768u) / 127u);
}

static void pan_gain(int pan, int32_t *left, int32_t *right)
{
    if (pan > 63)
        pan = 63;
    if (pan < -64)
        pan = -64;
    *left = 32768 - (pan > 0 ? pan * 512 : 0);
    *right = 32768 + (pan < 0 ? pan * 512 : 0);
}

static int32_t apply(int32_t sample, int32_t gain)
{
    return (int32_t)(((int64_t)sample * gain) >> 15);
}

static int16_t sat16(int32_t sample)
{
    if (sample > 32767)
        return 32767;
    if (sample < -32768)
        return -32768;
    return (int16_t)sample;
}

static uint16_t peak16(int32_t sample)
{
    if (sample < 0)
        sample = -sample;
    if (sample > 32767)
        sample = 32767;
    return (uint16_t)sample;
}

static int32_t voice(IfTrack *t)
{
    static const uint16_t semi[12] = {32768, 34716, 36781, 38968, 41285, 43740,
                                       46341, 49097, 52016, 55109, 58386, 61858};
    unsigned note, s;
    int rel, oct;
    uint32_t inc;
    int32_t amp;
    if (!t->sound_on || t->mute || !t->level)
        return 0;
    note = t->sound_note > 127u ? 69u : t->sound_note;
    rel = (int)note - 69;
    oct = rel >= 0 ? rel / 12 : -(((-rel) + 11) / 12);
    s = (unsigned)(rel - oct * 12);
    inc = (440u * 65536u) / IF_RATE;
    inc = (uint32_t)((uint64_t)inc * semi[s] / 32768u);
    while (oct > 0) {
        inc <<= 1;
        oct--;
    }
    while (oct < 0 && inc > 1u) {
        inc >>= 1;
        oct++;
    }
    t->phase = (uint16_t)(t->phase + (uint16_t)inc);
    amp = (t->phase & 0x8000u) ? 5000 : -5000;
    return apply(amp, gain_of(t->level));
}

void if_route_process(IfMachine *m, const int16_t *usb, uint32_t frames, int16_t *speaker, int16_t *usb_out)
{
    uint32_t i;
    int32_t usb_g, usb_l, usb_r;
    uint16_t peak_l = 0, peak_r = 0;
    if (!m)
        return;
    m->clipped = 0;
    usb_g = m->usb_mute ? 0 : gain_of(m->usb_level);
    pan_gain(m->usb_pan, &usb_l, &usb_r);
    for (i = 0; i < frames; i++) {
        int32_t in_l = usb ? usb[2u * i] : 0;
        int32_t in_r = usb ? usb[2u * i + 1u] : 0;
        int32_t loc_l = 0, loc_r = 0, l, r, a, out_l, out_r;
        unsigned tr;
        for (tr = 0; tr < IF_TRACKS; tr++) {
            IfTrack *t = &m->track[tr];
            int32_t g_l, g_r, s = voice(t);
            pan_gain(t->pan, &g_l, &g_r);
            loc_l += apply(s, g_l);
            loc_r += apply(s, g_r);
        }
        in_l = apply(apply(in_l, usb_g), usb_l);
        in_r = apply(apply(in_r, usb_g), usb_r);
        l = in_l + loc_l;
        r = in_r + loc_r;
        a = l < 0 ? -l : l;
        if ((r < 0 ? -r : r) > a)
            a = r < 0 ? -r : r;
        if (a > m->ceiling)
            m->clipped = 1;
        if (a > m->lim_env)
            m->lim_env += (a - m->lim_env) >> 2;
        else if (m->lim_env > m->ceiling)
            m->lim_env -= ((m->lim_env - m->ceiling) >> 8) + 1;
        if (m->lim_env > m->ceiling && m->lim_env > 0) {
            int32_t g = (int32_t)(((int64_t)m->ceiling << 15) / m->lim_env);
            l = apply(l, g);
            r = apply(r, g);
        }
        l = sat16(l);
        r = sat16(r);
        if (m->out_mode == IF_OUT_LOOP) {
            out_l = sat16(in_l);
            out_r = sat16(in_r);
        } else if (m->out_mode == IF_OUT_LOCAL) {
            int32_t ol = loc_l, orr = loc_r, oa;
            oa = ol < 0 ? -ol : ol;
            if ((orr < 0 ? -orr : orr) > oa)
                oa = orr < 0 ? -orr : orr;
            if (oa > m->ceiling && m->lim_env > 0) {
                int32_t g = (int32_t)(((int64_t)m->ceiling << 15) / m->lim_env);
                ol = apply(ol, g);
                orr = apply(orr, g);
            }
            out_l = sat16(ol);
            out_r = sat16(orr);
        } else {
            out_l = l;
            out_r = r;
        }
        if (speaker) {
            speaker[2u * i] = (int16_t)l;
            speaker[2u * i + 1u] = (int16_t)r;
        }
        if (usb_out) {
            usb_out[2u * i] = (int16_t)out_l;
            usb_out[2u * i + 1u] = (int16_t)out_r;
        }
        if (peak16(l) > peak_l)
            peak_l = peak16(l);
        if (peak16(r) > peak_r)
            peak_r = peak16(r);
    }
    if (frames) {
        m->meter_l = peak_l;
        m->meter_r = peak_r;
    }
}
