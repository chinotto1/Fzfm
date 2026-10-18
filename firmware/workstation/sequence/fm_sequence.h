/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FM_SEQUENCE_H
#define FM_SEQUENCE_H
#include "../core/fm.h"

#define FM_SEQUENCE_CHAIN_MAX 64u
#define FM_SEQUENCE_CANDIDATES 8u
typedef struct FmSequencer FmSequencer;
typedef void (*FmSequenceDispatch)(void *user, const FmEvent *event, uint32_t block_frame_offset);
typedef void (*FmSequenceFrozen)(void *user, FmId track, FmId pattern, FmFrame frame,
                                 uint32_t identity, uint32_t block_frame_offset);
/* Frozen callback pattern=0 releases that track's frozen playback. */
typedef struct
{
    double bpm, beats, confidence;
} FmTempoCandidate;
typedef struct
{
    FmFrame first_frame, end_frame, retained_frames;
    uint32_t retained_events, dropped_events, captured_events;
    bool armed, recording, overflow, ambiguous, sync_lost;
    uint8_t candidate_count;
    FmTempoCandidate candidates[FM_SEQUENCE_CANDIDATES];
} FmSequenceStatus;

/* Create/destroy and all edits/record publication are non-real-time operations.
 * live(), advance(), clock_pulse(), start(), stop() never allocate. A single
 * owner serializes edit/live/render, as required by the portable core contract. */
FmSequencer *fm_sequence_create(FmProject *, uint32_t history_events, uint32_t take_events);
void fm_sequence_destroy(FmSequencer *);
void fm_sequence_start(FmSequencer *);
void fm_sequence_continue(FmSequencer *);
/* Relocate only a newly prepared, stopped owner after atomic project load. */
void fm_sequence_rebind(FmSequencer *, FmProject *);
FmResult fm_sequence_arrange(FmSequencer *, bool begin);
bool fm_sequence_arranging(const FmSequencer *);
void fm_sequence_stop(FmSequencer *, FmSequenceDispatch, void *user);
bool fm_sequence_playing(const FmSequencer *);
double fm_sequence_tick(const FmSequencer *);
FmFrame fm_sequence_frame(const FmSequencer *);
void fm_sequence_advance(FmSequencer *, uint32_t frames, FmSequenceDispatch, void *user);
FmResult fm_sequence_live(FmSequencer *, const FmEvent *);
FmResult fm_sequence_record_arm(FmSequencer *, FmId track);
FmResult fm_sequence_record_close(FmSequencer *, FmFrame button_down);
FmResult fm_sequence_retrospective(FmSequencer *, FmId track, FmFrame button_down,
                                   FmFrame lookback_frames);
void fm_sequence_record_cancel(FmSequencer *);
FmSequenceStatus fm_sequence_status(const FmSequencer *);
/* Correction preserves raw frame timestamps; selected candidate changes only
 * musical placement/loop length. Existing/external master tempo is retained. */
FmResult fm_sequence_candidate(FmSequencer *, uint8_t candidate);
FmResult fm_sequence_queue_pattern(FmSequencer *, FmId track, uint8_t slot);
FmResult fm_sequence_scene(FmSequencer *, uint8_t slot);
FmResult fm_sequence_chain(FmSequencer *, FmId track, const uint8_t *slots, uint8_t count);
FmResult fm_sequence_chain_range(FmSequencer *, FmId track, uint8_t a, uint8_t b);
FmResult fm_sequence_step(FmSequencer *, FmId pattern, uint32_t step, uint8_t note,
                          uint8_t velocity, double length_ticks, bool enabled);
FmResult fm_sequence_motion(FmSequencer *, FmId track, FmParameter, float value, FmFrame frame);
FmResult fm_sequence_song_insert(FmSequencer *, uint16_t index, const FmSongEntry *entry);
FmResult fm_sequence_song_delete(FmSequencer *, uint16_t index);
FmResult fm_sequence_song_move(FmSequencer *, uint16_t from, uint16_t to);
FmResult fm_sequence_song_start(FmSequencer *, uint16_t index, bool loop);
FmResult fm_sequence_song_queue(FmSequencer *, uint16_t index);
/* Each MIDI clock pulse is 1/24 quarter note. Other sources are rejected;
 * after 0.5 seconds without a pulse playback freezes and reports sync loss. */
FmResult fm_sequence_clock_pulse(FmSequencer *, FmClockSource, FmFrame);
void fm_sequence_seed(FmSequencer *, uint32_t seed);
void fm_sequence_set_frozen_callback(FmSequencer *, FmSequenceFrozen, void *user);
#endif
