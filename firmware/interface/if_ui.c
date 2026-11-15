/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* 240x240 interface pages. Mix, sequence and arp only: there is no
 * equalizer page. Colors are the shared instrument palette. */
#include "if.h"

#include "ui/font.h"

#include <string.h>

const uint32_t if_palette[IF_COL_COUNT] = {0x080910u, 0xf5f5efu, 0x1b2030u, 0x294f88u, 0x52cb80u,
                                            0xf4bf35u, 0xb9c2d5u, 0xf27575u};

_Static_assert(IF_DIV_COUNT == 10, "rate labels cover every division");

static void px(uint8_t *fb, int x, int y, uint8_t color)
{
    if ((unsigned)x < IF_UI_W && (unsigned)y < IF_UI_H)
        fb[(unsigned)y * IF_UI_W + (unsigned)x] = color;
}

static void fill(uint8_t *fb, int x, int y, int w, int h, uint8_t color)
{
    int row, col;
    for (row = 0; row < h; row++)
        for (col = 0; col < w; col++)
            px(fb, x + col, y + row, color);
}

static void stroke(uint8_t *fb, int x, int y, int w, int h, uint8_t color)
{
    int col, row;
    for (col = 0; col < w; col++) {
        px(fb, x + col, y, color);
        px(fb, x + col, y + h - 1, color);
    }
    for (row = 0; row < h; row++) {
        px(fb, x, y + row, color);
        px(fb, x + w - 1, y + row, color);
    }
}

static void glyph(uint8_t *fb, int x, int y, char c, int scale, uint8_t color)
{
    uint8_t rows[8];
    int row, col, sy, sx;
    fz_font_rows(c, rows);
    for (row = 0; row < 8; row++)
        for (col = 0; col < 8; col++)
            if (rows[row] & (uint8_t)(0x80u >> col))
                for (sy = 0; sy < scale; sy++)
                    for (sx = 0; sx < scale; sx++)
                        px(fb, x + col * scale + sx, y + row * scale + sy, color);
}

static void text(uint8_t *fb, int x, int y, const char *s, int scale, uint8_t color)
{
    while (s && *s) {
        if (*s != ' ')
            glyph(fb, x, y, *s, scale, color);
        x += 8 * scale;
        s++;
    }
}

static void put_u(char *dst, unsigned n)
{
    char tmp[8];
    unsigned i = 0, k = 0;
    if (!n)
        tmp[i++] = '0';
    while (n && i < sizeof tmp) {
        tmp[i++] = (char)('0' + n % 10u);
        n /= 10u;
    }
    while (i)
        dst[k++] = tmp[--i];
    dst[k] = 0;
}

static void meter(uint8_t *fb, int y, uint16_t level)
{
    unsigned wide = (unsigned)level * IF_METER_W / 32768u;
    if (wide > IF_METER_W)
        wide = IF_METER_W;
    fill(fb, IF_METER_X, y, IF_METER_W, 8, IF_COL_PANEL);
    if (wide)
        fill(fb, IF_METER_X, y, (int)wide, 8, IF_COL_GREEN);
}

static const char *arp_name(unsigned mode)
{
    static const char *const name[] = {"OFF", "UP", "DOWN", "UPDN", "RND", "ORD", "REP"};
    if (mode > IF_ARP_REPEAT)
        return "OFF";
    return name[mode];
}

static const char *div_name(unsigned div)
{
    static const char *const name[] = {"1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T", "1/2", "1/1", "2BAR", "4BAR"};
    if (div >= IF_DIV_COUNT)
        return "1/16";
    return name[div];
}

static const char *out_name(unsigned mode)
{
    if (mode == IF_OUT_LOOP)
        return "LOOP";
    if (mode == IF_OUT_LOCAL)
        return "LOCAL";
    return "MIX";
}

const char *if_page_name(unsigned page)
{
    static const char *const name[] = {"MIX", "SEQ", "ARP"};
    if (page >= IF_PAGE_COUNT)
        return "MIX";
    return name[page];
}

int if_eq_bands(void)
{
    return IF_EQ_BANDS;
}

void if_ui_draw(const IfMachine *m, uint8_t *fb)
{
    const IfTrack *t;
    char bpm[8], oct[2];
    unsigned i, len;
    if (!m || !fb)
        return;
    t = &m->track[m->sel];
    memset(fb, IF_COL_BG, IF_UI_W * IF_UI_H);
    text(fb, 8, 6, "INTERFACE", 2, IF_COL_TEXT);
    fill(fb, 4, 38, 232, 22, IF_COL_BLUE);
    text(fb, 8, 42, if_page_name(m->page), 2, IF_COL_TEXT);
    if ((m->rec >> m->sel) & 1u)
        text(fb, IF_REC_X, IF_REC_Y, "REC", 1, IF_COL_RED);
    if (m->clipped)
        fill(fb, 224, 8, 8, 8, IF_COL_RED);
    put_u(bpm, m->bpm);
    if (m->page == IF_PAGE_MIX) {
        if (m->focus == 0)
            stroke(fb, 4, 66, 232, 20, IF_COL_AMBER);
        text(fb, 8, IF_METER_Y - 2, "L", 1, IF_COL_TEXT);
        meter(fb, IF_METER_Y, m->meter_l);
        text(fb, 8, 102, "R", 1, IF_COL_TEXT);
        meter(fb, 112, m->meter_r);
        text(fb, 8, 136, "LIMIT", 1, IF_COL_TEXT);
        text(fb, 64, 136, "ON", 1, IF_COL_GREEN);
        text(fb, 8, 160, "OUT", 1, IF_COL_TEXT);
        text(fb, 48, 160, out_name(m->out_mode), 1, IF_COL_AMBER);
        text(fb, 8, 188, bpm, 2, IF_COL_TEXT);
        text(fb, 8, 214, m->message, 1, IF_COL_TEXT);
    } else if (m->page == IF_PAGE_SEQ) {
        text(fb, 8, 70, bpm, 2, IF_COL_TEXT);
        text(fb, 72, 78, "BPM", 1, IF_COL_MUTED);
        len = t->length ? t->length : 1u;
        for (i = 0; i < 16u; i++) {
            int x = IF_STEP_X0 + (int)i * (IF_STEP_S + IF_STEP_G);
            int filled = t->step[i % len].n != 0;
            fill(fb, x, IF_STEP_Y, IF_STEP_S, IF_STEP_S, filled ? IF_COL_GREEN : IF_COL_BLUE);
            if ((m->playing && ((unsigned)t->seq_idx % 16u) == i) || (!m->playing && i == 0u))
                stroke(fb, x, IF_STEP_Y, IF_STEP_S, IF_STEP_S, IF_COL_AMBER);
        }
        text(fb, 8, 160, m->message, 1, IF_COL_AMBER);
    } else {
        text(fb, 8, 78, arp_name(t->arp_mode), 2, IF_COL_TEXT);
        text(fb, 8, 112, "RATE", 1, IF_COL_MUTED);
        text(fb, 48, 112, div_name(t->arp_rate), 1, IF_COL_AMBER);
        text(fb, 8, 136, "OCT", 1, IF_COL_MUTED);
        oct[0] = (char)('0' + (t->arp_oct > 9u ? 9u : (t->arp_oct ? t->arp_oct : 1u)));
        oct[1] = 0;
        text(fb, 48, 136, oct, 1, IF_COL_AMBER);
        if (m->focus == 0)
            stroke(fb, 4, 74, 96, 22, IF_COL_AMBER);
        text(fb, 8, 176, m->message, 1, IF_COL_TEXT);
    }
}
