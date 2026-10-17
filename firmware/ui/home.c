/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "home.h"

#include "font.h"

#include <stdio.h>
#include <string.h>

static void fill(uint16_t *px, uint16_t color)
{
    uint32_t i;
    for (i = 0; i < FZ_LCD_W * FZ_LCD_H; i++)
        px[i] = color;
}

static void rect(uint16_t *px, int x, int y, int w, int h, uint16_t color)
{
    int yy, xx;
    for (yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= FZ_LCD_H)
            continue;
        for (xx = x; xx < x + w; xx++) {
            if (xx < 0 || xx >= FZ_LCD_W)
                continue;
            px[yy * FZ_LCD_W + xx] = color;
        }
    }
}

static void glyph(uint16_t *px, int x, int y, char c, uint16_t color)
{
    uint8_t rows[8];
    int row, col;
    fz_font_rows(c, rows);
    for (row = 0; row < 8; row++) {
        for (col = 0; col < 8; col++) {
            if (rows[row] & (uint8_t)(0x80u >> col))
                rect(px, x + col, y + row, 1, 1, color);
        }
    }
}

static void text(uint16_t *px, int x, int y, const char *s, uint16_t color)
{
    while (*s) {
        glyph(px, x, y, *s, color);
        x += 8;
        s++;
    }
}

void fz_home_draw(uint16_t *rgb565, const FzProject *project)
{
    static const char *knob[4] = {"CUT", "RES", "VOL", "PAN"};
    int values[4] = {0, 0, 0, 0};
    int row = 0;
    int i;
    const FzTrack *selected = 0;
    char line[40];
    const uint16_t bg = FZ_RGB(8, 10, 16);
    const uint16_t fg = FZ_RGB(230, 230, 220);
    const uint16_t hi = FZ_RGB(40, 90, 160);
    const uint16_t bar = FZ_RGB(80, 200, 120);

    if (!rgb565)
        return;
    fill(rgb565, bg);
    text(rgb565, 4, 4, "HOME", fg);
    if (!project)
        return;
    for (i = 0; i < FZ_MAX_TRACKS; i++) {
        int y;
        const FzInstrument *inst;
        if (!project->track[i].alive)
            continue;
        y = FZ_HOME_LIST_Y + row * FZ_HOME_ROW_H;
        if (i == project->selected_track) {
            rect(rgb565, 0, y, FZ_LCD_W, FZ_HOME_ROW_H, hi);
            selected = &project->track[i];
        }
        inst = 0;
        {
            FzId id = project->track[i].instrument;
            if (id.index < FZ_MAX_INSTRUMENTS && project->instrument[id.index].alive &&
                project->instrument[id.index].generation == id.generation)
                inst = &project->instrument[id.index];
        }
        snprintf(line, sizeof line, "%d %s", row + 1, inst && inst->alive ? inst->name : "EMPTY");
        text(rgb565, 4, y + 4, line, fg);
        row++;
        if (y > FZ_LCD_H - 40)
            break;
    }
    if (selected) {
        values[0] = selected->cutoff;
        values[1] = selected->resonance;
        values[2] = selected->volume;
        values[3] = selected->pan;
    }
    for (i = 0; i < 4; i++) {
        int x = 4 + i * 58;
        int w = (values[i] * FZ_HOME_BAR_MAX) / 127;
        char num[16];
        text(rgb565, x, FZ_HOME_BAR_Y - 12, knob[i], fg);
        rect(rgb565, x, FZ_HOME_BAR_Y, w, 8, bar);
        snprintf(num, sizeof num, "%d", values[i]);
        text(rgb565, x, FZ_HOME_BAR_Y + 12, num, fg);
    }
}
