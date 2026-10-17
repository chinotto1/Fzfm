/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "seq.h"

#include "sequence/tempo.h"

#include <string.h>

static const uint64_t k_den = 36000ull * 60ull * 1000ull;

void fz_seq_init(FzSeq *seq)
{
    int i;
    if (!seq)
        return;
    memset(seq, 0, sizeof *seq);
    seq->bpm_milli = 120000;
    for (i = 0; i < FZ_MAX_TRACKS; i++)
        seq->current[i] = -1;
}

void fz_seq_set_tempo(FzSeq *seq, uint32_t bpm_milli)
{
    if (!seq)
        return;
    if (bpm_milli < 10000)
        bpm_milli = 10000;
    if (bpm_milli > 400000)
        bpm_milli = 400000;
    seq->bpm_milli = bpm_milli;
}

uint32_t fz_seq_bar_ticks(void)
{
    return FZ_BAR_TICKS;
}

uint32_t fz_seq_pattern_ticks(const FzProject *project, int pattern)
{
    uint32_t steps = 16;
    if (project && pattern >= 0 && pattern < FZ_MAX_PATTERNS && project->pattern[pattern].alive &&
        project->pattern[pattern].length_ticks > 0)
        steps = project->pattern[pattern].length_ticks;
    return steps * (uint32_t)FZ_STEP_TICKS;
}

uint32_t fz_seq_playback_tick(uint32_t raw_tick, int swing)
{
    uint32_t step, nudge;
    if (swing <= 0)
        return raw_tick;
    if (swing > 50)
        swing = 50;
    step = raw_tick / (uint32_t)FZ_STEP_TICKS;
    if ((step & 1u) == 0)
        return raw_tick;
    nudge = ((uint32_t)swing * (uint32_t)FZ_STEP_TICKS) / 100u;
    return raw_tick + nudge;
}

uint32_t fz_seq_quantize_tick(uint32_t raw_tick, uint32_t grid)
{
    if (grid == 0)
        return raw_tick;
    return ((raw_tick + grid / 2u) / grid) * grid;
}

int fz_seq_step_fires(const FzSeqEvent *ev, uint32_t seed)
{
    if (!ev || !ev->used)
        return 0;
    if (ev->prob == 0)
        return 0;
    if (ev->prob >= 100)
        return 1;
    return (seed % 100u) < ev->prob;
}

static int find_event(const FzSeq *seq, int pattern, uint32_t tick, int note)
{
    int i;
    for (i = 0; i < FZ_SEQ_EVENTS; i++) {
        const FzSeqEvent *e = &seq->ev[i];
        if (e->used && e->pattern == (uint16_t)pattern && e->tick == tick && e->note == (uint8_t)note)
            return i;
    }
    return -1;
}

static int alloc_event(FzSeq *seq)
{
    int i;
    for (i = 0; i < FZ_SEQ_EVENTS; i++) {
        if (!seq->ev[i].used)
            return i;
    }
    return -1;
}

int fz_seq_count_pattern(const FzSeq *seq, int pattern)
{
    int i, n = 0;
    if (!seq)
        return 0;
    for (i = 0; i < FZ_SEQ_EVENTS; i++) {
        if (seq->ev[i].used && seq->ev[i].pattern == (uint16_t)pattern)
            n++;
    }
    return n;
}

static void forget_pattern(FzSeq *seq, int pattern)
{
    int i;
    for (i = 0; i < FZ_SEQ_EVENTS; i++) {
        if (seq->ev[i].used && seq->ev[i].pattern == (uint16_t)pattern)
            seq->ev[i].used = 0;
    }
}

static FzStatus snapshot(FzSeq *seq, int pattern)
{
    FzSeqSnap *snap;
    int i;
    if (fz_seq_count_pattern(seq, pattern) > 96) {
        seq->undo_lost = 1;
        return FZ_ERR_FULL;
    }
    if (seq->undo_n == FZ_SEQ_UNDO) {
        memmove(&seq->undo[0], &seq->undo[1], sizeof seq->undo[0] * (FZ_SEQ_UNDO - 1));
        seq->undo_n--;
    }
    snap = &seq->undo[seq->undo_n++];
    memset(snap, 0, sizeof *snap);
    snap->valid = 1;
    snap->pattern = (uint16_t)pattern;
    for (i = 0; i < FZ_SEQ_EVENTS; i++) {
        if (seq->ev[i].used && seq->ev[i].pattern == (uint16_t)pattern)
            snap->ev[snap->count++] = seq->ev[i];
    }
    seq->redo_n = 0;
    return FZ_OK;
}

