/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "check.h"
#include "sim.h"
#include "ui/home.h"

#include <string.h>

void test_sim(void)
{
    int16_t buf[512];
    uint16_t *lcd;
    int peak;
    int i;

    sim_boot();
    EXPECT(strstr(sim_status(), "HOME") != 0);
    EXPECT(strstr(sim_status(), "OCT 0") != 0);
    EXPECT(strstr(sim_status(), "BPM 120") != 0);
    lcd = sim_lcd();
    EXPECT(lcd[FZ_HOME_LIST_Y * FZ_LCD_W + 10] == FZ_RGB(40, 90, 160));

    sim_button(1, 1, 1000);
    sim_button(1, 0, 8000);
    EXPECT(strstr(sim_status(), "OCT 1") != 0);

    sim_encoder(0, 1);
    EXPECT(strstr(sim_status(), "SELECT TURNS") != 0);

    sim_key(7, 1, 20000);
    peak = sim_render(buf, 64);
    EXPECT(peak > 100);
    sim_key(7, 0, 30000);

    sim_button(6, 1, 40000);
    sim_button(6, 0, 48000);
    EXPECT(strstr(sim_status(), "EDIT") != 0);

    sim_button(1, 1, 60000);
    sim_poll(60000 + 600000);
    sim_button(1, 0, 60000 + 700000);
    EXPECT(strstr(sim_status(), "OCT 1") != 0);

    sim_button(11, 1, 800000);
    sim_button(11, 0, 808000);
    EXPECT(strstr(sim_status(), "STEP") != 0);
    sim_key(0, 1, 820000);
    sim_key(0, 0, 830000);
    sim_button(12, 1, 840000);
    sim_button(12, 0, 848000);
    EXPECT(strstr(sim_status(), "PLAY") != 0);
    peak = sim_render(buf, 256);
    EXPECT(peak > 100);

    sim_boot();
    sim_button(6, 1, 1000);
    sim_button(6, 0, 4000);
    sim_button(1, 1, 10000);
    sim_button(1, 0, 14000);
    EXPECT(strstr(sim_status(), "NUMERIC") != 0);
    sim_key(1, 1, 20000);
    sim_key(1, 0, 21000);
    sim_key(5, 1, 22000);
    sim_key(5, 0, 23000);
    sim_button(1, 1, 30000);
    sim_button(1, 0, 34000);
    EXPECT(strstr(sim_status(), "EDIT") != 0);
    EXPECT(strstr(sim_status(), "NUMERIC") == 0);

    sim_boot();
    sim_button(11, 1, 1000);
    sim_button(11, 0, 4000);
    sim_button(12, 1, 8000);
    sim_button(12, 0, 12000);
    {
        uint16_t *frame = sim_lcd();
        uint16_t mark = FZ_RGB(240, 180, 40);
        EXPECT(frame[64 * FZ_LCD_W + 6] == mark);
        for (i = 0; i < 20; i++)
            sim_render(buf, 256);
        EXPECT(frame[64 * FZ_LCD_W + 6] != mark);
        EXPECT(frame[64 * FZ_LCD_W + 35] == mark);
    }
    for (i = 0; i < 400; i++)
        sim_render(buf, 256);
    EXPECT(sim_tick() < 384);

    sim_boot();
    for (i = 0; i < 7; i++) {
        sim_button(6, 1, 1000u + (uint32_t)i * 10000u);
        sim_button(6, 0, 4000u + (uint32_t)i * 10000u);
    }
    EXPECT(strstr(sim_status(), "WAVE H1 256") != 0);
    sim_encoder(3, 1);
    EXPECT(strstr(sim_status(), "WAVE H2 0") != 0);
    sim_encoder(4, 4);
    EXPECT(strstr(sim_status(), "WAVE H2 32") != 0);
    sim_key(7, 1, 200000);
    peak = sim_render(buf, 64);
    EXPECT(peak > 100);
    sim_key(7, 0, 210000);

    sim_boot();
    sim_button(4, 1, 1000);
    sim_button(4, 0, 4000);
    EXPECT(strstr(sim_status(), "ENV") != 0);
    sim_button(5, 1, 10000);
    sim_button(5, 0, 14000);
    EXPECT(strstr(sim_status(), "LFO OFF") != 0);
    sim_encoder(4, 1);
    EXPECT(strstr(sim_status(), "LFO CUTOFF") != 0);
    sim_encoder(4, 1);
    EXPECT(strstr(sim_status(), "LFO LEVEL") != 0);
    sim_encoder(4, 1);
    EXPECT(strstr(sim_status(), "LFO PITCH") != 0);
    sim_encoder(4, 1);
    EXPECT(strstr(sim_status(), "LFO PAN") != 0);
}
