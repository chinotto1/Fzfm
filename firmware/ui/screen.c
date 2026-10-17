/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "screen.h"

#include "audio/engine.h"
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
                                  "FX",   "NUMERIC", "NAME",  "CONFIRM", "NOTE", "RANGE", "ARP", "SAVE"};
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

static int iabs(int v)
{
    return v < 0 ? -v : v;
}

static void plot(uint16_t *px, int x, int y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= FZ_LCD_W || y >= FZ_LCD_H)
        return;
    px[y * FZ_LCD_W + x] = color;
}

static void line(uint16_t *px, int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    int steps = iabs(dx) > iabs(dy) ? iabs(dx) : iabs(dy);
    int i;
    if (steps < 1) {
        plot(px, x0, y0, color);
        return;
    }
    for (i = 0; i <= steps; i++)
        plot(px, x0 + dx * i / steps, y0 + dy * i / steps, color);
}

static void frame_rect(uint16_t *px, int x, int y, int w, int h, uint16_t color)
{
    if (w < 2 || h < 2)
        return;
    rect(px, x, y, w, 1, color);
    rect(px, x, y + h - 1, w, 1, color);
    rect(px, x, y, 1, h, color);
    rect(px, x + w - 1, y, 1, h, color);
}

static void share4(int w0, int w1, int w2, int w3, int total, int *out)
{
    int w[4];
    int i, sum = 0, used = 0;
    w[0] = w0 < 1 ? 1 : w0;
    w[1] = w1 < 1 ? 1 : w1;
    w[2] = w2 < 1 ? 1 : w2;
    w[3] = w3 < 1 ? 1 : w3;
    for (i = 0; i < 4; i++)
        sum += w[i];
    for (i = 0; i < 3; i++) {
        out[i] = total * w[i] / sum;
        if (out[i] < 3)
            out[i] = 3;
        used += out[i];
    }
    out[3] = total - used;
    if (out[3] < 3) {
        int big = 0;
        for (i = 0; i < 3; i++) {
            if (out[i] > out[big])
                big = i;
        }
        out[big] -= 3 - out[3];
        out[3] = 3;
    }
}

static int env_weight(int frames)
{
    if (frames < 1)
        frames = 1;
    if (frames > 8000)
        frames = 8000;
    return 12 + frames / 20;
}

static int env_y(int level, int top, int bot)
{
    if (level < 0)
        level = 0;
    if (level > 32767)
        level = 32767;
    return bot - level * (bot - top) / 32767;
}

static void draw_adsr(uint16_t *px, int x, int y, int w, int h, int atk, int dec, int sus, int rel, uint16_t color)
{
    int seg[4];
    int top = y + 2;
    int bot = y + h - 3;
    int x0, ypk, ysus, xcur;
    if (w < 16 || bot <= top)
        return;
    share4(env_weight(atk), env_weight(dec), 28, env_weight(rel), w - 4, seg);
    x0 = x + 2;
    ypk = env_y(32767, top, bot);
    ysus = env_y(sus, top, bot);
    xcur = x0;
    line(px, xcur, bot, xcur + seg[0], ypk, color);
    xcur += seg[0];
    line(px, xcur, ypk, xcur + seg[1], ysus, color);
    xcur += seg[1];
    line(px, xcur, ysus, xcur + seg[2], ysus, color);
    xcur += seg[2];
    line(px, xcur, ysus, xcur + seg[3], bot, color);
}

static void plot_lfo(uint16_t *px, int x, int y, int w, int h, int shape, uint16_t color)
{
    int i, prev = y + h / 2;
    int mid = y + h / 2;
    int amp = h / 2 - 1;
    if (w < 2 || amp < 1)
        return;
    for (i = 0; i < w; i++) {
        uint32_t phase = i == w - 1 ? 0xFFFFFFFFu : (uint32_t)(((uint64_t)i << 32) / (uint32_t)(w - 1));
        int sample = fz_lfo_bipolar(phase, shape);
        int yy = mid - sample * amp / 127;
        if (i)
            line(px, x + i - 1, prev, x + i, yy, color);
        else
            plot(px, x, yy, color);
        prev = yy;
    }
}

static void draw_knobs(uint16_t *px, const FzSession *session, uint16_t fg, int y)
{
    text(px, 4, y, "K1 ROW", fg);
    text(px, 80, y, fz_edit_knob_label(session->edit_tab, session->row[session->edit_tab], 1), fg);
}