static void restore_snap(FzSeq *seq, const FzSeqSnap *snap)
{
    int i;
    forget_pattern(seq, snap->pattern);
    for (i = 0; i < snap->count; i++) {
        int slot = alloc_event(seq);
        if (slot < 0)
            break;
        seq->ev[slot] = snap->ev[i];
        seq->ev[slot].used = 1;
    }
}

FzStatus fz_seq_undo(FzSeq *seq)
{
    FzSeqSnap back;
    if (!seq || seq->undo_n <= 0)
        return FZ_ERR_STATE;
    back = seq->undo[seq->undo_n - 1];
    if (seq->redo_n < FZ_SEQ_UNDO) {
        FzSeqSnap *redo = &seq->redo[seq->redo_n++];
        int i;
        memset(redo, 0, sizeof *redo);
        redo->valid = 1;
        redo->pattern = back.pattern;
        for (i = 0; i < FZ_SEQ_EVENTS; i++) {
            if (seq->ev[i].used && seq->ev[i].pattern == back.pattern && redo->count < 96)
                redo->ev[redo->count++] = seq->ev[i];
        }
    }
    seq->undo_n--;
    restore_snap(seq, &back);
    return FZ_OK;
}

FzStatus fz_seq_redo(FzSeq *seq)
{
    FzSeqSnap back;
    if (!seq || seq->redo_n <= 0)
        return FZ_ERR_STATE;
    back = seq->redo[seq->redo_n - 1];
    seq->redo_n--;
    if (snapshot(seq, back.pattern) != FZ_OK)
        return FZ_ERR_FULL;
    restore_snap(seq, &back);
    return FZ_OK;
}

FzStatus fz_seq_toggle_step(FzSeq *seq, int pattern, int step, int note, int velocity)
{
    uint32_t tick;
    int slot;
    FzStatus st;
    if (!seq || pattern < 0 || pattern >= FZ_MAX_PATTERNS || step < 0 || note < 0 || note > 127)
        return FZ_ERR_ARGS;
    tick = (uint32_t)step * (uint32_t)FZ_STEP_TICKS;
    st = snapshot(seq, pattern);
    if (st != FZ_OK)
        return st;
    slot = find_event(seq, pattern, tick, note);
    if (slot >= 0) {
        seq->ev[slot].used = 0;
        return FZ_OK;
    }
    slot = alloc_event(seq);
    if (slot < 0)
        return FZ_ERR_FULL;
    memset(&seq->ev[slot], 0, sizeof seq->ev[slot]);
    seq->ev[slot].used = 1;
    seq->ev[slot].pattern = (uint16_t)pattern;
    seq->ev[slot].tick = tick;
    seq->ev[slot].note = (uint8_t)note;
    seq->ev[slot].velocity = velocity <= 0 ? 100 : (uint8_t)(velocity > 127 ? 127 : velocity);
    seq->ev[slot].duration = FZ_STEP_TICKS;
    seq->ev[slot].prob = 100;
    return FZ_OK;
}

static void launch_chain(FzSeq *seq, int track)
{
    if (seq->chain_n[track] == 0)
        return;
    seq->chain_i[track] = 0;
    seq->current[track] = seq->chain[track][0];
}

FzStatus fz_seq_select_pattern(FzSeq *seq, int track, int pattern, int all_tracks)
{
    int i, first, last;
    if (!seq || pattern < 0 || pattern >= FZ_MAX_PATTERNS)
        return FZ_ERR_ARGS;
    first = all_tracks ? 0 : track;
    last = all_tracks ? FZ_MAX_TRACKS - 1 : track;
    if (track < 0 || track >= FZ_MAX_TRACKS)
        return FZ_ERR_ARGS;
    for (i = first; i <= last; i++) {
        seq->chain_n[i] = 0;
        if (!seq->playing) {
            seq->current[i] = pattern;
            seq->queued[i] = -1;
        } else {
            seq->queued[i] = pattern;
            seq->queue_on = 1;
            seq->queue_tick = (seq->tick / FZ_BAR_TICKS + 1u) * FZ_BAR_TICKS;
        }
    }
    return FZ_OK;
}

FzStatus fz_seq_chain_range(FzSeq *seq, int track, int first, int last)
{
    int ids[FZ_CHAIN_MAX];
    int n = 0, i, lo, hi;
    if (!seq || track < 0 || track >= FZ_MAX_TRACKS)
        return FZ_ERR_ARGS;
    lo = first < last ? first : last;
    hi = first < last ? last : first;
    if (lo < 0 || hi >= FZ_MAX_PATTERNS || hi - lo >= FZ_CHAIN_MAX)
        return FZ_ERR_RANGE;
    for (i = lo; i <= hi; i++)
        ids[n++] = i;
    return fz_seq_chain_set(seq, track, ids, n);
}

