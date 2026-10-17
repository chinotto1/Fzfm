/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "check.h"

#include "audio/engine.h"
#include "audio/play.h"
#include "core/project.h"
#include "dsp/bake.h"
#include "dsp/envelope.h"
#include "dsp/filter.h"
#include "dsp/fx.h"
#include "dsp/loopprog.h"
#include "dsp/mangle.h"
#include "import/import.h"
#include "sampler/capture.h"
#include "sequence/arp.h"
#include "sequence/history.h"
#include "sequence/seq.h"
#include "storage/bundle.h"
#include "storage/journal.h"
#include "ui/edit.h"
#include "ui/session.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void tap(FzInput *in, FzSession *session, FzButton button, uint32_t t, FzCommand *cmd)
{
    FzAction action;
    fz_session_bind_input(session, in);
    memset(cmd, 0, sizeof *cmd);
    fz_input_button(in, button, 1, t);
    fz_input_poll(in, t);
    if (fz_input_pop(in, &action))
        fz_session_handle(session, &action, cmd);
    fz_input_button(in, button, 0, t + 1000);
    if (fz_input_pop(in, &action))
        fz_session_handle(session, &action, cmd);
}

static void hold(FzInput *in, FzSession *session, FzButton button, uint32_t t, FzCommand *cmd)
{
    FzAction action;
    fz_session_bind_input(session, in);
    memset(cmd, 0, sizeof *cmd);
    fz_input_button(in, button, 1, t);
    fz_input_poll(in, t + FZ_HOLD_US);
    EXPECT(fz_input_pop(in, &action) == 1);
    fz_session_handle(session, &action, cmd);
    fz_input_button(in, button, 0, t + FZ_HOLD_US + 10);
    EXPECT(fz_input_pop(in, &action) == 0);
}

static void test_oct_plus_router(void)
{
    FzInput input;
    FzSession session;
    FzCommand cmd;
    FzHistory history;
    FzHistEvent ev;
    int octave;

    fz_input_init(&input);
    fz_session_init(&session);
    session.octave = 1;
    tap(&input, &session, FZ_BTN_OCT_UP, 10, &cmd);
    EXPECT(cmd.kind == FZ_CMD_OCTAVE_UP && session.octave == 2 && cmd.at_us == 10);

    octave = session.octave;
    hold(&input, &session, FZ_BTN_OCT_UP, 100, &cmd);
    EXPECT(cmd.kind == FZ_CMD_UNDO && session.octave == octave);

    session.screen = FZ_SCR_CAPTURE;
    session.octave = 3;
    tap(&input, &session, FZ_BTN_OCT_UP, 50, &cmd);
    EXPECT(cmd.kind == FZ_CMD_CAPTURE_FINISH);
    EXPECT(session.screen == FZ_SCR_HOME && session.octave == 3);

    session.screen = FZ_SCR_CAPTURE;
    hold(&input, &session, FZ_BTN_OCT_UP, 80, &cmd);
    EXPECT(cmd.kind == FZ_CMD_CAPTURE_UNDO);
    EXPECT(session.screen == FZ_SCR_CAPTURE && session.octave == 3);

    session.screen = FZ_SCR_EDIT;
    session.edit_tab = FZ_TAB_SAMPLE;
    session.row[FZ_TAB_SAMPLE] = 0;
    tap(&input, &session, FZ_BTN_OCT_UP, 90, &cmd);
    EXPECT(cmd.kind == FZ_CMD_CONFIRM && session.screen == FZ_SCR_NUMERIC);
    EXPECT(session.edit_tab == FZ_TAB_SAMPLE);
    tap(&input, &session, FZ_BTN_HOME, 91, &cmd);
    EXPECT(cmd.kind == FZ_CMD_CANCEL && session.screen == FZ_SCR_EDIT);
    tap(&input, &session, FZ_BTN_HOME, 92, &cmd);
    EXPECT(cmd.kind == FZ_CMD_HOME && session.screen == FZ_SCR_HOME);

    fz_session_init(&session);
    session.screen = FZ_SCR_NUMERIC;
    session.back = FZ_SCR_EDIT;
    session.num_min = 0;
    session.num_max = 50;
    hold(&input, &session, FZ_BTN_OCT_UP, 200, &cmd);
    EXPECT(cmd.kind == FZ_CMD_CANCEL && session.screen == FZ_SCR_EDIT);

    fz_session_init(&session);
    session.screen = FZ_SCR_CONFIRM;
    session.back = FZ_SCR_EDIT;
    tap(&input, &session, FZ_BTN_OCT_UP, 210, &cmd);
    EXPECT(cmd.kind == FZ_CMD_CONFIRM && session.screen == FZ_SCR_CONFIRM);
    hold(&input, &session, FZ_BTN_OCT_UP, 220, &cmd);
    EXPECT(cmd.kind == FZ_CMD_CANCEL && session.screen == FZ_SCR_EDIT);

    fz_session_init(&session);
    session.screen = FZ_SCR_STEP;
    session.page = 1;
    tap(&input, &session, FZ_BTN_OCT_UP, 300, &cmd);
    EXPECT(cmd.kind == FZ_CMD_PAGE && session.page == 2 && session.octave == 0);
    session.screen = FZ_SCR_PATTERNS;
    tap(&input, &session, FZ_BTN_OCT_DOWN, 310, &cmd);
    EXPECT(session.page == 1);

    fz_session_init(&session);
    session.screen = FZ_SCR_EDIT;
    session.edit_tab = 2;
    hold(&input, &session, FZ_BTN_EDIT, 400, &cmd);
    EXPECT(cmd.kind == FZ_CMD_NAMING && session.screen == FZ_SCR_NAMING && session.edit_tab == 2);
    tap(&input, &session, FZ_BTN_EDIT, 410, &cmd);
    EXPECT(session.edit_tab == 2);

    fz_history_init(&history);
    session.screen = FZ_SCR_EDIT;
    fz_session_numeric_open(&session, 0, 127);
    ev.note = 60;
    ev.velocity = 100;
    ev.track = 0;
    ev.source = FZ_SRC_KEYBOARD;
    ev.time_us = 1;
    fz_session_history_key(&session, &history, ev);
    EXPECT(history.count == 0);
    EXPECT(fz_session_wants_play(&session) == 0);
    EXPECT(fz_session_numeric_key(&session, 9) == FZ_OK);
    EXPECT(fz_session_numeric_key(&session, 9) == FZ_OK);
    EXPECT(fz_session_numeric_key(&session, 9) == FZ_OK);
    EXPECT(fz_session_numeric_key(&session, 9) == FZ_OK);
    EXPECT(fz_session_numeric_key(&session, 9) == FZ_OK);
    EXPECT(fz_session_numeric_key(&session, 9) == FZ_OK);
    EXPECT(fz_session_numeric_key(&session, 9) == FZ_ERR_RANGE);
    {
        int value = 0;
        EXPECT(fz_session_numeric_take(&session, &value) == FZ_ERR_RANGE);
        EXPECT(session.screen == FZ_SCR_NUMERIC);
    }
    session.numeric_n = 0;
    EXPECT(fz_session_numeric_key(&session, 4) == FZ_OK);
    EXPECT(fz_session_numeric_key(&session, 2) == FZ_OK);
    {
        int value = -1;
        EXPECT(fz_session_numeric_take(&session, &value) == FZ_OK);
        EXPECT(value == 42 && session.screen == FZ_SCR_EDIT);
    }

    fz_session_pick_open(&session, 1);
    EXPECT(fz_session_wants_play(&session) == 0);
    EXPECT(fz_session_pick_key(&session, 70) == FZ_OK);
    EXPECT(session.screen == FZ_SCR_PICK_RANGE);
    tap(&input, &session, FZ_BTN_OCT_UP, 500, &cmd);
    EXPECT(cmd.kind == FZ_CMD_OCTAVE_UP && session.octave == 1);
    EXPECT(fz_session_pick_key(&session, 60) == FZ_OK);
    EXPECT(session.pick_low == 60 && session.pick_high == 70);
    EXPECT(session.screen == FZ_SCR_EDIT);

    fz_session_init(&session);
    tap(&input, &session, FZ_BTN_ENV, 600, &cmd);
    EXPECT(cmd.kind == FZ_CMD_EDIT_TAB && session.screen == FZ_SCR_EDIT && session.edit_tab == FZ_TAB_AMP);
    tap(&input, &session, FZ_BTN_LFO, 610, &cmd);
    EXPECT(session.screen == FZ_SCR_EDIT && session.edit_tab == FZ_TAB_MOD);
    tap(&input, &session, FZ_BTN_ENV, 620, &cmd);
    EXPECT(session.edit_tab == FZ_TAB_AMP);

    fz_session_init(&session);
    session.cycle_inst = 1;
    session.screen = FZ_SCR_HOME;
    tap(&input, &session, FZ_BTN_EDIT, 700, &cmd);
    EXPECT(session.screen == FZ_SCR_EDIT && session.edit_tab == FZ_TAB_WAVE);
    tap(&input, &session, FZ_BTN_EDIT, 710, &cmd);
    EXPECT(session.edit_tab == FZ_TAB_DRAW);
    tap(&input, &session, FZ_BTN_EDIT, 720, &cmd);
    EXPECT(session.edit_tab == FZ_TAB_FILTER);
    session.edit_tab = FZ_TAB_AMP;
    tap(&input, &session, FZ_BTN_EDIT, 725, &cmd);
    EXPECT(session.edit_tab == FZ_TAB_WAVE);
    session.cycle_inst = 0;
    session.edit_tab = FZ_TAB_MOD;
    tap(&input, &session, FZ_BTN_EDIT, 728, &cmd);
    EXPECT(session.edit_tab == FZ_TAB_SAMPLE);
    session.edit_tab = FZ_TAB_SAMPLE;
    tap(&input, &session, FZ_BTN_EDIT, 730, &cmd);
    EXPECT(session.edit_tab == FZ_TAB_LOOP);

    fz_session_init(&session);
    tap(&input, &session, FZ_BTN_REC, 800, &cmd);
    EXPECT(cmd.kind == FZ_CMD_REC_TAP && session.screen == FZ_SCR_HOME);
    hold(&input, &session, FZ_BTN_REC, 900, &cmd);
    EXPECT(cmd.kind == FZ_CMD_REC_HOLD && session.screen == FZ_SCR_CAPTURE);
    tap(&input, &session, FZ_BTN_REC, 910, &cmd);
    EXPECT(cmd.kind == FZ_CMD_REC_TAP && cmd.value == 1 && session.screen == FZ_SCR_CAPTURE);
}

