/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "sim.h"

#include "audio/play.h"
#include "dsp/additive.h"
#include "dsp/fx.h"
#include "dsp/mangle.h"
#include "sequence/history.h"
#include "ui/edit.h"
#include "ui/input.h"
#include "ui/screen.h"

#include <stdio.h>
#include <string.h>

#define SIM_PCM 72000
#define SIM_KEYS 27

static FzProject project;
static int16_t pcm[SIM_PCM];
static int16_t take[4096];
static FzSession session;
static FzInput input;
static FzSeq seq;
static FzCapture capture;
static FzEngine engine;
static FzFxRack rack;
static FzHistory history;
static uint16_t lcd[FZ_LCD_W * FZ_LCD_H];
static int16_t mixbuf[1024 * 2];
static char status[160];
static char hint[24];
static int key_down[SIM_KEYS];
static int voice[SIM_KEYS];
static int step_voice;
static uint32_t clock_us;
static int booted;

static const int k_white[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};

static int white_of(int index)
{
    int i;
    for (i = 0; i < 16; i++) {
        if (k_white[i] == index)
            return i;
    }
    return -1;
}

static int note_of(int index)
{
    int note = 53 + index + session.octave * 12;
    if (note < 0)
        return 0;
    if (note > 127)
        return 127;
    return note;
}

static void refresh(void)
{
    const char *msg = project.last_error[0] ? project.last_error : "";
    char wave[24];
    char bpm[32];
    int partial;
    unsigned whole, tenth;
    fz_screen_draw(lcd, &project, &session, &seq, &capture);
    wave[0] = 0;
    partial = session.partial;
    if (partial < 0)
        partial = 0;
    if (partial >= FZ_HARMONICS)
        partial = FZ_HARMONICS - 1;
    if (session.screen == FZ_SCR_EDIT && session.edit_tab == FZ_TAB_AMP)
        snprintf(wave, sizeof wave, "ENV ");
    else if (session.screen == FZ_SCR_EDIT && session.edit_tab == FZ_TAB_MOD) {
        int dest = 0;
        if (project.selected_track >= 0 && project.selected_track < FZ_MAX_TRACKS)
            dest = project.track[project.selected_track].lfo.dest;
        snprintf(wave, sizeof wave, "LFO %s ", fz_lfo_target_name(dest));
    } else if (session.screen == FZ_SCR_EDIT)
        snprintf(wave, sizeof wave, "%s ", fz_edit_tab_name(session.edit_tab));
    if (session.screen == FZ_SCR_EDIT && session.edit_tab == FZ_TAB_WAVE && project.selected_track >= 0 &&
        project.selected_track < FZ_MAX_TRACKS) {
        FzInstrument *inst = fz_instrument(&project, project.track[project.selected_track].instrument);
        int level = inst ? (int)inst->additive.level[partial] : 0;
        snprintf(wave, sizeof wave, "WAVE H%d %d ", partial + 1, level);
    }
    whole = seq.bpm_milli / 1000u;
    tenth = (seq.bpm_milli % 1000u) / 100u;
    if (tenth)
        snprintf(bpm, sizeof bpm, "BPM %u.%u | ", whole, tenth);
    else
        snprintf(bpm, sizeof bpm, "BPM %u | ", whole);
    snprintf(status, sizeof status, "%s | OCT %d | %s%s%s%s%s%s",
             session.screen == FZ_SCR_HOME       ? "HOME"
             : session.screen == FZ_SCR_CAPTURE  ? "CAPTURE"
             : session.screen == FZ_SCR_EDIT     ? "EDIT"
             : session.screen == FZ_SCR_STEP     ? "STEP"
             : session.screen == FZ_SCR_PATTERNS ? "PATTERNS"
             : session.screen == FZ_SCR_SONG     ? "SONG"
             : session.screen == FZ_SCR_MIXER    ? "MIXER"
             : session.screen == FZ_SCR_FX       ? "FX"
             : session.screen == FZ_SCR_NUMERIC  ? "NUMERIC"
             : session.screen == FZ_SCR_NAMING   ? "NAME"
             : session.screen == FZ_SCR_CONFIRM  ? "CONFIRM"
             : session.screen == FZ_SCR_PICK_NOTE ? "NOTE"
             : session.screen == FZ_SCR_PICK_RANGE ? "RANGE"
                                                 : "?",
             session.octave, bpm, seq.playing ? "PLAY " : "", seq.recording ? "REC " : "", wave, hint, msg);
}