FzStatus fz_seq_chain_set(FzSeq *seq, int track, const int *ids, int count)
{
    int i;
    if (!seq || !ids || track < 0 || track >= FZ_MAX_TRACKS || count < 1 || count > FZ_CHAIN_MAX)
        return FZ_ERR_ARGS;
    for (i = 0; i < count; i++) {
        if (ids[i] < 0 || ids[i] >= FZ_MAX_PATTERNS)
            return FZ_ERR_RANGE;
        seq->chain[track][i] = (uint8_t)ids[i];
    }
    seq->chain_n[track] = (uint8_t)count;
    seq->chain_i[track] = 0;
    if (!seq->playing)
        seq->current[track] = ids[0];
    else {
        seq->queue_on = 1;
        seq->queue_tick = (seq->tick / FZ_BAR_TICKS + 1u) * FZ_BAR_TICKS;
        seq->queued[track] = ids[0];
    }
    return FZ_OK;
}

FzStatus fz_seq_copy(FzSeq *seq, int src, int dst, int overwrite)
{
    int i, n;
    FzStatus st;
    if (!seq || src < 0 || dst < 0 || src >= FZ_MAX_PATTERNS || dst >= FZ_MAX_PATTERNS)
        return FZ_ERR_ARGS;
    n = fz_seq_count_pattern(seq, dst);
    if (n > 0 && !overwrite)
        return FZ_ERR_STATE;
    st = snapshot(seq, dst);
    if (st != FZ_OK)
        return st;
    if (n > 0)
        forget_pattern(seq, dst);
    for (i = 0; i < FZ_SEQ_EVENTS; i++) {
        int slot;
        if (!seq->ev[i].used || seq->ev[i].pattern != (uint16_t)src)
            continue;
        slot = alloc_event(seq);
        if (slot < 0)
            return FZ_ERR_FULL;
        seq->ev[slot] = seq->ev[i];
        seq->ev[slot].pattern = (uint16_t)dst;
    }
    return FZ_OK;
}

static void apply_song_row(FzSeq *seq)
{
    int i;
    const FzSongRow *row;
    if (seq->song_pos < 0 || seq->song_pos >= seq->song_n)
        return;
    row = &seq->song[seq->song_pos];
    for (i = 0; i < FZ_MAX_TRACKS; i++)
        seq->current[i] = row->pattern[i];
    seq->song_left = row->repeats > 0 ? row->repeats : 1;
}

void fz_seq_song_start(FzSeq *seq)
{
    if (!seq || seq->song_n <= 0)
        return;
    seq->song_on = 1;
    seq->song_pos = 0;
    apply_song_row(seq);
}

FzStatus fz_seq_song_set(FzSeq *seq, int row, const uint8_t pattern[FZ_MAX_TRACKS], const uint8_t mute[FZ_MAX_TRACKS],
                         int repeats)
{
    int i;
    if (!seq || !pattern || row < 0 || row >= FZ_SONG_ROWS)
        return FZ_ERR_ARGS;
    if (repeats < 1)
        repeats = 1;
    if (row >= seq->song_n)
        seq->song_n = row + 1;
    for (i = 0; i < FZ_MAX_TRACKS; i++) {
        seq->song[row].pattern[i] = pattern[i];
        seq->song[row].mute[i] = mute ? mute[i] : 0;
    }
    seq->song[row].repeats = (uint16_t)repeats;
    return FZ_OK;
}

static void cross_bars(FzSeq *seq, uint32_t bars)
{
    uint32_t b;
    int t;
    for (b = 0; b < bars; b++) {
        int launched[FZ_MAX_TRACKS];
        memset(launched, 0, sizeof launched);
        if (seq->queue_on && seq->tick >= seq->queue_tick) {
            for (t = 0; t < FZ_MAX_TRACKS; t++) {
                if (seq->queued[t] >= 0) {
                    seq->current[t] = seq->queued[t];
                    seq->queued[t] = -1;
                    if (seq->chain_n[t] > 0)
                        launch_chain(seq, t);
                    launched[t] = 1;
                }
            }
            seq->queue_on = 0;
        }
        if (seq->song_on && seq->song_n > 0) {
            if (seq->song_left > 1)
                seq->song_left--;
            else {
                seq->song_pos++;
                if (seq->song_pos >= seq->song_n)
                    seq->song_pos = 0;
                apply_song_row(seq);
            }
        }
        for (t = 0; t < FZ_MAX_TRACKS; t++) {
            if (launched[t] || seq->song_on || seq->chain_n[t] < 2)
                continue;
            seq->chain_i[t] = (uint8_t)((seq->chain_i[t] + 1) % seq->chain_n[t]);
            seq->current[t] = seq->chain[t][seq->chain_i[t]];
        }
    }
    seq->gate_n = 0;
}