static void test_edit_scope_and_undo(void)
{
    static FzProject project;
    static int16_t store[4000];
    FzSession session;
    FzId inst, track, asset, region, other_inst, other_track, other_region;
    FzInstrument *it;
    int16_t pcm_a[10], pcm_b[4], pcm_c[8];
    char header[64];
    int i;
    int16_t before_short, before_long;

    fz_project_init(&project, store, 4000);
    EXPECT(fz_project_new_instrument(&project, FZ_INST_MULTI, &inst, &track) == FZ_OK);
    EXPECT(fz_project_new_instrument(&project, FZ_INST_MULTI, &other_inst, &other_track) == FZ_OK);
    for (i = 0; i < 10; i++)
        pcm_a[i] = (int16_t)(1000 + i);
    for (i = 0; i < 4; i++)
        pcm_b[i] = 500;
    for (i = 0; i < 8; i++)
        pcm_c[i] = 700;
    EXPECT(fz_project_add_pcm(&project, pcm_a, 10, FZ_ENGINE_RATE_HZ, 0, &asset) == FZ_OK);
    EXPECT(fz_project_add_region(&project, inst, asset, 36, 0, &region) == FZ_OK);
    EXPECT(fz_project_add_pcm(&project, pcm_b, 4, FZ_ENGINE_RATE_HZ, 0, &asset) == FZ_OK);
    EXPECT(fz_project_add_region(&project, inst, asset, 37, 1, &region) == FZ_OK);
    EXPECT(fz_project_add_pcm(&project, pcm_c, 8, FZ_ENGINE_RATE_HZ, 0, &asset) == FZ_OK);
    EXPECT(fz_project_add_region(&project, other_inst, asset, 40, 0, &other_region) == FZ_OK);
    project.selected_track = 0;

    fz_session_init(&session);
    session.screen = FZ_SCR_EDIT;
    session.scope_all = 1;
    session.edit_tab = FZ_TAB_SAMPLE;
    EXPECT(fz_edit_knob_label(FZ_TAB_SAMPLE, 0, 0)[0] == 'R');
    EXPECT(strcmp(fz_edit_knob_label(FZ_TAB_SAMPLE, 2, 1), "GAIN") == 0);
    EXPECT(strcmp(fz_edit_knob_label(FZ_TAB_SAMPLE, 2, 2), "-") == 0);
    EXPECT(fz_edit_knob(&session, &project, 0, 2) == FZ_OK);
    EXPECT(session.row[FZ_TAB_SAMPLE] == 2);
    EXPECT(fz_region(&project, project.instrument[0].region[0])->gain_q15 == 32767);
    EXPECT(fz_edit_knob(&session, &project, 2, -5) == FZ_OK);
    EXPECT(fz_region(&project, project.instrument[0].region[0])->gain_q15 == 32767);
    EXPECT(fz_edit_knob(&session, &project, 1, -1) == FZ_OK);
    EXPECT(fz_edit_knob(&session, &project, 1, -1) == FZ_OK);
    EXPECT(fz_region(&project, project.instrument[0].region[0])->gain_q15 == 32767 - 512);
    EXPECT(fz_region(&project, project.instrument[0].region[1])->gain_q15 == 32767 - 512);
    EXPECT(fz_region(&project, other_region)->gain_q15 == 32767);
    EXPECT(fz_edit_affected(&session, &project) == 2);
    session.undo_kind = FZ_UNDO_REGION;
    EXPECT(fz_session_undo(&session, &project, 0) == FZ_OK);
    EXPECT(fz_region(&project, project.instrument[0].region[0])->gain_q15 == 32767);
    EXPECT(fz_region(&project, project.instrument[0].region[1])->gain_q15 == 32767);
    EXPECT(fz_project_redo(&project) == FZ_OK);
    EXPECT(fz_region(&project, project.instrument[0].region[0])->gain_q15 == 32767 - 512);

    session.row[FZ_TAB_SAMPLE] = 0;
    session.scope_all = 1;
    EXPECT(fz_edit_knob(&session, &project, 1, 4) == FZ_ERR_STATE);
    EXPECT(strcmp(project.last_error, "ONE SAMPLE") == 0);
    EXPECT(fz_region(&project, project.instrument[0].region[0])->start_frame == 0);
    EXPECT(fz_region(&project, project.instrument[0].region[1])->start_frame == 0);

    session.row[FZ_TAB_SAMPLE] = 5;
    before_long = 0;
    before_short = 0;
    EXPECT(fz_edit_knob(&session, &project, 1, 100) == FZ_OK);
    EXPECT(fz_region(&project, project.instrument[0].region[0])->start_frame == 9);
    EXPECT(fz_region(&project, project.instrument[0].region[1])->start_frame == 3);
    (void)before_long;
    (void)before_short;

    session.scope_all = 0;
    session.region_sel = 0;
    session.edit_tab = FZ_TAB_SAMPLE;
    session.row[FZ_TAB_LOOP] = 1;
    session.edit_tab = FZ_TAB_LOOP;
    EXPECT(session.row[FZ_TAB_SAMPLE] == 5);
    EXPECT(session.scope_all == 0);
    session.edit_tab = FZ_TAB_SAMPLE;
    fz_edit_header(&session, &project, header, sizeof header);
    it = fz_instrument(&project, inst);
    EXPECT(it != 0);
    EXPECT(strstr(header, it->name) != 0);
    EXPECT(strstr(header, "SAMPLE") != 0);
    EXPECT(strstr(header, "ONE") != 0);
    session.scope_all = 1;
    fz_edit_header(&session, &project, header, sizeof header);
    EXPECT(strstr(header, "ALL") != 0);

    EXPECT(fz_project_rename_category(&project, inst, "PAD") == FZ_OK);
    EXPECT(strcmp(fz_instrument(&project, inst)->name, "PAD 1") == 0);
    EXPECT(fz_project_rename_category(&project, other_inst, "PAD") == FZ_OK);
    EXPECT(strcmp(fz_instrument(&project, other_inst)->name, "PAD 2") == 0);
    session.screen = FZ_SCR_NAMING;
    session.back = FZ_SCR_EDIT;
    session.edit_tab = 4;
    project.selected_track = 0;
    EXPECT(fz_session_name_key(&session, &project, 5) == FZ_OK);
    EXPECT(strcmp(fz_instrument(&project, inst)->name, "BASS 1") == 0);
    EXPECT(session.screen == FZ_SCR_EDIT && session.edit_tab == 4);

    session.scope_all = 1;
    session.edit_tab = FZ_TAB_LOOP;
    session.row[FZ_TAB_LOOP] = 2;
    EXPECT(fz_edit_knob(&session, &project, 1, 3) == FZ_OK);
    EXPECT(fz_region(&project, project.instrument[0].region[0])->loop.sec[0].crossfade == 3);
    EXPECT(fz_region(&project, project.instrument[0].region[1])->loop.sec[0].crossfade == 3);
    EXPECT(fz_region(&project, project.instrument[0].region[0])->start_frame == 9);
    EXPECT(fz_region(&project, other_region)->loop.count == 0);
    session.row[FZ_TAB_LOOP] = 3;
    EXPECT(fz_edit_knob(&session, &project, 1, 2) == FZ_OK);
    EXPECT(fz_region(&project, project.instrument[0].region[0])->loop.sec[0].repeats == 2);
    EXPECT(fz_region(&project, project.instrument[0].region[0])->tune_cents == 0);
    session.row[FZ_TAB_LOOP] = 0;
    EXPECT(fz_edit_knob(&session, &project, 1, 1) == FZ_ERR_STATE);

    session.edit_tab = FZ_TAB_AMP;
    session.row[FZ_TAB_AMP] = 1;
    EXPECT(fz_edit_knob(&session, &project, 1, 5) == FZ_OK);
    EXPECT(fz_region(&project, project.instrument[0].region[0])->amp.frames[1] == 205);
    EXPECT(fz_region(&project, project.instrument[0].region[0])->amp.frames[0] == 10);
    session.row[FZ_TAB_AMP] = 2;
    EXPECT(fz_edit_knob(&session, &project, 1, -1) == FZ_OK);
    EXPECT(fz_region(&project, project.instrument[0].region[0])->amp.level[1] == 20000 - 256);
    EXPECT(fz_region(&project, project.instrument[0].region[0])->gain_q15 == 32767 - 512);

    session.scope_all = 0;
    session.region_sel = 0;
    session.edit_tab = FZ_TAB_MAP;
    session.row[FZ_TAB_MAP] = 0;
    EXPECT(fz_edit_apply_number(&session, &project, 40) == FZ_OK);
    EXPECT(fz_region(&project, project.instrument[0].region[0])->root == 40);
    EXPECT(fz_region(&project, project.instrument[0].region[0])->tune_cents == 0);
    session.scope_all = 1;
    EXPECT(fz_edit_apply_number(&session, &project, 50) == FZ_ERR_STATE);
    EXPECT(fz_region(&project, project.instrument[0].region[0])->root == 40);
    EXPECT(strcmp(project.last_error, "ONE SAMPLE") == 0);

    session.edit_tab = FZ_TAB_MOD;
    session.row[FZ_TAB_MOD] = 1;
    EXPECT(fz_edit_knob(&session, &project, 1, 40) == FZ_OK);
    EXPECT(project.track[0].lfo.depth == 40);
    session.row[FZ_TAB_MOD] = 2;
    EXPECT(fz_edit_knob(&session, &project, 1, 1) == FZ_OK);
    EXPECT(project.track[0].lfo.dest == 1);
    EXPECT(strcmp(fz_lfo_target_name(project.track[0].lfo.dest), "CUTOFF") == 0);
    session.row[FZ_TAB_MOD] = 0;
    EXPECT(fz_edit_knob(&session, &project, 2, 1) == FZ_OK);
    EXPECT(project.track[0].lfo.dest == FZ_LFO_LEVEL);
    EXPECT(fz_edit_knob(&session, &project, 2, 1) == FZ_OK);
    EXPECT(project.track[0].lfo.dest == FZ_LFO_PITCH);
    EXPECT(fz_edit_knob(&session, &project, 2, 1) == FZ_OK);
    EXPECT(project.track[0].lfo.dest == FZ_LFO_PAN);
    EXPECT(fz_edit_knob(&session, &project, 2, 1) == FZ_OK);
    EXPECT(project.track[0].lfo.dest == FZ_LFO_PAN);
    session.row[FZ_TAB_MOD] = 3;
    EXPECT(fz_edit_knob(&session, &project, 1, 1) == FZ_OK);
    EXPECT(project.track[0].lfo.shape == FZ_LFO_SQR);
    EXPECT(strcmp(fz_lfo_shape_name(project.track[0].lfo.shape), "SQR") == 0);
    EXPECT(fz_edit_knob(&session, &project, 3, 1) == FZ_OK);
    EXPECT(project.track[0].lfo.shape == FZ_LFO_SIN);
    EXPECT(fz_edit_knob(&session, &project, 3, 1) == FZ_OK);
    EXPECT(project.track[0].lfo.shape == FZ_LFO_SAW);
    EXPECT(fz_edit_knob(&session, &project, 3, 1) == FZ_OK);
    EXPECT(project.track[0].lfo.shape == FZ_LFO_SH);
    EXPECT(fz_edit_knob(&session, &project, 3, 1) == FZ_OK);
    EXPECT(project.track[0].lfo.shape == FZ_LFO_SH);
    session.edit_tab = FZ_TAB_AMP;
    session.row[FZ_TAB_AMP] = 4;
    EXPECT(fz_edit_knob(&session, &project, 1, 3) == FZ_OK);
    EXPECT(fz_region(&project, project.instrument[0].region[0])->filt_on == 1);
    EXPECT(fz_region(&project, project.instrument[0].region[0])->filt.frames[0] == 13);
    EXPECT(fz_region(&project, project.instrument[0].region[0])->gain_q15 == 32767 - 512);
}

