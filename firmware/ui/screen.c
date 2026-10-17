/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "screen.h"

#include "font.h"
#include "ui/edit.h"

#include <stdio.h>
#include <string.h>

static void fill(uint16_t *px, uint16_t color)
{
    uint32_t i;
    for (i = 0; i < (uint32_t)FZ_LCD_W * FZ_LCD_H; i++)
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
    if (!s)
        return;
    while (*s && x < FZ_LCD_W - 8) {
        glyph(px, x, y, *s, color);
        x += 8;
        s++;
    }
}

static void tempo_text(char *dst, int cap, const FzProject *project, const FzSeq *seq)
{
    uint32_t milli = 120000;
    unsigned whole, tenth;
    if (!dst || cap <= 0)
        return;
    if (seq && seq->bpm_milli >= 10000)
        milli = seq->bpm_milli;
    else if (project && project->tempo_bpm > 0.0)
        milli = (uint32_t)(project->tempo_bpm * 1000.0 + 0.5);
    whole = milli / 1000u;
    tenth = (milli % 1000u) / 100u;
    if (tenth)
        snprintf(dst, (size_t)cap, "BPM %u.%u", whole, tenth);
    else
        snprintf(dst, (size_t)cap, "BPM %u", whole);
}

static const char *screen_name(FzScreen screen)
{
    static const char *names[] = {"HOME", "CAPTURE", "EDIT",  "STEP", "PATTERNS", "SONG", "MIXER",
                                  "FX",   "NUMERIC", "NAME",  "CONFIRM", "NOTE", "RANGE"};
    if ((int)screen < 0 || (int)screen >= (int)(sizeof names / sizeof names[0]))
        return "";
    return names[screen];
}

static int step_on(const FzSeq *seq, int pattern, int step)
{
    uint32_t tick;
    int i;
    if (!seq || step < 0)
        return 0;
    tick = (uint32_t)step * (uint32_t)FZ_STEP_TICKS;
    for (i = 0; i < FZ_SEQ_EVENTS; i++) {
        if (seq->ev[i].used && seq->ev[i].pattern == (uint16_t)pattern && seq->ev[i].tick == tick)
            return 1;
    }
    return 0;
}

static void draw_edit(uint16_t *px, const FzProject *project, const FzSession *session, uint16_t fg, uint16_t hi)
{
    char header[64];
    (void)project;
    int n, i, y;
    fz_edit_header(session, project, header, sizeof header);
    text(px, 4, 28, header, fg);
    n = fz_edit_row_count(session->edit_tab);
    for (i = 0; i < n; i++) {
        y = 48 + i * 16;
        if (i == session->row[session->edit_tab])
            rect(px, 0, y, FZ_LCD_W, 16, hi);
        text(px, 4, y + 4, fz_edit_row_name(session->edit_tab, i), fg);
    }
    text(px, 4, 200, "K1 ROW", fg);
    text(px, 80, 200, fz_edit_knob_label(session->edit_tab, session->row[session->edit_tab], 1), fg);
    text(px, 4, 216, "K3 -", fg);
    text(px, 80, 216, "K4 -", fg);
}

static int playhead_step(const FzProject *project, const FzSeq *seq, int pattern)
{
    uint32_t len, tick;
    if (!seq)
        return 0;
    len = fz_seq_pattern_ticks(project, pattern);
    tick = seq->tick;
    if (len > 0)
        tick %= len;
    return (int)(tick / (uint32_t)FZ_STEP_TICKS);
}

static void draw_playhead_strip(uint16_t *px, int step, int y, uint16_t head, uint16_t dim)
{
    int i;
    if (step < 0)
        step = 0;
    for (i = 0; i < 16; i++) {
        int x = 4 + i * 14;
        rect(px, x, y, 12, 6, i == (step % 16) ? head : dim);
    }
}

static void draw_steps(uint16_t *px, const FzProject *project, const FzSession *session, const FzSeq *seq, uint16_t fg,
                       uint16_t on, uint16_t hi, uint16_t head)
{
    int i, pattern, base, play;
    char line[64];
    pattern = seq && seq->current[0] >= 0 ? seq->current[0] : 0;
    base = session->page * 16;
    play = playhead_step(project, seq, pattern);
    snprintf(line, sizeof line, "PAT %d PAGE %d HEAD %d", pattern + 1, session->page + 1, play + 1);
    text(px, 4, 28, line, fg);
    text(px, 4, 44, "WHITE KEYS", fg);
    for (i = 0; i < 16; i++) {
        int x = 4 + (i % 8) * 29;
        int y = 64 + (i / 8) * 36;
        int lit = step_on(seq, pattern, base + i);
        rect(px, x, y, 26, 28, lit ? on : hi);
        if (base + i == play)
            rect(px, x, y, 26, 4, head);
    }
}

static void draw_harmonics(uint16_t *px, const FzProject *project, const FzSession *session, uint16_t fg, uint16_t on)
{
    const FzInstrument *inst = 0;
    int partial, level, i;
    char line[40];
    if (project && project->selected_track >= 0 && project->selected_track < FZ_MAX_TRACKS) {
        FzId id = project->track[project->selected_track].instrument;
        if (id.index < FZ_MAX_INSTRUMENTS && project->instrument[id.index].alive &&
            project->instrument[id.index].generation == id.generation)
            inst = &project->instrument[id.index];
    }
    partial = session->partial;
    if (partial < 0)
        partial = 0;
    if (partial >= FZ_HARMONICS)
        partial = FZ_HARMONICS - 1;
    level = inst ? (int)inst->additive.level[partial] : 0;
    snprintf(line, sizeof line, "H%d %d", partial + 1, level);
    text(px, 4, 80, line, fg);
    text(px, 4, 96, "K2 PARTIAL", fg);
    text(px, 120, 96, "K3 LEVEL", fg);
    for (i = 0; i < FZ_HARMONICS; i++) {
        int h = inst ? (int)inst->additive.level[i] * 36 / 256 : 0;
        int x = 4 + i * 4;
        if (h < 1)
            h = 1;
        rect(px, x, 160 - h, 3, h, i == partial ? fg : on);
    }
}

