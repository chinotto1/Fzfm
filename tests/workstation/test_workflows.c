/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "audio/fm_audio.h"
#include "audio/fm_src.h"
#include "dsp/fm_synthesis.h"
#include "sequence/fm_actions.h"
#include "storage/bake.h"
#include "storage/storage.h"
#include "test.h"
#include "ui/fm_ui.h"
#include <stdlib.h>
#include <string.h>
static int patterns_and_ui(void)
{
    FmProject p, restored;
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    CHECK(fm_project_init(&restored, NULL) == FM_OK);
    FmId tid, other, destination;
    CHECK(fm_instrument_create(&p, FM_SAMPLE, &tid) == FM_OK);
    CHECK(fm_instrument_create(&p, FM_SAMPLE, &other) == FM_OK);
    FmTrack *t = fm_track(&p, tid);
    CHECK(fm_pattern_create(&p, other, 1, &destination) == FM_OK);
    FmPattern *a = fm_pattern(&p, t->patterns[0]), *b = fm_pattern(&p, destination);
    /* Sparse steps, probability and locks survive copying to another track. */
    FmSequencer *seq = fm_sequence_create(&p, 128, 128);
    CHECK(seq);
    CHECK(fm_sequence_step(seq, a->id, 1, 60, 100, 120, true) == FM_OK);
    CHECK(fm_step_properties(&p, a->id, 1, 60, .5f, -30) == FM_OK);
    CHECK(fm_step_lock(&p, a->id, 1, FM_PARAM_CUTOFF, .25f) == FM_OK);
    CHECK(a->count == 3);
    CHECK(fm_sequence_step(seq, a->id, 2, 60, 100, 120, true) == FM_OK && a->count == 5);
    CHECK(fm_step_tie(&p, a->id, 2, 60) == FM_OK && a->count == 3);
    unsigned ons = 0;
    for (unsigned i = 0; i < a->count; i++)
    {
        ons += a->events[i].kind == FM_EVENT_NOTE_ON;
        if (a->events[i].kind == FM_EVENT_NOTE_OFF)
            NEAR(a->events[i].tick, 720, 0);
    }
    CHECK(ons == 1);
    CHECK(fm_undo(&p) == FM_OK && a->count == 5);
    CHECK(fm_redo(&p) == FM_OK && a->count == 3);

    CHECK(fm_pattern_copy(&p, a->id, b->id, false) == FM_OK);
    CHECK(b->count == 3 && b->events != a->events);
    for (unsigned i = 0; i < b->count; i++)
        CHECK(b->events[i].track == other);
    CHECK(fm_pattern_copy(&p, a->id, b->id, false) == FM_ERR_BUSY);
    CHECK(fm_pattern_clear(&p, b->id) == FM_OK && b->count == 0);
    CHECK(fm_undo(&p) == FM_OK && b->count == 3);
    CHECK(fm_redo(&p) == FM_OK && b->count == 0);
    FmId pat2;
    CHECK(fm_pattern_create(&p, tid, 1, &pat2) == FM_OK);
    uint8_t chain[] = {1, 0, 1};
    CHECK(fm_sequence_chain(seq, tid, chain, 3) == FM_OK);
    CHECK(t->chain_count == 3 && !memcmp(t->chain, chain, 3));
    CHECK(fm_sequence_arrange(seq, true) == FM_OK);
    fm_sequence_advance(seq, 18000, NULL, NULL);
    t->mute = true;
    fm_sequence_advance(seq, 18000, NULL, NULL);
    CHECK(fm_sequence_arrange(seq, false) == FM_OK);
    CHECK(p.song_count == 2 && p.song[0].bars > 0 && p.song[1].muted[0]);
    double tick = fm_sequence_tick(seq);
    fm_sequence_stop(seq, NULL, NULL);
    fm_sequence_continue(seq);
    NEAR(fm_sequence_tick(seq), tick, 0);
    fm_sequence_advance(seq, 10, NULL, NULL);
    CHECK(fm_sequence_tick(seq) > tick);
    fm_sequence_stop(seq, NULL, NULL);
    uint8_t *bytes = NULL;
    size_t size = 0;
    CHECK(fm_project_encode(&p, &bytes, &size) == FM_OK);
    CHECK(fm_project_decode(&restored, bytes, size) == FM_OK);
    CHECK(fm_track(&restored, tid)->chain_count == 3 && restored.song_count == 2);
    CHECK(!memcmp(fm_track(&restored, tid)->chain, chain, 3));
    CHECK(fm_pattern(&restored, a->id)->count == 3);
    free(bytes);
    FmEngine e;
    CHECK(fm_engine_init(&e, &p, 4) == FM_OK);
    FmUi ui;
    fm_ui_init(&ui, &p, &e, seq);
    p.selected_track = tid;
    fm_ui_sync_view(&ui);
    ui.tab = 5;
    ui.scope = 2;
    ui.rows[5] = 4;
    p.selected_track = other;
    fm_ui_sync_view(&ui);
    CHECK(ui.tab == 0 && ui.scope == 0);
    ui.tab = 6;
    p.selected_track = tid;
    fm_ui_sync_view(&ui);
    CHECK(ui.tab == 5 && ui.scope == 2 && ui.rows[5] == 4);
    t->selected_pattern = 0;
    CHECK(fm_ui_workspace_action(&ui, 15, false) == FM_OK);
    CHECK(ui.clipboard_pattern == a->id);
    CHECK(fm_ui_workspace_action(&ui, 17, false) == FM_OK);
    CHECK(ui.picker == FM_PICK_CONFIRM && a->count == 3);
    fm_ui_button(&ui, FM_BUTTON_HOME, true, 10);
    fm_ui_button(&ui, FM_BUTTON_HOME, false, 20);
    CHECK(ui.picker == FM_PICK_NONE && a->count == 3);
    CHECK(fm_ui_workspace_action(&ui, 21, false) == FM_OK && ui.screen == FM_SCREEN_STEP_DETAIL);
    fm_ui_render(&ui);
    CHECK(fm_ui_workspace_action(&ui, 22, false) == FM_OK &&
          ui.screen == FM_SCREEN_PATTERN_SETTINGS);
    fm_ui_render(&ui);
    CHECK(fm_ui_workspace_action(&ui, 20, false) == FM_OK && ui.screen == FM_SCREEN_TIMING);
    fm_ui_render(&ui);
    /* A continuous gesture is one undo even with multiple encoder events. */
    ui.screen = FM_SCREEN_HOME;
    float old_volume = t->volume;
    fm_ui_encoder(&ui, 4, 1, 200);
    fm_ui_encoder(&ui, 4, 2, 220);
    NEAR(t->volume, old_volume + .03, .0001);
    fm_ui_button(&ui, FM_BUTTON_SCL, true, 300);
    fm_ui_tick(&ui, 801);
    fm_ui_button(&ui, FM_BUTTON_SCL, false, 810);
    NEAR(t->volume, old_volume, .0001);
    /* Undo/redo must synchronize actual FX, not merely the saved knob value. */
    ui.screen = FM_SCREEN_FX;
    ui.command = 5;
    ui.fx_slot = 0;
    float gain = p.fx[0].gain;
    fm_ui_encoder(&ui, 3, 10, 1000);
    NEAR(e.fx[0].config.gain, gain + .1, .0001);
    fm_ui_button(&ui, FM_BUTTON_SCL, true, 1100);
    fm_ui_tick(&ui, 1601);
    fm_ui_button(&ui, FM_BUTTON_SCL, false, 1610);
    NEAR(p.fx[0].gain, gain, .0001);
    NEAR(e.fx[0].config.gain, gain, .0001);
    fm_ui_button(&ui, FM_BUTTON_HOME, true, 1700);
    fm_ui_tick(&ui,2200);
    fm_ui_button(&ui, FM_BUTTON_HOME, false, 2210);
    fm_ui_encoder(&ui, 2, 1, 2220);
    fm_ui_button(&ui, FM_BUTTON_SCL, true, 2230);
    fm_ui_button(&ui, FM_BUTTON_SCL, false, 2240);
    NEAR(p.fx[0].gain, gain + .1, .0001);
    NEAR(e.fx[0].config.gain, gain + .1, .0001);
    fm_engine_destroy(&e);
    fm_sequence_destroy(seq);
    fm_project_destroy(&restored);
    fm_project_destroy(&p);
    return 0;
}
static int persistent_unbake(void)
{
    FmProject p, q;
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    CHECK(fm_project_init(&q, NULL) == FM_OK);
    FmId tid, aid, rid;
    CHECK(fm_instrument_create(&p, FM_MULTI_SAMPLE, &tid) == FM_OK);
    FmTrack *t = fm_track(&p, tid);
    int16_t pcm[180];
    for (unsigned i = 0; i < 180; i++)
        pcm[i] = (int16_t)(12000 * sinf(i * .3f));
    CHECK(fm_asset_create(&p, pcm, 180, 1, FM_RATE, false, FM_SOURCE_CAPTURE, &aid) == FM_OK);
    CHECK(fm_region_attach(&p, t->instrument, aid, 36, 0, &rid) == FM_OK);
    t->sends[0] = .4f;
    FmBakeOptions options = {.sends = 15, .tail_frames = 256, .isolated_confirmed = true};
    CHECK(fm_bake_samples(&p, (FmEditScope){t->instrument, rid, false}, &options) == FM_OK);
    CHECK(p.bake_count == 1 && fm_region(&p, rid)->asset != aid);
    NEAR(t->sends[0], .4, .0001);
    uint8_t *bytes;
    size_t size;
    CHECK(fm_project_encode(&p, &bytes, &size) == FM_OK);
    CHECK(fm_project_decode(&q, bytes, size) == FM_OK);
    free(bytes);
    FmId baked = fm_region(&q, rid)->asset;
    fm_track(&q, tid)->sends[0] = .7f;
    CHECK(fm_unbake_sample(&q, rid) == FM_OK);
    CHECK(fm_region(&q, rid)->asset == aid && q.bake_count == 0);
    NEAR(fm_track(&q, tid)->sends[0], .7, .0001); /* ONE preserves unrelated routing. */
    CHECK(!memcmp(fm_asset(&q, aid)->pcm, pcm, sizeof(pcm)));
    CHECK(fm_undo(&q) == FM_OK && fm_region(&q, rid)->asset == baked && q.bake_count == 1);
    CHECK(fm_redo(&q) == FM_OK && fm_region(&q, rid)->asset == aid);
    CHECK(fm_bake_samples(&q, (FmEditScope){t->instrument, 0, true}, &options) == FM_OK);
    NEAR(fm_track(&q, tid)->sends[0], 0, 0);
    CHECK(fm_unbake_sample(&q, rid) == FM_OK);
    NEAR(fm_track(&q, tid)->sends[0], .7, .0001);
    CHECK(fm_project_validate(&q) == FM_OK);
    fm_project_destroy(&q);
    fm_project_destroy(&p);
    return 0;
}
static int audio_gestures(void)
{
    FmProject p;
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    FmId tid, rid;
    CHECK(fm_instrument_create(&p, FM_SAMPLE, &tid) == FM_OK);
    FmTrack *t = fm_track(&p, tid);
    CHECK(fm_synthesis_preset(&p, t->instrument, FM_WAVE_SINE, 600, &rid) == FM_OK);
    FmEngine e;
    CHECK(fm_engine_init(&e, &p, 4) == FM_OK);
    float audio[4096];
    FmEvent note = {
        .track = tid, .kind = FM_EVENT_NOTE_ON, .note = 69, .velocity = 100, .identity = 1};
    CHECK(fm_engine_event(&e, &note) == FM_OK);
    note.kind = FM_EVENT_NOTE_OFF;
    CHECK(fm_engine_event(&e, &note) == FM_OK);
    fm_engine_render(&e, audio, 2048);
    CHECK(fm_engine_active_voices(&e) == 0); /* same-frame click cannot stick. */
    note.kind = FM_EVENT_NOTE_ON;
    note.frame = e.frame;
    CHECK(fm_engine_event(&e, &note) == FM_OK);
    fm_engine_render(&e, audio, 256);
    const FmAsset *old = e.voices[0].asset;
    CHECK(fm_synthesis_preset(&p, t->instrument, FM_WAVE_SQUARE, 1024, &rid) == FM_OK);
    fm_engine_render(&e, audio, 1);
    CHECK(e.voices[0].asset != old && e.voices[0].previous_asset == old &&
          e.voices[0].table_fade == 63);
    NEAR(e.voices[0].step, 1024.0 * 440 / FM_RATE, 1e-9);
    fm_engine_render(&e, audio, 100);
    CHECK(e.voices[0].table_fade == 0 && e.voices[0].previous_asset == NULL);
    FmEvent pedal = {.track = tid,
                     .frame = e.frame,
                     .kind = FM_EVENT_SUSTAIN,
                     .value = 1,
                     .origin = FM_ORIGIN_USB};
    CHECK(fm_engine_event(&e, &pedal) == FM_OK);
    note.kind = FM_EVENT_NOTE_OFF;
    note.frame = e.frame;
    CHECK(fm_engine_event(&e, &note) == FM_OK);
    fm_engine_render(&e, audio, 2048);
    CHECK(fm_engine_active_voices(&e) == 0); /* USB pedal cannot hold local key. */
    FmFxConfig c = p.fx[0];
    c.parameter[0] = fm_fx_max_delay() + .01f;
    CHECK(fm_engine_fx_prepare(&e, 0, &c) == FM_ERR_RANGE);
    c = p.fx[0];
    c.return_muted = true;
    CHECK(fm_engine_fx_prepare(&e, 0, &c) == FM_OK && e.fx[0].config.return_muted);
    /* Bypass/re-enable retains live tails without replacement allocations. */
    float *buffer = e.fx[0].buffer;
    e.fx[0].energy = .2f;
    e.fx[0].position = 77;
    c.enabled = false;
    CHECK(fm_engine_fx_prepare(&e, 0, &c) == FM_OK);
    c.enabled = true;
    CHECK(fm_engine_fx_prepare(&e, 0, &c) == FM_OK);
    CHECK(e.fx[0].buffer == buffer && e.fx[0].position == 77 && e.fx[0].energy == .2f);
    /* Multi-slot admission failure publishes none of the prepared replacements. */
    size_t used = p.budget.used[FM_MEM_FX];
    p.budget.limit[FM_MEM_FX] = used;
    p.fx[0].type = FM_FX_CHARACTER;
    p.fx[1].type = FM_FX_DELAY;
    CHECK(fm_engine_fx_sync(&e, 3) == FM_ERR_MEMORY);
    CHECK(e.fx[0].config.type == FM_FX_DELAY && e.fx[0].buffer == buffer);
    CHECK(e.fx[1].config.type == FM_FX_REVERB);
    CHECK(p.budget.used[FM_MEM_FX] == used);
    fm_engine_destroy(&e);
    fm_project_destroy(&p);
    return 0;
}
static int long_wave_edit(void)
{
    FmProject p;
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    FmId tid, rid;
    CHECK(fm_instrument_create(&p, FM_SAMPLE, &tid) == FM_OK);
    FmTrack *t = fm_track(&p, tid);
    uint16_t levels[FM_HARMONICS] = {256};
    CHECK(fm_synthesis_additive(&p, t->instrument, levels, 1024, &rid) == FM_OK);
    FmId initial = fm_region(&p, rid)->asset;
    FmEngine e;
    CHECK(fm_engine_init(&e, &p, 4) == FM_OK);
    FmUi ui;
    fm_ui_init(&ui, &p, &e, NULL);
    ui.screen = FM_SCREEN_EDIT;
    ui.tab = 6;
    ui.rows[6] = 1;
    ui.scope = 1;
    ui.harmonic = 1;
    /* Initialize view state before setting the test's desired editor tab. */
    fm_ui_sync_view(&ui);
    ui.tab = 6;
    ui.rows[6] = 1;
    ui.scope = 1;
    FmEvent note = {
        .track = tid, .kind = FM_EVENT_NOTE_ON, .velocity = 100, .note = 60, .identity = 1};
    CHECK(fm_engine_event(&e, &note) == FM_OK);
    float output[256];
    fm_engine_render(&e, output, 128);
    for (unsigned i = 0; i < 600; i++)
    {
        fm_ui_encoder(&ui, 4, i % 2 ? -1 : 1, 100 + i * 10);
        CHECK(ui.last_result == FM_OK);
        fm_engine_render(&e, output, 128);
        CHECK(p.asset_count <= 4);
    }
    fm_ui_tick(&ui, 6200);
    CHECK(p.asset_count <= 3 && p.budget.used[FM_MEM_AUDIO] < 8192);
    CHECK(fm_undo(&p) == FM_OK && fm_region(&p, rid)->asset == initial);
    fm_engine_render(&e, output, 128);
    CHECK(e.voices[0].asset->id == initial);
    CHECK(fm_redo(&p) == FM_OK);
    fm_engine_render(&e, output, 128);
    CHECK(fm_project_validate(&p) == FM_OK);
    /* ALL drawing maps the reference position onto each anchor's own length. */
    FmId aid, second;
    int16_t points[8] = {100, 100, 100, 100, 100, 100, 100, 100};
    CHECK(fm_asset_create(&p, points, 8, 1, FM_RATE, true, FM_SOURCE_DRAW, &aid) == FM_OK);
    CHECK(fm_region_attach(&p, t->instrument, aid, 72, -1, &second) == FM_OK);
    FmId before = fm_region(&p, rid)->asset;
    ui.rows[6] = 4;
    ui.scope = 0;
    ui.point = 512;
    fm_ui_encoder(&ui, 4, 32, 7000);
    CHECK(ui.last_result == FM_OK);
    CHECK(fm_region(&p, rid)->asset != before && fm_region(&p, second)->asset != aid);
    CHECK(fm_asset(&p, fm_region(&p, rid)->asset)->pcm[512] == 8192);
    CHECK(fm_asset(&p, fm_region(&p, second)->asset)->pcm[4] == 8192);
    CHECK(fm_asset(&p, aid)->pcm[4] == 100);
    CHECK(fm_undo(&p) == FM_OK);
    CHECK(fm_region(&p, rid)->asset == before && fm_region(&p, second)->asset == aid);
    fm_engine_destroy(&e);
    fm_project_destroy(&p);
    return 0;
}
int test_workflows(void)
{
    CHECK(patterns_and_ui() == 0);
    CHECK(persistent_unbake() == 0);
    CHECK(audio_gestures() == 0);
    CHECK(long_wave_edit() == 0);
    return 0;
}
