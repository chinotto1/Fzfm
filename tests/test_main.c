/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "audio/engine.h"
#include "audio/play.h"
#include "audio/reader.h"
#include "audio/src.h"
#include "core/project.h"
#include "core/types.h"
#include "dsp/additive.h"
#include "sampler/capture.h"
#include "sequence/history.h"
#include "sequence/tempo.h"
#include "ui/font.h"
#include "ui/home.h"
#include "ui/input.h"
#include "check.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <string.h>

static int crossings(const int16_t *mono, int frames)
{
    int n = 0, i;
    if (frames < 2)
        return 0;
    for (i = 1; i < frames; i++) {
        if (mono[i - 1] < 0 && mono[i] >= 0)
            n++;
    }
    return n;
}

static void fill_sine(int16_t *dst, int frames, double rate, double freq, double amp)
{
    int i;
    for (i = 0; i < frames; i++)
        dst[i] = (int16_t)lround(sin(2.0 * M_PI * freq * (double)i / rate) * amp);
}

static void test_units(void)
{
    EXPECT(FZ_ENGINE_RATE_HZ == 36000u);
    EXPECT(FZ_MONO_BYTES_PER_SECOND == 72000u);
    EXPECT(strcmp(fz_status_str(FZ_ERR_SAMPLE_STORAGE), "SAMPLE STORAGE FULL") == 0);
}

static void test_pitch_ratio(void)
{
    uint32_t unity = fz_pitch_step_q16(FZ_ENGINE_RATE_HZ, FZ_ENGINE_RATE_HZ, 60, 60, 0);
    uint32_t up = fz_pitch_step_q16(FZ_ENGINE_RATE_HZ, FZ_ENGINE_RATE_HZ, 60, 72, 0);
    uint32_t down = fz_pitch_step_q16(FZ_ENGINE_RATE_HZ, FZ_ENGINE_RATE_HZ, 60, 48, 0);
    FzReader reader;
    int16_t sine[3600];
    int produced = 0;
    int16_t out[8000];

    EXPECT(unity == 65536u);
    EXPECT(up == unity * 2u);
    EXPECT(down == unity / 2u);
    fill_sine(sine, 3600, 36000.0, 1000.0, 12000.0);
    EXPECT(fz_reader_start_sample(&reader, sine, 3600, FZ_ENGINE_RATE_HZ, 60, 60, 0) == 0);
    while (reader.active && produced < 8000) {
        out[produced++] = fz_reader_next(&reader);
    }
    EXPECT(produced == 3600);
    EXPECT(crossings(out, produced) > 90 && crossings(out, produced) < 110);

    EXPECT(fz_reader_start_sample(&reader, sine, 3600, FZ_ENGINE_RATE_HZ, 60, 72, 0) == 0);
    produced = 0;
    while (reader.active && produced < 8000)
        out[produced++] = fz_reader_next(&reader);
    EXPECT(produced == 1800);
    EXPECT(crossings(out, produced) > 90 && crossings(out, produced) < 110);
}

static void test_cycles(void)
{
    int16_t a[1024], b[600], out[36000];
    FzReader reader;
    int i, n, zx;
    for (i = 0; i < 1024; i++)
        a[i] = (int16_t)lround(sin(2.0 * M_PI * (double)i / 1024.0) * 14000.0);
    for (i = 0; i < 600; i++)
        b[i] = (int16_t)lround(sin(2.0 * M_PI * (double)i / 600.0) * 14000.0);
    EXPECT(fz_reader_start_cycle(&reader, a, 1024, 69, 0) == 0);
    for (n = 0; n < 36000; n++)
        out[n] = fz_reader_next(&reader);
    zx = crossings(out, 36000);
    EXPECT(zx >= 435 && zx <= 445);
    EXPECT(fz_reader_start_cycle(&reader, b, 600, 69, 0) == 0);
    for (n = 0; n < 36000; n++)
        out[n] = fz_reader_next(&reader);
    zx = crossings(out, 36000);
    EXPECT(zx >= 435 && zx <= 445);
}

