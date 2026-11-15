/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "if.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed, g_passed;

static void expect_true(int cond, const char *msg, const char *file, int line)
{
    if (cond) {
        g_passed++;
        return;
    }
    fprintf(stderr, "FAIL %s:%d %s\n", file, line, msg);
    g_failed++;
}

#define EXPECT(cond) expect_true((cond), #cond, __FILE__, __LINE__)

static IfMachine g;

static void hit(unsigned track, unsigned note, unsigned vel, uint32_t on, uint32_t off)
{
    if_note(&g, track, note, vel, on);
    if (off)
        if_note(&g, track, note, 0, off);
}

static int arp_on(void)
{
    int note = -1, i;
    for (i = 0; i < g.arp_log_n; i++)
        if (g.arp_log[i].down) {
            EXPECT(note < 0);
            note = g.arp_log[i].note;
        }
    return note;
}

static int step_has(unsigned track, unsigned step, unsigned note)
{
    const IfStep *s = &g.track[track].step[step];
    unsigned i;
    for (i = 0; i < s->n; i++)
        if (s->note[i] == note)
            return 1;
    return 0;
}

static void write_ppm(const char *path, const uint8_t *fb)
{
    FILE *f = fopen(path, "wb");
    int i;
    EXPECT(f != NULL);
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", IF_UI_W, IF_UI_H);
    for (i = 0; i < IF_UI_W * IF_UI_H; i++) {
        uint32_t rgb = if_palette[fb[i] < IF_COL_COUNT ? fb[i] : 0];
        uint8_t px[3] = {(uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

static void test_formula(void)
{
    EXPECT(IF_RATE == 36000u);
    EXPECT(IF_EQ_BANDS == 0);
    EXPECT(if_eq_bands() == 0);
    EXPECT(if_div_units(IF_DIV_1_16) == 6144u);
    EXPECT(if_div_units(IF_DIV_1_8) == 12288u);
    EXPECT(if_cap_loop_bpm_ms(16, 6144) == 240000u);
    EXPECT(if_cap_loop_bpm_ms(32, 6144) == 480000u);
    EXPECT(if_cap_loop_bpm_ms(64, 6144) == 960000u);
    EXPECT(strcmp(if_page_name(IF_PAGE_MIX), "MIX") == 0);
    EXPECT(strcmp(if_page_name(IF_PAGE_SEQ), "SEQ") == 0);
    EXPECT(strcmp(if_page_name(IF_PAGE_ARP), "ARP") == 0);
    EXPECT(IF_PAGE_COUNT == 3);
}

static void test_router(void)
{
    int16_t impulse[8] = {1000, 1000, 0, 0, 0, 0, 0, 0};
    int16_t out[8];
    int16_t hot[800];
    int16_t speaker[800], usb_out[800];
    int16_t pan_in[2] = {8000, 8000};
    int16_t pan_out[2];
    int i;
    if_init(&g);
    if_advance(&g, 4, impulse, out, NULL);
    EXPECT(out[0] == 1000 && out[1] == 1000);
    EXPECT(out[2] == 0 && out[3] == 0 && out[4] == 0 && out[5] == 0 && out[6] == 0 && out[7] == 0);

    if_init(&g);
    g.ceiling = 10000;
    g.lim_env = 10000;
    for (i = 0; i < 400; i++) {
        hot[2 * i] = 30000;
        hot[2 * i + 1] = 30000;
    }
    if_advance(&g, 400, hot, speaker, NULL);
    EXPECT(speaker[798] > 9000 && speaker[798] < 11000);
    EXPECT(speaker[799] > 9000 && speaker[799] < 11000);
    EXPECT(g.clipped == 1);

    if_init(&g);
    g.usb_pan = 63;
    if_advance(&g, 1, pan_in, pan_out, NULL);
    EXPECT(pan_out[0] < 400 && pan_out[1] > 7000);

    if_init(&g);
    g.usb_mute = 1;
    if_advance(&g, 1, pan_in, pan_out, NULL);
    EXPECT(pan_out[0] == 0 && pan_out[1] == 0);

    if_init(&g);
    g.out_mode = IF_OUT_LOOP;
    if_note(&g, 0, 69, 100, 0);
    memset(speaker, 0, sizeof speaker);
    memset(usb_out, 0, 64);
    if_advance(&g, 32, NULL, speaker, usb_out);
    EXPECT(usb_out[0] == 0 && usb_out[1] == 0 && usb_out[10] == 0);
    EXPECT(speaker[0] != 0 || speaker[2] != 0);
}

static void test_free_take(void)
{
    IfCaptureInfo info;
    uint8_t prior = 60;
    if_init(&g);
    g.track[0].length = 32;
    hit(0, 60, 100, 0, 100);
    hit(0, 64, 90, 500, 700);
    info = if_capture(&g, 2000, 0);
    EXPECT(info.status == IF_CAP_OK);
    EXPECT(info.free_take == 1);
    EXPECT(info.steps == 16);
    EXPECT(info.bpm == 120);
    EXPECT(g.bpm == 120);
    EXPECT(g.playing == 1);
    EXPECT(g.track[0].length == 16);
    EXPECT(strcmp(info.text, "CAPTURED 1 BAR 120 BPM") == 0);
    EXPECT(step_has(0, 0, 60));
    EXPECT(if_undo(&g) == 1);
    EXPECT(g.bpm == 120);
    EXPECT(g.track[0].length == 32);
    EXPECT(!step_has(0, 0, 60));
    EXPECT(strcmp(g.message, "UNDO RECORDING") == 0);
    EXPECT(if_redo(&g) == 1);
    EXPECT(g.track[0].length == 16);
    EXPECT(step_has(0, 0, 60));
    EXPECT(if_undo(&g) == 1);
    EXPECT(if_undo(&g) == 0);

    if_init(&g);
    if_set_bpm(&g, 100);
    hit(0, 60, 100, 0, 200);
    hit(0, 67, 100, 1000, 1200);
    info = if_capture(&g, 2000, 0);
    EXPECT(info.bpm == 120 && g.bpm == 120 && info.steps == 16);

    if_init(&g);
    hit(0, 60, 80, 0, 200);
    hit(0, 62, 80, 1000, 1200);
    info = if_capture(&g, 1983, 0);
    EXPECT(info.status == IF_CAP_OK);
    EXPECT(g.bpm == 120);
    EXPECT(info.bpm == 120);

    if_init(&g);
    if_set_bpm(&g, 100);
    hit(0, 60, 80, 0, 100);
    hit(0, 62, 80, 1000, 1100);
    info = if_capture(&g, 2341, 0);
    EXPECT(g.bpm == 100);
    EXPECT(info.bpm == 100);

    if_init(&g);
    if_set_step(&g, 1, 0, &prior, 1, 100);
    hit(0, 60, 100, 0, 100);
    hit(0, 64, 100, 500, 600);
    info = if_capture(&g, 2000, 0);
    EXPECT(info.status == IF_CAP_OK);
    EXPECT(g.bpm == 120);
    EXPECT(step_has(0, 0, 60));
    EXPECT(step_has(1, 0, 60));

    if_init(&g);
    hit(0, 72, 100, 0, 200);
    info = if_capture(&g, 2000, 0);
    EXPECT(info.status == IF_CAP_OK);
    EXPECT(g.bpm == 120);
    EXPECT(step_has(0, 0, 72));

    if_init(&g);
    g.track[0].division = IF_DIV_1_8;
    hit(0, 60, 100, 0, 200);
    hit(0, 64, 100, 1000, 1200);
    info = if_capture(&g, 2000, 0);
    EXPECT(info.steps == 16);
    EXPECT(g.bpm == 240);

    if_init(&g);
    if_set_bpm(&g, 90);
    hit(0, 60, 100, 0, 200);
    hit(0, 63, 100, 3500, 3600);
    info = if_capture(&g, 4000, 0);
    EXPECT(info.steps == 16);
    EXPECT(g.bpm == 60);

    if_init(&g);
    hit(0, 60, 110, 0, 100);
    hit(0, 64, 70, 500, 600);
    info = if_capture(&g, 2000, 500);
    EXPECT(info.steps == 16);
    EXPECT(g.bpm == 160);
    EXPECT(g.track[0].rec_n >= 2);
    EXPECT(g.track[0].rec[1].on == 85);
    EXPECT(g.track[0].rec[1].step == 5);

    if_init(&g);
    hit(0, 40, 100, 0, 50);
    hit(0, 48, 100, 8001, 8100);
    info = if_capture(&g, 9000, 0);
    EXPECT(info.status == IF_CAP_OK);
    EXPECT(g.bpm == 120);
    EXPECT(step_has(0, 0, 48));
    EXPECT(!step_has(0, 0, 40));

    if_init(&g);
    if_clock_external(&g, 1);
    hit(0, 60, 100, 0, 100);
    hit(0, 64, 100, 400, 500);
    info = if_capture(&g, 2000, 0);
    EXPECT(info.status == IF_CAP_EMPTY);
    EXPECT(g.bpm == 120);
    EXPECT(if_track_empty(&g, 0));

    if_init(&g);
    if_arm(&g, 1);
    hit(0, 60, 100, 0, 100);
    info = if_capture(&g, 2000, 0);
    EXPECT(info.status == IF_CAP_RECORDING);
    EXPECT(strcmp(g.message, "RECORDING") == 0);
    EXPECT(if_track_empty(&g, 0));

    if_init(&g);
    hit(0, 60, 100, 0, 100);
    hit(0, 64, 100, 400, 500);
    if_select(&g, 1);
    info = if_capture(&g, 2000, 0);
    EXPECT(info.status == IF_CAP_EMPTY);
    EXPECT(if_track_empty(&g, 0));
    EXPECT(if_track_empty(&g, 1));
}

static void test_transport_capture(void)
{
    IfCaptureInfo info;
    uint8_t kick = 36, hat = 42;
    int i;
    if_init(&g);
    if_play(&g, 1);
    if_advance(&g, 4500, NULL, NULL, NULL);
    EXPECT(g.track[0].steps_entered == 1);
    EXPECT(g.track[0].seq_idx == 0);
    EXPECT(g.track[0].cap_step == 0);
    hit(0, 60, 100, g.ms, g.ms + 1);
    for (i = 0; i < 16; i++)
        if_advance(&g, 4500, NULL, NULL, NULL);
    EXPECT(g.track[0].cap_step == 16);
    hit(0, 64, 90, g.ms, g.ms + 1);
    info = if_capture(&g, g.ms, 0);
    EXPECT(info.status == IF_CAP_OK);
    EXPECT(info.free_take == 0);
    EXPECT(info.steps == 32);
    EXPECT(g.track[0].length == 32);
    EXPECT(g.bpm == 120);
    EXPECT(strcmp(info.text, "CAPTURED 2 BARS") == 0);
    EXPECT(step_has(0, 0, 60));
    EXPECT(step_has(0, 16, 64));

    if_init(&g);
    if_set_step(&g, 0, 0, &kick, 1, 100);
    if_play(&g, 1);
    if_advance(&g, 4500, NULL, NULL, NULL);
    hit(0, hat, 40, g.ms, g.ms + 1);
    info = if_capture(&g, g.ms, 0);
    EXPECT(info.status == IF_CAP_OK);
    EXPECT(step_has(0, 0, kick));
    EXPECT(step_has(0, 0, hat));
    EXPECT(g.track[0].step[0].vel == 100);
    EXPECT(if_undo(&g) == 1);
    EXPECT(step_has(0, 0, kick));
    EXPECT(!step_has(0, 0, hat));

    if_init(&g);
    if_arm(&g, 1);
    if_play(&g, 1);
    if_advance(&g, 4500, NULL, NULL, NULL);
    hit(0, 60, 100, g.ms, g.ms + 1);
    info = if_capture(&g, g.ms, 0);
    EXPECT(info.status == IF_CAP_RECORDING);
    EXPECT(if_track_empty(&g, 0));
}

static void test_ring(void)
{
    IfCaptureInfo info;
    int i, found = 0;
    if_init(&g);
    hit(0, 10, 100, 0, 5);
    for (i = 1; i < 130; i++)
        hit(0, 20 + (unsigned)(i % 20), 80, (uint32_t)i * 10u, (uint32_t)i * 10u + 5u);
    info = if_capture(&g, 2000, 0);
    EXPECT(info.status == IF_CAP_OK);
    EXPECT(g.track[0].rec_n == IF_REC_MAX);
    for (i = 0; i < g.track[0].rec_n; i++)
        if (g.track[0].rec[i].note == 10)
            found = 1;
    EXPECT(!found);
}

static void test_arp_and_grid(void)
{
    int i, note;
    uint8_t pitches[17];
    if_init(&g);
    g.track[0].arp_mode = IF_ARP_UP;
    if_note(&g, 0, 67, 100, 0);
    if_note(&g, 0, 60, 100, 1);
    if_note(&g, 0, 64, 100, 2);
    if_advance(&g, 1, NULL, NULL, NULL);
    EXPECT(arp_on() == 60);
    if_advance(&g, 4500, NULL, NULL, NULL);
    EXPECT(arp_on() == 64);
    if_advance(&g, 4500, NULL, NULL, NULL);
    EXPECT(arp_on() == 67);

    if_init(&g);
    g.track[0].arp_mode = IF_ARP_DOWN;
    if_note(&g, 0, 60, 100, 0);
    if_note(&g, 0, 64, 100, 1);
    if_note(&g, 0, 67, 100, 2);
    if_advance(&g, 1, NULL, NULL, NULL);
    EXPECT(arp_on() == 67);

    if_init(&g);
    g.track[0].arp_mode = IF_ARP_ORDER;
    if_note(&g, 0, 67, 100, 0);
    if_note(&g, 0, 60, 100, 1);
    if_note(&g, 0, 64, 100, 2);
    if_advance(&g, 1, NULL, NULL, NULL);
    EXPECT(arp_on() == 67);
    if_advance(&g, 4500, NULL, NULL, NULL);
    EXPECT(arp_on() == 60);

    if_init(&g);
    g.track[0].arp_mode = IF_ARP_UPDOWN;
    if_note(&g, 0, 60, 100, 0);
    if_note(&g, 0, 64, 100, 1);
    if_note(&g, 0, 67, 100, 2);
    if_advance(&g, 1, NULL, NULL, NULL);
    EXPECT(arp_on() == 60);
    if_advance(&g, 4500, NULL, NULL, NULL);
    EXPECT(arp_on() == 64);
    if_advance(&g, 4500, NULL, NULL, NULL);
    EXPECT(arp_on() == 67);
    if_advance(&g, 4500, NULL, NULL, NULL);
    EXPECT(arp_on() == 64);
    if_advance(&g, 4500, NULL, NULL, NULL);
    EXPECT(arp_on() == 60);

    if_init(&g);
    g.track[0].arp_mode = IF_ARP_REPEAT;
    if_note(&g, 0, 60, 100, 0);
    if_note(&g, 0, 64, 100, 1);
    if_advance(&g, 1, NULL, NULL, NULL);
    EXPECT(arp_on() == 64);
    if_advance(&g, 4500, NULL, NULL, NULL);
    EXPECT(arp_on() == 64);

    if_init(&g);
    g.track[0].arp_mode = IF_ARP_UP;
    g.track[0].arp_hold = 1;
    if_note(&g, 0, 60, 100, 0);
    if_note(&g, 0, 64, 100, 1);
    if_advance(&g, 1, NULL, NULL, NULL);
    EXPECT(arp_on() == 60);
    if_note(&g, 0, 60, 0, 2);
    if_note(&g, 0, 64, 0, 3);
    if_advance(&g, 4500, NULL, NULL, NULL);
    EXPECT(arp_on() == 64);
    if_note(&g, 0, 72, 100, 4);
    if_advance(&g, 1, NULL, NULL, NULL);
    EXPECT(arp_on() == 72);

    if_init(&g);
    g.track[0].arp_mode = IF_ARP_OFF;
    if_note(&g, 0, 60, 100, 0);
    if_advance(&g, 4500, NULL, NULL, NULL);
    EXPECT(g.arp_log_n == 0);
    EXPECT(g.track[0].sound_on == 1);
    EXPECT(g.track[0].sound_note == 60);

    if_init(&g);
    g.track[0].arp_mode = IF_ARP_RANDOM;
    if_note(&g, 0, 60, 100, 0);
    if_note(&g, 0, 64, 100, 1);
    if_note(&g, 0, 67, 100, 2);
    for (i = 0; i < 6; i++) {
        if_advance(&g, i ? 4500 : 1, NULL, NULL, NULL);
        note = arp_on();
        EXPECT(note == 60 || note == 64 || note == 67);
    }

    if_init(&g);
    g.track[0].arp_mode = IF_ARP_UP;
    for (i = 0; i < 17; i++) {
        pitches[i] = (uint8_t)(10 + i);
        if_note(&g, 0, pitches[i], 100, (uint32_t)i);
    }
    EXPECT(g.track[0].nheld == IF_HELD);
    note = 0;
    for (i = 0; i < g.track[0].nheld; i++)
        if (g.track[0].held[i] == 26)
            note = 1;
    EXPECT(note == 0);

    if_init(&g);
    pitches[0] = 38;
    if_set_step(&g, 0, 0, pitches, 1, 110);
    if_play(&g, 1);
    if_advance(&g, 4500, NULL, NULL, NULL);
    EXPECT(g.track[0].steps_entered == 1);
    EXPECT(g.track[0].seq_idx == 0);
    EXPECT(g.track[0].sound_on == 1);
    EXPECT(g.track[0].sound_note == 38);
    if_advance(&g, 4500, NULL, NULL, NULL);
    EXPECT(g.track[0].seq_idx == 1);
    EXPECT(g.track[0].steps_entered == 2);
}

static void test_screen(void)
{
    uint8_t *fb = malloc(IF_UI_W * IF_UI_H);
    uint8_t note = 60;
    int16_t loud[64];
    int i;
    EXPECT(fb != NULL);
    if (!fb)
        return;
    EXPECT(if_palette[IF_COL_BG] == 0x080910u);
    EXPECT(if_palette[IF_COL_TEXT] == 0xf5f5efu);
    EXPECT(if_palette[IF_COL_PANEL] == 0x1b2030u);
    EXPECT(if_palette[IF_COL_BLUE] == 0x294f88u);
    EXPECT(if_palette[IF_COL_GREEN] == 0x52cb80u);
    EXPECT(if_palette[IF_COL_AMBER] == 0xf4bf35u);
    EXPECT(if_palette[IF_COL_MUTED] == 0xb9c2d5u);
    EXPECT(if_palette[IF_COL_RED] == 0xf27575u);

    if_init(&g);
    for (i = 0; i < 32; i++) {
        loud[2 * i] = 16000;
        loud[2 * i + 1] = 16000;
    }
    if_advance(&g, 32, loud, NULL, NULL);
    if_ui_draw(&g, fb);
    write_ppm("build/interface-mix.ppm", fb);
    EXPECT(fb[0] == IF_COL_BG);
    EXPECT(fb[48 * IF_UI_W + 70] == IF_COL_BLUE);
    EXPECT(fb[IF_METER_Y * IF_UI_W + IF_METER_X] == IF_COL_GREEN);
    EXPECT(fb[IF_REC_Y * IF_UI_W + IF_REC_X + 1] == IF_COL_BLUE);
    if_arm(&g, 1);
    if_ui_draw(&g, fb);
    EXPECT(fb[IF_REC_Y * IF_UI_W + IF_REC_X + 1] == IF_COL_RED);

    if_init(&g);
    g.page = IF_PAGE_SEQ;
    if_set_step(&g, 0, 0, &note, 1, 100);
    if_ui_draw(&g, fb);
    write_ppm("build/interface-seq.ppm", fb);
    EXPECT(fb[(IF_STEP_Y + 6) * IF_UI_W + (IF_STEP_X0 + 6)] == IF_COL_GREEN);
    EXPECT(fb[(IF_STEP_Y + 6) * IF_UI_W + (IF_STEP_X0 + IF_STEP_S + IF_STEP_G + 6)] == IF_COL_BLUE);
    EXPECT(fb[IF_STEP_Y * IF_UI_W + IF_STEP_X0] == IF_COL_AMBER);

    g.page = IF_PAGE_ARP;
    g.track[0].arp_mode = IF_ARP_UP;
    if_ui_draw(&g, fb);
    write_ppm("build/interface-arp.ppm", fb);
    EXPECT(fb[48 * IF_UI_W + 70] == IF_COL_BLUE);
    EXPECT(fb[74 * IF_UI_W + 4] == IF_COL_AMBER);
    free(fb);
}

int main(void)
{
    test_formula();
    test_router();
    test_free_take();
    test_transport_capture();
    test_ring();
    test_arp_and_grid();
    test_screen();
    printf("interface tests: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