static void test_mangle_loop_env(void)
{
    int16_t a[3] = {10, 20, 30};
    int16_t b[2] = {40, 50};
    int16_t dst[8];
    int16_t rev[3];
    int16_t wave[16];
    uint32_t frames = 0;
    FzLoopProg prog;
    FzLoopRun run;
    FzEnvelope env, scaled;
    int32_t level, peak;
    int i;

    EXPECT(fz_render_reverse(a, 3, rev) == FZ_OK);
    EXPECT(rev[0] == 30 && rev[2] == 10);
    EXPECT(fz_render_mix(a, 3, b, 2, 32767, 32767, -2, dst, 8, &frames) == FZ_OK);
    EXPECT(frames == 5 && dst[0] == 40 && dst[1] == 50 && dst[2] == 10 && dst[4] == 30);
    EXPECT(fz_render_cross(a, 3, b, 2, 0, dst, 8, &frames) == FZ_OK);
    EXPECT(frames == 5 && dst[3] == 40);

    for (i = 0; i < 16; i++)
        wave[i] = (int16_t)((i == 3) ? -2000 : 100);
    fz_draw_normalize(wave, 16);
    EXPECT(wave[3] == -28000);
    {
        int16_t sharp[5] = {0, 0, 3000, 0, 0};
        int16_t copy[5];
        memcpy(copy, sharp, sizeof sharp);
        fz_draw_smooth(sharp, 5);
        EXPECT(memcmp(sharp, copy, sizeof sharp) != 0);
    }
    fz_preset_sine(wave, 16);
    EXPECT(wave[0] == 0);
    EXPECT(wave[4] != 0);

    memset(&prog, 0, sizeof prog);
    prog.count = 1;
    prog.sec[0].start = 0;
    prog.sec[0].end = 0;
    EXPECT(fz_loop_validate(&prog, 8) == FZ_ERR_RANGE);
    prog.sec[0].end = 8;
    prog.sec[0].crossfade = 8;
    EXPECT(fz_loop_validate(&prog, 8) == FZ_ERR_RANGE);
    prog.sec[0].crossfade = 0;
    prog.sec[0].next = 3;
    EXPECT(fz_loop_validate(&prog, 8) == FZ_ERR_RANGE);
    prog.count = 2;
    prog.sec[0].next = 1;
    prog.sec[0].repeats = 0;
    prog.sec[0].crossfade = 4;
    prog.sec[1].start = 8;
    prog.sec[1].end = 12;
    prog.sec[1].next = 255;
    prog.sec[1].repeats = 1;
    EXPECT(fz_loop_validate(&prog, 12) == FZ_OK);
    {
        int16_t pcm[12];
        int faded = 0;
        for (i = 0; i < 8; i++)
            pcm[i] = 1000;
        for (i = 8; i < 12; i++)
            pcm[i] = 0;
        fz_loop_start(&run);
        for (i = 0; i < 8; i++) {
            int16_t s = fz_loop_sample(&prog, &run, pcm, 12, 0);
            if (s != 1000)
                faded = 1;
            EXPECT(run.section == 0);
        }
        EXPECT(faded == 1);
        EXPECT(run.section == 0);
        for (i = 0; i < 8; i++)
            fz_loop_sample(&prog, &run, pcm, 12, 1);
        EXPECT(run.section == 1);
    }

    fz_env_adsr(&env, 4, 4, 20000, 4);
    {
        FzEnvRun run_env;
        int32_t first, mid;
        fz_env_start(&run_env, &env);
        first = fz_env_next(&run_env);
        for (i = 0; i < 6; i++)
            mid = fz_env_next(&run_env);
        EXPECT(mid > first);
        fz_env_release(&run_env);
        for (i = 0; i < 3; i++)
            level = fz_env_next(&run_env);
        EXPECT(level < mid);
        for (i = 0; i < 20; i++)
            level = fz_env_next(&run_env);
        EXPECT(level == 0);
    }
    scaled = env;
    fz_env_scale_time(&scaled, 96, 60, 127);
    EXPECT(scaled.frames[0] < env.frames[0]);
    EXPECT(scaled.frames[0] >= 1);

    memset(&env, 0, sizeof env);
    env.n_stages = 8;
    env.sustain = -1;
    for (i = 0; i < 8; i++) {
        env.level[i] = (int16_t)((i < 7) ? (i + 1) * 1000 : 0);
        env.frames[i] = 1;
    }
    {
        FzEnvRun run_env;
        peak = 0;
        fz_env_start(&run_env, &env);
        for (i = 0; i < 24; i++) {
            level = fz_env_next(&run_env);
            if (level > peak)
                peak = level;
        }
        EXPECT(peak >= 7000);
        EXPECT(fz_env_next(&run_env) == 0 || run_env.active == 0);
    }
}

static double rms_left(const int16_t *lr, int frames)
{
    double acc = 0;
    int i;
    for (i = 0; i < frames; i++)
        acc += (double)lr[i * 2] * (double)lr[i * 2];
    return sqrt(acc / (double)frames);
}

static void test_filter_playback(void)
{
    static FzProject project;
    static int16_t store[8000];
    FzEngine low, high, plain;
    FzId inst, track, asset, region;
    int16_t pcm[64];
    int16_t tone[3600];
    int16_t mix_lo[3600 * 2], mix_hi[3600 * 2], mix_a[16], mix_b[16];
    FzSvFilter svf;
    int i;
    float y;

    EXPECT(fz_cutoff_hz(0) == 20);
    EXPECT(fz_cutoff_hz(127) == 14000);
    EXPECT(fz_resonance_q(0) > 0.49f && fz_resonance_q(0) < 0.51f);
    EXPECT(fz_resonance_q(127) > 7.9f && fz_resonance_q(127) < 8.1f);
    fz_svf_set(&svf, 127, 127);
    for (i = 0; i < 8000; i++) {
        y = fz_svf_low(&svf, (i & 1) ? 4.0f : -4.0f);
        EXPECT(isfinite(y) && y <= 4.0f && y >= -4.0f);
    }
    fz_svf_set(&svf, 0, 127);
    for (i = 0; i < 2000; i++) {
        y = fz_svf_low(&svf, 1.0f);
        EXPECT(isfinite(y) && y <= 4.0f && y >= -4.0f);
    }

    for (i = 0; i < 3600; i++)
        tone[i] = (int16_t)lround(sin(2.0 * M_PI * 2000.0 * (double)i / 36000.0) * 14000.0);
    fz_project_init(&project, store, 8000);
    EXPECT(fz_project_new_instrument(&project, FZ_INST_SAMPLE, &inst, &track) == FZ_OK);
    EXPECT(fz_project_add_pcm(&project, tone, 3600, FZ_ENGINE_RATE_HZ, 0, &asset) == FZ_OK);
    EXPECT(fz_project_add_region(&project, inst, asset, 69, 0, &region) == FZ_OK);
    fz_track_set_volume(&project, track, 127);
    fz_track_set_pan(&project, track, 64);
    project.selected_track = (int)track.index;
    fz_track_set_cutoff(&project, track, 2);
    fz_engine_init(&low);
    EXPECT(fz_performance_note(&low, &project, 69, 127, 1) >= 0);
    fz_performance_render(&low, &project, mix_lo, 3600);
    fz_track_set_cutoff(&project, track, 120);
    fz_engine_init(&high);
    EXPECT(fz_performance_note(&high, &project, 69, 127, 1) >= 0);
    fz_performance_render(&high, &project, mix_hi, 3600);
    EXPECT(low.bad_samples == 0 && high.bad_samples == 0);
    EXPECT(rms_left(mix_lo, 3600) < rms_left(mix_hi, 3600) * 0.5);

    for (i = 0; i < 64; i++)
        pcm[i] = 10000;
    fz_engine_init(&plain);
    fz_engine_note_on_pcm(&plain, pcm, 64, FZ_ENGINE_RATE_HZ, 60, 60, 0, 127, 127, 64, 1, 0, 1);
    fz_engine_render(&plain, mix_a, 4);
    fz_engine_set_filter(&plain, 0, 10, 10, 0);
    fz_engine_init(&high);
    fz_engine_note_on_pcm(&high, pcm, 64, FZ_ENGINE_RATE_HZ, 60, 60, 0, 127, 127, 64, 1, 0, 1);
    fz_engine_set_filter(&high, 0, 120, 80, 40);
    fz_engine_render(&high, mix_b, 4);
    EXPECT(memcmp(mix_a, mix_b, 4u * 2u * sizeof(int16_t)) == 0);

    {
        FzEngine choked, open_mix;
        int16_t one[8], two[8];
        FzRegion *reg;
        EXPECT(fz_project_add_pcm(&project, pcm, 64, FZ_ENGINE_RATE_HZ, 0, &asset) == FZ_OK);
        reg = fz_instrument_region_for_note(&project, inst, 69);
        reg->asset = asset;
        reg->choke = 3;
        reg->one_shot = 1;
        fz_engine_init(&choked);
        fz_performance_note(&choked, &project, 69, 127, 1);
        fz_performance_note(&choked, &project, 69, 127, 1);
        fz_engine_render(&choked, one, 2);
        reg->choke = 0;
        fz_engine_init(&open_mix);
        fz_performance_note(&open_mix, &project, 69, 127, 1);
        fz_performance_note(&open_mix, &project, 69, 127, 1);
        fz_engine_render(&open_mix, two, 2);
        EXPECT(two[0] != one[0]);
    }

    {
        FzEngine env_engine;
        FzEnvelope env;
        int16_t body[64];
        int slot;
        int16_t early[32], later[160];
        fz_env_adsr(&env, 1, 1, 30000, 40);
        fz_engine_init(&env_engine);
        slot = fz_engine_note_on_pcm(&env_engine, pcm, 64, FZ_ENGINE_RATE_HZ, 60, 60, 0, 127, 127, 64, 0, 0, 9);
        fz_engine_voice_amp(&env_engine, slot, &env);
        fz_engine_render(&env_engine, body, 16);
        EXPECT(body[10] != 0 || body[20] != 0);
        fz_engine_note_off(&env_engine, 60);
        fz_engine_render(&env_engine, early, 8);
        EXPECT(early[0] != 0 || early[2] != 0);
        fz_engine_render(&env_engine, later, 80);
        EXPECT(later[70] == 0 && later[71] == 0);
        EXPECT(env_engine.voice[slot].alive == 0);
    }

    {
        FzRegion *reg = fz_instrument_region_for_note(&project, inst, 69);
        FzEngine rev;
        int16_t first[4];
        int16_t ramp[8] = {1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000};
        EXPECT(fz_project_add_pcm(&project, ramp, 8, FZ_ENGINE_RATE_HZ, 0, &asset) == FZ_OK);
        reg->asset = asset;
        reg->reverse = 1;
        reg->end_frame = 4;
        reg->start_frame = 0;
        reg->choke = 0;
        reg->one_shot = 1;
        fz_track_set_pan(&project, track, 0);
        fz_engine_init(&rev);
        EXPECT(fz_performance_note(&rev, &project, 69, 127, 1) >= 0);
        fz_engine_render(&rev, first, 1);
        EXPECT(first[0] > 2500);
        EXPECT(first[1] == 0);
    }

    {
        FzRegion *reg = fz_instrument_region_for_note(&project, inst, 69);
        FzEngine looped;
        int16_t tone[8] = {1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000};
        int16_t out[16];
        EXPECT(fz_project_add_pcm(&project, tone, 8, FZ_ENGINE_RATE_HZ, 0, &asset) == FZ_OK);
        reg->asset = asset;
        reg->reverse = 0;
        reg->start_frame = 0;
        reg->end_frame = 0;
        reg->one_shot = 0;
        reg->loop.count = 1;
        reg->loop.sec[0].start = 0;
        reg->loop.sec[0].end = 4;
        reg->loop.sec[0].next = 255;
        fz_engine_init(&looped);
        EXPECT(fz_performance_note(&looped, &project, 69, 127, 1) >= 0);
        fz_engine_render(&looped, out, 6);
        EXPECT(looped.voice[0].alive == 1);
        EXPECT(out[8] > 500 && out[8] < 1500);
    }

    {
        FzEngine dry, moved;
        int16_t held[4] = {8000, 8000, 8000, 8000};
        int16_t a[8], b[8];
        FzId held_asset;
        FzRegion *reg = fz_instrument_region_for_note(&project, inst, 69);
        EXPECT(fz_project_add_pcm(&project, held, 4, FZ_ENGINE_RATE_HZ, 0, &held_asset) == FZ_OK);
        reg->asset = held_asset;
        reg->loop.count = 0;
        reg->one_shot = 1;
        project.track[track.index].lfo.depth = 0;
        project.track[track.index].cutoff = 127;
        project.track[track.index].resonance = 0;
        fz_engine_init(&dry);
        fz_performance_note(&dry, &project, 69, 127, 1);
        fz_performance_render(&dry, &project, a, 4);
        project.track[track.index].lfo.rate = 127;
        project.track[track.index].lfo.depth = 127;
        project.track[track.index].lfo.dest = 2;
        project.track[track.index].lfo.shape = 1;
        fz_engine_init(&moved);
        fz_performance_note(&moved, &project, 69, 127, 1);
        fz_performance_render(&moved, &project, b, 4);
        EXPECT(a[0] != b[0]);
        project.track[track.index].lfo.depth = 0;
        project.track[track.index].lfo.dest = 0;
    }
}