void fz_seq_advance(FzSeq *seq, const FzProject *project, uint32_t frames)
{
    uint32_t before, bars_before, bars_after;
    uint64_t sum;
    (void)project;
    if (!seq || frames == 0 || !seq->playing)
        return;
    before = seq->tick;
    sum = seq->acc + (uint64_t)frames * (uint64_t)seq->bpm_milli * (uint64_t)FZ_PPQ;
    seq->tick += (uint32_t)(sum / k_den);
    seq->acc = sum % k_den;
    seq->frame += frames;
    bars_before = before / FZ_BAR_TICKS;
    bars_after = seq->tick / FZ_BAR_TICKS;
    if (bars_after > bars_before)
        cross_bars(seq, bars_after - bars_before);
    else if (seq->queue_on && before < seq->queue_tick && seq->tick >= seq->queue_tick)
        cross_bars(seq, 1);
}

void fz_seq_play(FzSeq *seq, int playing)
{
    if (!seq)
        return;
    seq->playing = playing ? 1 : 0;
    if (!seq->playing) {
        seq->overdub = 0;
        seq->gate_n = 0;
    }
}

static uint32_t frames_to_ticks(uint32_t frames, uint32_t bpm_milli)
{
    return (uint32_t)(((uint64_t)frames * bpm_milli * (uint64_t)FZ_PPQ) / k_den);
}

static void publish_take(FzSeq *seq, int pattern)
{
    int i;
    forget_pattern(seq, pattern);
    for (i = 0; i < seq->take_n; i++) {
        int slot = alloc_event(seq);
        uint32_t tick;
        if (slot < 0)
            break;
        tick = frames_to_ticks(seq->take_frame[i], seq->bpm_milli);
        memset(&seq->ev[slot], 0, sizeof seq->ev[slot]);
        seq->ev[slot].used = 1;
        seq->ev[slot].pattern = (uint16_t)pattern;
        seq->ev[slot].note = seq->take_note[i];
        seq->ev[slot].velocity = seq->take_vel[i];
        seq->ev[slot].tick = tick;
        seq->ev[slot].duration = seq->take_dur[i] ? seq->take_dur[i] : FZ_STEP_TICKS;
        seq->ev[slot].prob = 100;
    }
}

FzStatus fz_seq_perform(FzSeq *seq, int note, int velocity, uint32_t frame)
{
    int i;
    if (!seq || note < 0 || note > 127)
        return FZ_ERR_ARGS;
    if (seq->overdub) {
        int slot;
        if (velocity <= 0)
            return FZ_OK;
        slot = alloc_event(seq);
        if (slot < 0)
            return FZ_ERR_FULL;
        memset(&seq->ev[slot], 0, sizeof seq->ev[slot]);
        seq->ev[slot].used = 1;
        seq->ev[slot].pattern = (uint16_t)seq->take_pattern;
        seq->ev[slot].note = (uint8_t)note;
        seq->ev[slot].velocity = (uint8_t)(velocity > 127 ? 127 : velocity);
        seq->ev[slot].tick = seq->tick;
        seq->ev[slot].duration = FZ_STEP_TICKS;
        seq->ev[slot].prob = 100;
        return FZ_OK;
    }
    if (!seq->armed && !seq->recording)
        return FZ_OK;
    if (!seq->recording) {
        if (velocity <= 0)
            return FZ_OK;
        seq->recording = 1;
        seq->rec_start_frame = frame;
        seq->take_n = 0;
    }
    if (velocity > 0) {
        if (seq->take_n >= 64)
            return FZ_ERR_FULL;
        i = seq->take_n++;
        seq->take_note[i] = (uint8_t)note;
        seq->take_vel[i] = (uint8_t)(velocity > 127 ? 127 : velocity);
        seq->take_frame[i] = frame - seq->rec_start_frame;
        seq->take_dur[i] = FZ_STEP_TICKS;
    } else {
        for (i = seq->take_n - 1; i >= 0; i--) {
            if (seq->take_note[i] == (uint8_t)note) {
                uint32_t dur = frames_to_ticks(frame - seq->rec_start_frame - seq->take_frame[i], seq->bpm_milli);
                seq->take_dur[i] = dur ? (uint16_t)dur : 1;
                break;
            }
        }
    }
    return FZ_OK;
}

