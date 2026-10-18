/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../firmware/workstation/sequence/fm_sequence.h"
#include "test.h"
#include <string.h>

typedef struct
{
    FmFrame base, frames[2048];
    uint8_t notes[2048];
    unsigned on, off, active, peak, controls;
} Log;
typedef struct
{
    unsigned on, off;
    FmFrame frames[8];
} FrozenLog;
static void frozen_event(void *user, FmId track, FmId pattern, FmFrame frame, uint32_t identity,
                         uint32_t offset)
{
    FrozenLog *log = user;
    (void)track;
    (void)identity;
    (void)offset;
    if (pattern)
    {
        if (log->on < 8)
            log->frames[log->on] = frame;
        ++log->on;
    }
    else
        ++log->off;
}
static void log_event(void *user, const FmEvent *e, uint32_t offset)
{
    Log *log = user;
    if (e->kind == FM_EVENT_NOTE_ON && e->velocity)
    {
        if (log->on < 2048)
        {
            log->frames[log->on] = log->base + offset;
            log->notes[log->on] = e->note;
        }
        ++log->on;
        ++log->active;
        if (log->active > log->peak)
            log->peak = log->active;
    }
    else if (e->kind == FM_EVENT_NOTE_OFF)
    {
        ++log->off;
        if (log->active)
            --log->active;
    }
    else if (e->kind == FM_EVENT_CONTROL)
        ++log->controls;
}
static void advance(FmSequencer *s, Log *log, uint32_t frames)
{
    while (frames)
    {
        uint32_t n = frames > 127 ? 127 : frames;
        log->base = fm_sequence_frame(s);
        fm_sequence_advance(s, n, log_event, log);
        frames -= n;
    }
}
static FmEvent note(FmId track, FmFrame frame, uint32_t identity, uint8_t key, bool on)
{
    FmEvent e = {0};
    e.track = track;
    e.frame = frame;
    e.identity = identity;
    e.kind = on ? FM_EVENT_NOTE_ON : FM_EVENT_NOTE_OFF;
    e.note = key;
    e.velocity = on ? 100 : 0;
    e.origin = FM_ORIGIN_LOCAL;
    e.probability = 1;
    return e;
}
static int free_take(void)
{
    FmProject p;
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    FmId tid;
    CHECK(fm_instrument_create(&p, FM_SAMPLE, &tid) == FM_OK);
    FmTrack *t = fm_track(&p, tid);
    FmPattern *pat = fm_pattern(&p, t->patterns[0]);
    FmSequencer *s = fm_sequence_create(&p, 128, 128);
    CHECK(s);
    CHECK(fm_sequence_record_arm(s, tid) == FM_OK);
    for (unsigned i = 0; i < 4; i++)
    {
        FmEvent on = note(tid, 1000 + i * 18000, i + 1, 60, true);
        FmEvent off = note(tid, 1000 + i * 18000 + 9000, i + 1, 60, false);
        CHECK(fm_sequence_live(s, &on) == FM_OK);
        CHECK(fm_sequence_live(s, &off) == FM_OK);
    }
    /* Events that arrive during hold recognition must not enter the closed take. */
    FmEvent late = note(tid, 74000, 99, 65, true);
    CHECK(fm_sequence_live(s, &late) == FM_OK);
    CHECK(fm_sequence_record_close(s, 73000) == FM_OK);
    NEAR(p.bpm, 120, 0.001);
    NEAR(pat->length_ticks, 3840, 0.001);
    CHECK(pat->count == 8 && pat->events[0].frame == 1000);
    CHECK(fm_sequence_status(s).end_frame == 73000);
    CHECK(fm_sequence_playing(s));
    CHECK(fm_undo(&p) == FM_OK);
    CHECK(pat->count == 0 && p.instrument_count == 1 && p.track_count == 1);
    CHECK(fm_redo(&p) == FM_OK && pat->count == 8);
    /* Correction can double/halve the initial inferred master without losing raw time. */
    FmSequenceStatus status = fm_sequence_status(s);
    CHECK(status.candidate_count >= 2);
    uint8_t alternate = 1;
    double bpm = status.candidates[alternate].bpm;
    CHECK(fm_sequence_candidate(s, alternate) == FM_OK);
    NEAR(p.bpm, bpm, 1e-6);
    CHECK(pat->events[0].frame == 1000);
    fm_sequence_stop(s, NULL, NULL);
    /* Existing music keeps its chosen tempo, including for a short first-loop take. */
    p.bpm = 111.25;
    p.tempo_established = true;
    CHECK(fm_sequence_record_arm(s, tid) == FM_OK);
    FmEvent e = note(tid, 80000, 50, 60, true);
    CHECK(fm_sequence_live(s, &e) == FM_OK);
    CHECK(fm_sequence_record_close(s, 116000) == FM_OK);
    NEAR(p.bpm, 111.25, 1e-10);
    CHECK(fm_sequence_status(s).ambiguous);
    fm_sequence_destroy(s);
    fm_project_destroy(&p);
    return 0;
}
static int history_capture(void)
{
    FmProject p;
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    FmId a, b;
    CHECK(fm_instrument_create(&p, FM_SAMPLE, &a) == FM_OK);
    CHECK(fm_instrument_create(&p, FM_SAMPLE, &b) == FM_OK);
    FmSequencer *s = fm_sequence_create(&p, 4, 64);
    CHECK(s);
    FmEvent held = note(a, 0, 1, 60, true);
    CHECK(fm_sequence_live(s, &held) == FM_OK);
    for (unsigned i = 1; i <= 6; i++)
    {
        FmEvent c = {0};
        c.track = b;
        c.frame = i * 400;
        c.origin = FM_ORIGIN_USB;
        c.kind = FM_EVENT_CONTROL;
        c.parameter = FM_PARAM_CUTOFF;
        c.value = 0.5f;
        CHECK(fm_sequence_live(s, &c) == FM_OK);
    }
    CHECK(fm_sequence_status(s).dropped_events == 3);
    FmEvent phantom = note(a, 2600, 2, 99, true);
    phantom.origin = FM_ORIGIN_SEQUENCER;
    CHECK(fm_sequence_live(s, &phantom) == FM_OK);
    CHECK(fm_sequence_status(s).retained_events == 4);
    CHECK(fm_sequence_retrospective(s, a, 2800, 1600) == FM_OK);
    FmPattern *pat = fm_pattern(&p, fm_track(&p, a)->patterns[0]);
    CHECK(pat->count == 2 && pat->events[0].note == 60);
    CHECK(pat->events[0].frame == 1200 && pat->events[1].frame == 2800);
    CHECK(fm_sequence_status(s).retained_events == 4); /* retry remains possible */
    Log log = {0};
    advance(s, &log, 1000);
    fm_sequence_stop(s, log_event, &log);
    CHECK(log.on > 0 && log.active == 0 && log.peak == 1);
    CHECK(fm_undo(&p) == FM_OK && pat->count == 0 && p.instrument_count == 2);
    fm_sequence_destroy(s);
    fm_project_destroy(&p);
    return 0;
}
static int timing_patterns_song(void)
{
    FmProject p;
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    FmId tid;
    CHECK(fm_instrument_create(&p, FM_SAMPLE, &tid) == FM_OK);
    FmTrack *t = fm_track(&p, tid);
    FmSequencer *s = fm_sequence_create(&p, 32, 64);
    CHECK(s);
    for (uint8_t slot = 0; slot < 3; slot++)
    {
        FmId id;
        CHECK(fm_pattern_create(&p, tid, slot, &id) == FM_OK);
        CHECK(fm_sequence_step(s, id, 0, (uint8_t)(60 + slot), 100, 120, true) == FM_OK);
    }
    FmPattern *p0 = fm_pattern(&p, t->patterns[0]);
    CHECK(fm_sequence_step(s, p0->id, 1, 67, 90, 60, true) == FM_OK);
    CHECK(fm_sequence_step(s, p0->id, 1, 67, 90, 60, false) == FM_OK);
    CHECK(p0->count == 2);
    p.bpm = 123.45;
    fm_sequence_start(s);
    Log log = {0};
    FmBudget before = p.budget;
    advance(s, &log, 2000000);
    for (unsigned i = 0; i < FM_MEM_COUNT; i++)
        CHECK(before.used[i] == p.budget.used[i]);
    CHECK(log.on > 20 && log.on < 100);
    for (unsigned i = 0; i < log.on; i++)
    {
        double expected = ceil((double)i * 4 * 60 * FM_RATE / p.bpm - 1e-7);
        NEAR(log.frames[i], expected, 1);
    }
    NEAR(fm_sequence_tick(s), 2000000.0 * p.bpm * FM_PPQN / (60 * FM_RATE), 1e-6);
    fm_sequence_stop(s, log_event, &log);
    CHECK(log.active == 0);
    p.bpm = 120;
    fm_sequence_start(s);
    memset(&log, 0, sizeof(log));
    CHECK(fm_sequence_queue_pattern(s, tid, 1) == FM_OK);
    advance(s, &log, 72001);
    CHECK(log.on == 2 && log.notes[0] == 60 && log.notes[1] == 61);
    CHECK(t->playing_pattern == 1);
    fm_sequence_stop(s, NULL, NULL);
    for (unsigned slot = 0; slot < 3; slot++)
        fm_pattern(&p, t->patterns[slot])->length_ticks = 960;
    uint8_t chain[] = {0, 0, 2, 1};
    CHECK(fm_sequence_chain(s, tid, chain, 4) == FM_OK);
    fm_sequence_start(s);
    memset(&log, 0, sizeof(log));
    advance(s, &log, 72001);
    CHECK(log.on == 5 && log.notes[0] == 60 && log.notes[1] == 60 && log.notes[2] == 62 &&
          log.notes[3] == 61 && log.notes[4] == 60);
    fm_sequence_stop(s, NULL, NULL);
    CHECK(fm_sequence_queue_pattern(s, tid, 0) == FM_OK);
    FmSongEntry entry = {0};
    entry.count = 1;
    entry.tracks[0] = tid;
    entry.patterns[0] = 0;
    entry.bars = 0.25;
    entry.repeats = 2;
    CHECK(fm_sequence_song_insert(s, 0, &entry) == FM_OK);
    entry.patterns[0] = 1;
    entry.repeats = 1;
    CHECK(fm_sequence_song_insert(s, 1, &entry) == FM_OK);
    CHECK(fm_sequence_song_move(s, 1, 0) == FM_OK);
    CHECK(p.song[0].patterns[0] == 1);
    CHECK(fm_undo(&p) == FM_OK);
    CHECK(p.song[0].patterns[0] == 0);
    CHECK(fm_sequence_song_start(s, 0, false) == FM_OK);
    memset(&log, 0, sizeof(log));
    advance(s, &log, 54002);
    CHECK(!fm_sequence_playing(s));
    CHECK(log.on == 3 && log.notes[0] == 60 && log.notes[1] == 60 && log.notes[2] == 61);
    CHECK(log.active == 0);
    CHECK(fm_sequence_song_delete(s, 1) == FM_OK && p.song_count == 1);
    fm_sequence_destroy(s);
    fm_project_destroy(&p);
    return 0;
}
static int external_and_budget(void)
{
    FmProject p;
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    FmId tid;
    CHECK(fm_instrument_create(&p, FM_SAMPLE, &tid) == FM_OK);
    FmSequencer *s = fm_sequence_create(&p, 16, 16);
    CHECK(s);
    p.clock_source = FM_CLOCK_USB;
    p.bpm = 130;
    CHECK(fm_sequence_record_arm(s, tid) == FM_OK);
    FmEvent e = note(tid, 100, 1, 60, true);
    CHECK(fm_sequence_live(s, &e) == FM_OK);
    CHECK(fm_sequence_record_close(s, 36100) == FM_OK);
    NEAR(p.bpm, 130, 0.0001);
    CHECK(fm_sequence_clock_pulse(s, FM_CLOCK_TRS, 1) == FM_ERR_ARGUMENT);
    CHECK(fm_sequence_clock_pulse(s, FM_CLOCK_USB, 1) == FM_OK);
    CHECK(fm_sequence_clock_pulse(s, FM_CLOCK_USB, 751) == FM_OK);
    Log log = {0};
    advance(s, &log, 30000);
    CHECK(fm_sequence_status(s).sync_lost);
    fm_sequence_stop(s, NULL, NULL);
    FmPattern *pat = fm_pattern(&p, fm_track(&p, tid)->patterns[0]);
    uint32_t old = pat->count;
    p.budget.limit[FM_MEM_EVENTS] = p.budget.used[FM_MEM_EVENTS];
    CHECK(fm_sequence_step(s, pat->id, 1, 65, 100, 120, true) == FM_ERR_MEMORY);
    CHECK(pat->count == old);
    fm_sequence_destroy(s);
    fm_project_destroy(&p);
    return 0;
}
static int sustain_motion_frozen(void)
{
    FmProject p;
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    FmId tid;
    CHECK(fm_instrument_create(&p, FM_SAMPLE, &tid) == FM_OK);
    FmSequencer *s = fm_sequence_create(&p, 4, 64);
    CHECK(s);
    FmEvent pedal = {0};
    pedal.track = tid;
    pedal.origin = FM_ORIGIN_LOCAL;
    pedal.kind = FM_EVENT_SUSTAIN;
    pedal.value = 1;
    CHECK(fm_sequence_live(s, &pedal) == FM_OK);
    FmEvent e = note(tid, 10, 1, 60, true);
    CHECK(fm_sequence_live(s, &e) == FM_OK);
    e = note(tid, 20, 1, 60, false);
    CHECK(fm_sequence_live(s, &e) == FM_OK);
    e = note(tid, 30, 2, 60, true);
    CHECK(fm_sequence_live(s, &e) == FM_OK);
    for (unsigned i = 1; i <= 4; i++)
        CHECK(fm_sequence_motion(s, tid, FM_PARAM_CUTOFF, .5f, i * 400) == FM_OK);
    CHECK(fm_sequence_retrospective(s, tid, 2000, 800) == FM_OK);
    FmPattern *pat = fm_pattern(&p, fm_track(&p, tid)->patterns[0]);
    unsigned ons = 0, pedals = 0;
    bool id1 = false, id2 = false;
    for (uint32_t i = 0; i < pat->count; i++)
    {
        FmEvent *x = &pat->events[i];
        if (x->kind == FM_EVENT_NOTE_ON)
        {
            ++ons;
            id1 |= x->identity == 1;
            id2 |= x->identity == 2;
        }
        if (x->kind == FM_EVENT_SUSTAIN)
            ++pedals;
    }
    CHECK(ons == 2 && id1 && id2 && pedals == 2);
    Log log = {0};
    advance(s, &log, 8001);
    fm_sequence_stop(s, log_event, &log);
    CHECK(log.active == 0 && log.peak <= 2);
    /* Separate candidate creation preserves an active take if admission fails. */
    CHECK(fm_sequence_record_arm(s, tid) == FM_OK);
    e = note(tid, 2400, 3, 64, true);
    CHECK(fm_sequence_live(s, &e) == FM_OK);
    size_t limit = p.budget.limit[FM_MEM_EVENTS];
    p.budget.limit[FM_MEM_EVENTS] = p.budget.used[FM_MEM_EVENTS];
    CHECK(fm_sequence_retrospective(s, tid, 2500, 1000) == FM_ERR_MEMORY);
    CHECK(fm_sequence_status(s).recording);
    p.budget.limit[FM_MEM_EVENTS] = limit;
    CHECK(fm_sequence_record_close(s, 6000) == FM_OK);
    fm_sequence_stop(s, NULL, NULL);
    pat->frozen_asset = 123;
    pat->length_ticks = 3840;
    p.bpm = 120;
    FrozenLog frozen = {0};
    fm_sequence_set_frozen_callback(s, frozen_event, &frozen);
    FmFrame initial = fm_sequence_frame(s);
    fm_sequence_start(s);
    memset(&log, 0, sizeof(log));
    advance(s, &log, 72001);
    CHECK(log.on == 0 && frozen.on == 2 && frozen.off == 1);
    CHECK(frozen.frames[0] == initial && frozen.frames[1] == initial + 72000);
    fm_sequence_stop(s, log_event, &log);
    CHECK(frozen.off == 2);
    fm_sequence_destroy(s);
    fm_project_destroy(&p);
    return 0;
}
int test_sequence(void)
{
    CHECK(free_take() == 0);
    CHECK(history_capture() == 0);
    CHECK(timing_patterns_song() == 0);
    CHECK(external_and_budget() == 0);
    CHECK(sustain_motion_frozen() == 0);
    return 0;
}
