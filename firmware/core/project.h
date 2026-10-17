/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R05, R06, R07, R21, R23, R31: instruments, regions, tracks, patterns.
 * Handles are index + generation, never raw pointers. Ceilings below are
 * host-test safety limits, not a claim about FM-1 capacity. */
#ifndef FZ_PROJECT_H
#define FZ_PROJECT_H

#include <stdint.h>

#include "types.h"

#define FZ_MAX_INSTRUMENTS 8
#define FZ_MAX_TRACKS 8
#define FZ_MAX_PATTERNS 32
#define FZ_MAX_REGIONS 64
#define FZ_MAX_ASSETS 48
#define FZ_NAME_LEN 20
#define FZ_MULTI_SLOTS 16

typedef struct {
    uint16_t index;
    uint16_t generation;
} FzId;

typedef enum { FZ_INST_NONE = 0, FZ_INST_MULTI = 1, FZ_INST_SAMPLE = 2 } FzInstKind;

typedef struct {
    int alive;
    uint16_t generation;
    const int16_t *pcm;
    uint32_t frames;
    uint32_t native_rate;
    int cycle;
    uint32_t checksum;
    uint16_t tag;
} FzAsset;

typedef struct {
    int alive;
    uint16_t generation;
    FzId asset;
    uint8_t root;
    int16_t tune_cents;
    uint8_t key_low;
    uint8_t key_high;
    uint8_t manual_zone;
    uint8_t one_shot;
    uint8_t slot; /* multi-sample slot, or 0xFF */
} FzRegion;

typedef struct {
    int alive;
    uint16_t generation;
    FzInstKind kind;
    char name[FZ_NAME_LEN];
    FzId region[FZ_MULTI_SLOTS];
    int region_count;
    uint8_t base_note;
    int8_t anchor_pitch_class; /* 0 = C ... 11 = B */
} FzInstrument;

typedef struct {
    int alive;
    uint16_t generation;
    uint32_t length_ticks;
    uint32_t event_count;
} FzPattern;

typedef struct {
    int alive;
    uint16_t generation;
    FzId instrument;
    FzId pattern;
    int16_t cutoff;    /* stored only; the filter is not implemented */
    int16_t resonance; /* stored only */
    int16_t volume;    /* 0..127, shared by HOME and GLO */
    int16_t pan;       /* 0..127, 64 center, shared by HOME and GLO */
} FzTrack;

typedef struct {
    FzInstrument instrument[FZ_MAX_INSTRUMENTS];
    FzTrack track[FZ_MAX_TRACKS];
    FzPattern pattern[FZ_MAX_PATTERNS];
    FzRegion region[FZ_MAX_REGIONS];
    FzAsset asset[FZ_MAX_ASSETS];
    int16_t *pcm_store;
    uint32_t pcm_capacity;
    uint32_t pcm_used;
    uint16_t asset_tags;
    int selected_track; /* -1 none */
    int has_music;
    int external_clock;
    double tempo_bpm;
    char last_error[48];
} FzProject;

static inline FzId fz_id_none(void)
{
    FzId id;
    id.index = 0;
    id.generation = 0;
    return id;
}

void fz_project_init(FzProject *p, int16_t *pcm_store, uint32_t pcm_capacity_frames);
FzStatus fz_project_new_instrument(FzProject *p, FzInstKind kind, FzId *out_inst, FzId *out_track);
FzStatus fz_project_delete_instrument(FzProject *p, FzId inst);

FzInstrument *fz_instrument(FzProject *p, FzId id);
FzTrack *fz_track(FzProject *p, FzId id);
FzRegion *fz_region(FzProject *p, FzId id);
FzAsset *fz_asset(FzProject *p, FzId id);
FzId fz_track_id(const FzProject *p, int index);

/* PCM is copied into the project store. The asset is immutable after return. */
FzStatus fz_project_add_pcm(FzProject *p, const int16_t *pcm, uint32_t frames, uint32_t native_rate,
                            int cycle, FzId *out_asset);
int fz_project_asset_refs(const FzProject *p, FzId asset);

FzStatus fz_project_add_region(FzProject *p, FzId inst, FzId asset, uint8_t root, int slot, FzId *out_region);
void fz_project_recompute_zones(FzProject *p, FzId inst);
/* Closest root, tie breaks toward the lower root. Returns -1 if none. */
int fz_zone_root_for_note(const uint8_t *roots, int n_roots, int note);
FzRegion *fz_instrument_region_for_note(FzProject *p, FzId inst, int note);

void fz_track_set_volume(FzProject *p, FzId track, int volume);
void fz_track_set_pan(FzProject *p, FzId track, int pan);
void fz_track_set_cutoff(FzProject *p, FzId track, int cutoff);
void fz_track_set_resonance(FzProject *p, FzId track, int resonance);
void fz_home_turn(FzProject *p, int knob, int detents);
void fz_home_select_track(FzProject *p, int detents);

uint32_t fz_pcm_checksum(const int16_t *pcm, uint32_t frames);

#endif