static int pull_all(FzSrc *s, int16_t *dst, int cap)
{
    int n = 0;
    while (n < cap) {
        uint32_t got = fz_src_read(s, dst + n, (uint32_t)(cap - n));
        if (got == 0)
            break;
        n += (int)got;
    }
    return n;
}

/* The converter queue is bounded. Feed and drain together. */
static int pump(FzSrc *s, const int16_t *in, int frames, int chunk, int16_t *dst, int cap)
{
    int off = 0, n = 0;
    while (off < frames || s->q_n > 0) {
        if (off < frames && s->q_n < FZ_SRC_QUEUE / 2) {
            int nfeed = chunk;
            uint32_t got;
            if (off + nfeed > frames)
                nfeed = frames - off;
            got = fz_src_write(s, in + off * s->channels, (uint32_t)nfeed);
            off += (int)got;
            if (got == 0 && s->q_n >= FZ_SRC_QUEUE)
                nfeed = 0;
        }
        if (n < cap) {
            uint32_t got = fz_src_read(s, dst + n * s->channels, (uint32_t)((cap - n) > 256 ? 256 : cap - n));
            if (got == 0) {
                if (off >= frames)
                    break;
                continue;
            }
            n += (int)got;
        } else if (off >= frames) {
            break;
        }
    }
    return n;
}

static void test_src(void)
{
    FzSrc whole, chunked;
    int16_t in[8000];
    int16_t a[8000], b[8000], tmp[128];
    int na, nb = 0, i, off;
    int64_t acc = 0;
    FzSrc up, same, down;
    int16_t tone[44100];
    int16_t out[40000];
    int zx, n;
    double rms_hi = 0, rms_lo = 0;

    for (i = 0; i < 8000; i++)
        in[i] = (int16_t)(i % 17 == 0 ? 8000 : (int16_t)(1000 + (i % 50)));
    EXPECT(fz_src_init(&whole, 44100, 36000, 1) == 0);
    EXPECT(fz_src_init(&chunked, 44100, 36000, 1) == 0);
    na = pump(&whole, in, 8000, 1000, a, 8000);
    nb = pump(&chunked, in, 8000, 17, b, 8000);
    EXPECT(na == nb && na > 1000);
    EXPECT(memcmp(a, b, (size_t)na * sizeof(int16_t)) == 0);
    (void)off;
    (void)tmp;

    for (i = 0; i < 4000; i++)
        in[i] = 9000;
    fz_src_reset(&whole);
    fz_src_write(&whole, in, 4000);
    na = pull_all(&whole, a, 4000);
    EXPECT(na > 200);
    for (i = 80; i < na; i++)
        acc += a[i] > 9000 ? a[i] - 9000 : 9000 - a[i];
    EXPECT(acc / (na - 80) < 3);

    fill_sine(tone, 44100, 44100.0, 1000.0, 10000.0);
    EXPECT(fz_src_init(&down, 44100, 36000, 1) == 0);
    n = pump(&down, tone, 44100, 512, out, 40000);
    EXPECT(n > 30000);
    zx = crossings(out + 400, n - 800);
    EXPECT(zx > 900 && zx < 1100);

    fill_sine(tone, 44100, 44100.0, 20000.0, 10000.0);
    fz_src_reset(&down);
    n = pump(&down, tone, 20000, 512, out, 20000);
    for (i = 500; i < n - 500; i++)
        rms_hi += (double)out[i] * (double)out[i];
    rms_hi = sqrt(rms_hi / (double)(n - 1000));
    fill_sine(tone, 44100, 44100.0, 1000.0, 10000.0);
    fz_src_reset(&down);
    n = pump(&down, tone, 20000, 512, out, 20000);
    for (i = 500; i < n - 500; i++)
        rms_lo += (double)out[i] * (double)out[i];
    rms_lo = sqrt(rms_lo / (double)(n - 1000));
    EXPECT(rms_hi < rms_lo * 0.5);

    EXPECT(fz_src_init(&up, 36000, 44100, 1) == 0);
    fill_sine(in, 3600, 36000.0, 440.0, 10000.0);
    fz_src_write(&up, in, 3600);
    n = pull_all(&up, out, 8000);
    EXPECT(n > 3000);
    zx = crossings(out + 100, n - 200);
    EXPECT(zx > 30 && zx < 60);

    EXPECT(fz_src_init(&same, 36000, 36000, 1) == 0);
    fz_src_write(&same, in, 1000);
    n = pull_all(&same, out, 2000);
    EXPECT(n == 1000);
    EXPECT(memcmp(in, out, 1000 * sizeof(int16_t)) == 0);

    {
        FzSrc full;
        int16_t one = 1;
        uint32_t accepted = 0;
        EXPECT(fz_src_init(&full, 48000, 36000, 1) == 0);
        for (i = 0; i < FZ_SRC_QUEUE + 50; i++)
            accepted += fz_src_write(&full, &one, 1);
        EXPECT(accepted == FZ_SRC_QUEUE);
        EXPECT(full.overrun_frames >= 50);
        pull_all(&full, out, 100);
        EXPECT(fz_src_write(&full, &one, 1) == 1);
    }
    EXPECT(fz_pcm_mono_sum(20000, 20000) == 20000);
    EXPECT(fz_pcm_mono_sum(1000, -1000) == 0);
    EXPECT(fz_pcm_mono_sum(32767, 32767) == 32767);
}