static void test_fx_and_bake(void)
{
    static FzFxRack rack;
    static FzProject project;
    static int16_t store[8000];
    static int16_t scratch[2000];
    FzId inst, track, asset, region_a, region_b, pattern;
    int16_t pcm[32];
    int32_t send[4];
    int32_t dry, wet_l, wet_r;
    int32_t in[4];
    int i;
    uint8_t trim_b0, trim_b1;
    int16_t send0, send1;
    FzId asset_before;
    uint32_t used_before;
    FzPattern *other;

    fz_fx_init(&rack);
    EXPECT(fz_fx_set(&rack, 0, FZ_FX_REVERB) == FZ_OK);
    EXPECT(fz_fx_set(&rack, 1, FZ_FX_REVERB) == FZ_ERR_STATE);
    EXPECT(rack.slot[1].kind != FZ_FX_REVERB);
    EXPECT(fz_fx_set(&rack, 1, FZ_FX_DELAY) == FZ_OK);
    EXPECT(fz_fx_set(&rack, 2, FZ_FX_CHORUS) == FZ_OK);
    EXPECT(fz_fx_set(&rack, 3, FZ_FX_DRIVE) == FZ_OK);
    rack.slot[1].param_a = 4;
    rack.slot[1].param_b = 0;
    memset(in, 0, sizeof in);
    in[1] = 10000;
    fz_fx_process(&rack, in, &wet_l, &wet_r);
    EXPECT(wet_l == 0);
    in[1] = 0;
    for (i = 0; i < 3; i++)
        fz_fx_process(&rack, in, &wet_l, &wet_r);
    fz_fx_process(&rack, in, &wet_l, &wet_r);
    EXPECT(wet_l > 8000);
    EXPECT(fz_fx_ram_used(&rack) > 0);
    for (i = 0; i < 4; i++)
        fz_fx_enable(&rack, i, 0);
    memset(in, 0, sizeof in);
    for (i = 0; i < 8000; i++)
        fz_fx_process(&rack, in, &wet_l, &wet_r);
    EXPECT(fz_fx_ram_used(&rack) == 0);
    EXPECT(fz_bake_shared_sum_is_exact() == 0);

    EXPECT(fz_send_amount(1000, 127, 127) == 1000);
    EXPECT(fz_send_amount(1000, 0, 127) == 0);
    EXPECT(fz_send_amount(1000, 127, 0) == 0);

    for (i = 0; i < 32; i++)
        pcm[i] = 8000;
    fz_project_init(&project, store, 8000);
    EXPECT(fz_project_new_instrument(&project, FZ_INST_MULTI, &inst, &track) == FZ_OK);
    EXPECT(fz_project_add_pcm(&project, pcm, 32, FZ_ENGINE_RATE_HZ, 0, &asset) == FZ_OK);
    EXPECT(fz_project_add_region(&project, inst, asset, 36, 0, &region_a) == FZ_OK);
    EXPECT(fz_project_add_pcm(&project, pcm, 32, FZ_ENGINE_RATE_HZ, 0, &asset) == FZ_OK);
    EXPECT(fz_project_add_region(&project, inst, asset, 37, 1, &region_b) == FZ_OK);
    project.track[track.index].send[0] = 127;
    project.track[track.index].send[1] = 127;
    send0 = project.track[track.index].send[0];
    send1 = project.track[track.index].send[1];
    trim_b0 = fz_region(&project, region_b)->send_trim[0];
    trim_b1 = fz_region(&project, region_b)->send_trim[1];
    asset_before = fz_region(&project, region_a)->asset;

    fz_fx_init(&rack);
    EXPECT(fz_fx_set(&rack, 0, FZ_FX_DELAY) == FZ_OK);
    EXPECT(fz_fx_set(&rack, 1, FZ_FX_CHORUS) == FZ_OK);
    rack.slot[0].param_a = 8;
    rack.slot[0].param_b = 0;
    EXPECT(fz_bake_region(&project, &rack, track, region_a, 0x1, scratch, 2000) == FZ_OK);
    EXPECT(fz_region(&project, region_a)->send_trim[0] == 0);
    EXPECT(fz_region(&project, region_a)->send_trim[1] == 127);
    EXPECT(fz_region(&project, region_a)->wet_asset.generation != 0);
    EXPECT(fz_region(&project, region_a)->asset.index == asset_before.index);
    EXPECT(fz_region(&project, region_a)->asset.generation == asset_before.generation);
    EXPECT(fz_region(&project, region_b)->send_trim[0] == trim_b0);
    EXPECT(fz_region(&project, region_b)->send_trim[1] == trim_b1);
    EXPECT(project.track[track.index].send[0] == send0);
    EXPECT(project.track[track.index].send[1] == send1);
    fz_voice_sends(&project.track[track.index], 0, fz_region(&project, region_a), 1000, 1, &dry, send);
    EXPECT(dry == 1000 && send[0] == 0 && send[1] == 0);
    fz_voice_sends(&project.track[track.index], 0, fz_region(&project, region_b), 1000, 0, &dry, send);
    EXPECT(send[1] == 1000);
    project.track[track.index].mute = 1;
    fz_voice_sends(&project.track[track.index], 0, fz_region(&project, region_b), 1000, 0, &dry, send);
    EXPECT(dry == 0 && send[0] == 0);
    project.track[track.index].mute = 0;

    EXPECT(fz_project_undo(&project) == FZ_OK);
    EXPECT(fz_region(&project, region_a)->send_trim[0] == 127);
    EXPECT(fz_region(&project, region_a)->wet_asset.generation == 0);

    fz_fx_init(&rack);
    EXPECT(fz_fx_set(&rack, 0, FZ_FX_DELAY) == FZ_OK);
    EXPECT(fz_fx_set(&rack, 1, FZ_FX_DRIVE) == FZ_OK);
    rack.slot[0].param_a = 4;
    rack.slot[0].param_b = 0;
    asset_before = fz_region(&project, region_a)->asset;
    EXPECT(fz_bake_region(&project, &rack, track, region_a, 0x3, scratch, 2000) == FZ_OK);
    EXPECT(fz_region(&project, region_a)->asset.generation != asset_before.generation ||
           fz_region(&project, region_a)->asset.index != asset_before.index);
    EXPECT(fz_region(&project, region_a)->send_trim[0] == 0);
    EXPECT(fz_region(&project, region_a)->send_trim[1] == 0);
    EXPECT(fz_region(&project, region_a)->wet_asset.generation == 0);
    EXPECT(fz_region(&project, region_b)->send_trim[0] == 127);
    EXPECT(project.track[track.index].send[0] == 127);

    pattern = project.track[track.index].pattern;
    other = &project.pattern[1];
    other->alive = 1;
    other->generation = 1;
    other->length_ticks = 16;
    EXPECT(fz_bake_pattern(&project, &rack, track, pattern, region_b, 0x1, scratch, 2000) == FZ_OK);
    EXPECT(project.pattern[pattern.index].baked == 1);
    EXPECT(project.pattern[pattern.index].unsend_mask & 0x1);
    EXPECT(project.track[track.index].send[0] == 127);
    EXPECT(fz_region(&project, region_b)->send_trim[0] == 127);
    fz_voice_sends(&project.track[track.index], &project.pattern[pattern.index], fz_region(&project, region_b), 1000, 0,
                   &dry, send);
    EXPECT(send[0] == 0);
    fz_voice_sends(&project.track[track.index], other, fz_region(&project, region_b), 1000, 0, &dry, send);
    EXPECT(send[0] == 1000);
    EXPECT(fz_project_undo(&project) == FZ_OK);
    EXPECT(project.pattern[pattern.index].baked == 0);

    used_before = project.pcm_used;
    project.pcm_capacity = project.pcm_used;
    EXPECT(fz_bake_region(&project, &rack, track, region_b, 0x1, scratch, 2000) == FZ_ERR_SAMPLE_STORAGE);
    EXPECT(project.pcm_used == used_before);
    EXPECT(fz_region(&project, region_b)->send_trim[0] == 127);
    project.pcm_capacity = 8000;

    {
        FzEngine eng, plain;
        int16_t impulse[16];
        int16_t wet_mix[24];
        int16_t dry_mix[24];
        uint8_t send_k[4] = {127, 0, 0, 0};
        uint8_t trim_k[4] = {127, 0, 0, 0};
        int slot;
        memset(impulse, 0, sizeof impulse);
        impulse[0] = 16000;
        fz_fx_init(&rack);
        EXPECT(fz_fx_set(&rack, 0, FZ_FX_DELAY) == FZ_OK);
        rack.slot[0].param_a = 8;
        rack.slot[0].param_b = 0;
        rack.slot[0].ret_gain = 127;
        fz_engine_init(&eng);
        slot = fz_engine_note_on_pcm(&eng, impulse, 16, FZ_ENGINE_RATE_HZ, 60, 60, 0, 127, 127, 64, 1, 0, 3);
        EXPECT(slot >= 0);
        fz_engine_voice_send(&eng, slot, send_k, trim_k);
        fz_engine_set_fx(&eng, &rack);
        fz_engine_render(&eng, wet_mix, 12);
        fz_engine_init(&plain);
        fz_engine_note_on_pcm(&plain, impulse, 16, FZ_ENGINE_RATE_HZ, 60, 60, 0, 127, 127, 64, 1, 0, 3);
        fz_engine_render(&plain, dry_mix, 12);
        EXPECT(dry_mix[16] == 0);
        EXPECT(wet_mix[16] > 8000);
        EXPECT(wet_mix[0] == dry_mix[0]);
    }
}