FzStatus fz_seq_rec_tap(FzSeq *seq, FzProject *project, int pattern, uint32_t frame)
{
    FzTempoCandidate cand[6];
    uint32_t onsets[64];
    int i, n;
    if (!seq || !project || pattern < 0 || pattern >= FZ_MAX_PATTERNS)
        return FZ_ERR_ARGS;
    if (seq->playing) {
        if (!seq->overdub) {
            if (snapshot(seq, pattern) != FZ_OK)
                return FZ_ERR_FULL;
            seq->overdub = 1;
            seq->take_pattern = pattern;
        } else {
            seq->overdub = 0;
        }
        return FZ_OK;
    }
    if (!seq->armed) {
        seq->armed = 1;
        seq->recording = 0;
        seq->take_n = 0;
        seq->take_pattern = pattern;
        return FZ_OK;
    }
    if (!seq->recording) {
        seq->armed = 0;
        return FZ_OK;
    }
    if (snapshot(seq, pattern) != FZ_OK)
        return FZ_ERR_FULL;
    n = seq->take_n;
    if (n > 64)
        n = 64;
    for (i = 0; i < n; i++)
        onsets[i] = seq->take_frame[i];
    if (fz_tempo_propose(frame > seq->rec_start_frame ? frame - seq->rec_start_frame : 1, FZ_ENGINE_RATE_HZ, onsets, n,
                         cand, 6) > 0) {
        if (fz_tempo_apply_policy(project->has_music, project->external_clock, &project->tempo_bpm, cand[0].bpm))
            fz_seq_set_tempo(seq, (uint32_t)(project->tempo_bpm * 1000.0 + 0.5));
    }
    publish_take(seq, pattern);
    seq->armed = 0;
    seq->recording = 0;
    seq->take_n = 0;
    return FZ_OK;
}

FzStatus fz_seq_capture_history(FzSeq *seq, FzProject *project, const FzHistory *history, int pattern, uint32_t end_us,
                                uint32_t window_us)
{
    FzHistEvent got[128];
    int n, i, open_n = 0;
    uint32_t origin = 0;
    int have_origin = 0;
    struct {
        uint8_t note;
        uint32_t frame;
        int slot;
    } open[32];
    if (!seq || !project || !history || pattern < 0)
        return FZ_ERR_ARGS;
    n = fz_history_slice(history, end_us, window_us, got, 128);
    if (snapshot(seq, pattern) != FZ_OK)
        return FZ_ERR_FULL;
    forget_pattern(seq, pattern);
    for (i = 0; i < n; i++) {
        uint32_t frame = (uint32_t)(((uint64_t)got[i].time_us * (uint64_t)FZ_ENGINE_RATE_HZ) / 1000000ull);
        if (!have_origin) {
            origin = frame;
            have_origin = 1;
        }
        if (got[i].velocity > 0) {
            int slot = alloc_event(seq);
            uint32_t tick;
            if (slot < 0)
                return FZ_ERR_FULL;
            tick = frames_to_ticks(frame >= origin ? frame - origin : 0, seq->bpm_milli);
            memset(&seq->ev[slot], 0, sizeof seq->ev[slot]);
            seq->ev[slot].used = 1;
            seq->ev[slot].pattern = (uint16_t)pattern;
            seq->ev[slot].note = got[i].note;
            seq->ev[slot].velocity = got[i].velocity;
            seq->ev[slot].tick = tick;
            seq->ev[slot].duration = FZ_STEP_TICKS;
            seq->ev[slot].prob = 100;
            if (open_n < 32) {
                open[open_n].note = got[i].note;
                open[open_n].frame = frame;
                open[open_n].slot = slot;
                open_n++;
            }
        } else if (open_n > 0) {
            int k;
            for (k = open_n - 1; k >= 0; k--) {
                if (open[k].note == got[i].note) {
                    uint32_t dur = frames_to_ticks(frame > open[k].frame ? frame - open[k].frame : 0, seq->bpm_milli);
                    seq->ev[open[k].slot].duration = dur ? (uint16_t)(dur > 65535u ? 65535u : dur) : 1;
                    open[k] = open[open_n - 1];
                    open_n--;
                    break;
                }
            }
        }
    }
    (void)project;
    return FZ_OK;
}
