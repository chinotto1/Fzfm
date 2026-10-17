/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "engine.h"

#include "dsp/fx.h"

#include <math.h>
#include <string.h>

void fz_engine_init(FzEngine *e)
{
    memset(e, 0, sizeof *e);
}

static int find_free(FzEngine *e)
{
    int i;
    for (i = 0; i < FZ_MAX_VOICES; i++) {
        if (!e->voice[i].alive)
            return i;
    }
    return -1;
}

static int oldest(FzEngine *e)
{
    int i, best = 0;
    uint16_t best_age = 0xFFFFu;
    for (i = 0; i < FZ_MAX_VOICES; i++) {
        if (e->voice[i].age < best_age) {
            best_age = e->voice[i].age;
            best = i;
        }
    }
    return best;
}

int fz_engine_note_on_pcm(FzEngine *e, const int16_t *pcm, uint32_t frames, uint32_t native_rate,
                          int root, int note, int cents, int velocity, int volume, int pan,
                          int one_shot, int cycle, uint16_t asset_tag)
{
    int slot;
    FzVoice *v;
    int rc;

    if (!e || !pcm || frames == 0)
        return -1;
    if (velocity < 0)
        velocity = 0;
    if (velocity > 127)
        velocity = 127;
    if (volume < 0)
        volume = 0;
    if (volume > 127)
        volume = 127;
    if (pan < 0)
        pan = 0;
    if (pan > 127)
        pan = 127;

    slot = find_free(e);
    if (slot < 0) {
        slot = oldest(e);
        e->stolen++;
    }
    v = &e->voice[slot];
    memset(v, 0, sizeof *v);
    if (cycle)
        rc = fz_reader_start_cycle(&v->reader, pcm, frames, note, cents);
    else
        rc = fz_reader_start_sample(&v->reader, pcm, frames, native_rate, root, note, cents);
    if (rc != 0)
        return -1;
    v->alive = 1;
    v->gated = one_shot ? 0 : 1;
    v->note = note;
    v->velocity = velocity;
    v->volume = volume;
    v->pan = pan;
    v->age = ++e->age_clock;
    v->asset_tag = asset_tag;
    v->pcm = pcm;
    v->gain_q15 = 32767;
    return slot;
}

static FzVoice *voice_at(FzEngine *e, int slot)
{
    if (!e || slot < 0 || slot >= FZ_MAX_VOICES || !e->voice[slot].alive)
        return 0;
    return &e->voice[slot];
}

void fz_engine_set_filter(FzEngine *e, int enabled, int cutoff, int resonance, int drive)
{
    if (!e)
        return;
    e->filter_on = enabled ? 1 : 0;
    if (cutoff < 0)
        cutoff = 0;
    if (cutoff > 127)
        cutoff = 127;
    if (resonance < 0)
        resonance = 0;
    if (resonance > 127)
        resonance = 127;
    if (drive < 0)
        drive = 0;
    if (drive > 127)
        drive = 127;
    e->cutoff = cutoff;
    e->resonance = resonance;
    e->drive = drive;
}

void fz_engine_set_lfo(FzEngine *e, int rate, int depth, int shape, int dest, int retrigger)
{
    double hz;
    if (!e)
        return;
    if (rate < 0)
        rate = 0;
    if (rate > 127)
        rate = 127;
    if (depth < 0)
        depth = 0;
    if (depth > 127)
        depth = 127;
    if (dest < 0)
        dest = 0;
    if (dest > 2)
        dest = 2;
    e->lfo_rate = rate;
    e->lfo_depth = depth;
    e->lfo_shape = shape ? 1 : 0;
    e->lfo_dest = dest;
    e->lfo_retrigger = retrigger ? 1 : 0;
    if (rate <= 0 || depth <= 0 || dest == 0) {
        e->lfo_inc = 0;
        return;
    }
    hz = 0.1 * pow(200.0, (double)rate / 127.0);
    e->lfo_inc = (uint32_t)(hz / (double)FZ_ENGINE_RATE_HZ * 4294967296.0 + 0.5);
}

