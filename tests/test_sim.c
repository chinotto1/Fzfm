/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "check.h"
#include "sim.h"
#include "ui/home.h"

#include <string.h>

static void count_slot_pixels(int rows, int *hi_n, int *on_n, int *head_n)
{
    uint16_t *frame = sim_lcd();
    uint16_t hi = FZ_RGB(40, 90, 160);
    uint16_t on = FZ_RGB(80, 200, 120);
    uint16_t head = FZ_RGB(240, 180, 40);
    int i, y, x, n;
    *hi_n = *on_n = *head_n = 0;
    n = rows * 8;
    for (i = 0; i < n; i++) {
        int x0 = 4 + (i % 8) * 29;
        int y0 = 56 + (i / 8) * 36;
        for (y = y0; y < y0 + 28; y++) {
            for (x = x0; x < x0 + 26; x++) {
                uint16_t p = frame[y * FZ_LCD_W + x];
                if (p == hi)
                    (*hi_n)++;
                else if (p == on)
                    (*on_n)++;
                else if (p == head)
                    (*head_n)++;
            }
        }
    }
}

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
    EXPECT(strstr(sim_status(), "PRESET SINE") != 0);
    sim_encoder(6, 1);
    EXPECT(strstr(sim_status(), "PRESET SAW") != 0);
    sim_encoder(6, 1);
    EXPECT(strstr(sim_status(), "PRESET SQUARE") != 0);
    sim_encoder(6, 1);
    EXPECT(strstr(sim_status(), "PRESET TRI") != 0);
    sim_encoder(6, 1);
    EXPECT(strstr(sim_status(), "PRESET SINE") != 0);
    sim_encoder(6, -1);
    EXPECT(strstr(sim_status(), "PRESET TRI") != 0);
    sim_encoder(6, 1);
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
    peak = sim_render(buf, 128);
    EXPECT(peak == 0);

    sim_key(7, 1, 1000);
    sim_key(10, 1, 2000);
    sim_key(7, 0, 3000);
    peak = sim_render(buf, 64);
    EXPECT(peak > 100);
    sim_key(10, 0, 4000);
    peak = sim_render(buf, 64);
    EXPECT(peak == 0);

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
    sim_button(4, 1, 1000);
    sim_button(4, 0, 4000);
    EXPECT(strstr(sim_status(), "ENV") != 0);
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
    for (i = 0; i < 5; i++) {
        sim_button(6, 1, 1000u + (uint32_t)i * 10000u);
        sim_button(6, 0, 4000u + (uint32_t)i * 10000u);
    }
    EXPECT(strstr(sim_status(), "WAVE H1 256") != 0);
    EXPECT(strstr(sim_status(), "ENV") == 0);
    sim_encoder(3, 1);
    EXPECT(strstr(sim_status(), "WAVE H2 0") != 0);
    sim_encoder(4, 4);
    EXPECT(strstr(sim_status(), "WAVE H2 32") != 0);
    sim_key(7, 1, 200000);
    peak = sim_render(buf, 64);
    EXPECT(peak > 100);
    sim_key(7, 0, 210000);

    sim_boot();
    sim_button(6, 1, 1000);
    sim_button(6, 0, 4000);
    EXPECT(strstr(sim_status(), "WAVE") != 0);
    EXPECT(strstr(sim_status(), "LOOP") == 0);
    sim_button(6, 1, 10000);
    sim_button(6, 0, 14000);
    EXPECT(strstr(sim_status(), "DRAW P0") != 0);
    sim_encoder(4, 1);
    EXPECT(strstr(sim_status(), "DRAW P0 1024") != 0);
    sim_encoder(3, 1);
    EXPECT(strstr(sim_status(), "DRAW P8") != 0);

    sim_boot();
    sim_button(4, 1, 1000);
    sim_button(4, 0, 4000);
    EXPECT(strstr(sim_status(), "ENV") != 0);
    {
        uint16_t *frame = sim_lcd();
        uint16_t bg = FZ_RGB(8, 10, 16);
        int amp = 0, flt = 0, y, x;
        for (y = 44; y < 100; y++) {
            for (x = 10; x < 110; x++)
                if (frame[y * FZ_LCD_W + x] != bg)
                    amp++;
            for (x = 126; x < 228; x++)
                if (frame[y * FZ_LCD_W + x] != bg)
                    flt++;
        }
        EXPECT(amp > 30);
        EXPECT(flt > 30);
    }
    sim_button(6, 1, 8000);
    sim_button(6, 0, 12000);
    EXPECT(strstr(sim_status(), "WAVE") != 0);
    EXPECT(strstr(sim_status(), "ENV") == 0);
    sim_button(5, 1, 20000);
    sim_button(5, 0, 24000);
    EXPECT(strstr(sim_status(), "LFO OFF") != 0);
    EXPECT(strstr(sim_status(), "TRI") != 0);
    {
        uint16_t *frame = sim_lcd();
        uint16_t ink = FZ_RGB(240, 180, 40);
        int drawn = 0, y, x;
        for (y = 44; y < 90; y++)
            for (x = 8; x < 230; x++)
                if (frame[y * FZ_LCD_W + x] == ink)
                    drawn++;
        EXPECT(drawn > 40);
        drawn = 0;
        for (y = 112; y < 134; y++)
            for (x = 8; x < 230; x++)
                if (frame[y * FZ_LCD_W + x] == ink || frame[y * FZ_LCD_W + x] == FZ_RGB(80, 200, 120))
                    drawn++;
        EXPECT(drawn > 40);
    }
    sim_encoder(4, 1);
    EXPECT(strstr(sim_status(), "LFO CUTOFF") != 0);
    sim_encoder(4, 1);
    EXPECT(strstr(sim_status(), "LFO LEVEL") != 0);
    sim_encoder(4, 1);
    EXPECT(strstr(sim_status(), "LFO PITCH") != 0);
    sim_encoder(4, 1);
    EXPECT(strstr(sim_status(), "LFO PAN") != 0);
    sim_encoder(5, 1);
    EXPECT(strstr(sim_status(), "SQR") != 0);
    sim_encoder(5, 1);
    EXPECT(strstr(sim_status(), "SIN") != 0);
    sim_encoder(5, 1);
    EXPECT(strstr(sim_status(), "SAW") != 0);
    sim_encoder(5, 1);
    EXPECT(strstr(sim_status(), "SH") != 0);

    sim_boot();
    sim_button(10, 1, 1000);
    sim_button(10, 0, 4000);
    EXPECT(strstr(sim_status(), "ARP") != 0);
    EXPECT(strstr(sim_status(), "OFF") != 0);
    EXPECT(strstr(sim_status(), "1/16") != 0);
    EXPECT(strstr(sim_status(), "UP") == 0);
    sim_encoder(2, 1);
    EXPECT(strstr(sim_status(), "UP") != 0);
    EXPECT(strstr(sim_status(), "OFF") == 0);
    sim_key(0, 1, 20000);
    sim_key(7, 1, 21000);
    peak = sim_render(buf, 256);
    EXPECT(peak > 100);
    {
        uint16_t *frame = sim_lcd();
        uint16_t head = FZ_RGB(240, 180, 40);
        uint16_t bar = FZ_RGB(80, 200, 120);
        int drawn = 0, y, x;
        for (y = 40; y < 120; y++)
            for (x = 4; x < 80; x++)
                if (frame[y * FZ_LCD_W + x] == head || frame[y * FZ_LCD_W + x] == bar)
                    drawn++;
        EXPECT(drawn > 20);
    }
    sim_key(0, 0, 30000);
    sim_key(7, 0, 31000);
    peak = sim_render(buf, 256);
    EXPECT(peak == 0);
    sim_button(10, 1, 40000);
    sim_button(10, 0, 44000);
    EXPECT(strstr(sim_status(), "ARP2") != 0);
    sim_encoder(4, 1);
    sim_key(7, 1, 50000);
    peak = sim_render(buf, 256);
    EXPECT(peak > 100);
    sim_key(7, 0, 60000);
    peak = sim_render(buf, 256);
    EXPECT(peak > 100);

    sim_boot();
    for (i = 0; i < 8; i++)
        sim_key(i, 1, 1000u + (uint32_t)i * 1000u);
    EXPECT(sim_voices() == 8);
    sim_key(7, 0, 20000);
    EXPECT(sim_voices() == 7);
    peak = sim_render(buf, 64);
    EXPECT(peak > 100);
    sim_key(7, 1, 21000);
    EXPECT(sim_voices() == 8);
    sim_key(8, 1, 22000);
    EXPECT(sim_voices() == 8);
    for (i = 1; i <= 8; i++)
        sim_key(i, 0, 30000u + (uint32_t)i * 1000u);
    EXPECT(sim_voices() == 0);
    peak = sim_render(buf, 64);
    EXPECT(peak == 0);

    sim_boot();
    sim_button(11, 1, 1000);
    sim_button(11, 0, 4000);
    sim_key(0, 1, 8000);
    sim_key(0, 0, 9000);
    sim_button(9, 1, 12000);
    sim_button(9, 0, 16000);
    EXPECT(strstr(sim_status(), "PROJECT") != 0);
    EXPECT(strstr(sim_status(), "SAVE") != 0);
    EXPECT(strstr(sim_status(), "E1") != 0);
    EXPECT(strstr(sim_status(), "NO FLASH") == 0);
    sim_encoder(2, 1);
    EXPECT(strstr(sim_status(), "LOAD") != 0);
    sim_encoder(2, -1);
    EXPECT(strstr(sim_status(), "LOAD") == 0);
    sim_button(1, 1, 20000);
    sim_button(1, 0, 24000);
    EXPECT(sim_file_kind() == 1);
    EXPECT(sim_file_len() > 32);
    {
        static uint8_t kept[16384];
        uint32_t n = sim_file_len();
        EXPECT(n <= sizeof kept);
        memcpy(kept, sim_file_ptr(), n);
        sim_boot();
        memcpy(sim_file_ptr(), kept, n);
        EXPECT(sim_load(n) == 0);
        sim_button(9, 1, 30000);
        sim_button(9, 0, 34000);
        EXPECT(strstr(sim_status(), "E1") != 0);
        EXPECT(strstr(sim_status(), "LOADED") != 0);
    }

    sim_boot();
    sim_button(13, 1, 1000);
    sim_button(13, 0, 4000);
    EXPECT(strstr(sim_status(), "CAPTURE") == 0);
    sim_button(13, 1, 10000);
    sim_poll(10000 + 600000);
    sim_button(13, 0, 10000 + 700000);
    EXPECT(strstr(sim_status(), "CAPTURE") != 0);
    EXPECT(strstr(sim_status(), "SAMPLING SAMPLE") != 0);
    EXPECT(strstr(sim_status(), "C 2") != 0);
    EXPECT(strstr(sim_status(), "WAIT") != 0);
    {
        int hi_n, on_n, head_n;
        count_slot_pixels(1, &hi_n, &on_n, &head_n);
        EXPECT(hi_n == 5720);
        EXPECT(on_n == 0);
        EXPECT(head_n == 104);
    }
    sim_encoder(2, 1);
    EXPECT(strstr(sim_status(), "SAMPLING MULTI") != 0);
    {
        int hi_n, on_n, head_n;
        count_slot_pixels(2, &hi_n, &on_n, &head_n);
        EXPECT(hi_n == 11544);
        EXPECT(on_n == 0);
        EXPECT(head_n == 104);
    }
    sim_encoder(2, -1);
    EXPECT(strstr(sim_status(), "SAMPLING SAMPLE") != 0);
    {
        int hi_n, on_n, head_n;
        count_slot_pixels(1, &hi_n, &on_n, &head_n);
        EXPECT(hi_n == 5720);
        EXPECT(head_n == 104);
    }
    {
        int16_t *dst = sim_capture_buf();
        int cap = sim_capture_cap();
        int sent = 0;
        while (sent < 12000) {
            int n = 12000 - sent;
            int s;
            if (n > cap)
                n = cap;
            for (s = 0; s < n; s++)
                dst[s] = (sent + s) < 4000 ? 8000 : 0;
            sim_capture(n);
            sent += n;
        }
    }
    EXPECT(strstr(sim_status(), "WAIT 2") != 0);
    EXPECT(strstr(sim_status(), "C 3") != 0);
    {
        int hi_n, on_n, head_n;
        count_slot_pixels(1, &hi_n, &on_n, &head_n);
        EXPECT(on_n == 728);
        EXPECT(head_n == 104);
        EXPECT(hi_n == 4992);
    }
    sim_button(1, 1, 2000000);
    sim_button(1, 0, 2004000);
    EXPECT(strstr(sim_status(), "HOME") != 0);
    sim_key(7, 1, 2010000);
    peak = sim_render(buf, 64);
    EXPECT(peak > 100);
    sim_key(7, 0, 2020000);

    sim_boot();
    sim_button(13, 1, 1000);
    sim_poll(1000 + 600000);
    sim_button(13, 0, 1000 + 700000);
    sim_button(8, 1, 800000);
    sim_button(8, 0, 808000);
    EXPECT(strstr(sim_status(), "HOME") != 0);
    EXPECT(strstr(sim_status(), "SAMPLING") == 0);
    sim_key(7, 1, 900000);
    peak = sim_render(buf, 64);
    EXPECT(peak > 100);
    sim_key(7, 0, 910000);
}