static void draw_row_span(uint16_t *px, const FzProject *project, const FzSession *session, uint16_t fg, uint16_t hi,
                          int y0, int pitch, int first, int count)
{
    char value[16];
    int n = fz_edit_row_count(session->edit_tab);
    int sel = session->row[session->edit_tab];
    int i;
    if (sel < 0)
        sel = 0;
    if (sel >= n)
        sel = n - 1;
    for (i = 0; i < count; i++) {
        int row = first + i;
        int y = y0 + i * pitch;
        if (row < 0 || row >= n)
            continue;
        if (row == sel)
            rect(px, 0, y, FZ_LCD_W, pitch, hi);
        text(px, 4, y + 2, fz_edit_row_name(session->edit_tab, row), fg);
        fz_edit_row_value(session, project, row, value, sizeof value);
        text(px, 128, y + 2, value, fg);
    }
}

static void draw_env_page(uint16_t *px, const FzProject *project, const FzSession *session, uint16_t fg, uint16_t hi,
                          uint16_t head)
{
    char header[64];
    int atk, dec, sus, rel;
    int n, sel, start, filter;
    fz_edit_header(session, project, header, sizeof header);
    text(px, 4, 28, header, fg);
    filter = session->row[FZ_TAB_AMP] >= 4;
    fz_edit_shown_env(session, project, 0, &atk, &dec, &sus, &rel);
    draw_adsr(px, 8, 42, 108, 64, atk, dec, sus, rel, filter ? fg : head);
    text(px, 8, 108, "AMP", fg);
    fz_edit_shown_env(session, project, 1, &atk, &dec, &sus, &rel);
    draw_adsr(px, 124, 42, 108, 64, atk, dec, sus, rel, filter ? head : fg);
    text(px, 124, 108, "FLT", fg);
    frame_rect(px, filter ? 124 : 8, 42, 108, 64, head);
    n = fz_edit_row_count(FZ_TAB_AMP);
    sel = session->row[FZ_TAB_AMP];
    if (sel < 0)
        sel = 0;
    if (sel >= n)
        sel = n - 1;
    start = sel;
    if (n > 4 && start > n - 4)
        start = n - 4;
    if (start < 0)
        start = 0;
    draw_row_span(px, project, session, fg, hi, 122, 16, start, 4);
    draw_knobs(px, session, fg, 200);
    text(px, 4, 216, "K3 -", fg);
    text(px, 80, 216, "K4 -", fg);
}

static int track_lfo_shape(const FzProject *project)
{
    int shape = 0;
    if (project && project->selected_track >= 0 && project->selected_track < FZ_MAX_TRACKS &&
        project->track[project->selected_track].alive)
        shape = project->track[project->selected_track].lfo.shape;
    if (shape < 0 || shape >= FZ_LFO_SHAPE_COUNT)
        shape = 0;
    return shape;
}

static void draw_lfo_page(uint16_t *px, const FzProject *project, const FzSession *session, uint16_t fg, uint16_t hi,
                          uint16_t on, uint16_t head)
{
    char header[64];
    int shape = track_lfo_shape(project);
    int i;
    fz_edit_header(session, project, header, sizeof header);
    text(px, 4, 28, header, fg);
    plot_lfo(px, 8, 42, 224, 52, shape, head);
    for (i = 0; i < FZ_LFO_SHAPE_COUNT; i++) {
        int x = 8 + i * 46;
        if (i == shape)
            rect(px, x, 98, 44, 40, hi);
        text(px, x + 4, 100, fz_lfo_shape_name(i), fg);
        plot_lfo(px, x + 4, 112, 36, 22, i, i == shape ? head : on);
    }
    draw_row_span(px, project, session, fg, hi, 142, 12, 0, fz_edit_row_count(FZ_TAB_MOD));
    draw_knobs(px, session, fg, 206);
    text(px, 4, 220, "K3 TARGET", fg);
    text(px, 120, 220, "K4 SHAPE", fg);
}

