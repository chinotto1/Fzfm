/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "sim.h"

#include "audio/play.h"
#include "dsp/additive.h"
#include "dsp/fx.h"
#include "dsp/mangle.h"
#include "sampler/capture.h"
#include "sequence/arp.h"
#include "sequence/history.h"
#include "storage/bundle.h"
#include "ui/edit.h"
#include "ui/input.h"
#include "ui/screen.h"

#include <stdio.h>
#include <string.h>

#define SIM_PCM 72000
#define SIM_KEYS 27
#define SIM_FILE (256u * 1024u)
#define SIM_TAKE 65536
#define SIM_CAP_BLOCK 1024

static FzProject project;
static int16_t pcm[SIM_PCM];
static int16_t take[SIM_TAKE];
static int16_t capblock[SIM_CAP_BLOCK];
static int sampling;
static int return_track;
static int cap_meter;
static FzSession session;
static FzInput input;
static FzSeq seq;
static FzArp arp;
static int arp_sounding;
static FzCapture capture;
static FzEngine engine;
static FzFxRack rack;
static FzHistory history;
static uint16_t lcd[FZ_LCD_W * FZ_LCD_H];
static int16_t mixbuf[1024 * 2];
static char status[256];
static char hint[24];
static uint8_t filebuf[SIM_FILE];
static uint32_t file_len;
static uint32_t file_gen;
static int file_kind;
static int key_down[SIM_KEYS];
static int voice[SIM_KEYS];
static int held_note[SIM_KEYS];
static int step_voice;
static int step_note;
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

static int selected_cycle(void)
{
    FzInstrument *inst;
    FzRegion *region;
    FzAsset *asset;
    if (project.selected_track < 0 || project.selected_track >= FZ_MAX_TRACKS)
        return 0;
    if (!project.track[project.selected_track].alive)
        return 0;
    inst = fz_instrument(&project, project.track[project.selected_track].instrument);
    if (!inst || inst->region_count < 1)
        return 0;
    region = fz_region(&project, inst->region[0]);
    asset = region ? fz_asset(&project, region->asset) : 0;
    return asset && asset->cycle;
}

static void sync_kind(void)
{
    session.cycle_inst = selected_cycle();
    if (session.screen == FZ_SCR_EDIT && session.cycle_inst && session.edit_tab != FZ_TAB_AMP &&
        session.edit_tab != FZ_TAB_MOD && !fz_edit_tab_on_cycle(session.edit_tab))
        session.edit_tab = FZ_TAB_WAVE;
}