static void note_on(int index)
{
    int note = note_of(index);
    int v;
    if (session.screen == FZ_SCR_NUMERIC && index < 10) {
        fz_session_numeric_key(&session, index);
        return;
    }
    if (session.screen == FZ_SCR_NAMING && index < FZ_NAME_CATEGORIES) {
        fz_session_name_key(&session, &project, index);
        return;
    }
    if (session.screen == FZ_SCR_PICK_NOTE || session.screen == FZ_SCR_PICK_RANGE) {
        fz_session_pick_key(&session, note);
        return;
    }
    if (session.screen == FZ_SCR_STEP || session.screen == FZ_SCR_PATTERNS) {
        int white = white_of(index);
        if (white >= 0)
            fz_session_grid_key(&session, &seq, white);
        return;
    }
    if (!fz_session_wants_play(&session))
        return;
    v = fz_performance_note(&engine, &project, note, 110, 1);
    voice[index] = v;
    if (seq.recording || seq.armed) {
        FzHistEvent ev;
        memset(&ev, 0, sizeof ev);
        ev.note = (uint8_t)note;
        ev.velocity = 110;
        ev.time_us = clock_us;
        fz_session_history_key(&session, &history, ev);
        fz_seq_perform(&seq, note, 110, seq.frame);
    }
}

static void note_off(int index)
{
    int note = note_of(index);
    if (voice[index] >= 0)
        fz_engine_note_off(&engine, voice[index]);
    voice[index] = -1;
    if (seq.recording || seq.armed) {
        FzHistEvent ev;
        memset(&ev, 0, sizeof ev);
        ev.note = (uint8_t)note;
        ev.velocity = 0;
        ev.time_us = clock_us;
        fz_session_history_key(&session, &history, ev);
    }
}

static void apply_cmd(const FzCommand *cmd)
{
    if (!cmd || cmd->kind == FZ_CMD_NONE)
        return;
    if (cmd->kind == FZ_CMD_REC_HOLD)
        fz_seq_capture_history(&seq, &project, &history, 0, cmd->at_us, 2000000u);
    else
        fz_session_apply(&session, &project, &capture, &seq, cmd);
    if (cmd->kind == FZ_CMD_SAVE)
        snprintf(project.last_error, sizeof project.last_error, "NO FLASH");
}

static void drain(void)
{
    FzAction action;
    fz_session_bind_input(&session, &input);
    while (fz_input_pop(&input, &action)) {
        FzCommand cmd;
        fz_session_handle(&session, &action, &cmd);
        apply_cmd(&cmd);
    }
    refresh();
}

void sim_boot(void)
{
    int16_t wave[FZ_ADDITIVE_POINTS];
    FzId inst, track, asset, region;
    FzInstrument *made;
    int i;
    memset(&project, 0, sizeof project);
    memset(&session, 0, sizeof session);
    memset(&seq, 0, sizeof seq);
    memset(&engine, 0, sizeof engine);
    memset(&rack, 0, sizeof rack);
    fz_project_init(&project, pcm, SIM_PCM);
    fz_project_new_instrument(&project, FZ_INST_SAMPLE, &inst, &track);
    made = fz_instrument(&project, inst);
    fz_additive_clear(&made->additive);
    made->additive.level[0] = 256;
    fz_additive_render(&made->additive, wave, FZ_ADDITIVE_POINTS);
    fz_project_add_pcm(&project, wave, FZ_ADDITIVE_POINTS, FZ_ENGINE_RATE_HZ, 1, &asset);
    fz_project_add_region(&project, inst, asset, 60, 0, &region);
    fz_session_init(&session);
    fz_input_init(&input);
    fz_seq_init(&seq);
    fz_seq_set_tempo(&seq, 120000);
    fz_capture_init(&capture, &project, inst, track, take, 4096);
    fz_engine_init(&engine);
    fz_fx_init(&rack);
    fz_history_init(&history);
    for (i = 0; i < SIM_KEYS; i++) {
        key_down[i] = 0;
        voice[i] = -1;
    }
    step_voice = -1;
    hint[0] = 0;
    clock_us = 0;
    booted = 1;
    refresh();
}

void sim_poll(uint32_t time_us)
{
    if (!booted)
        sim_boot();
    clock_us = time_us;
    fz_session_bind_input(&session, &input);
    fz_input_poll(&input, time_us);
    drain();
}

