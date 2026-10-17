/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R09, R10, R12: sparse patterns, queued launch, chains, and a short song.
 * Raw ticks stay put. Swing and quantize are views unless quantize is applied
 * on purpose. 96 PPQ. A 16th note is 24 ticks. A bar is 4 quarters, not the
 * length of any one track. */
#ifndef FZ_SEQ_H
#define FZ_SEQ_H

#include <stdint.h>

#include "core/project.h"
#include "sequence/history.h"

#define FZ_PPQ 96
#define FZ_STEP_TICKS 24
#define FZ_BAR_TICKS (FZ_PPQ * 4)
#define FZ_SEQ_EVENTS 512
#define FZ_SEQ_UNDO 6
#define FZ_SONG_ROWS 16
#define FZ_CHAIN_MAX 16

typedef struct {
    uint8_t used;
    uint8_t note;
    uint8_t velocity;
    uint8_t prob;
    uint16_t pattern;
    uint16_t duration;
    uint32_t tick;
    int16_t micro;
} FzSeqEvent;

typedef struct {
    int valid;
    uint16_t pattern;
    int count;
    int overflow;
    FzSeqEvent ev[96];
} FzSeqSnap;

typedef struct {
    uint8_t pattern[FZ_MAX_TRACKS];
    uint8_t mute[FZ_MAX_TRACKS];
    uint16_t repeats;
} FzSongRow;

typedef struct FzSeq {
    FzSeqEvent ev[FZ_SEQ_EVENTS];
    int count;
    int playing;
    int overdub;
    int armed;
    int recording;
    uint32_t bpm_milli;
    uint64_t acc;
    uint32_t tick;
    uint32_t frame;
    uint32_t rec_start_frame;
    int current[FZ_MAX_TRACKS];
    int queued[FZ_MAX_TRACKS];
    int queue_on;
    uint32_t queue_tick;
    uint8_t chain[FZ_MAX_TRACKS][FZ_CHAIN_MAX];
    uint8_t chain_n[FZ_MAX_TRACKS];
    uint8_t chain_i[FZ_MAX_TRACKS];
    FzSongRow song[FZ_SONG_ROWS];
    int song_n;
    int song_pos;
    int song_left;
    int song_on;
    int swing;
    int quantize_grid;
    int gate_n;
    int undo_n;
    int redo_n;
    int undo_lost;
    FzSeqSnap undo[FZ_SEQ_UNDO];
    FzSeqSnap redo[FZ_SEQ_UNDO];
    uint8_t take_note[64];
    uint8_t take_vel[64];
    uint32_t take_frame[64];
    uint16_t take_dur[64];
    int take_n;
    int take_track;
    int take_pattern;
} FzSeq;

void fz_seq_init(FzSeq *seq);
void fz_seq_set_tempo(FzSeq *seq, uint32_t bpm_milli);
void fz_seq_advance(FzSeq *seq, const FzProject *project, uint32_t frames);
void fz_seq_play(FzSeq *seq, int playing);

uint32_t fz_seq_bar_ticks(void);
uint32_t fz_seq_pattern_ticks(const FzProject *project, int pattern);
uint32_t fz_seq_playback_tick(uint32_t raw_tick, int swing);
uint32_t fz_seq_quantize_tick(uint32_t raw_tick, uint32_t grid);

FzStatus fz_seq_toggle_step(FzSeq *seq, int pattern, int step, int note, int velocity);
FzStatus fz_seq_select_pattern(FzSeq *seq, int track, int pattern, int all_tracks);
FzStatus fz_seq_chain_range(FzSeq *seq, int track, int first, int last);
FzStatus fz_seq_chain_set(FzSeq *seq, int track, const int *ids, int count);
FzStatus fz_seq_copy(FzSeq *seq, int src, int dst, int overwrite);

FzStatus fz_seq_song_set(FzSeq *seq, int row, const uint8_t pattern[FZ_MAX_TRACKS], const uint8_t mute[FZ_MAX_TRACKS],
                         int repeats);
void fz_seq_song_start(FzSeq *seq);

/* Stopped: tap arms, the next tap after notes closes. Playing: tap toggles overdub. */
FzStatus fz_seq_rec_tap(FzSeq *seq, FzProject *project, int pattern, uint32_t frame);
FzStatus fz_seq_perform(FzSeq *seq, int note, int velocity, uint32_t frame);
FzStatus fz_seq_capture_history(FzSeq *seq, FzProject *project, const FzHistory *history, int pattern, uint32_t end_us,
                                uint32_t window_us);
FzStatus fz_seq_undo(FzSeq *seq);
FzStatus fz_seq_redo(FzSeq *seq);
int fz_seq_count_pattern(const FzSeq *seq, int pattern);
int fz_seq_step_fires(const FzSeqEvent *ev, uint32_t seed);

#endif