static void test_seq(void)
{
    static FzProject project;
    static int16_t store[64];
    static FzSeq seq;
    FzId inst, track;
    FzHistory history;
    FzHistEvent ev;
    uint8_t patterns[FZ_MAX_TRACKS];
    uint8_t mutes[FZ_MAX_TRACKS];
    int chain[4];
    uint16_t generation;
    int i;

    fz_project_init(&project, store, 64);
    EXPECT(fz_project_new_instrument(&project, FZ_INST_MULTI, &inst, &track) == FZ_OK);
    generation = fz_instrument(&project, inst)->generation;
    fz_seq_init(&seq);
    fz_seq_play(&seq, 1);
    fz_seq_advance(&seq, &project, 18000);
    EXPECT(seq.tick == 96);
    fz_seq_advance(&seq, &project, 18000);
    EXPECT(seq.tick == 192);
    EXPECT(fz_seq_bar_ticks() == 384);
    project.pattern[0].length_ticks = 12;
    EXPECT(fz_seq_pattern_ticks(&project, 0) == 288);
    EXPECT(fz_seq_pattern_ticks(&project, 0) != fz_seq_bar_ticks());

    fz_seq_init(&seq);
    EXPECT(fz_seq_toggle_step(&seq, 0, 0, 36, 100) == FZ_OK);
    EXPECT(fz_seq_count_pattern(&seq, 0) == 1);
    EXPECT(seq.ev[0].tick == 0 || fz_seq_count_pattern(&seq, 0) == 1);
    EXPECT(fz_seq_toggle_step(&seq, 0, 1, 36, 90) == FZ_OK);
    {
        int found = 0;
        for (i = 0; i < FZ_SEQ_EVENTS; i++) {
            if (seq.ev[i].used && seq.ev[i].tick == 24) {
                found = 1;
                EXPECT(fz_seq_playback_tick(seq.ev[i].tick, 50) > seq.ev[i].tick);
                EXPECT(seq.ev[i].tick == 24);
                EXPECT(fz_seq_quantize_tick(seq.ev[i].tick + 3, 24) == 24);
                EXPECT(seq.ev[i].tick == 24);
            }
        }
        EXPECT(found == 1);
    }
    EXPECT(fz_seq_toggle_step(&seq, 0, 0, 36, 100) == FZ_OK);
    EXPECT(fz_seq_count_pattern(&seq, 0) == 1);
    EXPECT(fz_seq_undo(&seq) == FZ_OK);
    EXPECT(fz_seq_count_pattern(&seq, 0) == 2);
    EXPECT(fz_instrument(&project, inst)->generation == generation);

    fz_seq_init(&seq);
    EXPECT(fz_seq_chain_range(&seq, 0, 1, 3) == FZ_OK);
    EXPECT(seq.chain_n[0] == 3 && seq.chain[0][0] == 1 && seq.chain[0][2] == 3);
    EXPECT(seq.current[0] == 1);
    chain[0] = 0;
    chain[1] = 0;
    chain[2] = 2;
    chain[3] = 1;
    EXPECT(fz_seq_chain_set(&seq, 0, chain, 4) == FZ_OK);
    EXPECT(seq.current[0] == 0);
    fz_seq_play(&seq, 1);
    fz_seq_advance(&seq, &project, 72000);
    EXPECT(seq.current[0] == 0);
    fz_seq_advance(&seq, &project, 72000);
    EXPECT(seq.current[0] == 2);
    fz_seq_advance(&seq, &project, 72000);
    EXPECT(seq.current[0] == 1);

    fz_seq_init(&seq);
    fz_seq_play(&seq, 1);
    seq.current[0] = 4;
    EXPECT(fz_seq_select_pattern(&seq, 0, 6, 0) == FZ_OK);
    EXPECT(seq.current[0] == 4);
    fz_seq_advance(&seq, &project, 54000);
    EXPECT(seq.current[0] == 4);
    fz_seq_advance(&seq, &project, 18000);
    EXPECT(seq.current[0] == 6);

    memset(patterns, 0, sizeof patterns);
    memset(mutes, 0, sizeof mutes);
    patterns[0] = 1;
    EXPECT(fz_seq_song_set(&seq, 0, patterns, mutes, 1) == FZ_OK);
    patterns[0] = 3;
    mutes[1] = 1;
    EXPECT(fz_seq_song_set(&seq, 1, patterns, mutes, 1) == FZ_OK);
    fz_seq_init(&seq);
    patterns[0] = 1;
    mutes[1] = 0;
    EXPECT(fz_seq_song_set(&seq, 0, patterns, mutes, 1) == FZ_OK);
    patterns[0] = 3;
    EXPECT(fz_seq_song_set(&seq, 1, patterns, mutes, 1) == FZ_OK);
    fz_seq_song_start(&seq);
    EXPECT(seq.current[0] == 1);
    fz_seq_play(&seq, 1);
    fz_seq_advance(&seq, &project, 72000);
    EXPECT(seq.current[0] == 3);
    EXPECT(seq.playing == 1);
    fz_seq_play(&seq, 0);
    EXPECT(seq.playing == 0);

    fz_seq_init(&seq);
    project.tempo_bpm = 100.0;
    project.has_music = 0;
    project.external_clock = 0;
    EXPECT(fz_seq_rec_tap(&seq, &project, 0, 0) == FZ_OK);
    EXPECT(seq.armed == 1);
    EXPECT(fz_seq_perform(&seq, 36, 100, 0) == FZ_OK);
    EXPECT(fz_seq_perform(&seq, 40, 90, 18000) == FZ_OK);
    EXPECT(fz_seq_rec_tap(&seq, &project, 0, 72000) == FZ_OK);
    EXPECT(project.tempo_bpm > 110.0 && project.tempo_bpm < 130.0);
    EXPECT(fz_seq_count_pattern(&seq, 0) >= 2);
    EXPECT(fz_instrument(&project, inst)->alive == 1);
    EXPECT(fz_seq_undo(&seq) == FZ_OK);
    EXPECT(fz_seq_count_pattern(&seq, 0) == 0);
    EXPECT(fz_instrument(&project, inst)->generation == generation);

    project.has_music = 1;
    project.tempo_bpm = 100.0;
    fz_seq_init(&seq);
    fz_seq_rec_tap(&seq, &project, 0, 0);
    fz_seq_perform(&seq, 36, 100, 0);
    fz_seq_rec_tap(&seq, &project, 0, 72000);
    EXPECT(project.tempo_bpm == 100.0);

    fz_history_init(&history);
    ev.track = 0;
    ev.source = FZ_SRC_KEYBOARD;
    ev.note = 48;
    ev.velocity = 100;
    ev.time_us = 1000;
    fz_history_push(&history, ev);
    ev.source = FZ_SRC_SEQUENCER;
    ev.note = 50;
    ev.time_us = 2000;
    fz_history_push(&history, ev);
    ev.source = FZ_SRC_KEYBOARD;
    ev.note = 48;
    ev.velocity = 0;
    ev.time_us = 400000;
    fz_history_push(&history, ev);
    ev.note = 52;
    ev.velocity = 80;
    ev.time_us = 900000;
    fz_history_push(&history, ev);
    fz_seq_init(&seq);
    EXPECT(fz_seq_capture_history(&seq, &project, &history, 0, 500000, 500000) == FZ_OK);
    EXPECT(fz_seq_count_pattern(&seq, 0) == 1);
    EXPECT(fz_instrument(&project, inst)->generation == generation);
    seq.gate_n = 4;
    fz_seq_play(&seq, 1);
    fz_seq_advance(&seq, &project, 72000);
    EXPECT(seq.gate_n == 0);
}

static void test_import_journal_bundle(void)
{
    static FzProject project;
    static FzProject restored;
    static int16_t store[4000];
    static int16_t store_b[4000];
    uint8_t rom[1024];
    uint8_t junk[64];
    uint8_t wav[64 + 600 * 2];
    uint8_t journal[256];
    uint8_t bundle[8192];
    int16_t cycle[600];
    FzId inst, track;
    uint32_t points = 0, rate = 0, used = 0, len = 0;
    const uint8_t *payload = 0;
    uint32_t plen = 0;
    int i;

    for (i = 0; i < 1024; i++)
        rom[i] = (uint8_t)(128 + (i < 256 ? 10 : 0));
    fz_project_init(&project, store, 4000);
    EXPECT(fz_project_new_instrument(&project, FZ_INST_MULTI, &inst, &track) == FZ_OK);
    EXPECT(fz_import_rom(&project, inst, "NO-SUCH", rom, 1024) == FZ_ERR_FORMAT);
    EXPECT(fz_instrument(&project, inst)->region_count == 0);
    EXPECT(fz_import_rom(&project, inst, "FZTEST", rom, 1023) == FZ_ERR_FORMAT);
    EXPECT(fz_instrument(&project, inst)->region_count == 0);
    EXPECT(fz_import_rom(&project, inst, "FZTEST", rom, 1024) == FZ_OK);
    EXPECT(fz_instrument(&project, inst)->region_count == 4);
    EXPECT(fz_instrument_region_for_note(&project, inst, 36)->root == 36);
    EXPECT(fz_instrument_region_for_note(&project, inst, 39)->root == 39);
    {
        FzAsset *asset = fz_asset(&project, fz_region(&project, fz_instrument(&project, inst)->region[0])->asset);
        EXPECT(asset && asset->native_rate == 8000 && asset->pcm[0] == 2560);
    }
    for (i = 0; i < 40; i++) {
        junk[i % 64] = (uint8_t)(i * 13 + 1);
        EXPECT(fz_import_rom(&project, inst, "FZTEST", junk, (uint32_t)(i % 64) + 1u) == FZ_ERR_FORMAT);
        EXPECT(fz_import_wav_cycle(junk, (uint32_t)(i % 48) + 1u, cycle, 600, &points, &rate) == FZ_ERR_FORMAT);
    }
    EXPECT(fz_instrument(&project, inst)->region_count == 4);

    memset(wav, 0, sizeof wav);
    memcpy(wav, "RIFF", 4);
    memcpy(wav + 8, "WAVE", 4);
    memcpy(wav + 12, "fmt ", 4);
    wav[16] = 16;
    wav[20] = 1;
    wav[22] = 1;
    wav[24] = 0x44;
    wav[25] = 0xAC;
    wav[34] = 16;
    memcpy(wav + 36, "data", 4);
    wav[40] = (uint8_t)((600 * 2) & 255);
    wav[41] = (uint8_t)((600 * 2) >> 8);
    for (i = 0; i < 600; i++) {
        wav[44 + i * 2] = 0xE8;
        wav[45 + i * 2] = 0x03;
    }
    {
        uint32_t riff = 36u + 600u * 2u;
        wav[4] = (uint8_t)riff;
        wav[5] = (uint8_t)(riff >> 8);
    }
    EXPECT(fz_import_wav_cycle(wav, 44u + 600u * 2u, cycle, 600, &points, &rate) == FZ_OK);
    EXPECT(points == 600 && rate == 44100);
    EXPECT(cycle[0] == 1000);
    EXPECT(rate != 69);
    wav[40] = 10;
    EXPECT(fz_import_wav_cycle(wav, 20, cycle, 600, &points, &rate) == FZ_ERR_FORMAT);
    wav[40] = 0xF0;
    wav[41] = 0xFF;
    wav[42] = 0xFF;
    wav[43] = 0xFF;
    EXPECT(fz_import_wav_cycle(wav, 80, cycle, 600, &points, &rate) == FZ_ERR_FORMAT);

    used = 0;
    EXPECT(fz_journal_append(journal, sizeof journal, &used, "AAA", 3) == FZ_OK);
    {
        uint32_t good = used;
        memset(journal + used, 0, 19);
        journal[used] = 0x52;
        journal[used + 1] = 0x4A;
        journal[used + 2] = 0x5A;
        journal[used + 3] = 0x46;
        journal[used + 4] = 3;
        memcpy(journal + used + 16, "BBB", 3);
        EXPECT(fz_journal_recover(journal, good + 19) == good);
        EXPECT(fz_journal_count(journal, good + 19) == 1);
        EXPECT(fz_journal_payload(journal, good, 0, &payload, &plen) == FZ_OK);
        EXPECT(plen == 3 && payload[0] == 'A');
    }
    EXPECT(fz_journal_append(journal, 8, &used, "NO", 2) == FZ_ERR_FULL);

    project.track[track.index].volume = 41;
    project.track[track.index].send[2] = 15;
    fz_region(&project, fz_instrument(&project, inst)->region[0])->send_trim[2] = 9;
    EXPECT(fz_bundle_write(&project, bundle, sizeof bundle, &len) == FZ_OK);
    fz_project_init(&restored, store_b, 4000);
    EXPECT(fz_bundle_read(&restored, bundle, len) == FZ_OK);
    EXPECT(restored.track[0].volume == 41);
    EXPECT(restored.track[0].send[2] == 15);
    EXPECT(restored.instrument[0].region_count == 4);
    EXPECT(fz_region(&restored, restored.instrument[0].region[0])->send_trim[2] == 9);
    bundle[len - 1] ^= 0xFF;
    EXPECT(fz_instrument(&project, inst)->region_count == 4);
    EXPECT(fz_bundle_read(&project, bundle, len) == FZ_ERR_FORMAT);
    EXPECT(fz_instrument(&project, inst)->region_count == 4);
    EXPECT(project.track[track.index].volume == 41);
}