void sim_button(int button, int down, uint32_t time_us)
{
    if (!booted)
        sim_boot();
    clock_us = time_us;
    fz_session_bind_input(&session, &input);
    fz_input_button(&input, (FzButton)button, down ? 1 : 0, time_us);
    fz_input_poll(&input, time_us);
    drain();
}

void sim_encoder(int encoder, int detents)
{
    if (!booted)
        sim_boot();
    if (detents == 0)
        return;
    hint[0] = 0;
    if (encoder == 0) {
        snprintf(hint, sizeof hint, "SELECT TURNS ");
        refresh();
        return;
    }
    if (encoder == 1) {
        fz_home_select_track(&project, detents);
        refresh();
        return;
    }
    if (encoder >= 2 && encoder <= 5)
        fz_session_knob(&session, &project, encoder - 2, detents);
    refresh();
}

void sim_key(int index, int down, uint32_t time_us)
{
    if (!booted)
        sim_boot();
    if (index < 0 || index >= SIM_KEYS)
        return;
    clock_us = time_us;
    if (down && !key_down[index]) {
        key_down[index] = 1;
        note_on(index);
    } else if (!down && key_down[index]) {
        key_down[index] = 0;
        note_off(index);
    }
    refresh();
}

static void fire_steps(uint32_t t0, uint32_t t1)
{
    int pattern, i, ended = 0, started = 0;
    if (!seq.playing || t1 <= t0)
        return;
    pattern = seq.current[0] >= 0 ? seq.current[0] : 0;
    for (i = 0; i < FZ_SEQ_EVENTS; i++) {
        FzSeqEvent *ev = &seq.ev[i];
        uint32_t off;
        if (!ev->used || ev->pattern != (uint16_t)pattern)
            continue;
        off = ev->tick + (ev->duration ? ev->duration : (uint32_t)FZ_STEP_TICKS);
        if (off > t0 && off <= t1)
            ended = 1;
        if (ev->tick >= t0 && ev->tick < t1 && fz_seq_step_fires(ev, t0 + (uint32_t)i))
            started = 1;
    }
    if (ended && step_voice >= 0) {
        fz_engine_note_off(&engine, step_voice);
        step_voice = -1;
    }
    if (!started)
        return;
    for (i = 0; i < FZ_SEQ_EVENTS; i++) {
        FzSeqEvent *ev = &seq.ev[i];
        if (!ev->used || ev->pattern != (uint16_t)pattern)
            continue;
        if (ev->tick >= t0 && ev->tick < t1 && fz_seq_step_fires(ev, t0 + (uint32_t)i))
            step_voice = fz_performance_note(&engine, &project, ev->note, ev->velocity ? ev->velocity : 100, 1);
    }
}

static void advance_transport(uint32_t frames)
{
    int pattern;
    uint32_t t0, t1, len;
    t0 = seq.tick;
    fz_seq_advance(&seq, &project, frames);
    t1 = seq.tick;
    pattern = seq.current[0] >= 0 ? seq.current[0] : 0;
    len = fz_seq_pattern_ticks(&project, pattern);
    if (seq.playing && len > 0 && t1 >= len && t0 < len) {
        fire_steps(t0, len);
        seq.tick = t1 - len;
        fire_steps(0, seq.tick);
    } else {
        fire_steps(t0, t1);
    }
    if (seq.playing && session.screen == FZ_SCR_STEP && len > 0) {
        int step = (int)((seq.tick % len) / (uint32_t)FZ_STEP_TICKS);
        int page = step / 16;
        if (page > 7)
            page = 7;
        session.page = page;
    }
}

int sim_render(int16_t *stereo, int frames)
{
    int peak = 0;
    int i;
    if (!booted)
        sim_boot();
    if (!stereo || frames <= 0)
        return 0;
    advance_transport((uint32_t)frames);
    refresh();
    fz_performance_render_sends(&engine, &project, &rack, stereo, frames);
    for (i = 0; i < frames * 2; i++) {
        int a = stereo[i] < 0 ? -stereo[i] : stereo[i];
        if (a > peak)
            peak = a;
    }
    return peak;
}

int16_t *sim_mix(void)
{
    return mixbuf;
}

uint16_t *sim_lcd(void)
{
    if (!booted)
        sim_boot();
    return lcd;
}

uint32_t sim_tick(void)
{
    if (!booted)
        sim_boot();
    return seq.tick;
}

char *sim_status(void)
{
    if (!booted)
        sim_boot();
    return status;
}