void fz_engine_set_fx(FzEngine *e, struct FzFxRack *fx)
{
    if (e)
        e->fx = fx;
}

void fz_engine_voice_send(FzEngine *e, int slot, const uint8_t track_send[4], const uint8_t trim[4])
{
    FzVoice *v = voice_at(e, slot);
    int i;
    if (!v || !track_send || !trim)
        return;
    for (i = 0; i < 4; i++) {
        v->send_track[i] = track_send[i];
        v->send_trim[i] = trim[i];
    }
}

static int lfo_bipolar(uint32_t phase, int shape)
{
    uint32_t p = phase >> 16;
    if (shape == 1)
        return p < 32768u ? 127 : -127;
    if (p < 32768u)
        return (int)((p * 254u) / 32768u) - 127;
    return 127 - (int)(((p - 32768u) * 254u) / 32768u);
}

void fz_engine_voice_region(FzEngine *e, int slot, int gain_q15, int cut_offset, int res_offset, int choke)
{
    FzVoice *v = voice_at(e, slot);
    if (!v)
        return;
    if (gain_q15 < 0)
        gain_q15 = 0;
    if (gain_q15 > 32767)
        gain_q15 = 32767;
    v->gain_q15 = (int16_t)gain_q15;
    v->cut_offset = (int16_t)cut_offset;
    v->res_offset = (int16_t)res_offset;
    v->choke = choke;
    if (choke > 0) {
        int i;
        for (i = 0; i < FZ_MAX_VOICES; i++) {
            if (i != slot && e->voice[i].alive && e->voice[i].choke == choke)
                e->voice[i].alive = 0;
        }
    }
}

void fz_engine_voice_amp(FzEngine *e, int slot, const FzEnvelope *env)
{
    FzVoice *v = voice_at(e, slot);
    if (!v || !env)
        return;
    fz_env_start(&v->amp_env, env);
    v->use_env = v->amp_env.active;
}

void fz_engine_voice_filter_env(FzEngine *e, int slot, const FzEnvelope *env, int amount)
{
    FzVoice *v = voice_at(e, slot);
    if (!v || !env)
        return;
    if (amount > 127)
        amount = 127;
    if (amount < -127)
        amount = -127;
    fz_env_start(&v->filt_env, env);
    v->use_filter_env = v->filt_env.active;
    v->env_amount = amount;
}

void fz_engine_note_off(FzEngine *e, int note)
{
    int i;
    if (!e)
        return;
    for (i = FZ_MAX_VOICES - 1; i >= 0; i--) {
        if (e->voice[i].alive && e->voice[i].gated && e->voice[i].note == note) {
            if (e->voice[i].use_env || e->voice[i].use_filter_env) {
                e->voice[i].released = 1;
                if (e->voice[i].use_env)
                    fz_env_release(&e->voice[i].amp_env);
                if (e->voice[i].use_filter_env)
                    fz_env_release(&e->voice[i].filt_env);
            } else {
                e->voice[i].alive = 0;
                e->voice[i].reader.active = 0;
            }
            return;
        }
    }
}

int fz_engine_asset_busy(const FzEngine *e, uint16_t asset_tag)
{
    int i;
    if (!e)
        return 0;
    for (i = 0; i < FZ_MAX_VOICES; i++) {
        if (e->voice[i].alive && e->voice[i].asset_tag == asset_tag)
            return 1;
    }
    return 0;
}