static void test_project_and_zones(void)
{
    static FzProject project;
    static int16_t store[8000];
    FzId inst, track, asset, region;
    FzInstrument *it;
    uint8_t roots[2];
    int i;

    fz_project_init(&project, store, 8000);
    EXPECT(fz_project_new_instrument(&project, FZ_INST_MULTI, &inst, &track) == FZ_OK);
    it = fz_instrument(&project, inst);
    EXPECT(it && strcmp(it->name, "DRUMS 1") == 0);
    EXPECT(project.selected_track == (int)track.index);
    EXPECT(fz_track(&project, track)->pattern.generation != 0);
    EXPECT(fz_project_new_instrument(&project, FZ_INST_SAMPLE, &inst, &track) == FZ_OK);
    EXPECT(strcmp(fz_instrument(&project, inst)->name, "SYNTH 1") == 0);
    EXPECT(fz_project_delete_instrument(&project, inst) == FZ_OK);
    EXPECT(fz_project_new_instrument(&project, FZ_INST_SAMPLE, &inst, &track) == FZ_OK);
    EXPECT(strcmp(fz_instrument(&project, inst)->name, "SYNTH 1") == 0);

    {
        int16_t pcm[32];
        for (i = 0; i < 32; i++)
            pcm[i] = (int16_t)(1000 + i);
        EXPECT(fz_project_add_pcm(&project, pcm, 32, FZ_ENGINE_RATE_HZ, 0, &asset) == FZ_OK);
        EXPECT(fz_project_add_region(&project, inst, asset, 48, 0, &region) == FZ_OK);
        EXPECT(fz_project_add_pcm(&project, pcm, 32, FZ_ENGINE_RATE_HZ, 0, &asset) == FZ_OK);
        EXPECT(fz_project_add_region(&project, inst, asset, 60, 1, &region) == FZ_OK);
    }
    roots[0] = 48;
    roots[1] = 60;
    EXPECT(fz_zone_root_for_note(roots, 2, 54) == 0);
    EXPECT(fz_zone_root_for_note(roots, 2, 55) == 1);
    EXPECT(fz_zone_root_for_note(roots, 2, 0) == 0);
    EXPECT(fz_zone_root_for_note(roots, 2, 127) == 1);
    {
        FzRegion *low = fz_instrument_region_for_note(&project, inst, 54);
        FzRegion *high = fz_instrument_region_for_note(&project, inst, 55);
        EXPECT(low && low->root == 48 && low->key_high == 54);
        EXPECT(high && high->root == 60 && high->key_low == 55);
        low->manual_zone = 1;
        low->key_low = 50;
        low->key_high = 52;
        fz_project_recompute_zones(&project, inst);
        EXPECT(fz_instrument_region_for_note(&project, inst, 51)->root == 48);
        EXPECT(fz_instrument_region_for_note(&project, inst, 54)->root == 60);
    }

    fz_home_turn(&project, 2, 5);
    EXPECT(fz_track(&project, track)->volume == 105);
    fz_track_set_volume(&project, track, 80);
    EXPECT(fz_track(&project, track)->volume == 80);
    fz_home_turn(&project, 0, -1000);
    EXPECT(fz_track(&project, track)->cutoff == 0);
    {
        FzId extra_inst, extra_track;
        int guard = 0;
        while (fz_project_new_instrument(&project, FZ_INST_MULTI, &extra_inst, &extra_track) == FZ_OK && guard++ < 20)
            ;
        EXPECT(fz_project_new_instrument(&project, FZ_INST_MULTI, &extra_inst, &extra_track) != FZ_OK);
        EXPECT(strcmp(project.last_error, "OK") != 0);
    }
}