static void draw_edit(uint16_t *px, const FzProject *project, const FzSession *session, uint16_t fg, uint16_t hi,
                      uint16_t on, uint16_t head)
{
    char header[64];
    char value[16];
    int n, i, y;
    if (session->edit_tab == FZ_TAB_AMP) {
        draw_env_page(px, project, session, fg, hi, head);
        return;
    }
    if (session->edit_tab == FZ_TAB_MOD) {
        draw_lfo_page(px, project, session, fg, hi, on, head);
        return;
    }
    fz_edit_header(session, project, header, sizeof header);
    text(px, 4, 28, header, fg);
    n = fz_edit_row_count(session->edit_tab);
    for (i = 0; i < n; i++) {
        y = 48 + i * 16;
        if (i == session->row[session->edit_tab])
            rect(px, 0, y, FZ_LCD_W, 16, hi);
        text(px, 4, y + 4, fz_edit_row_name(session->edit_tab, i), fg);
        fz_edit_row_value(session, project, i, value, sizeof value);
        text(px, 128, y + 4, value, fg);
    }
    draw_knobs(px, session, fg, 200);
    text(px, 4, 216, session->edit_tab == FZ_TAB_DRAW ? "K3 LEVEL" : "K3 -", fg);
    text(px, 80, 216, "K4 -", fg);
}

static const FzAsset *cycle_asset(const FzProject *project)
{
    const FzInstrument *inst;
    const FzRegion *region;
    const FzAsset *asset;
    FzId id;
    if (!project || project->selected_track < 0 || project->selected_track >= FZ_MAX_TRACKS)
        return 0;
    if (!project->track[project->selected_track].alive)
        return 0;
    id = project->track[project->selected_track].instrument;
    if (id.index >= FZ_MAX_INSTRUMENTS)
        return 0;
    inst = &project->instrument[id.index];
    if (!inst->alive || inst->generation != id.generation || inst->region_count < 1)
        return 0;
    id = inst->region[0];
    if (id.index >= FZ_MAX_REGIONS)
        return 0;
    region = &project->region[id.index];
    if (!region->alive || region->generation != id.generation)
        return 0;
    id = region->asset;
    if (id.index >= FZ_MAX_ASSETS)
        return 0;
    asset = &project->asset[id.index];
    if (!asset->alive || asset->generation != id.generation || !asset->cycle || !asset->pcm || asset->frames < 2)
        return 0;
    return asset;
}