void fz_screen_draw(uint16_t *rgb565, const FzProject *project, const FzSession *session, const FzSeq *seq,
                    const FzCapture *capture)
{
    const uint16_t bg = FZ_RGB(8, 10, 16);
    const uint16_t fg = FZ_RGB(230, 230, 220);
    const uint16_t hi = FZ_RGB(40, 90, 160);
    const uint16_t on = FZ_RGB(80, 200, 120);
    const uint16_t head = FZ_RGB(240, 180, 40);
    char line[40];
    FzScreen screen = session ? session->screen : FZ_SCR_HOME;

    if (!rgb565)
        return;
    if (screen == FZ_SCR_HOME && project) {
        fz_home_draw(rgb565, project);
    } else {
        fill(rgb565, bg);
    }
    snprintf(line, sizeof line, "%s", screen_name(screen));
    if (seq && seq->playing)
        snprintf(line, sizeof line, "%s PLAY", screen_name(screen));
    text(rgb565, 4, 4, line, fg);
    if (session) {
        snprintf(line, sizeof line, "OCT %d", session->octave);
        text(rgb565, 160, 4, line, fg);
    }
    tempo_text(line, (int)sizeof line, project, seq);
    text(rgb565, 4, 14, line, fg);
    if (screen == FZ_SCR_HOME) {
        int pattern = seq && seq->current[0] >= 0 ? seq->current[0] : 0;
        draw_playhead_strip(rgb565, playhead_step(project, seq, pattern), 176, head, hi);
        return;
    }
    if (screen == FZ_SCR_EDIT && session) {
        draw_edit(rgb565, project, session, fg, hi);
        if (session->edit_tab == FZ_TAB_WAVE)
            draw_harmonics(rgb565, project, session, fg, on);
    }
    else if (screen == FZ_SCR_STEP && session)
        draw_steps(rgb565, project, session, seq, fg, on, hi, head);
    else if (screen == FZ_SCR_PATTERNS && session)
        draw_steps(rgb565, project, session, seq, fg, on, hi, head);
    else if (screen == FZ_SCR_SONG && seq) {
        snprintf(line, sizeof line, "ROW %d / %d", seq->song_n > 0 ? seq->song_pos + 1 : 0, seq->song_n);
        text(rgb565, 4, 40, line, fg);
    } else if (screen == FZ_SCR_MIXER && project && session) {
        const FzTrack *t = 0;
        if (project->selected_track >= 0 && project->selected_track < FZ_MAX_TRACKS)
            t = &project->track[project->selected_track];
        snprintf(line, sizeof line, "TR %d  SEND %d", project->selected_track + 1, session->send_slot + 1);
        text(rgb565, 4, 40, line, fg);
        if (t) {
            snprintf(line, sizeof line, "VOL %d", t->volume);
            text(rgb565, 4, 64, line, fg);
            snprintf(line, sizeof line, "PAN %d", t->pan);
            text(rgb565, 4, 80, line, fg);
            snprintf(line, sizeof line, "S1 %d S2 %d", t->send[0], t->send[1]);
            text(rgb565, 4, 104, line, fg);
            snprintf(line, sizeof line, "S3 %d S4 %d", t->send[2], t->send[3]);
            text(rgb565, 4, 120, line, fg);
            text(rgb565, 4, 150, t->mute ? "MUTE" : "HEARD", fg);
        }
    } else if (screen == FZ_SCR_FX && session) {
        snprintf(line, sizeof line, "SLOT %d", session->fx_slot + 1);
        text(rgb565, 4, 40, line, fg);
        text(rgb565, 4, 64, "DELAY REVERB", fg);
        text(rgb565, 4, 80, "CHORUS DRIVE", fg);
    } else if (screen == FZ_SCR_NUMERIC && session) {
        text(rgb565, 4, 48, "NUMBER", fg);
        text(rgb565, 4, 72, session->numeric_n ? session->numeric : "-", fg);
        text(rgb565, 4, 110, "LOW 10 KEYS 0-9", fg);
        text(rgb565, 4, 130, "OCT+ CONFIRMS", fg);
    } else if (screen == FZ_SCR_NAMING) {
        text(rgb565, 4, 40, "DRUMS SYNTH PAD", fg);
        text(rgb565, 4, 56, "LEAD FX BASS", fg);
        text(rgb565, 4, 72, "KEYS PERC LOOP", fg);
        text(rgb565, 4, 110, "LOW 9 KEYS", fg);
    } else if (screen == FZ_SCR_CAPTURE && capture) {
        text(rgb565, 4, 48, capture->message, fg);
        snprintf(line, sizeof line, "SLOT %d", capture->slot + 1);
        text(rgb565, 4, 72, line, fg);
    } else if (screen == FZ_SCR_PICK_NOTE || screen == FZ_SCR_PICK_RANGE) {
        text(rgb565, 4, 48, "PICK A KEY", fg);
    } else if (screen == FZ_SCR_CONFIRM) {
        text(rgb565, 4, 48, "OCT+ CONFIRMS", fg);
        text(rgb565, 4, 64, "HOLD CANCELS", fg);
    }
}