static void hit(FzCapture *c, int amp, int body, int tail)
{
    int16_t buf[512];
    int i;
    for (i = 0; i < body && i < 512; i++)
        buf[i] = (int16_t)amp;
    fz_capture_accept(c, buf, (uint32_t)body);
    for (i = 0; i < tail && i < 512; i++)
        buf[i] = 0;
    fz_capture_accept(c, buf, (uint32_t)tail);
    fz_capture_service(c);
}

static void test_capture(void)
{
    static FzProject project;
    static int16_t store[20000];
    static int16_t take[4000];
    FzId inst, track;
    FzCapture cap;
    FzEngine engine;
    int16_t mix[64];
    int i, before;

    fz_project_init(&project, store, 20000);
    EXPECT(fz_project_new_instrument(&project, FZ_INST_MULTI, &inst, &track) == FZ_OK);
    fz_capture_init(&cap, &project, inst, track, take, 4000);
    cap.octave = 2;
    fz_capture_set_thresholds(&cap, -36, -42, 4, 3, 1);
    cap.tail_keep = 0;
    before = 1;
    fz_capture_oct_plus(&cap);
    EXPECT(cap.state == FZ_CAP_PERFORMANCE);
    EXPECT(cap.octave == 2);
    EXPECT(fz_instrument(&project, inst) != 0);
    fz_capture_oct_plus(&cap);
    EXPECT(fz_instrument(&project, inst) != 0);

    fz_capture_init(&cap, &project, inst, track, take, 4000);
    fz_capture_set_thresholds(&cap, -36, -42, 4, 3, 1);
    cap.tail_keep = 0;
    cap.octave = 1;
    fz_capture_arm(&cap);
    EXPECT(fz_capture_oct_flash(&cap) == 1);
    for (i = 0; i < 16; i++)
        hit(&cap, 9000, 6, 3);
    EXPECT(cap.slot == 16);
    EXPECT(cap.state == FZ_CAP_FINISHED);
    EXPECT(fz_instrument(&project, inst)->region_count == 16);
    EXPECT(fz_instrument_region_for_note(&project, inst, 36)->root == 36);
    EXPECT(fz_instrument_region_for_note(&project, inst, 51)->root == 51);
    {
        FzAsset *a = fz_asset(&project, fz_region(&project, fz_instrument(&project, inst)->region[0])->asset);
        int peak = 0, n;
        EXPECT(a != 0);
        for (n = 0; n < (int)a->frames; n++) {
            if (a->pcm[n] > peak)
                peak = a->pcm[n];
        }
        EXPECT(peak == 9000);
    }

    fz_project_init(&project, store, 20000);
    EXPECT(fz_project_new_instrument(&project, FZ_INST_MULTI, &inst, &track) == FZ_OK);
    fz_capture_init(&cap, &project, inst, track, take, 4000);
    fz_capture_set_thresholds(&cap, -36, -42, 2, 2, 1);
    cap.tail_keep = 0;
    fz_capture_arm(&cap);
    hit(&cap, 8000, 5, 2);
    hit(&cap, 7000, 5, 2);
    hit(&cap, 6000, 5, 2);
    EXPECT(fz_instrument(&project, inst)->region_count == 3);
    fz_engine_init(&engine);
    project.selected_track = (int)track.index;
    EXPECT(fz_performance_note(&engine, &project, 36, 127, 1) >= 0);
    fz_engine_render(&engine, mix, 8);
    EXPECT(mix[0] != 0 || mix[2] != 0);
    fz_engine_init(&engine);
    EXPECT(fz_performance_note(&engine, &project, 39, 127, 1) < 0);

    cap.octave = 3;
    fz_capture_oct_plus(&cap);
    EXPECT(cap.state == FZ_CAP_PERFORMANCE);
    EXPECT(cap.octave == 3);
    EXPECT(fz_capture_oct_flash(&cap) == 0);
    EXPECT(fz_instrument(&project, inst)->region_count == 3);
    before = fz_instrument(&project, inst)->region_count;
    fz_capture_oct_plus(&cap);
    EXPECT(fz_instrument(&project, inst)->region_count == before);

    fz_capture_init(&cap, &project, inst, track, take, 4000);
    fz_capture_set_thresholds(&cap, -36, -42, 2, 2, 1);
    cap.tail_keep = 0;
    fz_capture_arm(&cap);
    EXPECT(fz_instrument(&project, inst)->region_count == 3);
    hit(&cap, 4000, 5, 2);
    EXPECT(fz_instrument(&project, inst)->region_count == 4);
    fz_capture_undo_last(&cap);
    EXPECT(fz_instrument(&project, inst)->region_count == 3);
    EXPECT(fz_instrument_region_for_note(&project, inst, 36) != 0);

    fz_capture_set_thresholds(&cap, -36, -42, 1, 2, 50);
    cap.tail_keep = 0;
    fz_capture_arm(&cap);
    hit(&cap, 8000, 4, 2);
    EXPECT(fz_instrument(&project, inst)->region_count == 3);
    EXPECT(cap.state == FZ_CAP_WAIT_INPUT);

    fz_project_init(&project, store, 30);
    EXPECT(fz_project_new_instrument(&project, FZ_INST_MULTI, &inst, &track) == FZ_OK);
    fz_capture_init(&cap, &project, inst, track, take, 4000);
    fz_capture_set_thresholds(&cap, -36, -42, 1, 2, 1);
    cap.tail_keep = 0;
    fz_capture_arm(&cap);
    hit(&cap, 8000, 20, 2);
    EXPECT(fz_instrument(&project, inst)->region_count == 1);
    {
        int16_t loud[20], quiet[2] = {0, 0};
        int k;
        for (k = 0; k < 20; k++)
            loud[k] = 8000;
        fz_capture_accept(&cap, loud, 20);
        fz_capture_accept(&cap, quiet, 2);
        EXPECT(cap.state == FZ_CAP_END_DETECTED);
        EXPECT(fz_capture_service(&cap) == FZ_ERR_SAMPLE_STORAGE);
        EXPECT(cap.state == FZ_CAP_STORAGE_FULL);
        EXPECT(fz_instrument(&project, inst)->region_count == 1);
        EXPECT(strcmp(cap.message, "SAMPLE STORAGE FULL") == 0);
    }

    {
        FzCapState states[] = {FZ_CAP_CONFIGURE, FZ_CAP_PAUSED, FZ_CAP_FINISHED, FZ_CAP_CANCELLED,
                               FZ_CAP_INPUT_LOST, FZ_CAP_PERFORMANCE, FZ_CAP_WAIT_INPUT};
        int s;
        for (s = 0; s < 7; s++) {
            int count = fz_instrument(&project, inst)->region_count;
            cap.state = states[s];
            cap.octave = 4;
            fz_capture_oct_plus(&cap);
            EXPECT(cap.octave == 4);
            EXPECT(cap.state == FZ_CAP_PERFORMANCE);
            EXPECT(fz_instrument(&project, inst)->region_count == count);
        }
    }
    (void)before;
}

