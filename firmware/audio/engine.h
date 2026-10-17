/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R03, R04, R21, R22: 36 kHz voice mixer. The FZ INSPIRED filter runs only
 * after fz_engine_set_filter enables it, so older renders stay bit-identical.
 * Fidelity against an FZ-1 has not been measured. */
#ifndef FZ_ENGINE_H
#define FZ_ENGINE_H

#include <stdint.h>

#include "core/types.h"
#include "audio/reader.h"
#include "dsp/envelope.h"
#include "dsp/filter.h"

struct FzFxRack;

#define FZ_MAX_VOICES 16
/* Eight notes sound together. The next note replaces the oldest. */
#define FZ_POLYPHONY 8

typedef struct {
    FzReader reader;
    int alive;
    int gated; /* note-off stops a gated voice; a one-shot plays out */
    int note;
    int velocity;
    int volume; /* 0..127, 127 = unity before pan */
    int pan;    /* 0 left, 64 center, 127 right */
    uint16_t age;
    uint16_t asset_tag; /* copied so a freed asset can be refused */
    const int16_t *pcm;
    int16_t gain_q15;
    int16_t cut_offset;
    int16_t res_offset;
    int use_env;
    int use_filter_env;
    int env_amount;
    int choke;
    int no_send;
    uint8_t send_track[4];
    uint8_t send_trim[4];
    int released;
    uint32_t lfo_phase;
    uint32_t base_step; /* Q16 playback step before pitch LFO */
    int pitch_bent;
    FzSvFilter svf;
    FzEnvRun amp_env;
    FzEnvRun filt_env;
} FzVoice;

typedef struct {
    FzVoice voice[FZ_MAX_VOICES];
    uint16_t age_clock;
    uint32_t rendered_frames;
    uint32_t stolen;
    uint32_t bad_samples;
    int active_notes;
    int filter_on;
    int cutoff;
    int resonance;
    int drive; /* 0 = no extra shaper inside the filter */
    int lfo_rate;
    int lfo_depth;
    int lfo_shape;
    int lfo_dest;
    int lfo_retrigger;
    uint32_t lfo_inc;
    uint32_t lfo_phase;
    struct FzFxRack *fx; /* null: no send return is mixed */
} FzEngine;

void fz_engine_init(FzEngine *e);

/* Direct PCM start for tests and for notes whose region was resolved by the
 * caller. `asset_tag` is remembered so storage can ask whether it is busy. */
int fz_engine_note_on_pcm(FzEngine *e, const int16_t *pcm, uint32_t frames,
                          uint32_t native_rate, int root, int note, int cents,
                          int velocity, int volume, int pan, int one_shot,
                          int cycle, uint16_t asset_tag);
void fz_engine_note_off(FzEngine *e, int note);
int fz_engine_asset_busy(const FzEngine *e, uint16_t asset_tag);
void fz_engine_render(FzEngine *e, int16_t *interleaved_lr, uint32_t frames);

/* `enabled` 0 leaves the mix bit-identical to a build without a filter. */
void fz_engine_set_filter(FzEngine *e, int enabled, int cutoff, int resonance, int drive);
/* Depth 0 or FZ_LFO_OFF leaves the sample path unchanged.
 * FZ_LFO_PITCH bends the reader step by about two semitones at full depth.
 * FZ_LFO_PAN adds that bipolar depth to the voice pan.
 * Shape is FZ_LFO_TRI, FZ_LFO_SQR, FZ_LFO_SIN, FZ_LFO_SAW, or FZ_LFO_SH. */
void fz_engine_set_lfo(FzEngine *e, int rate, int depth, int shape, int dest, int retrigger);
/* One cycle of the LFO, -127..127. Phase 0 is the start of the shape. */
int fz_lfo_bipolar(uint32_t phase, int shape);
/* A null rack leaves the mix unchanged. Sends are post-voice and mono into the rack. */
void fz_engine_set_fx(FzEngine *e, struct FzFxRack *fx);
void fz_engine_voice_send(FzEngine *e, int slot, const uint8_t track_send[4], const uint8_t trim[4]);
void fz_engine_voice_region(FzEngine *e, int slot, int gain_q15, int cut_offset, int res_offset, int choke);
void fz_engine_voice_amp(FzEngine *e, int slot, const FzEnvelope *env);
void fz_engine_voice_filter_env(FzEngine *e, int slot, const FzEnvelope *env, int amount);

#endif