void fz_engine_render(FzEngine *e, int16_t *interleaved_lr, uint32_t frames)
{
    uint32_t n;
    int i;

    if (!e || !interleaved_lr)
        return;
    for (n = 0; n < frames; n++) {
        int32_t left = 0;
        int32_t right = 0;
        int alive = 0;
        int lfo_on = e->lfo_inc != 0 && e->lfo_depth > 0 && e->lfo_dest != 0;
        int shared_mod = 0;
        int32_t send_acc[4];
        send_acc[0] = send_acc[1] = send_acc[2] = send_acc[3] = 0;
        if (lfo_on && !e->lfo_retrigger) {
            e->lfo_phase += e->lfo_inc;
            shared_mod = lfo_bipolar(e->lfo_phase, e->lfo_shape) * e->lfo_depth / 127;
        }
        for (i = 0; i < FZ_MAX_VOICES; i++) {
            FzVoice *v = &e->voice[i];
            int32_t y, l, r;
            int16_t s;
            if (!v->alive)
                continue;
            s = fz_reader_next(&v->reader);
            if (!v->reader.active && !v->reader.cycle)
                v->alive = 0;
            y = ((int32_t)s * v->volume * v->velocity) / (127 * 127);
            if (v->gain_q15 != 32767)
                y = (y * v->gain_q15) / 32767;
            if (lfo_on && e->lfo_dest == 2) {
                int mod = shared_mod;
                if (e->lfo_retrigger) {
                    v->lfo_phase += e->lfo_inc;
                    mod = lfo_bipolar(v->lfo_phase, e->lfo_shape) * e->lfo_depth / 127;
                }
                y = y * (256 + mod) / 256;
            }
            if (v->use_env) {
                int32_t eg = fz_env_next(&v->amp_env);
                y = (y * eg) / 32767;
                if (v->released && !v->amp_env.active)
                    v->alive = 0;
            }
            if (e->filter_on) {
                int cut = e->cutoff + v->cut_offset;
                int res = e->resonance + v->res_offset;
                float in, out;
                if (v->use_filter_env) {
                    int32_t eg = fz_env_next(&v->filt_env);
                    cut += (int)((v->env_amount * eg) / 32767);
                }
                if (lfo_on && e->lfo_dest == 1) {
                    int mod = shared_mod;
                    if (e->lfo_retrigger) {
                        v->lfo_phase += e->lfo_inc;
                        mod = lfo_bipolar(v->lfo_phase, e->lfo_shape) * e->lfo_depth / 127;
                    }
                    cut += mod;
                }
                if (cut < 0)
                    cut = 0;
                if (cut > 127)
                    cut = 127;
                if (res < 0)
                    res = 0;
                if (res > 127)
                    res = 127;
                if (v->svf.a1 == 0.0f || cut != v->svf.cutoff || res != v->svf.resonance)
                    fz_svf_set(&v->svf, cut, res);
                in = (float)y / 32768.0f;
                out = fz_svf_low(&v->svf, in);
                if (e->drive > 0 && isfinite(out)) {
                    float gain = 1.0f + (float)e->drive * (3.0f / 127.0f);
                    float shaped = out * gain;
                    float mag = shaped < 0.0f ? -shaped : shaped;
                    if (mag > 1.15f) {
                        float sign = shaped < 0.0f ? -1.0f : 1.0f;
                        shaped = sign * (1.15f + (mag - 1.15f) / (1.0f + (mag - 1.15f)));
                    }
                    out = shaped;
                }
                if (!isfinite(out)) {
                    out = 0.0f;
                    e->bad_samples++;
                }
                if (out > 4.0f)
                    out = 4.0f;
                if (out < -4.0f)
                    out = -4.0f;
                y = (int32_t)lrintf(out * 32768.0f);
            }
            l = (y * (128 - v->pan)) / 128;
            r = (y * v->pan) / 128;
            left += l;
            right += r;
            if (e->fx && !v->no_send) {
                int s;
                for (s = 0; s < 4; s++) {
                    if (v->send_track[s] > 0 && v->send_trim[s] > 0)
                        send_acc[s] += fz_send_amount(y, v->send_track[s], v->send_trim[s]);
                }
            }
            if (v->alive)
                alive++;
        }
        if (e->fx) {
            int32_t wet_l = 0, wet_r = 0;
            fz_fx_process(e->fx, send_acc, &wet_l, &wet_r);
            left += wet_l;
            right += wet_r;
        }
        interleaved_lr[n * 2u] = fz_sat16(left);
        interleaved_lr[n * 2u + 1u] = fz_sat16(right);
        e->active_notes = alive;
    }
    e->rendered_frames += frames;
}