static void test_input_history_tempo(void)
{
    FzInput input;
    FzAction action;
    int n = 0;
    FzHistory hist;
    FzHistEvent ev, got[8];
    FzTempoCandidate cand[8];
    uint32_t onsets[4];
    double bpm = 100.0;
    int count;

    fz_input_init(&input);
    fz_input_button(&input, FZ_BTN_REC, 1, 1000);
    fz_input_poll(&input, 1000 + FZ_HOLD_US - 1);
    EXPECT(fz_input_pop(&input, &action) == 0);
    fz_input_set_screen(&input, 2);
    fz_input_poll(&input, 1000 + FZ_HOLD_US);
    EXPECT(fz_input_pop(&input, &action) == 1);
    EXPECT(action.kind == FZ_ACT_HOLD && action.button == FZ_BTN_REC && action.at_us == 1000);
    fz_input_button(&input, FZ_BTN_REC, 0, 1000 + FZ_HOLD_US + 10);
    EXPECT(fz_input_pop(&input, &action) == 0);

    fz_input_button(&input, FZ_BTN_EDIT, 1, 50);
    fz_input_button(&input, FZ_BTN_EDIT, 0, 200);
    EXPECT(fz_input_pop(&input, &action) == 1);
    EXPECT(action.kind == FZ_ACT_TAP && action.at_us == 50);
    EXPECT(fz_input_pop(&input, &action) == 0);

    fz_input_set_confirming(&input, 1);
    fz_input_button(&input, FZ_BTN_SEL, 1, 10);
    fz_input_poll(&input, 10 + FZ_HOLD_US);
    EXPECT(fz_input_pop(&input, &action) == 1);
    EXPECT(action.kind == FZ_ACT_CANCEL && action.at_us == 10);
    fz_input_button(&input, FZ_BTN_SEL, 0, 10 + FZ_HOLD_US);
    EXPECT(fz_input_pop(&input, &action) == 0);

    fz_input_button(&input, FZ_BTN_OCT_UP, 1, 5);
    EXPECT(fz_input_pop(&input, &action) == 0);
    fz_input_button(&input, FZ_BTN_OCT_UP, 0, 20);
    EXPECT(fz_input_pop(&input, &action) == 1);
    EXPECT(action.kind == FZ_ACT_TAP && action.button == FZ_BTN_OCT_UP && action.at_us == 5);
    EXPECT(fz_input_pop(&input, &action) == 0);

    fz_history_init(&hist);
    ev.note = 60;
    ev.velocity = 100;
    ev.track = 0;
    ev.source = FZ_SRC_KEYBOARD;
    ev.time_us = 100;
    fz_history_push(&hist, ev);
    ev.time_us = 160;
    fz_history_push(&hist, ev);
    ev.time_us = 140;
    ev.source = FZ_SRC_SEQUENCER;
    fz_history_push(&hist, ev);
    n = fz_history_slice(&hist, 150, 1000, got, 8);
    EXPECT(n == 1 && got[0].time_us == 100);

    onsets[0] = 0;
    onsets[1] = 18000;
    onsets[2] = 36000;
    onsets[3] = 54000;
    count = fz_tempo_propose(72000, FZ_ENGINE_RATE_HZ, onsets, 4, cand, 8);
    EXPECT(count >= 1);
    EXPECT(cand[0].bars == 1);
    EXPECT(cand[0].bpm > 119.0 && cand[0].bpm < 121.0);
    EXPECT(cand[0].confidence > 50);
    EXPECT(fz_tempo_propose(72000, FZ_ENGINE_RATE_HZ, 0, 0, cand, 8) == 1);
    EXPECT(cand[0].confidence == 0);
    EXPECT(fz_tempo_apply_policy(0, 0, &bpm, cand[0].bpm) == 0);
    EXPECT(fz_tempo_apply_policy(0, 0, &bpm, 96.0) == 1);
    EXPECT(bpm == 96.0);
    EXPECT(fz_tempo_apply_policy(1, 0, &bpm, 140.0) == 0);
    EXPECT(bpm == 96.0);
    EXPECT(fz_tempo_apply_policy(0, 1, &bpm, 140.0) == 0);
    EXPECT(bpm == 96.0);
}