static void refresh(void)
{
    const char *msg = project.last_error[0] ? project.last_error : "";
    session.arp = &arp;
    char wave[48];
    char bpm[32];
    int partial;
    unsigned whole, tenth;
    sync_kind();
    fz_screen_draw(lcd, &project, &session, &seq, &capture);
    wave[0] = 0;
    partial = session.partial;
    if (partial < 0)
        partial = 0;
    if (partial >= FZ_HARMONICS)
        partial = FZ_HARMONICS - 1;
    if (session.screen == FZ_SCR_ARP)
        snprintf(wave, sizeof wave, "ARP%s %s %s ", session.arp_page ? "2" : "", fz_arp_mode_name(arp.mode),
                 fz_arp_rate_name(arp.rate));
    else if (session.screen == FZ_SCR_CAPTURE) {
        char msg[16];
        char note[12];
        int mi;
        const char *src = capture.message[0] ? capture.message : "READY";
        const char *kind = "SAMPLE";
        FzInstrument *cap_inst = fz_instrument(&project, capture.instrument);
        for (mi = 0; mi < 15 && src[mi]; mi++)
            msg[mi] = src[mi];
        msg[mi] = 0;
        if (cap_inst && cap_inst->kind == FZ_INST_MULTI)
            kind = "MULTI";
        else if (!cap_inst && session.row[0])
            kind = "MULTI";
        fz_note_name(note, (int)sizeof note, fz_capture_next_note(&capture));
        snprintf(wave, sizeof wave, "SAMPLING %s %s %s ", kind, note, msg);
    }
    else if (session.screen == FZ_SCR_SAVE) {
        int evn = 0;
        int ei;
        for (ei = 0; ei < FZ_SEQ_EVENTS; ei++)
            if (seq.ev[ei].used)
                evn++;
        snprintf(wave, sizeof wave, "PROJECT %s E%d ", session.row[0] ? "LOAD" : "SAVE", evn);
    } else if (session.screen == FZ_SCR_EDIT && session.edit_tab == FZ_TAB_AMP)
        snprintf(wave, sizeof wave, "ENV ");
    else if (session.screen == FZ_SCR_EDIT && session.edit_tab == FZ_TAB_MOD) {
        int dest = 0;
        int shape = 0;
        if (project.selected_track >= 0 && project.selected_track < FZ_MAX_TRACKS) {
            dest = project.track[project.selected_track].lfo.dest;
            shape = project.track[project.selected_track].lfo.shape;
        }
        snprintf(wave, sizeof wave, "LFO %s %s ", fz_lfo_target_name(dest), fz_lfo_shape_name(shape));
    } else if (session.screen == FZ_SCR_EDIT)
        snprintf(wave, sizeof wave, "%s ", fz_edit_tab_name(session.edit_tab));
    if (session.screen == FZ_SCR_EDIT && session.edit_tab == FZ_TAB_WAVE && project.selected_track >= 0 &&
        project.selected_track < FZ_MAX_TRACKS) {
        FzInstrument *inst = fz_instrument(&project, project.track[project.selected_track].instrument);
        int level = inst ? (int)inst->additive.level[partial] : 0;
        snprintf(wave, sizeof wave, "WAVE H%d %d ", partial + 1, level);
    }
    if (session.screen == FZ_SCR_EDIT && session.edit_tab == FZ_TAB_DRAW) {
        int sample = 0;
        FzInstrument *inst = 0;
        FzRegion *region = 0;
        FzAsset *asset = 0;
        int pt = session.draw_pt < 0 ? 0 : session.draw_pt;
        if (project.selected_track >= 0 && project.selected_track < FZ_MAX_TRACKS)
            inst = fz_instrument(&project, project.track[project.selected_track].instrument);
        if (inst && inst->region_count > 0)
            region = fz_region(&project, inst->region[0]);
        if (region)
            asset = fz_asset(&project, region->asset);
        if (asset && asset->pcm && asset->frames > 0) {
            if ((uint32_t)pt >= asset->frames)
                pt = (int)asset->frames - 1;
            sample = asset->pcm[pt];
        }
        snprintf(wave, sizeof wave, "DRAW P%d %d ", pt, sample);
    }
    whole = seq.bpm_milli / 1000u;
    tenth = (seq.bpm_milli % 1000u) / 100u;
    if (tenth)
        snprintf(bpm, sizeof bpm, "BPM %u.%u | ", whole, tenth);
    else
        snprintf(bpm, sizeof bpm, "BPM %u | ", whole);
    {
        char word[8];
        char preset[16];
        const char *name = session.cycle_inst ? fz_cycle_preset_name(session.preset) : "";
        int pi;
        preset[0] = 0;
        for (pi = 0; pi < 6 && name[pi]; pi++)
            word[pi] = name[pi];
        word[pi] = 0;
        if (word[0])
            snprintf(preset, sizeof preset, "PRESET %s ", word);
        snprintf(status, sizeof status, "%s | OCT %d | %s%s%s%s%s%s%s",
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
             : session.screen == FZ_SCR_ARP      ? "ARP"
             : session.screen == FZ_SCR_SAVE     ? "SAVE"
                                                 : "?",
             session.octave, bpm, seq.playing ? "PLAY " : "", seq.recording ? "REC " : "", wave, preset, hint, msg);
    }
}

