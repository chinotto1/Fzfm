/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R03, R04, R21: 36 kHz voice mixer. Cutoff and resonance are stored on the
 * track and are not yet a filter (R22 is unimplemented, not a silent bypass
 * labelled as an FZ model). Volume and pan are the mix state. */
#ifndef FZ_ENGINE_H
#define FZ_ENGINE_H

#include <stdint.h>

#include "core/types.h"
#include "audio/reader.h"

#define FZ_MAX_VOICES 16

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
} FzVoice;

typedef struct {
    FzVoice voice[FZ_MAX_VOICES];
    uint16_t age_clock;
    uint32_t rendered_frames;
    uint32_t stolen;
    int active_notes;
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

#endif