static void test_additive(void)
{
    FzAdditiveRecipe recipe, loaded;
    int16_t wave[FZ_ADDITIVE_POINTS];
    int16_t other[FZ_ADDITIVE_POINTS];
    uint8_t blob[256];
    uint8_t junk[64];
    uint32_t len = 0;
    int i;

    fz_additive_clear(&recipe);
    recipe.level[0] = 256;
    EXPECT(fz_additive_render(&recipe, wave, FZ_ADDITIVE_POINTS) == FZ_OK);
    EXPECT(wave[0] == 0);
    EXPECT(wave[FZ_ADDITIVE_POINTS / 4] > 20000);
    EXPECT(wave[FZ_ADDITIVE_POINTS / 2] == 0 || (wave[FZ_ADDITIVE_POINTS / 2] > -5 && wave[FZ_ADDITIVE_POINTS / 2] < 5));

    fz_additive_clear(&recipe);
    recipe.level[1] = 256;
    EXPECT(fz_additive_render(&recipe, wave, FZ_ADDITIVE_POINTS) == FZ_OK);
    EXPECT(wave[FZ_ADDITIVE_POINTS / 8] > 20000);
    EXPECT(wave[FZ_ADDITIVE_POINTS / 4] > -5 && wave[FZ_ADDITIVE_POINTS / 4] < 5);

    fz_additive_clear(&recipe);
    for (i = 0; i < FZ_HARMONICS; i++)
        recipe.level[i] = (uint16_t)((i * 3) % 257);
    EXPECT(fz_additive_render(&recipe, wave, FZ_ADDITIVE_POINTS) == FZ_OK);
    recipe.level[47] = (uint16_t)(recipe.level[47] == 0 ? 200 : 0);
    EXPECT(fz_additive_render(&recipe, other, FZ_ADDITIVE_POINTS) == FZ_OK);
    EXPECT(memcmp(wave, other, sizeof wave) != 0);

    recipe.level[47] = 10;
    EXPECT(fz_additive_write(&recipe, blob, sizeof blob, &len) == FZ_OK);
    EXPECT(fz_additive_read(&loaded, blob, len) == FZ_OK);
    EXPECT(fz_additive_render(&loaded, other, loaded.points) == FZ_OK);
    EXPECT(fz_additive_render(&recipe, wave, FZ_ADDITIVE_POINTS) == FZ_OK);
    EXPECT(memcmp(wave, other, FZ_ADDITIVE_POINTS * sizeof(int16_t)) == 0);
    blob[0] = 'X';
    EXPECT(fz_additive_read(&loaded, blob, len) == FZ_ERR_FORMAT);
    for (i = 0; i < 40; i++) {
        junk[i % 64] = (uint8_t)(i * 17 + 3);
        EXPECT(fz_additive_read(&loaded, junk, (uint32_t)(i % 64) + 1u) == FZ_ERR_FORMAT);
    }
}