static void release_direct_keys(void)
{
    int i;
    for (i = 0; i < SIM_KEYS; i++) {
        if (voice[i] >= 0 && held_note[i] >= 0)
            fz_engine_note_off(&engine, held_note[i]);
        voice[i] = -1;
    }
}

static void drive_arp(uint32_t frames)
{
    FzArpEvent ev[32];
    int n, i;
    n = fz_arp_advance(&arp, frames, seq.bpm_milli, ev, 32);
    for (i = 0; i < n; i++) {
        if (!ev[i].on) {
            if (arp_sounding >= 0) {
                fz_engine_note_off(&engine, arp_sounding);
                arp_sounding = -1;
            }
        } else {
            int v;
            if (arp_sounding >= 0) {
                fz_engine_note_off(&engine, arp_sounding);
                arp_sounding = -1;
            }
            v = fz_performance_note(&engine, &project, ev[i].note, 110, 1);
            arp_sounding = v >= 0 ? ev[i].note : -1;
        }
    }
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
    fz_arp_key(&arp, note, 1);
    if (arp.mode == FZ_ARP_OFF) {
        v = fz_performance_note(&engine, &project, note, 110, 1);
        voice[index] = v;
    } else {
        voice[index] = -1;
    }
    held_note[index] = note;
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
    int played = held_note[index];
    int note = played >= 0 ? played : note_of(index);
    if (played >= 0) {
        fz_arp_key(&arp, played, 0);
        if (arp.mode == FZ_ARP_OFF)
            fz_engine_note_off(&engine, played);
    }
    held_note[index] = -1;
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

static void silence_keys(void);
static void rebind_capture(void);

static void stop_sounding(void)
{
    int i;
    for (i = 0; i < SIM_KEYS; i++) {
        if (held_note[i] >= 0 && arp.mode == FZ_ARP_OFF)
            fz_engine_note_off(&engine, held_note[i]);
    }
    if (arp_sounding >= 0)
        fz_engine_note_off(&engine, arp_sounding);
    if (step_note >= 0)
        fz_engine_note_off(&engine, step_note);
    silence_keys();
}

static int sampling_committed(void)
{
    int i;
    for (i = 0; i < FZ_MULTI_SLOTS; i++)
        if (capture.committed[i])
            return 1;
    return 0;
}

static void sampling_thresholds(void)
{
    int stop = session.cap_db - 6;
    if (stop < -66)
        stop = -66;
    fz_capture_set_thresholds(&capture, session.cap_db, stop, 360, (FZ_ENGINE_RATE_HZ * 200u) / 1000u, 1);
}

static void begin_sampling(void)
{
    FzId inst, track;
    if (sampling)
        return;
    return_track = project.selected_track;
    session.row[0] = 0;
    session.cap_db = -36;
    if (fz_project_new_instrument(&project, FZ_INST_SAMPLE, &inst, &track) != FZ_OK) {
        snprintf(hint, sizeof hint, "FULL ");
        return;
    }
    stop_sounding();
    cap_meter = 0;
    fz_capture_init(&capture, &project, inst, track, take, SIM_TAKE);
    sampling_thresholds();
    fz_capture_arm(&capture);
    sampling = 1;
}

static void sampling_apply_kind(void)
{
    FzInstrument *cur;
    FzId inst, track;
    FzInstKind kind = session.row[0] ? FZ_INST_MULTI : FZ_INST_SAMPLE;
    if (!sampling || sampling_committed() || capture.state == FZ_CAP_CAPTURING) {
        cur = fz_instrument(&project, capture.instrument);
        if (cur)
            session.row[0] = cur->kind == FZ_INST_MULTI ? 1 : 0;
        return;
    }
    cur = fz_instrument(&project, capture.instrument);
    if (cur && cur->kind == kind)
        return;
    if (cur && cur->region_count == 0)
        fz_project_delete_instrument(&project, capture.instrument);
    if (fz_project_new_instrument(&project, kind, &inst, &track) != FZ_OK) {
        snprintf(hint, sizeof hint, "FULL ");
        return;
    }
    fz_capture_init(&capture, &project, inst, track, take, SIM_TAKE);
    sampling_thresholds();
    fz_capture_arm(&capture);
}

static void leave_sampling(int keep)
{
    FzId id;
    int any;
    if (!sampling)
        return;
    any = sampling_committed();
    id = capture.instrument;
    sampling = 0;
    if (!keep && !any) {
        fz_project_delete_instrument(&project, id);
        if (return_track >= 0 && return_track < FZ_MAX_TRACKS && project.track[return_track].alive)
            project.selected_track = return_track;
    }
    rebind_capture();
}

static void apply_cmd(const FzCommand *cmd)
{
    if (!cmd || cmd->kind == FZ_CMD_NONE)
        return;
    if (cmd->kind == FZ_CMD_REC_HOLD)
        begin_sampling();
    else if (cmd->kind == FZ_CMD_REC_TAP && session.screen == FZ_SCR_CAPTURE) {
        fz_capture_punch(&capture);
        fz_capture_service(&capture);
    } else
        fz_session_apply(&session, &project, &capture, &seq, cmd);
    if (cmd->kind == FZ_CMD_HOME && sampling)
        leave_sampling(0);
    if (cmd->kind == FZ_CMD_CAPTURE_FINISH && sampling)
        leave_sampling(1);
    if (cmd->kind == FZ_CMD_CONFIRM && session.screen == FZ_SCR_SAVE) {
        if (session.row[0] == 0) {
            uint32_t n = 0;
            FzStatus st = fz_bundle_write_all(&project, &seq, filebuf, SIM_FILE, &n);
            if (st == FZ_OK) {
                file_len = n;
                file_kind = 1;
                file_gen++;
                snprintf(hint, sizeof hint, "SAVED ");
            } else {
                snprintf(hint, sizeof hint, "SAVE FULL ");
            }
        } else {
            file_kind = 2;
            file_gen++;
            snprintf(hint, sizeof hint, "PICK FILE ");
        }
    }
}

static void drain(void)
{
    FzAction action;
    fz_session_bind_input(&session, &input);
    sync_kind();
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
    fz_arp_init(&arp);
    arp_sounding = -1;
    fz_capture_init(&capture, &project, inst, track, take, SIM_TAKE);
    sampling = 0;
    cap_meter = 0;
    return_track = project.selected_track;
    fz_engine_init(&engine);
    fz_fx_init(&rack);
    fz_history_init(&history);
    for (i = 0; i < SIM_KEYS; i++) {
        key_down[i] = 0;
        voice[i] = -1;
        held_note[i] = -1;
    }
    step_voice = -1;
    step_note = -1;
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
    if (encoder == 6) {
        int count = fz_cycle_preset_count();
        int next = session.preset + detents;
        if (count < 1)
            count = 1;
        next %= count;
        if (next < 0)
            next += count;
        if (fz_edit_cycle_preset(&project, next) == FZ_OK)
            session.preset = next;
        else
            snprintf(hint, sizeof hint, "NOT A CYCLE ");
    }
    if (encoder >= 2 && encoder <= 5) {
        int was = arp.mode;
        int kind = session.row[0];
        int db = session.cap_db;
        fz_session_knob(&session, &project, encoder - 2, detents);
        if (session.screen == FZ_SCR_CAPTURE && encoder == 2 && session.row[0] != kind)
            sampling_apply_kind();
        if (session.screen == FZ_SCR_CAPTURE && encoder == 3 && session.cap_db != db)
            sampling_thresholds();
        if (was == FZ_ARP_OFF && arp.mode != FZ_ARP_OFF)
            release_direct_keys();
    }
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
    if (ended && step_note >= 0) {
        fz_engine_note_off(&engine, step_note);
        step_voice = -1;
        step_note = -1;
    }
    if (!started)
        return;
    for (i = 0; i < FZ_SEQ_EVENTS; i++) {
        FzSeqEvent *ev = &seq.ev[i];
        if (!ev->used || ev->pattern != (uint16_t)pattern)
            continue;
        if (ev->tick >= t0 && ev->tick < t1 && fz_seq_step_fires(ev, t0 + (uint32_t)i)) {
            step_voice = fz_performance_note(&engine, &project, ev->note, ev->velocity ? ev->velocity : 100, 1);
            step_note = ev->note;
        }
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
    drive_arp(frames);
}

int sim_voices(void)
{
    int i, n = 0;
    if (!booted)
        sim_boot();
    for (i = 0; i < FZ_MAX_VOICES; i++) {
        if (engine.voice[i].alive)
            n++;
    }
    return n;
}

static void silence_keys(void)
{
    int i;
    for (i = 0; i < SIM_KEYS; i++) {
        key_down[i] = 0;
        voice[i] = -1;
        held_note[i] = -1;
    }
    step_voice = -1;
    step_note = -1;
    arp_sounding = -1;
}

static void rebind_capture(void)
{
    FzId inst;
    FzId track;
    int i;
    inst = fz_id_none();
    track = fz_id_none();
    if (project.selected_track >= 0 && project.selected_track < FZ_MAX_TRACKS &&
        project.track[project.selected_track].alive) {
        track = fz_track_id(&project, project.selected_track);
        inst = project.track[project.selected_track].instrument;
    } else {
        for (i = 0; i < FZ_MAX_TRACKS; i++) {
            if (!project.track[i].alive)
                continue;
            project.selected_track = i;
            track = fz_track_id(&project, i);
            inst = project.track[i].instrument;
            break;
        }
    }
    fz_capture_init(&capture, &project, inst, track, take, SIM_TAKE);
}

uint8_t *sim_file_ptr(void)
{
    return filebuf;
}

uint32_t sim_file_cap(void)
{
    return SIM_FILE;
}

uint32_t sim_file_len(void)
{
    return file_len;
}

uint32_t sim_file_gen(void)
{
    return file_gen;
}

int sim_file_kind(void)
{
    return file_kind;
}

int sim_load(uint32_t len)
{
    FzStatus st;
    if (!booted)
        sim_boot();
    if (len == 0 || len > SIM_FILE)
        return FZ_ERR_ARGS;
    st = fz_bundle_read_all(&project, &seq, filebuf, len);
    if (st != FZ_OK) {
        snprintf(hint, sizeof hint, "BAD FILE ");
        refresh();
        return (int)st;
    }
    file_len = len;
    sampling = 0;
    fz_engine_init(&engine);
    fz_arp_init(&arp);
    silence_keys();
    rebind_capture();
    snprintf(hint, sizeof hint, "LOADED ");
    refresh();
    return FZ_OK;
}

int16_t *sim_capture_buf(void)
{
    return capblock;
}

int sim_capture_cap(void)
{
    return SIM_CAP_BLOCK;
}

void sim_capture(int frames)
{
    int i;
    int block = 0;
    if (!booted)
        sim_boot();
    if (session.screen != FZ_SCR_CAPTURE || !sampling || frames <= 0)
        return;
    if (frames > SIM_CAP_BLOCK)
        frames = SIM_CAP_BLOCK;
    fz_capture_accept(&capture, capblock, (uint32_t)frames);
    fz_capture_service(&capture);
    for (i = 0; i < frames; i++) {
        int a = capblock[i] < 0 ? -capblock[i] : capblock[i];
        if (a > block)
            block = a;
    }
    if (block > cap_meter)
        cap_meter = block;
    else
        cap_meter = cap_meter * 3 / 4;
    if (cap_meter < 0)
        cap_meter = 0;
    if (cap_meter > 32767)
        cap_meter = 32767;
    capture.peak = (uint16_t)cap_meter;
    refresh();
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
