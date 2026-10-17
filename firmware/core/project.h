/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R05, R06, R07, R21, R23, R31: instruments, regions, tracks, patterns.
 * Handles are index + generation, never raw pointers. Ceilings below are
 * host-test safety limits, not a claim about FM-1 capacity. */
#ifndef FZ_PROJECT_H
#define FZ_PROJECT_H

#include <stdint.h>

#include "types.h"
#include "dsp/additive.h"
#include "dsp/envelope.h"
#include "dsp/loopprog.h"

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
    uint32_t start_frame;
    uint32_t end_frame; /* 0 = through the end of the asset */
    int reverse;
    int16_t gain_q15; /* 32767 = unity */
    int16_t cut_offset; /* added to the track cutoff, then clamped */
    int16_t res_offset;
    uint8_t send_trim[4]; /* 0 = silent send, 127 = follow the track send */
    FzEnvelope amp;
    uint8_t amp_on;
    FzEnvelope filt;
    uint8_t filt_on;
    int16_t env_amount; /* filter-envelope depth, -127..127 */
    FzLoopProg loop;
    FzId wet_asset; /* generation 0: no baked wet stem */
    uint8_t choke;  /* 0 = none */
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
    FzAdditiveRecipe additive; /* 48 partials. Playback is the rendered cycle. */
} FzInstrument;

typedef struct {
    int alive;
    uint16_t generation;
    uint32_t length_ticks; /* steps; a 16th note is 24 ticks at 96 PPQ */
    uint32_t event_count;
    uint8_t baked;
    uint8_t unsend_mask; /* slots removed for this pattern only */
    FzId baked_asset;
} FzPattern;

/* Track LFO. Depth 0 is off and does not change the mix. */
typedef struct {
    uint8_t rate;      /* 0..127, about 0.1 Hz .. 20 Hz */
    uint8_t depth;     /* 0 = off */
    uint8_t shape;     /* FZ_LFO_TRI .. FZ_LFO_SH */
    uint8_t dest;      /* FZ_LFO_OFF .. FZ_LFO_PAN */
    uint8_t retrigger; /* 1 = each note starts at phase 0 */
} FzLfo;

typedef struct {
    int alive;
    uint16_t generation;
    FzId instrument;
    FzId pattern;
    int16_t cutoff;    /* 0..127, FZ INSPIRED lowpass via fz_performance_render */
    int16_t resonance; /* 0..127 */
    int16_t volume;    /* 0..127, shared by HOME and GLO */
    int16_t pan;       /* 0..127, 64 center, shared by HOME and GLO */
    int16_t send[4];   /* post-fader, 0 = no send */
    uint8_t mute;
    uint8_t solo;
    int16_t key_track; /* cutoff steps added per octave, 0 off */
    FzLfo lfo;
} FzTrack;

typedef struct {
    int valid;
    int group;
    int is_pattern;
    FzId id;
    FzRegion snap;
    int pattern_index;
    FzPattern pattern_snap;
} FzUndoItem;

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
    int undo_serial;
    int undo_count;
    int redo_count;
    FzUndoItem undo_item[24], redo_item[24];
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

/* PCM is copied into the project store. */
FzStatus fz_project_add_pcm(FzProject *p, const int16_t *pcm, uint32_t frames, uint32_t native_rate,
                            int cycle, FzId *out_asset);
/* Replace a cycle table of the same length. The asset id stays put. */
FzStatus fz_project_overwrite_pcm(FzProject *p, FzId asset, const int16_t *pcm, uint32_t frames);
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

/* Remember the region's current fields. `group` ties a batch into one undo. */
int fz_project_undo_begin(FzProject *p);
void fz_project_undo_mark(FzProject *p, FzId id, int group);
void fz_project_undo_mark_pattern(FzProject *p, int pattern_index, int group);
FzStatus fz_project_undo(FzProject *p);
FzStatus fz_project_redo(FzProject *p);
/* Next free "CATEGORY n". The instrument's existing name is not a collision. */
FzStatus fz_project_rename_category(FzProject *p, FzId inst, const char *category);
const char *fz_lfo_target_name(int dest);
const char *fz_lfo_shape_name(int shape);

#endif