static void test_home_and_mix(void)
{
    static FzProject project;
    static int16_t store[1000];
    static uint16_t fb[FZ_LCD_W * FZ_LCD_H];
    static uint16_t fb2[FZ_LCD_W * FZ_LCD_H];
    FzId inst, track, asset, region;
    FzEngine engine, engine2;
    int16_t pcm[64];
    int16_t mix_a[32], mix_b[32];
    int i;
    uint8_t rows[8];
    const uint16_t hi = FZ_RGB(40, 90, 160);
    const uint16_t bg = FZ_RGB(8, 10, 16);

    fz_font_rows('H', rows);
    EXPECT(rows[3] == 0x7E);

    fz_project_init(&project, store, 1000);
    EXPECT(fz_project_new_instrument(&project, FZ_INST_MULTI, &inst, &track) == FZ_OK);
    EXPECT(fz_project_new_instrument(&project, FZ_INST_SAMPLE, &inst, &track) == FZ_OK);
    project.selected_track = 0;
    fz_home_draw(fb, &project);
    EXPECT(fb[4 * FZ_LCD_W + 5] != bg);
    EXPECT(fb[FZ_HOME_LIST_Y * FZ_LCD_W + 10] == hi);
    EXPECT(fb[(FZ_HOME_LIST_Y + FZ_HOME_ROW_H) * FZ_LCD_W + 10] == bg);
    fz_track_set_volume(&project, fz_track_id(&project, 0), 0);
    fz_home_draw(fb2, &project);
    EXPECT(memcmp(fb, fb2, sizeof fb) != 0);
    fz_track_set_volume(&project, fz_track_id(&project, 0), 127);
    EXPECT(project.track[0].volume == 127);

    for (i = 0; i < 64; i++)
        pcm[i] = 10000;
    EXPECT(fz_project_add_pcm(&project, pcm, 64, FZ_ENGINE_RATE_HZ, 0, &asset) == FZ_OK);
    EXPECT(fz_project_add_region(&project, project.track[0].instrument, asset, 36, 0, &region) == FZ_OK);
    project.selected_track = 0;
    fz_track_set_volume(&project, fz_track_id(&project, 0), 127);
    fz_track_set_pan(&project, fz_track_id(&project, 0), 0);
    fz_engine_init(&engine);
    EXPECT(fz_performance_note(&engine, &project, 36, 127, 1) >= 0);
    fz_engine_render(&engine, mix_a, 4);
    EXPECT(mix_a[0] > 9000 && mix_a[1] == 0);

    fz_track_set_cutoff(&project, fz_track_id(&project, 0), 10);
    fz_engine_init(&engine);
    fz_performance_note(&engine, &project, 36, 127, 1);
    fz_engine_render(&engine, mix_a, 4);
    fz_track_set_cutoff(&project, fz_track_id(&project, 0), 120);
    fz_engine_init(&engine2);
    fz_performance_note(&engine2, &project, 36, 127, 1);
    fz_engine_render(&engine2, mix_b, 4);
    EXPECT(memcmp(mix_a, mix_b, 4u * 2u * sizeof(int16_t)) == 0);

    fz_engine_init(&engine);
    for (i = 0; i < FZ_MAX_VOICES + 1; i++)
        EXPECT(fz_engine_note_on_pcm(&engine, pcm, 64, FZ_ENGINE_RATE_HZ, 60, 60, 0, 100, 80, 64, 0, 0, 1) >= 0);
    EXPECT(engine.stolen >= 1);
    fz_engine_render(&engine, mix_a, 8);
}