static void draw_cycle_plot(uint16_t *px, const FzProject *project, const FzSession *session, uint16_t fg, uint16_t head)
{
    const FzAsset *asset = cycle_asset(project);
    int points, i, cursor, sample, y, top, h;
    if (!asset)
        return;
    points = (int)asset->frames;
    if (points > 4096)
        points = 4096;
    for (i = 0; i < 224; i++) {
        sample = asset->pcm[i * points / 224];
        y = 150 - sample * 28 / 32768;
        if (y < 118)
            y = 118;
        if (y > 182)
            y = 182;
        top = y < 150 ? y : 150;
        h = y < 150 ? 150 - y : y - 150;
        if (h < 1)
            h = 1;
        rect(px, 8 + i, top, 1, h, fg);
    }
    cursor = session ? session->draw_pt : 0;
    if (cursor < 0)
        cursor = 0;
    if (cursor >= points)
        cursor = points - 1;
    rect(px, 8 + cursor * 224 / points, 118, 1, 64, head);
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

static void draw_arp(uint16_t *px, const FzSession *session, uint16_t fg, uint16_t on, uint16_t head)
{
    const FzArp *arp = session ? session->arp : 0;
    int notes[32];
    int cursor = -1;
    int n = 0;
    int page = session && session->arp_page ? 1 : 0;
    char line[40];
    int i;
    if (arp)
        n = fz_arp_pattern(arp, notes, 32, &cursor);
    snprintf(line, sizeof line, page ? "ARP 2" : "ARP");
    text(px, 4, 28, line, fg);
    if (n <= 0) {
        text(px, 4, 64, "HOLD KEYS", fg);
    } else {
        int slot = 220 / n;
        int x0;
        if (slot < 4)
            slot = 4;
        if (slot > 18)
            slot = 18;
        x0 = 8;
        for (i = 0; i < n; i++) {
            int h = (notes[i] - 36) * 52 / 48;
            int x = x0 + i * slot;
            if (h < 3)
                h = 3;
            if (h > 56)
                h = 56;
            rect(px, x, 108 - h, slot - 2, h, i == cursor ? head : on);
        }
    }
    if (!arp)
        return;
    if (!page) {
        snprintf(line, sizeof line, "MODE %s", fz_arp_mode_name(arp->mode));
        text(px, 4, 124, line, fg);
        snprintf(line, sizeof line, "RATE %s", fz_arp_rate_name(arp->rate));
        text(px, 4, 142, line, fg);
        snprintf(line, sizeof line, "OCT %d", arp->octaves);
        text(px, 4, 160, line, fg);
        snprintf(line, sizeof line, "GATE %d", arp->gate);
        text(px, 4, 178, line, fg);
        text(px, 4, 204, "K1 MODE", fg);
        text(px, 120, 204, "K2 RATE", fg);
        text(px, 4, 220, "K3 OCT", fg);
        text(px, 120, 220, "K4 GATE", fg);
    } else {
        snprintf(line, sizeof line, "SWING %d", arp->swing);
        text(px, 4, 124, line, fg);
        snprintf(line, sizeof line, "PROB %d", arp->prob);
        text(px, 4, 142, line, fg);
        snprintf(line, sizeof line, "HOLD %s", arp->hold ? "ON" : "OFF");
        text(px, 4, 160, line, fg);
        snprintf(line, sizeof line, "ORDER %s", arp->order_play ? "PLAY" : "NOTE");
        text(px, 4, 178, line, fg);
        text(px, 4, 204, "K1 SWING", fg);
        text(px, 120, 204, "K2 PROB", fg);
        text(px, 4, 220, "K3 HOLD", fg);
        text(px, 120, 220, "K4 ORDER", fg);
    }
}

static int count_events(const FzSeq *seq)
{
    int i, n = 0;
    if (!seq)
        return 0;
    for (i = 0; i < FZ_SEQ_EVENTS; i++)
        if (seq->ev[i].used)
            n++;
    return n;
}

static void draw_save(uint16_t *px, const FzProject *project, const FzSession *session, const FzSeq *seq, uint16_t fg,
                      uint16_t head)
{
    char line[40];
    int row = session && session->row[0] ? 1 : 0;
    unsigned samples = project ? project->pcm_used : 0;
    text(px, 4, 40, "PROJECT", head);
    snprintf(line, sizeof line, "SAMPLES %u", samples);
    text(px, 4, 64, line, fg);
    snprintf(line, sizeof line, "EVENTS %d", count_events(seq));
    text(px, 4, 82, line, fg);
    text(px, 4, 110, "SAVE FILE", row == 0 ? head : fg);
    text(px, 4, 128, "LOAD FILE", row == 1 ? head : fg);
    text(px, 4, 204, "K1 PICKS", fg);
    text(px, 120, 204, "OCT+ GO", fg);
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
        draw_edit(rgb565, project, session, fg, hi, on, head);
        if (session->edit_tab == FZ_TAB_WAVE)
            draw_harmonics(rgb565, project, session, fg, on);
        else if (session->edit_tab == FZ_TAB_DRAW)
            draw_cycle_plot(rgb565, project, session, fg, head);
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
    } else if (screen == FZ_SCR_ARP && session) {
        draw_arp(rgb565, session, fg, on, head);
    } else if (screen == FZ_SCR_SAVE && session) {
        draw_save(rgb565, project, session, seq, fg, head);
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
        int peak = capture->peak;
        int width = peak * 200 / 32767;
        const char *kind = session && session->row[0] ? "MULTI" : "SAMPLE";
        text(rgb565, 4, 40, "SAMPLING", head);
        text(rgb565, 4, 64, kind, fg);
        snprintf(line, sizeof line, "SLOT %d", capture->slot + 1);
        text(rgb565, 120, 64, line, fg);
        text(rgb565, 4, 88, capture->message[0] ? capture->message : "READY", fg);
        if (width < 0)
            width = 0;
        if (width > 200)
            width = 200;
        rect(rgb565, 4, 112, 200, 10, hi);
        if (width > 0)
            rect(rgb565, 4, 112, width, 10, on);
        if (session)
            snprintf(line, sizeof line, "THR %d", session->cap_db);
        text(rgb565, 4, 136, line, fg);
        text(rgb565, 4, 168, "K1 KIND", fg);
        text(rgb565, 120, 168, "K2 LEVEL", fg);
        text(rgb565, 4, 192, "TAP REC", fg);
        text(rgb565, 120, 192, "OCT+ PLAY", head);
    } else if (screen == FZ_SCR_PICK_NOTE || screen == FZ_SCR_PICK_RANGE) {
        text(rgb565, 4, 48, "PICK A KEY", fg);
    } else if (screen == FZ_SCR_CONFIRM) {
        text(rgb565, 4, 48, "OCT+ CONFIRMS", fg);
        text(rgb565, 4, 64, "HOLD CANCELS", fg);
    }
}