static void test_capture_command_and_mix(void)
{
    static FzProject project;
    static int16_t store[4000];
    static int16_t take[1000];
    static int16_t dst[64];
    FzCapture cap;
    FzSession session;
    FzInput input;
    FzCommand cmd;
    FzId inst, track, asset, a, b, mixed;
    int16_t pcm[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    int16_t buf[16];
    int i;
    FzSession mix_session;

    fz_project_init(&project, store, 4000);
    EXPECT(fz_project_new_instrument(&project, FZ_INST_MULTI, &inst, &track) == FZ_OK);
    fz_capture_init(&cap, &project, inst, track, take, 1000);
    fz_capture_set_thresholds(&cap, -36, -42, 2, 2, 1);
    cap.tail_keep = 0;
    cap.octave = 2;
    fz_capture_arm(&cap);
    for (i = 0; i < 6; i++)
        buf[i] = 6000;
    fz_capture_accept(&cap, buf, 6);
    for (i = 0; i < 2; i++)
        buf[i] = 0;
    fz_capture_accept(&cap, buf, 2);
    fz_capture_service(&cap);
    EXPECT(fz_instrument(&project, inst)->region_count == 1);

    fz_input_init(&input);
    fz_session_init(&session);
    session.screen = FZ_SCR_CAPTURE;
    session.octave = 4;
    hold(&input, &session, FZ_BTN_OCT_UP, 10, &cmd);
    EXPECT(cmd.kind == FZ_CMD_CAPTURE_UNDO);
    fz_session_apply(&session, &project, &cap, 0, &cmd);
    EXPECT(fz_instrument(&project, inst)->region_count == 0);
    EXPECT(cap.octave == 2 && session.octave == 4 && session.screen == FZ_SCR_CAPTURE);

    for (i = 0; i < 6; i++)
        buf[i] = 5000;
    fz_capture_accept(&cap, buf, 6);
    for (i = 0; i < 2; i++)
        buf[i] = 0;
    fz_capture_accept(&cap, buf, 2);
    fz_capture_service(&cap);
    tap(&input, &session, FZ_BTN_OCT_UP, 20, &cmd);
    EXPECT(cmd.kind == FZ_CMD_CAPTURE_FINISH && session.octave == 4);
    fz_session_apply(&session, &project, &cap, 0, &cmd);
    EXPECT(cap.state == FZ_CAP_PERFORMANCE && cap.octave == 2);
    EXPECT(fz_instrument(&project, inst)->region_count == 1);

    EXPECT(fz_project_add_pcm(&project, pcm, 8, FZ_ENGINE_RATE_HZ, 0, &asset) == FZ_OK);
    EXPECT(fz_project_add_region(&project, inst, asset, 50, 2, &a) == FZ_OK);
    EXPECT(fz_project_add_pcm(&project, pcm, 8, FZ_ENGINE_RATE_HZ, 0, &asset) == FZ_OK);
    EXPECT(fz_project_add_region(&project, inst, asset, 51, 3, &b) == FZ_OK);
    EXPECT(fz_edit_mix(&project, a, b, 32767, 32767, 0, dst, 64, &mixed) == FZ_OK);
    EXPECT(fz_asset(&project, mixed)->frames == 8);
    EXPECT(fz_asset(&project, fz_region(&project, a)->asset)->pcm[0] == 1);
    EXPECT(fz_edit_truncate(&project, a, 1, 4) == FZ_OK);
    EXPECT(fz_asset(&project, fz_region(&project, a)->asset)->frames == 3);
    EXPECT(fz_project_undo(&project) == FZ_OK);
    EXPECT(fz_asset(&project, fz_region(&project, a)->asset)->frames == 8);
    EXPECT(fz_edit_reverse_write(&project, a, dst, 64) == FZ_OK);
    EXPECT(fz_region(&project, a)->reverse == 0);
    EXPECT(fz_asset(&project, fz_region(&project, a)->asset)->pcm[0] == 8);
    {
        int16_t cut[4];
        int16_t saved = fz_asset(&project, fz_region(&project, b)->asset)->pcm[0];
        FzId cycle;
        EXPECT(fz_edit_cut_cycle(&project, b, 2, 4, cut, &cycle) == FZ_OK);
        EXPECT(fz_asset(&project, cycle)->cycle == 1);
        EXPECT(fz_asset(&project, cycle)->frames == 4);
        EXPECT(fz_asset(&project, fz_region(&project, b)->asset)->pcm[0] == saved);
        EXPECT(fz_edit_preset(&project, b, 0, dst, 32) == FZ_OK);
        EXPECT(fz_asset(&project, fz_region(&project, b)->asset)->cycle == 1);
    }

    fz_session_init(&mix_session);
    mix_session.screen = FZ_SCR_MIXER;
    project.selected_track = (int)track.index;
    fz_session_knob(&mix_session, &project, 1, -10);
    EXPECT(project.track[track.index].volume == 90);
    mix_session.send_slot = 2;
    fz_session_knob(&mix_session, &project, 3, 6);
    EXPECT(project.track[track.index].send[2] == 6);
    mix_session.screen = FZ_SCR_HOME;
    fz_session_knob(&mix_session, &project, 2, 5);
    EXPECT(project.track[track.index].volume == 95);

    {
        FzSeq seq;
        FzInput play_in;
        fz_seq_init(&seq);
        fz_input_init(&play_in);
        fz_session_init(&session);
        tap(&play_in, &session, FZ_BTN_PLAY, 1, &cmd);
        fz_session_apply(&session, &project, 0, &seq, &cmd);
        EXPECT(seq.playing == 1);
        tap(&play_in, &session, FZ_BTN_SEQ, 2, &cmd);
        EXPECT(session.screen == FZ_SCR_STEP);
        EXPECT(seq.playing == 1);
        tap(&play_in, &session, FZ_BTN_SEQ, 3, &cmd);
        EXPECT(session.screen == FZ_SCR_PATTERNS);
        tap(&play_in, &session, FZ_BTN_GLO, 4, &cmd);
        EXPECT(session.screen == FZ_SCR_MIXER);
        tap(&play_in, &session, FZ_BTN_GLO, 5, &cmd);
        EXPECT(session.send_slot == 1);
        tap(&play_in, &session, FZ_BTN_FX, 6, &cmd);
        tap(&play_in, &session, FZ_BTN_FX, 7, &cmd);
        EXPECT(session.fx_slot == 1);
        EXPECT(seq.playing == 1);
    }
}

static void test_lfo_targets(void)
{
    FzEngine dry, pitched, panned;
    int16_t pcm[8] = {8000, 1000, 4000, 8000, 8000, 4000, 1000, 0};
    int16_t a[8], b[8], c[8];

    fz_engine_init(&dry);
    fz_engine_set_filter(&dry, 0, 0, 0, 0);
    EXPECT(fz_engine_note_on_pcm(&dry, pcm, 8, FZ_ENGINE_RATE_HZ, 60, 60, 0, 127, 127, 0, 1, 0, 1) >= 0);
    fz_engine_render(&dry, a, 4);
    EXPECT(a[1] == 0);

    fz_engine_init(&pitched);
    fz_engine_set_filter(&pitched, 0, 0, 0, 0);
    EXPECT(fz_engine_note_on_pcm(&pitched, pcm, 8, FZ_ENGINE_RATE_HZ, 60, 60, 0, 127, 127, 0, 1, 0, 1) >= 0);
    fz_engine_set_lfo(&pitched, 127, 127, 1, FZ_LFO_PITCH, 0);
    fz_engine_render(&pitched, b, 4);
    EXPECT(b[0] == a[0]);
    EXPECT(b[2] != a[2]);

    fz_engine_init(&panned);
    fz_engine_set_filter(&panned, 0, 0, 0, 0);
    EXPECT(fz_engine_note_on_pcm(&panned, pcm, 8, FZ_ENGINE_RATE_HZ, 60, 60, 0, 127, 127, 0, 1, 0, 1) >= 0);
    fz_engine_set_lfo(&panned, 127, 127, 1, FZ_LFO_PAN, 0);
    fz_engine_render(&panned, c, 4);
    EXPECT(c[1] != 0);

    EXPECT(fz_lfo_bipolar(0, FZ_LFO_TRI) == -127);
    EXPECT(fz_lfo_bipolar(0x80000000u, FZ_LFO_TRI) == 127);
    EXPECT(fz_lfo_bipolar(0, FZ_LFO_SQR) == 127);
    EXPECT(fz_lfo_bipolar(0x80000000u, FZ_LFO_SQR) == -127);
    EXPECT(fz_lfo_bipolar(0, FZ_LFO_SIN) == 0);
    EXPECT(fz_lfo_bipolar(0x40000000u, FZ_LFO_SIN) == 127);
    EXPECT(fz_lfo_bipolar(0xC0000000u, FZ_LFO_SIN) == -127);
    EXPECT(fz_lfo_bipolar(0, FZ_LFO_SAW) == -127);
    EXPECT(fz_lfo_bipolar(0, FZ_LFO_SH) != fz_lfo_bipolar(0x10000000u, FZ_LFO_SH));
    EXPECT(strcmp(fz_lfo_shape_name(FZ_LFO_SIN), "SIN") == 0);
    EXPECT(strcmp(fz_lfo_shape_name(99), "TRI") == 0);
    {
        FzEngine sine, saw;
        int16_t tone[4] = {8000, 8000, 8000, 8000};
        int16_t s[8], w[8];
        int i, differ = 0;
        fz_engine_init(&sine);
        fz_engine_init(&saw);
        EXPECT(fz_engine_note_on_pcm(&sine, tone, 4, FZ_ENGINE_RATE_HZ, 60, 60, 0, 127, 127, 0, 1, 0, 1) >= 0);
        EXPECT(fz_engine_note_on_pcm(&saw, tone, 4, FZ_ENGINE_RATE_HZ, 60, 60, 0, 127, 127, 0, 1, 0, 1) >= 0);
        fz_engine_set_lfo(&sine, 127, 127, FZ_LFO_SIN, FZ_LFO_LEVEL, 0);
        fz_engine_set_lfo(&saw, 127, 127, FZ_LFO_SAW, FZ_LFO_LEVEL, 0);
        fz_engine_render(&sine, s, 4);
        fz_engine_render(&saw, w, 4);
        for (i = 0; i < 8; i += 2) {
            if (s[i] != w[i])
                differ = 1;
        }
        EXPECT(differ == 1);
    }
}

static void test_cycle_draw(void)
{
    static FzProject project;
    static int16_t store[4000];
    FzSession session;
    FzId inst, track, asset, region;
    int16_t table[32];
    FzAsset *got;
    int i;
    fz_project_init(&project, store, 4000);
    EXPECT(fz_project_new_instrument(&project, FZ_INST_SAMPLE, &inst, &track) == FZ_OK);
    for (i = 0; i < 32; i++)
        table[i] = (int16_t)(i == 0 ? 100 : 0);
    EXPECT(fz_project_add_pcm(&project, table, 32, FZ_ENGINE_RATE_HZ, 1, &asset) == FZ_OK);
    EXPECT(fz_project_add_region(&project, inst, asset, 60, 0, &region) == FZ_OK);
    fz_session_init(&session);
    session.screen = FZ_SCR_EDIT;
    session.edit_tab = FZ_TAB_DRAW;
    session.row[FZ_TAB_DRAW] = 0;
    EXPECT(fz_edit_knob(&session, &project, 2, 1) == FZ_OK);
    got = fz_asset(&project, asset);
    EXPECT(got != 0 && got->pcm[0] == 1124);
    session.row[FZ_TAB_DRAW] = 1;
    EXPECT(fz_edit_knob(&session, &project, 1, 1) == FZ_OK);
    got = fz_asset(&project, asset);
    EXPECT(got->pcm[0] == 28000);
    session.cycle_inst = 0;
    EXPECT(fz_edit_next_tab(0, FZ_TAB_SAMPLE) == FZ_TAB_LOOP);
    EXPECT(fz_edit_next_tab(0, FZ_TAB_MIX) == FZ_TAB_FILTER);
    EXPECT(fz_edit_next_tab(0, FZ_TAB_FILTER) == FZ_TAB_WAVE);
    EXPECT(fz_edit_next_tab(1, FZ_TAB_WAVE) == FZ_TAB_DRAW);
    EXPECT(fz_edit_next_tab(1, FZ_TAB_DRAW) == FZ_TAB_FILTER);
    EXPECT(fz_edit_next_tab(1, FZ_TAB_AMP) == FZ_TAB_WAVE);
    EXPECT(fz_edit_tab_on_cycle(FZ_TAB_LOOP) == 0);
    EXPECT(fz_edit_tab_on_cycle(FZ_TAB_AMP) == 0);
    EXPECT(fz_edit_tab_on_cycle(FZ_TAB_MOD) == 0);
    EXPECT(fz_edit_tab_on_cycle(FZ_TAB_DRAW) == 1);
    EXPECT(fz_edit_row_count(FZ_TAB_FILTER) == 2);
    EXPECT(strcmp(fz_edit_row_name(FZ_TAB_AMP, 8), "FLT AMT") == 0);
}

static void test_cycle_presets(void)
{
    static FzProject project;
    static int16_t store[4096];
    static int16_t wave[FZ_ADDITIVE_POINTS];
    FzId inst, track, asset, region;
    FzInstrument *it;
    int i;

    fz_project_init(&project, store, 4096);
    EXPECT(fz_project_new_instrument(&project, FZ_INST_SAMPLE, &inst, &track) == FZ_OK);
    for (i = 0; i < 8; i++)
        wave[i] = 1;
    EXPECT(fz_project_add_pcm(&project, wave, 8, FZ_ENGINE_RATE_HZ, 0, &asset) == FZ_OK);
    EXPECT(fz_project_add_region(&project, inst, asset, 60, 0, &region) == FZ_OK);
    EXPECT(fz_edit_cycle_preset(&project, 1) == FZ_ERR_STATE);
    EXPECT(fz_asset(&project, asset)->pcm[0] == 1);

    fz_project_init(&project, store, 4096);
    EXPECT(fz_project_new_instrument(&project, FZ_INST_SAMPLE, &inst, &track) == FZ_OK);
    for (i = 0; i < FZ_ADDITIVE_POINTS; i++)
        wave[i] = 0;
    wave[0] = 100;
    EXPECT(fz_project_add_pcm(&project, wave, FZ_ADDITIVE_POINTS, FZ_ENGINE_RATE_HZ, 1, &asset) == FZ_OK);
    EXPECT(fz_project_add_region(&project, inst, asset, 60, 0, &region) == FZ_OK);
    EXPECT(fz_cycle_preset_count() == 4);
    EXPECT(strcmp(fz_cycle_preset_name(0), "SINE") == 0);
    EXPECT(strcmp(fz_cycle_preset_name(1), "SAW") == 0);
    EXPECT(strcmp(fz_cycle_preset_name(2), "SQUARE") == 0);
    EXPECT(strcmp(fz_cycle_preset_name(3), "TRI") == 0);
    EXPECT(fz_edit_cycle_preset(&project, 0) == FZ_OK);
    it = fz_instrument(&project, inst);
    EXPECT(it && it->additive.level[0] == 256 && it->additive.level[1] == 0);
    EXPECT(fz_edit_cycle_preset(&project, 1) == FZ_OK);
    EXPECT(it->additive.level[1] == 128);
    EXPECT(fz_edit_cycle_preset(&project, 2) == FZ_OK);
    EXPECT(it->additive.level[1] == 0);
    EXPECT(it->additive.level[2] == (uint16_t)(256 / 3));
    EXPECT(fz_edit_cycle_preset(&project, 3) == FZ_OK);
    EXPECT(it->additive.level[2] == (uint16_t)(256 / 9));
    EXPECT(fz_asset(&project, it->region[0])->cycle == 1);
}

static void test_arp(void)
{
    FzArp arp;
    FzArpEvent ev[16];
    int notes[8];
    int n, i, cursor;
    FzSession session;
    FzInput input;
    FzCommand cmd;

    fz_arp_init(&arp);
    EXPECT(arp.mode == FZ_ARP_OFF);
    EXPECT(arp.rate == FZ_ARP_1_16);
    EXPECT(arp.gate == 80);
    EXPECT(arp.prob == 100);
    fz_arp_key(&arp, 64, 1);
    fz_arp_key(&arp, 60, 1);
    fz_arp_turn(&arp, 0, 0, 1);
    EXPECT(arp.mode == FZ_ARP_UP);
    n = fz_arp_advance(&arp, 0, 120000, ev, 16);
    EXPECT(n == 1 && ev[0].on == 1 && ev[0].note == 60);
    cursor = -2;
    n = fz_arp_pattern(&arp, notes, 8, &cursor);
    EXPECT(n == 2 && notes[0] == 60 && notes[1] == 64 && cursor == 0);
    n = fz_arp_advance(&arp, 4500, 120000, ev, 16);
    EXPECT(n == 2);
    EXPECT(ev[0].on == 0 && ev[0].note == 60);
    EXPECT(ev[1].on == 1 && ev[1].note == 64);

    fz_arp_init(&arp);
    fz_arp_key(&arp, 60, 1);
    fz_arp_key(&arp, 64, 1);
    fz_arp_key(&arp, 67, 1);
    fz_arp_turn(&arp, 0, 0, 1);
    fz_arp_turn(&arp, 0, 0, 1);
    EXPECT(arp.mode == FZ_ARP_DN);
    n = fz_arp_advance(&arp, 0, 120000, ev, 16);
    EXPECT(n == 1 && ev[0].on == 1 && ev[0].note == 67);

    fz_arp_init(&arp);
    fz_arp_key(&arp, 64, 1);
    fz_arp_key(&arp, 60, 1);
    fz_arp_turn(&arp, 0, 2, 1);
    EXPECT(arp.octaves == 2);
    n = fz_arp_pattern(&arp, notes, 8, 0);
    EXPECT(n == 4);
    EXPECT(notes[0] == 60 && notes[1] == 64 && notes[2] == 72 && notes[3] == 76);

    fz_arp_init(&arp);
    fz_arp_key(&arp, 67, 1);
    fz_arp_key(&arp, 60, 1);
    fz_arp_key(&arp, 64, 1);
    fz_arp_turn(&arp, 0, 0, 1);
    fz_arp_turn(&arp, 0, 0, 1);
    fz_arp_turn(&arp, 0, 0, 1);
    EXPECT(strcmp(fz_arp_mode_name(arp.mode), "UPDN") == 0);
    n = fz_arp_pattern(&arp, notes, 8, 0);
    EXPECT(n == 4);
    EXPECT(notes[0] == 60 && notes[1] == 64 && notes[2] == 67 && notes[3] == 64);
    EXPECT(fz_arp_advance(&arp, 0, 120000, ev, 16) == 1);
    EXPECT(ev[0].note == 60 && ev[0].on == 1);
    EXPECT(fz_arp_advance(&arp, 4500, 120000, ev, 16) == 2);
    EXPECT(ev[1].note == 64 && ev[1].on == 1);
    EXPECT(fz_arp_advance(&arp, 4500, 120000, ev, 16) == 2);
    EXPECT(ev[1].note == 67 && ev[1].on == 1);
    EXPECT(fz_arp_advance(&arp, 4500, 120000, ev, 16) == 2);
    EXPECT(ev[1].note == 64 && ev[1].on == 1);
    EXPECT(fz_arp_advance(&arp, 4500, 120000, ev, 16) == 2);
    EXPECT(ev[1].note == 60 && ev[1].on == 1);

    fz_arp_init(&arp);
    fz_arp_turn(&arp, 1, 3, 1);
    EXPECT(arp.order_play == 1);
    fz_arp_key(&arp, 64, 1);
    fz_arp_key(&arp, 60, 1);
    for (i = 0; i < 5; i++)
        fz_arp_turn(&arp, 0, 0, 1);
    EXPECT(arp.mode == FZ_ARP_ORD);
    n = fz_arp_advance(&arp, 0, 120000, ev, 16);
    EXPECT(n == 1 && ev[0].on == 1 && ev[0].note == 64);
    fz_arp_init(&arp);
    fz_arp_key(&arp, 64, 1);
    fz_arp_key(&arp, 60, 1);
    for (i = 0; i < 5; i++)
        fz_arp_turn(&arp, 0, 0, 1);
    EXPECT(arp.order_play == 0);
    n = fz_arp_advance(&arp, 0, 120000, ev, 16);
    EXPECT(n == 1 && ev[0].on == 1 && ev[0].note == 60);

    fz_arp_init(&arp);
    fz_arp_turn(&arp, 1, 2, 1);
    EXPECT(arp.hold == 1);
    fz_arp_key(&arp, 60, 1);
    fz_arp_turn(&arp, 0, 0, 1);
    EXPECT(fz_arp_advance(&arp, 0, 120000, ev, 16) == 1);
    fz_arp_key(&arp, 60, 0);
    n = fz_arp_advance(&arp, 4500, 120000, ev, 16);
    EXPECT(n == 2 && ev[1].on == 1 && ev[1].note == 60);
    fz_arp_key(&arp, 64, 1);
    n = fz_arp_advance(&arp, 0, 120000, ev, 16);
    {
        int saw = 0;
        for (i = 0; i < n; i++) {
            if (ev[i].on)
                saw = ev[i].note;
        }
        EXPECT(saw == 64);
    }

    fz_arp_init(&arp);
    fz_arp_turn(&arp, 1, 1, -20);
    EXPECT(arp.prob == 0);
    fz_arp_key(&arp, 60, 1);
    fz_arp_turn(&arp, 0, 0, 1);
    n = fz_arp_advance(&arp, 4500, 120000, ev, 16);
    EXPECT(n == 0);
    EXPECT(arp.tick == 24);

    fz_arp_init(&arp);
    fz_arp_turn(&arp, 1, 0, 20);
    EXPECT(arp.swing == 100);
    fz_arp_key(&arp, 60, 1);
    fz_arp_key(&arp, 64, 1);
    fz_arp_turn(&arp, 0, 0, 1);
    EXPECT(fz_arp_advance(&arp, 0, 120000, ev, 16) == 1);
    EXPECT(ev[0].note == 60 && ev[0].on == 1);
    n = fz_arp_advance(&arp, 4500, 120000, ev, 16);
    EXPECT(n == 1 && ev[0].on == 0 && ev[0].note == 60);
    n = fz_arp_advance(&arp, 2250, 120000, ev, 16);
    EXPECT(n == 1 && ev[0].on == 1 && ev[0].note == 64);

    fz_arp_init(&arp);
    fz_arp_key(&arp, 60, 1);
    fz_arp_turn(&arp, 0, 0, 1);
    EXPECT(fz_arp_advance(&arp, 0, 120000, ev, 16) == 1);
    fz_arp_turn(&arp, 0, 0, -1);
    EXPECT(arp.mode == FZ_ARP_OFF);
    n = fz_arp_advance(&arp, 0, 120000, ev, 16);
    EXPECT(n == 1 && ev[0].on == 0 && ev[0].note == 60);
    EXPECT(fz_arp_advance(&arp, 4500, 120000, ev, 16) == 0);

    fz_arp_init(&arp);
    fz_arp_key(&arp, 60, 1);
    fz_arp_key(&arp, 64, 1);
    fz_arp_turn(&arp, 0, 0, 1);
    EXPECT(fz_arp_advance(&arp, 0, 120000, ev, 16) == 1);
    EXPECT(ev[0].note == 60);
    fz_arp_key(&arp, 60, 0);
    n = fz_arp_advance(&arp, 0, 120000, ev, 16);
    EXPECT(n == 2);
    EXPECT(ev[0].on == 0 && ev[0].note == 60);
    EXPECT(ev[1].on == 1 && ev[1].note == 64);

    EXPECT(strcmp(fz_arp_rate_name(FZ_ARP_1_8T), "1/8T") == 0);
    EXPECT(strcmp(fz_arp_mode_name(99), "OFF") == 0);
    EXPECT(fz_arp_advance(0, 1, 120000, ev, 16) == 0);
    fz_arp_turn(0, 0, 0, 1);

    fz_session_init(&session);
    fz_input_init(&input);
    fz_arp_init(&arp);
    session.arp = &arp;
    tap(&input, &session, FZ_BTN_ARP, 1000, &cmd);
    EXPECT(session.screen == FZ_SCR_ARP);
    EXPECT(session.arp_page == 0);
    EXPECT(cmd.kind == FZ_CMD_PAGE && cmd.value == 0);
    tap(&input, &session, FZ_BTN_ARP, 5000, &cmd);
    EXPECT(session.arp_page == 1);
    EXPECT(cmd.kind == FZ_CMD_PAGE && cmd.value == 1);
    fz_session_knob(&session, 0, 0, 1);
    EXPECT(arp.mode == FZ_ARP_OFF);
    EXPECT(arp.swing == 5);
    tap(&input, &session, FZ_BTN_ARP, 9000, &cmd);
    EXPECT(session.arp_page == 0);
    fz_session_knob(&session, 0, 0, 1);
    EXPECT(arp.mode == FZ_ARP_UP);
    tap(&input, &session, FZ_BTN_ARP, 13000, &cmd);
    fz_session_knob(&session, 0, 2, 1);
    EXPECT(arp.hold == 1);
    tap(&input, &session, FZ_BTN_HOME, 20000, &cmd);
    EXPECT(session.screen == FZ_SCR_HOME);
    EXPECT(arp.mode == FZ_ARP_UP);
}

static void test_bundle_v2(void)
{
    static FzProject project;
    static FzProject restored;
    static FzSeq seq;
    static FzSeq back;
    static int16_t store[4096];
    static int16_t store_b[4096];
    static uint8_t bundle[32768];
    static int16_t pcm[8] = {1000, -1000, 500, -500, 250, -250, 100, -100};
    FzId inst, track, asset, region;
    uint32_t len = 0;
    int regions;
    uint8_t patterns[FZ_MAX_TRACKS];
    uint8_t mutes[FZ_MAX_TRACKS];
    int t;

    fz_project_init(&project, store, 4096);
    EXPECT(fz_project_new_instrument(&project, FZ_INST_SAMPLE, &inst, &track) == FZ_OK);
    EXPECT(fz_project_add_pcm(&project, pcm, 8, 36000, 1, &asset) == FZ_OK);
    EXPECT(fz_project_add_region(&project, inst, asset, 60, 0, &region) == FZ_OK);
    EXPECT(fz_asset(&project, asset)->cycle == 1);
    fz_instrument(&project, inst)->additive.level[0] = 200;
    fz_instrument(&project, inst)->additive.points = 1024;
    project.track[track.index].lfo.rate = 40;
    project.track[track.index].lfo.depth = 12;
    project.track[track.index].lfo.shape = FZ_LFO_SIN;
    project.track[track.index].lfo.dest = FZ_LFO_CUTOFF;
    project.track[track.index].lfo.retrigger = 1;
    project.track[track.index].key_track = -7;
    project.pattern[0].length_ticks = 32;
    project.pattern[0].baked = 1;
    project.pattern[0].unsend_mask = 3;
    project.tempo_bpm = 90.0;

    fz_seq_init(&seq);
    fz_seq_set_tempo(&seq, 100000);
    seq.ev[0].used = 1;
    seq.ev[0].note = 64;
    seq.ev[0].velocity = 90;
    seq.ev[0].prob = 80;
    seq.ev[0].pattern = 0;
    seq.ev[0].duration = 24;
    seq.ev[0].tick = 48;
    seq.ev[0].micro = -3;
    seq.count = 1;
    seq.current[0] = 0;
    seq.swing = 20;
    seq.quantize_grid = 24;
    seq.playing = 1;
    for (t = 0; t < FZ_MAX_TRACKS; t++) {
        patterns[t] = 0;
        mutes[t] = 0;
    }
    patterns[0] = 2;
    mutes[1] = 1;
    EXPECT(fz_seq_song_set(&seq, 0, patterns, mutes, 4) == FZ_OK);
    EXPECT(fz_bundle_write_all(0, &seq, bundle, sizeof bundle, &len) == FZ_ERR_ARGS);
    EXPECT(fz_bundle_write_all(&project, &seq, bundle, sizeof bundle, &len) == FZ_OK);
    EXPECT(len > 32);

    regions = fz_instrument(&project, inst)->region_count;
    EXPECT(fz_bundle_read(&project, bundle, len) == FZ_ERR_FORMAT);
    EXPECT(fz_instrument(&project, inst)->region_count == regions);
    EXPECT(fz_asset(&project, asset)->pcm[0] == 1000);
    EXPECT(fz_asset(&project, asset)->cycle == 1);

    fz_project_init(&restored, store_b, 4096);
    fz_seq_init(&back);
    back.ev[0].note = 7;
    back.ev[0].used = 1;
    back.count = 9;
    EXPECT(fz_bundle_read_all(&restored, &back, bundle, len) == FZ_OK);
    {
        FzRegion *reg = fz_region(&restored, restored.instrument[0].region[0]);
        FzAsset *got = reg ? fz_asset(&restored, reg->asset) : 0;
        EXPECT(got != 0);
        EXPECT(got->cycle == 1);
        EXPECT(got->frames == 8);
        EXPECT(got->pcm[0] == 1000);
    }
    EXPECT(restored.instrument[0].additive.level[0] == 200);
    EXPECT(restored.instrument[0].additive.points == 1024);
    EXPECT(restored.track[0].lfo.dest == FZ_LFO_CUTOFF);
    EXPECT(restored.track[0].lfo.rate == 40);
    EXPECT(restored.track[0].lfo.shape == FZ_LFO_SIN);
    EXPECT(restored.track[0].lfo.retrigger == 1);
    EXPECT(restored.track[0].key_track == -7);
    EXPECT(restored.pattern[0].length_ticks == 32);
    EXPECT(restored.pattern[0].baked == 1);
    EXPECT(restored.pattern[0].unsend_mask == 3);
    EXPECT(back.count == 1);
    EXPECT(back.ev[0].used == 1);
    EXPECT(back.ev[0].note == 64);
    EXPECT(back.ev[0].velocity == 90);
    EXPECT(back.ev[0].prob == 80);
    EXPECT(back.ev[0].tick == 48);
    EXPECT(back.ev[0].duration == 24);
    EXPECT(back.ev[0].micro == -3);
    EXPECT(back.bpm_milli == 100000);
    EXPECT(restored.tempo_bpm > 99.9 && restored.tempo_bpm < 100.1);
    EXPECT(back.song_n == 1);
    EXPECT(back.song[0].pattern[0] == 2);
    EXPECT(back.song[0].mute[1] == 1);
    EXPECT(back.song[0].repeats == 4);
    EXPECT(back.current[0] == 0);
    EXPECT(back.current[1] == -1);
    EXPECT(back.swing == 20);
    EXPECT(back.quantize_grid == 24);
    EXPECT(back.playing == 0);

    bundle[len - 1] ^= 0xFF;
    EXPECT(fz_bundle_read_all(&restored, &back, bundle, len) == FZ_ERR_FORMAT);
    EXPECT(restored.instrument[0].region_count == 1);
    EXPECT(back.ev[0].note == 64);
    bundle[len - 1] ^= 0xFF;
    EXPECT(fz_bundle_read_all(&restored, &back, bundle, len - 1) == FZ_ERR_FORMAT);
    EXPECT(restored.instrument[0].region_count == 1);
    EXPECT(back.bpm_milli == 100000);

    project.tempo_bpm = 90.0;
    EXPECT(fz_bundle_write(&project, bundle, sizeof bundle, &len) == FZ_OK);
    back.ev[0].used = 1;
    back.ev[0].note = 3;
    back.count = 4;
    back.bpm_milli = 150000;
    fz_project_init(&restored, store_b, 4096);
    EXPECT(fz_bundle_read_all(&restored, &back, bundle, len) == FZ_OK);
    EXPECT(back.ev[0].used == 0);
    EXPECT(back.count == 0);
    EXPECT(back.playing == 0);
    EXPECT(back.bpm_milli == 90000);
    EXPECT(restored.track[0].volume == project.track[track.index].volume);
}

void test_product(void)
{
    test_oct_plus_router();
    test_cycle_draw();
    test_cycle_presets();
    test_arp();
    test_edit_scope_and_undo();
    test_lfo_targets();
    test_mangle_loop_env();
    test_filter_playback();
    test_fx_and_bake();
    test_seq();
    test_import_journal_bundle();
    test_bundle_v2();
    test_capture_command_and_mix();
}