static void test_gated_notes(void)
{
    FzEngine engine;
    int16_t pcm[32];
    int16_t mix[8];
    int i;
    for (i = 0; i < 32; i++)
        pcm[i] = 8000;
    fz_engine_init(&engine);
    EXPECT(fz_engine_note_on_pcm(&engine, pcm, 32, FZ_ENGINE_RATE_HZ, 60, 60, 0, 127, 127, 64, 0, 0, 3) >= 0);
    EXPECT(fz_engine_note_on_pcm(&engine, pcm, 32, FZ_ENGINE_RATE_HZ, 60, 60, 0, 127, 127, 64, 0, 0, 3) >= 0);
    fz_engine_note_off(&engine, 60);
    fz_engine_note_off(&engine, 60);
    fz_engine_render(&engine, mix, 4);
    EXPECT(mix[0] == 0 && mix[1] == 0 && mix[2] == 0);
    EXPECT(fz_engine_asset_busy(&engine, 3) == 0);
}

int main(void)
{
    extern int g_failed;
    extern int g_passed;
    test_units();
    test_pitch_ratio();
    test_cycles();
    test_src();
    test_project_and_zones();
    test_capture();
    test_input_history_tempo();
    test_additive();
    test_home_and_mix();
    test_gated_notes();
    test_product();
    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
