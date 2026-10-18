/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fm_ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Shared by the panel adapter, native screenshots and WebAssembly display. */
const uint32_t fm_ui_palette[FM_UI_COLOR_COUNT] = {0x080910, 0xf5f5ef, 0x1b2030, 0x294f88, 0x52cb80,
                                                   0xf4bf35, 0xb9c2d5, 0xf27575, 0x38445c};

/* R13-R17, R21, R23-R24, R33: the board and simulator draw these exact
 * 240x240 pixels. This original 5x7 uppercase font has no external assets.
 * Rendering belongs to the non-real-time owner and performs no I/O/allocation. */
typedef struct
{
    char character;
    uint8_t row[7];
} Glyph;
static const Glyph font[] = {
    {' ', {0, 0, 0, 0, 0, 0, 0}},        {'!', {4, 4, 4, 4, 4, 0, 4}},
    {'"', {10, 10, 10, 0, 0, 0, 0}},     {'#', {10, 31, 10, 10, 31, 10, 0}},
    {'%', {17, 2, 4, 4, 8, 17, 0}},      {'&', {12, 18, 20, 8, 21, 18, 13}},
    {'\'', {4, 4, 8, 0, 0, 0, 0}},       {'(', {2, 4, 8, 8, 8, 4, 2}},
    {')', {8, 4, 2, 2, 2, 4, 8}},        {'*', {0, 21, 14, 31, 14, 21, 0}},
    {'+', {0, 4, 4, 31, 4, 4, 0}},       {',', {0, 0, 0, 0, 0, 4, 8}},
    {'-', {0, 0, 0, 31, 0, 0, 0}},       {'.', {0, 0, 0, 0, 0, 0, 4}},
    {'/', {1, 2, 2, 4, 8, 8, 16}},       {'0', {14, 17, 19, 21, 25, 17, 14}},
    {'1', {4, 12, 4, 4, 4, 4, 14}},      {'2', {14, 17, 1, 2, 4, 8, 31}},
    {'3', {30, 1, 1, 14, 1, 1, 30}},     {'4', {2, 6, 10, 18, 31, 2, 2}},
    {'5', {31, 16, 16, 30, 1, 1, 30}},   {'6', {6, 8, 16, 30, 17, 17, 14}},
    {'7', {31, 1, 2, 4, 8, 8, 8}},       {'8', {14, 17, 17, 14, 17, 17, 14}},
    {'9', {14, 17, 17, 15, 1, 2, 12}},   {':', {0, 4, 0, 0, 4, 0, 0}},
    {';', {0, 4, 0, 0, 4, 4, 8}},        {'<', {1, 2, 4, 8, 4, 2, 1}},
    {'=', {0, 0, 31, 0, 31, 0, 0}},      {'>', {16, 8, 4, 2, 4, 8, 16}},
    {'?', {14, 17, 1, 2, 4, 0, 4}},      {'@', {14, 17, 23, 21, 23, 16, 14}},
    {'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}},
    {'C', {14, 17, 16, 16, 16, 17, 14}}, {'D', {30, 17, 17, 17, 17, 17, 30}},
    {'E', {31, 16, 16, 30, 16, 16, 31}}, {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'G', {14, 17, 16, 23, 17, 17, 15}}, {'H', {17, 17, 17, 31, 17, 17, 17}},
    {'I', {14, 4, 4, 4, 4, 4, 14}},      {'J', {7, 2, 2, 2, 2, 18, 12}},
    {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 25, 21, 19, 17, 17, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}}, {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'Q', {14, 17, 17, 17, 21, 18, 13}}, {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},   {'T', {31, 4, 4, 4, 4, 4, 4}},
    {'U', {17, 17, 17, 17, 17, 17, 14}}, {'V', {17, 17, 17, 17, 17, 10, 4}},
    {'W', {17, 17, 17, 21, 21, 21, 10}}, {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'Y', {17, 17, 10, 4, 4, 4, 4}},     {'Z', {31, 1, 2, 4, 8, 16, 31}},
    {'[', {14, 8, 8, 8, 8, 8, 14}},      {'\\', {16, 8, 8, 4, 2, 2, 1}},
    {']', {14, 2, 2, 2, 2, 2, 14}},      {'^', {4, 10, 17, 0, 0, 0, 0}},
    {'_', {0, 0, 0, 0, 0, 0, 31}},       {'|', {4, 4, 4, 4, 4, 4, 4}},
    {'~', {0, 0, 9, 22, 0, 0, 0}}};
static const char *const tabs[] = {"SAMPLE", "LOOP", "MIX", "AMP",   "FILTER",
                                   "MOD",    "WAVE", "MAP", "RENDER"};
static const char *const sample_rows[] = {"START",         "END",          "GAIN",     "REVERSE",
                                          "ROOT NOTE",     "TUNING CENTS", "ONE SHOT", "NORMALIZE",
                                          "REVERSE WRITE", "TRUNCATE"};
static const char *const loop_rows[] = {"ENABLE", "SECTION",      "START",
                                        "END",    "CROSSFADE",    "REPEATS",
                                        "ACTION", "JUMP SECTION", "UNTIL RELEASE"};
static const char *const mix_rows[] = {"SOURCE B",      "GAIN A",       "GAIN B",
                                       "OFFSET FRAMES", "TUNING CENTS", "OVERLAP FRAMES",
                                       "EQUAL POWER",   "MIX WRITE",    "CROSS MIX"};
static const char *const amp_rows[] = {"STAGE",         "TIME MS",       "LEVEL",
                                       "SUSTAIN STAGE", "RELEASE STAGE", "STAGE COUNT",
                                       "KEY RATE",      "VELOCITY",      "COPY TO FILTER"};
static const char *const filter_rows[] = {
    "CUTOFF",  "RESONANCE", "DRIVE",         "ENV AMOUNT",    "KEY TRACK",   "STAGE",
    "TIME MS", "LEVEL",     "SUSTAIN STAGE", "RELEASE STAGE", "STAGE COUNT", "COPY TO AMP"};
static const char *const mod_rows[] = {"RATE HZ", "DEPTH", "SHAPE", "DESTINATION", "PHASE MODE"};
static const char *const wave_rows[] = {"SOURCE",     "HARMONIC",   "NORMALIZE",   "APPLY RECIPE",
                                        "DRAW POINT", "CLEAR WAVE", "SMOOTH WAVE", "CUT SAMPLE"};
static const char *const map_rows[] = {
    "ROOT NOTE",   "KEY RANGE",   "BASE NOTE",   "CHOKE GROUP", "PAN",         "SEND 1 TRIM",
    "SEND 2 TRIM", "SEND 3 TRIM", "SEND 4 TRIM", "AUTO ZONES",  "PICK SAMPLE", "DELETE SAMPLE"};
static const char *const render_rows[] = {"BAKE ALL SENDS", "FREEZE PATTERN", "UNFREEZE",
                                          "UNBAKE SAMPLE"};
static const char *const *const edit_rows[] = {sample_rows, loop_rows,   mix_rows,
                                               amp_rows,    filter_rows, mod_rows,
                                               wave_rows,   map_rows,    render_rows};
static const unsigned row_count[] = {10, 9, 9, 9, 12, 5, 8, 12, 4};
static const char *const sources[] = {"USB CAPTURE", "ADDITIVE", "PRESET WAVE",
                                      "CUT SAMPLE",  "DRAW",     "EXISTING WAVE"};
static const char *const categories[] = {"DRUMS", "SYNTH", "PAD",  "LEAD", "FX",
                                         "BASS",  "KEYS",  "PERC", "LOOP"};
static const char *const fx_types[] = {"DELAY", "REVERB", "CHORUS", "CHARACTER"};

static void rectangle(FmUi *u, int x, int y, int width, int height, uint8_t color)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + width > (int)FM_DISPLAY_WIDTH ? (int)FM_DISPLAY_WIDTH : x + width;
    int y1 = y + height > (int)FM_DISPLAY_HEIGHT ? (int)FM_DISPLAY_HEIGHT : y + height;
    for (int row = y0; row < y1; row++)
        for (int col = x0; col < x1; col++)
            u->pixels[(unsigned)row * FM_DISPLAY_WIDTH + (unsigned)col] = color;
}
static void outline(FmUi *u, int x, int y, int w, int h, uint8_t color)
{
    rectangle(u, x, y, w, 1, color);
    rectangle(u, x, y + h - 1, w, 1, color);
    rectangle(u, x, y, 1, h, color);
    rectangle(u, x + w - 1, y, 1, h, color);
}
static const uint8_t *glyph(char c)
{
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    for (unsigned i = 0; i < sizeof(font) / sizeof(font[0]); i++)
        if (font[i].character == c)
            return font[i].row;
    return font[30].row; /* '?' */
}
static void text_at(FmUi *u, int x, int y, const char *s, unsigned limit, unsigned scale,
                    uint8_t color)
{
    if (!s)
        return;
    for (unsigned i = 0; i < limit && s[i]; i++)
    {
        const uint8_t *rows = glyph(s[i]);
        for (unsigned row = 0; row < 7; row++)
            for (unsigned col = 0; col < 5; col++)
                if (rows[row] & (1u << (4u - col)))
                    rectangle(u, x + (int)(i * 6u * scale + col * scale), y + (int)(row * scale),
                              (int)scale, (int)scale, color);
    }
}
static void text(FmUi *u, int x, int y, const char *s, unsigned limit, uint8_t color)
{
    text_at(u, x, y, s, limit, 1, color);
}
/* Larger pixel-aligned 7x10 face derived from our original 5x7 glyphs.
 * Primary labels use this face; compact legends remain secondary. */
static void bold(FmUi *u, int x, int y, const char *s, unsigned limit, uint8_t color)
{
    if (!s)
        return;
    for (unsigned i = 0; i < limit && s[i]; i++)
    {
        const uint8_t *rows = glyph(s[i]);
        static const unsigned column[] = {0, 1, 3, 4, 5};
        for (unsigned row = 0; row < 10; row++)
            for (unsigned col = 0; col < 5; col++)
                if (rows[row * 7 / 10] & (1u << (4u - col)))
                    rectangle(u, x + (int)(i * 8 + column[col]), y + (int)row, 2, 1, color);
    }
}
static void bold_right(FmUi *u, int x, int y, const char *s, unsigned limit, uint8_t color)
{
    unsigned n = 0;
    while (s && n < limit && s[n])
        n++;
    bold(u, x - (int)n * 8, y, s, n, color);
}
static void right(FmUi *u, int right_x, int y, const char *s, unsigned limit, uint8_t color)
{
    unsigned n = 0;
    while (n < limit && s[n])
        n++;
    text(u, right_x - (int)(n * 6u), y, s, n, color);
}
static void note(char *s, size_t n, unsigned midi)
{
    static const char *const names[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                        "F#", "G",  "G#", "A",  "A#", "B"};
    if (midi > 127)
    {
        snprintf(s, n, "--");
        return;
    }
    snprintf(s, n, "%s%d/%u", names[midi % 12], (int)(midi / 12) - 1, midi);
}
static FmTrack *selected_track(FmUi *u)
{
    return u->project ? fm_track(u->project, u->project->selected_track) : NULL;
}
static FmInstrument *selected_instrument(FmUi *u)
{
    FmTrack *t = selected_track(u);
    return t ? fm_instrument(u->project, t->instrument) : NULL;
}
static FmRegion *selected_region(FmUi *u)
{
    FmInstrument *in = selected_instrument(u);
    if (!in)
        return NULL;
    if (u->scope && u->scope <= FM_ANCHORS && in->regions[u->scope - 1].id)
        return &in->regions[u->scope - 1];
    for (unsigned i = 0; i < FM_ANCHORS; i++)
        if (in->regions[i].id)
            return &in->regions[i];
    return NULL;
}
static FmPattern *selected_pattern(FmUi *u)
{
    FmTrack *t = selected_track(u);
    return t && t->selected_pattern < FM_PATTERN_SLOTS
               ? fm_pattern(u->project, t->patterns[t->selected_pattern])
               : NULL;
}
static unsigned first_visible(unsigned selected, unsigned count, unsigned shown)
{
    if (count <= shown)
        return 0;
    unsigned first = selected >= shown ? selected - shown + 1 : 0;
    return first > count - shown ? count - shown : first;
}
static void scroll_mark(FmUi *u, unsigned first, unsigned count, unsigned shown)
{
    if (count <= shown)
        return;
    rectangle(u, 237, 52, 1, 125, 1);
    int height = (int)(125u * shown / count);
    if (height < 3)
        height = 3;
    int y = 52 + (int)((125u - (unsigned)height) * first / (count - shown));
    rectangle(u, 236, y, 3, height, 1);
}
static void list_row(FmUi *u, unsigned visible, const char *name, const char *value,
                     const char *detail, bool selected)
{
    int y = 52 + (int)visible * 25;
    uint8_t ink = FM_UI_TEXT;
    rectangle(u, 2, y, 232, 23, selected ? FM_UI_BLUE : visible % 2 ? FM_UI_PANEL : FM_UI_BG);
    unsigned n = value ? (unsigned)strlen(value) : 0;
    if (n > 14)
        n = 14;
    bold(u, 7, y + 2, name, n ? 27u - n : 27u, ink);
    if (value)
        bold_right(u, 230, y + 2, value, 14, selected ? FM_UI_AMBER : FM_UI_TEXT);
    if (detail)
        text(u, 8, y + 15, detail, 36, FM_UI_MUTED);
}
static void footer(FmUi *u, const char *a, const char *b, const char *c, const char *d,
                   const char *va, const char *vb, const char *vc, const char *vd)
{
    const char *labels[] = {a, b, c, d};
    const char *values[] = {va, vb, vc, vd};
    rectangle(u, 0, 212, 240, 28, FM_UI_PANEL);
    rectangle(u, 0, 212, 240, 2, FM_UI_BLUE);
    for (unsigned i = 0; i < 4; i++)
    {
        char label[12];
        snprintf(label, sizeof(label), "%u %s", i + 1, labels[i] ? labels[i] : "-");
        if (i)
            rectangle(u, (int)i * 60, 214, 1, 26, FM_UI_GRID);
        text(u, (int)i * 60 + 3, 216, label, 9, FM_UI_MUTED);
        const char *value = values[i] ? values[i] : "";
        if (!strcmp(value, "RELATIVE"))
            value = "TURN";
        if (strlen(value) <= 7)
            bold(u, (int)i * 60 + 3, 227, value, 7, FM_UI_AMBER);
        else
            text(u, (int)i * 60 + 3, 229, value, 9, FM_UI_AMBER);
    }
}
static void header(FmUi *u)
{
    char line[48];
    text_at(u, 4, 4, fm_ui_screen_name(u->screen), 18, 2, 1);
    rectangle(u, 0, 22, 240, 2, FM_UI_BLUE);
    bool playing = u->sequence && fm_sequence_playing(u->sequence);
    FmSequenceStatus status = {0};
    if (u->sequence)
        status = fm_sequence_status(u->sequence);
    snprintf(line, sizeof(line), "%s %6.1f BPM %s%s", playing ? "PLAY" : "STOP", u->project->bpm,
             status.recording ? "REC"
             : status.armed   ? "ARM"
                              : "",
             u->project->dirty ? " *" : "");
    rectangle(u, 0, 25, 240, 11, FM_UI_PANEL);
    text(u, 4, 27, line, 38, status.recording ? FM_UI_RED : playing ? FM_UI_GREEN : FM_UI_TEXT);
    if (u->project->volatile_audio)
        right(u, 237, 27, "VOLATILE", 8, FM_UI_AMBER);
}
static unsigned track_list(FmUi *u, FmTrack *list[FM_OBJECT_LIMIT], unsigned *selected)
{
    unsigned n = 0;
    *selected = 0;
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (u->project->tracks[i])
        {
            if (u->project->tracks[i]->id == u->project->selected_track)
                *selected = n;
            list[n++] = u->project->tracks[i];
        }
    return n;
}
static bool active_track(FmUi *u, FmTrack *track)
{
    if (!u->engine)
        return false;
    unsigned count =
        u->engine->max_voices > FM_VOICE_LIMIT ? FM_VOICE_LIMIT : u->engine->max_voices;
    for (unsigned i = 0; i < count; i++)
        if (u->engine->voices[i].active && u->engine->voices[i].track == track)
            return true;
    return false;
}
static void home(FmUi *u, bool mixer)
{
    FmTrack *list[FM_OBJECT_LIMIT];
    unsigned selected;
    unsigned count = track_list(u, list, &selected), first = first_visible(selected, count, 5);
    FmTrack *t = selected_track(u);
    FmInstrument *in = selected_instrument(u);
    char value[32], detail[48], name[48], a[16] = "", b[16] = "", c[16] = "", d[16] = "";
    if (mixer)
        snprintf(name, sizeof(name), "SEND %u / ARP: MUTE-SOLO COMMANDS", u->mixer_send % 4 + 1);
    else
        snprintf(name, sizeof(name), "%s", in ? in->name : "SCL: NEW / ARP: ACTIONS");
    text(u, 4, 39, name, u->project->volatile_audio ? 26u : 38u, 1);
    if (!count)
    {
        text(u, 12, 82, "CREATE AN INSTRUMENT", 35, 1);
        text(u, 12, 97, "SCL -> NEW INSTRUMENT", 35, 1);
    }
    for (unsigned i = first; i < count && i < first + 5; i++)
    {
        FmTrack *row = list[i];
        FmInstrument *instrument = fm_instrument(u->project, row->instrument);
        snprintf(name, sizeof(name), "%02u %s", i + 1,
                 instrument ? instrument->name : "MISSING INSTRUMENT");
        snprintf(value, sizeof(value), "P%02u %c%c%c", row->selected_pattern + 1,
                 active_track(u, row) ? '>' : ' ', row->mute ? 'M' : ' ', row->solo ? 'S' : ' ');
        if (mixer)
            snprintf(detail, sizeof(detail), "LEVEL %.2f PAN %+.2f SEND%u %.2f",
                     (double)row->volume, (double)row->pan, u->mixer_send % 4 + 1,
                     (double)row->sends[u->mixer_send % 4]);
        else
            snprintf(detail, sizeof(detail), "%s / %u REGIONS / MIDI %u",
                     instrument && instrument->type == FM_MULTI_SAMPLE ? "MULTI SAMPLE" : "SAMPLE",
                     instrument ? instrument->count : 0, row->midi_channel + 1);
        list_row(u, i - first, name, value, detail, i == selected);
    }
    scroll_mark(u, first, count, 5);
    if (t)
    {
        if (mixer)
        {
            snprintf(name, sizeof(name), "S1 %.2f S2 %.2f S3 %.2f S4 %.2f", (double)t->sends[0],
                     (double)t->sends[1], (double)t->sends[2], (double)t->sends[3]);
            text(u, 4, 184, name, 39, 1);
            snprintf(a, sizeof(a), "%u/%u", selected + 1, count);
            snprintf(b, sizeof(b), "%.2f", (double)t->volume);
            snprintf(c, sizeof(c), "%+.2f", (double)t->pan);
            snprintf(d, sizeof(d), "%.2f", (double)t->sends[u->mixer_send % 4]);
        }
        else
        {
            snprintf(name, sizeof(name), "ALGORITHM: TRACK %u/%u   OCT %+d", selected + 1, count,
                     u->octave);
            text(u, 4, 184, name, 39, 1);
            snprintf(a, sizeof(a), "%.3f", (double)t->cutoff);
            snprintf(b, sizeof(b), "%.3f", (double)t->resonance);
            snprintf(c, sizeof(c), "%.2f", (double)t->volume);
            snprintf(d, sizeof(d), "%+.2f", (double)t->pan);
        }
    }
    if (t && !mixer)
    {
        float meters[] = {t->cutoff, t->resonance, t->volume / 4, (t->pan + 1) / 2};
        for (unsigned i = 0; i < 4; i++)
        {
            rectangle(u, 4 + (int)i * 60, 194, 50, 4, FM_UI_BLUE);
            rectangle(u, 4 + (int)i * 60, 194, (int)(50 * fminf(1, fmaxf(0, meters[i]))), 4,
                      FM_UI_GREEN);
        }
    }
    if (mixer)
    {
        char label[12];
        snprintf(label, sizeof(label), "SEND %u", u->mixer_send % 4 + 1);
        footer(u, "TRACK", "LEVEL", "PAN", label, a, b, c, d);
    }
    else
        footer(u, "CUTOFF", "RES", "VOLUME", "PAN", a, b, c, d);
}
static void edit_value(FmUi *u, unsigned tab, unsigned row, char *out, size_t size)
{
    FmRegion *r = selected_region(u);
    FmInstrument *in = selected_instrument(u);
    FmAsset *asset = r ? fm_asset(u->project, r->asset) : NULL;
    unsigned stage = u->stage < FM_ENV_STAGES ? u->stage : FM_ENV_STAGES - 1;
    unsigned section = u->loop_section < FM_LOOP_SECTIONS ? u->loop_section : FM_LOOP_SECTIONS - 1;
    const FmEnvelope *envelope = r ? (tab == 3 ? &r->amp : &r->filter) : NULL;
    if (tab == 2)
    {
        switch (row)
        {
        case 0:
            snprintf(out, size, "SAMPLE %02u", u->mix_source + 1);
            break;
        case 1:
            snprintf(out, size, "%.3f", (double)u->mix_gain_a);
            break;
        case 2:
            snprintf(out, size, "%.3f", (double)u->mix_gain_b);
            break;
        case 3:
            snprintf(out, size, "%ld FR", (long)u->mix_offset);
            break;
        case 4:
            snprintf(out, size, "%+.0f CT", (double)u->mix_tuning);
            break;
        case 5:
            snprintf(out, size, "%lu FR", (unsigned long)u->mix_overlap);
            break;
        case 6:
            snprintf(out, size, "%s", u->mix_equal_power ? "ON" : "OFF");
            break;
        default:
            snprintf(out, size, "SEL >");
            break;
        }
        return;
    }
    if (tab == 6)
    {
        switch (row)
        {
        case 0:
            snprintf(out, size, "%s", sources[u->source % 6]);
            break;
        case 1:
            snprintf(out, size, "%02u: %u", u->harmonic % 48 + 1,
                     in ? in->harmonics[u->harmonic % 48] : 0);
            break;
        case 4:
            snprintf(out, size, "%u", u->point);
            break;
        default:
            snprintf(out, size, "SEL >");
            break;
        }
        return;
    }
    if (tab == 8)
    {
        snprintf(out, size, "SEL >");
        return;
    }
    if (!r)
    {
        snprintf(out, size, "NO AUDIO");
        return;
    }
    if (tab == 0)
    {
        switch (row)
        {
        case 0:
        case 1:
            if (!u->scope && asset && asset->frames)
                snprintf(out, size, "%.2f %%", 100.0 * (row ? r->end : r->start) / asset->frames);
            else
                snprintf(out, size, "%lu FR", (unsigned long)(row ? r->end : r->start));
            break;
        case 2:
            snprintf(out, size, "%.3f", (double)r->gain);
            break;
        case 3:
            snprintf(out, size, "%s", r->reverse ? "ON" : "OFF");
            break;
        case 4:
            note(out, size, r->root);
            break;
        case 5:
            snprintf(out, size, "%+.0f CT", (double)r->tuning);
            break;
        case 6:
            snprintf(out, size, "%s", r->one_shot ? "ON" : "OFF");
            break;
        default:
            snprintf(out, size, "SEL >");
            break;
        }
    }
    else if (tab == 1)
    {
        const FmLoopSection *loop = &r->loops[section];
        static const char *const actions[] = {"CONTINUE", "JUMP", "END"};
        switch (row)
        {
        case 0:
            snprintf(out, size, "%s", r->loop_enabled ? "ON" : "OFF");
            break;
        case 1:
            snprintf(out, size, "%u / %u", section + 1, r->loop_count);
            break;
        case 2:
        case 3:
        case 4:
        {
            uint32_t marker = row == 2 ? loop->start : row == 3 ? loop->end : loop->crossfade;
            if (!u->scope && asset && asset->frames)
                snprintf(out, size, "%.2f %%", 100.0 * marker / asset->frames);
            else
                snprintf(out, size, "%lu FR", (unsigned long)marker);
            break;
        }
        case 5:
            snprintf(out, size, "%u", loop->repeats);
            break;
        case 6:
            snprintf(out, size, "%s",
                     actions[loop->action <= FM_LOOP_END ? loop->action : FM_LOOP_END]);
            break;
        case 7:
            snprintf(out, size, "%u", loop->jump + 1);
            break;
        default:
            snprintf(out, size, "%s", loop->until_release ? "ON" : "OFF");
            break;
        }
    }
    else if (tab == 4 && row == 11)
    {
        snprintf(out, size, "SEL >");
    }
    else if (tab == 3 || (tab == 4 && row >= 5))
    {
        unsigned er = tab == 3 ? row : row - 5;
        switch (er)
        {
        case 0:
            snprintf(out, size, "%u / %u", stage + 1, envelope->count);
            break;
        case 1:
            snprintf(out, size, "%.1f MS", (double)envelope->stage[stage].milliseconds);
            break;
        case 2:
            snprintf(out, size, "%.3f", (double)envelope->stage[stage].level);
            break;
        case 3:
            snprintf(out, size, "%u", envelope->sustain + 1);
            break;
        case 4:
            snprintf(out, size, "%u", envelope->release + 1);
            break;
        case 5:
            snprintf(out, size, "%u", envelope->count);
            break;
        case 6:
            snprintf(out, size, "%+.2f", (double)envelope->key_rate);
            break;
        case 7:
            snprintf(out, size, "%.3f", (double)envelope->velocity);
            break;
        default:
            snprintf(out, size, "SEL >");
            break;
        }
    }
    else if (tab == 4)
    {
        float values[] = {r->cutoff, r->resonance, r->drive, r->filter_env, r->keytrack};
        snprintf(out, size, "%.3f", (double)values[row < 5 ? row : 0]);
    }
    else if (tab == 5)
    {
        static const char *const shapes[] = {"SINE", "TRIANGLE", "SQUARE", "SAW", "SAMPLE & HOLD"};
        static const char *const dest[] = {"CUTOFF", "AMPLITUDE", "PITCH", "PAN"};
        if (row == 0)
            snprintf(out, size, "%.2f HZ", (double)r->lfo_rate);
        else if (row == 1)
            snprintf(out, size, "%.3f", (double)r->lfo_depth);
        else if (row == 2)
            snprintf(
                out, size, "%s",
                shapes[(r->lfo_shape & 0x7fu) <= FM_LFO_SAMPLE_HOLD ? r->lfo_shape & 0x7fu : 0]);
        else if (row == 3)
            snprintf(out, size, "%s", dest[r->lfo_destination % 4]);
        else
            snprintf(out, size, "%s", r->lfo_shape & 0x80u ? "FREE RUN" : "RETRIGGER");
    }
    else if (tab == 7)
    {
        switch (row)
        {
        case 0:
            note(out, size, r->root);
            break;
        case 1:
            snprintf(out, size, "%u - %u", r->low, r->high);
            break;
        case 2:
            note(out, size, in ? in->base_note : 0);
            break;
        case 3:
            snprintf(out, size, "%u", r->choke);
            break;
        case 4:
            snprintf(out, size, "%+.3f", (double)r->pan);
            break;
        default:
            if (row >= 5 && row <= 8)
                snprintf(out, size, "%.3f", (double)r->sends[row - 5]);
            else
                snprintf(out, size, "SEL >");
            break;
        }
    }
}
static void line(FmUi *u, int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, error = dx + dy;
    for (;;)
    {
        rectangle(u, x0, y0, 2, 2, FM_UI_GREEN);
        if (x0 == x1 && y0 == y1)
            break;
        int twice = 2 * error;
        if (twice >= dy)
        {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx)
        {
            error += dx;
            y0 += sy;
        }
    }
}
/* Graphs are part of the real 240x240 framebuffer, including on the board.
 * Selected stage/point and exact parameter values remain visible below. */
static void edit_graph(FmUi *u, unsigned tab)
{
    FmRegion *r = selected_region(u);
    FmInstrument *in = selected_instrument(u);
    rectangle(u, 7, 53, 226, 64, FM_UI_PANEL);
    outline(u, 7, 53, 226, 64, FM_UI_GRID);
    if (!r)
    {
        text(u, 30, 80, "ADD AUDIO TO SEE GRAPH", 30, 1);
        return;
    }
    if (tab == 3 || tab == 4)
    {
        const FmEnvelope *e = tab == 3 ? &r->amp : &r->filter;
        unsigned count = e->count > FM_ENV_STAGES ? FM_ENV_STAGES : e->count;
        float total = 0;
        for (unsigned i = 0; i < count; i++)
            total += fmaxf(0, e->stage[i].milliseconds);
        float minimum = fmaxf(1, total * .02f), scale = 0, at = 0;
        for (unsigned i = 0; i < count; i++)
            scale += fmaxf(minimum, e->stage[i].milliseconds);
        int x = 9, y = 113;
        for (unsigned i = 0; i < count; i++)
        {
            at += fmaxf(minimum, e->stage[i].milliseconds);
            int nx = 9 + (int)(220 * at / fmaxf(1, scale));
            int ny = 113 - (int)(48 * fminf(1, fmaxf(0, e->stage[i].level)));
            line(u, x, y, nx, ny);
            rectangle(u, nx - 1, ny - 1, 3, 3, FM_UI_TEXT);
            if (i == u->stage)
                outline(u, nx - 3, ny - 3, 7, 7, FM_UI_AMBER);
            if (i == e->sustain || i == e->release)
                text(u, nx > 220 ? 220 : nx, 55, i == e->sustain ? "S" : "R", 1, 1);
            x = nx;
            y = ny;
        }
    }
    else if (tab == 5)
    {
        /* One second at the actual rate/depth. The DSP helper supplies the shape. */
        for (int x = 10; x < 230; x += 4)
            rectangle(u, x, 85, 1, 1, FM_UI_GRID);
        int previous = 85;
        for (int x = 0; x < 220; x++)
        {
            double cycles = (double)x / 219 * r->lfo_rate;
            int y =
                85 - (int)(27 * r->lfo_depth * fm_lfo_sample(cycles, r->lfo_shape & 0x7fu, r->id));
            if (x)
            {
                if ((r->lfo_shape & 0x7fu) == FM_LFO_SAMPLE_HOLD)
                {
                    line(u, 9 + x - 1, previous, 9 + x, previous);
                    line(u, 9 + x, previous, 9 + x, y);
                }
                else
                    line(u, 9 + x - 1, previous, 9 + x, y);
            }
            previous = y;
        }
        text(u, 12, 55, "1 SECOND", 10, 1);
    }
    else if (tab == 6 && u->rows[6] <= 3 && in && in->additive)
    {
        for (unsigned h = 0; h < FM_HARMONICS; h++)
        {
            int x = 10 + (int)h * 220 / FM_HARMONICS;
            int height = (int)in->harmonics[h] * 48 / 256;
            rectangle(u, x, 113 - height, 3, height ? height : 1,
                      h == u->harmonic ? FM_UI_AMBER : FM_UI_GREEN);
            if (h == u->harmonic)
                outline(u, x - 1, 57, 5, 58, FM_UI_AMBER);
        }
    }
    else
    {
        FmAsset *a = fm_asset(u->project, r->asset);
        if (!a || r->end <= r->start)
            return;
        int previous = 85;
        for (unsigned x = 0; x < 220; x++)
        {
            uint32_t n = r->start + (uint32_t)((uint64_t)x * (r->end - r->start - 1) / 219);
            int y = 85 - (int)(a->pcm[(size_t)n * a->channels] * 27 / 32768);
            if (x)
                line(u, 8 + (int)x, previous, 9 + (int)x, y);
            previous = y;
        }
        if (u->rows[6] == 4)
        {
            int x = 9 + (int)(220.0 * u->point / (r->end - r->start));
            for (int y = 55; y < 115; y += 3)
                rectangle(u, x, y, 1, 1, 1);
        }
    }
}
static void edit(FmUi *u)
{
    unsigned tab = u->tab % 9,
             row = u->rows[tab] < row_count[tab] ? u->rows[tab] : row_count[tab] - 1;
    bool graph = tab == 3 || tab == 4 || tab == 5 || tab == 6;
    unsigned shown = graph ? 3 : 5;
    unsigned first = first_visible(row, row_count[tab], shown);
    FmInstrument *in = selected_instrument(u);
    char line[64], value[32];
    text(u, 4, 39, in ? in->name : "NO INSTRUMENT", 24, 1);
    if (u->scope)
        snprintf(line, sizeof(line), "%s S%02u", tabs[tab], u->scope);
    else
        snprintf(line, sizeof(line), "%s ALL", tabs[tab]);
    right(u, 237, 39, line, 14, 1);
    if (graph)
        edit_graph(u, tab);
    for (unsigned i = first; i < row_count[tab] && i < first + shown; i++)
    {
        value[0] = 0;
        edit_value(u, tab, i, value, sizeof(value));
        const char *detail = "";
        if (tab == 0 && i < 2)
            detail = u->scope ? "ABSOLUTE FRAMES / FINE AVAILABLE"
                              : "ALL: NORMALIZED PERCENT PER REGION";
        if (tab == 6 && i == 1)
            detail = "KNOB 2: PARTIAL / KNOB 3: LEVEL";
        if (tab == 7 && i == 1)
            detail = "SEL: PICK LOW THEN HIGH";
        if (tab == 8)
            detail = i == 0   ? "ISOLATED CREATIVE BAKE; USES STORAGE"
                     : i == 1 ? "PATTERN ROUTING ONLY; KEEPS NOTES"
                              : "RESTORE LIVE NOTES AND SENDS";
        if (graph)
        {
            int y = 124 + (int)(i - first) * 18;
            rectangle(u, 2, y, 232, 16, i == row ? FM_UI_BLUE : FM_UI_PANEL);
            unsigned n = (unsigned)strlen(value);
            if (n > 14)
                n = 14;
            bold(u, 7, y + 3, edit_rows[tab][i], 27u - n, FM_UI_TEXT);
            bold_right(u, 230, y + 3, value, 14, i == row ? FM_UI_AMBER : FM_UI_TEXT);
        }
        else
            list_row(u, i - first, edit_rows[tab][i], value, detail, i == row);
    }
    if (!graph)
        scroll_mark(u, first, row_count[tab], shown);
    if (u->scope)
        snprintf(line, sizeof(line), "SELECT: SCOPE / SAMPLE %02u", u->scope);
    else
        snprintf(line, sizeof(line), "SELECT: SCOPE / ALL %u / FIRST VALUE", in ? in->count : 0);
    text(u, 4, 184, line, 39, 1);
    if (tab == 6 && row == 1)
        footer(u, "ROW", "PARTIAL", "LEVEL", "-", "", "1-48", "0-256", "");
    else if (tab == 6 && row == 4)
        footer(u, "ROW", "POINT", "VALUE", "-", "", "", "", "");
    else if ((tab == 0 && row >= 7) || (tab == 2 && row >= 7) || (tab == 3 && row == 8) ||
             (tab == 4 && row == 11) || (tab == 6 && row >= 2) || (tab == 7 && row >= 9) ||
             tab == 8)
        footer(u, "ROW", "-", "-", "-", "SEL: OPEN", "", "", "");
    else
        footer(u, "ROW", "VALUE", "FINE", "COARSE", "", "RELATIVE", "RELATIVE", "RELATIVE");
}

static void effects(FmUi *u)
{
    static const char *const labels[] = {"TYPE", "INPUT",        "",           "",
                                         "",     "RETURN LEVEL", "RETURN MUTE"};
    static const char *const parameters[4][3] = {{"TIME", "FEEDBACK", "DAMPING"},
                                                 {"SIZE", "DECAY", "DAMPING"},
                                                 {"RATE", "DEPTH", "FEEDBACK"},
                                                 {"DRIVE", "BIT REDUCTION", "RATE REDUCTION"}};
    unsigned slot = u->fx_slot % FM_FX_SLOTS, row = u->command % 7,
             first = first_visible(row, 7, 5);
    FmFxConfig *fx = &u->project->fx[slot];
    char line[48], value[24];
    snprintf(line, sizeof(line), "SHARED SLOT %u / %s", slot + 1, fx_types[(unsigned)fx->type % 4]);
    text(u, 4, 39, line, 38, 1);
    for (unsigned i = first; i < 7 && i < first + 5; i++)
    {
        if (i == 0)
            snprintf(value, sizeof(value), "%s", fx_types[(unsigned)fx->type % 4]);
        else if (i == 1)
            snprintf(value, sizeof(value), "%s", fx->enabled ? "ON" : "BYPASS");
        else if (i == 5)
            snprintf(value, sizeof(value), "%.3f", (double)fx->gain);
        else if (i == 6)
            snprintf(value, sizeof(value), "%s", fx->return_muted ? "MUTED" : "AUDIBLE");
        else if (i == 2 && fx->type == FM_FX_DELAY)
            snprintf(value, sizeof(value), "%.1f MS", (double)fx->parameter[0] * 1000);
        else if (i == 2 && fx->type == FM_FX_CHORUS)
            snprintf(value, sizeof(value), "%.2f HZ", .05 + 5.95 * fx->parameter[0]);
        else
            snprintf(value, sizeof(value), "%.1f %%", (double)fx->parameter[i - 2] * 100);
        list_row(u, i - first,
                 i >= 2 && i <= 4 ? parameters[(unsigned)fx->type % 4][i - 2] : labels[i], value,
                 "", i == row);
    }
    scroll_mark(u, first, 7, 5);
    text(u, 4, 184, "FX: NEXT SLOT / PROJECT WET RETURN", 38, 1);
    snprintf(value, sizeof(value), "%+.2f", (double)fx->pan);
    footer(u, "ROW", "VALUE", "FINE", "PAN", "", "RELATIVE", "RELATIVE", value);
}
static bool step_on(FmPattern *p, unsigned step)
{
    if (!p)
        return false;
    double tick = (double)step * (FM_PPQN / 4.0);
    for (unsigned i = 0; i < p->count; i++)
        if (p->events[i].kind == FM_EVENT_NOTE_ON && p->events[i].velocity &&
            fabs(p->events[i].tick - tick) < 0.001)
            return true;
    return false;
}
static void steps(FmUi *u)
{
    FmTrack *track = selected_track(u);
    FmPattern *pattern = selected_pattern(u);
    char line[64], n[16];
    note(n, sizeof(n), u->step_note);
    snprintf(line, sizeof(line), "P%02u PAGE %u NOTE %s", track ? track->selected_pattern + 1 : 0,
             u->page + 1, n);
    text(u, 4, 39, line, 39, 1);
    unsigned playing = UINT32_MAX;
    if (u->sequence && fm_sequence_playing(u->sequence) && pattern && pattern->length_ticks > 0)
        playing =
            (unsigned)(fmod(fm_sequence_tick(u->sequence) * pattern->rate, pattern->length_ticks) /
                       (FM_PPQN / 4.0));
    for (unsigned i = 0; i < 16; i++)
    {
        int x = 4 + (int)(i % 4) * 59, y = 53 + (int)(i / 4) * 30;
        unsigned step = u->page * 16 + i;
        bool enabled = step_on(pattern, step);
        uint8_t color = enabled ? FM_UI_BG : FM_UI_TEXT;
        rectangle(u, x, y, 54, 25, enabled ? FM_UI_GREEN : FM_UI_BLUE);
        if (u->selected_step % 16 == i)
            outline(u, x, y, 54, 25, FM_UI_AMBER);
        if (playing == step)
            rectangle(u, x, y, 54, 3, FM_UI_AMBER);
        snprintf(line, sizeof(line), "%02u%s", step + 1, playing == step ? ">" : "");
        bold(u, x + 11, y + 8, line, 5, color);
    }
    text(u, 4, 180, "WHITE KEYS 1-16: TOGGLE / SEL: NOTE", 39, 1);
    text(u, 4, 190, "KEY 1: REST / KEY 3: TIE / OCT: PAGE", 39, 1);
    char selected[16], velocity[16], length[16];
    snprintf(selected, sizeof(selected), "%u", u->page * 16 + u->selected_step % 16 + 1);
    snprintf(velocity, sizeof(velocity), "%u", u->step_velocity);
    snprintf(length, sizeof(length), "%.0f TK", u->step_length);
    footer(u, "STEP", "NOTE", "VELOCITY", "LENGTH", selected, n, velocity, length);
}
static void patterns(FmUi *u)
{
    FmTrack *track = selected_track(u);
    char line[64];
    unsigned page = u->page % 2;
    snprintf(line, sizeof(line), "%s / BANK %u / %s",
             u->scene_scope ? "ALL TRACKS" : "SELECTED TRACK", page + 1,
             u->pattern_chained ? "CHAIN" : "SINGLE");
    text(u, 4, 39, line, 39, 1);
    for (unsigned i = 0; i < 16; i++)
    {
        unsigned slot = page * 16 + i;
        int x = 4 + (int)(i % 4) * 59, y = 53 + (int)(i / 4) * 30;
        bool selected = track && track->selected_pattern == slot;
        bool filled = track && track->patterns[slot];
        rectangle(u, x, y, 54, 25, filled ? FM_UI_GREEN : FM_UI_BLUE);
        if (selected)
            outline(u, x, y, 54, 25, FM_UI_AMBER);
        uint8_t ink = filled ? FM_UI_BG : FM_UI_TEXT;
        snprintf(line, sizeof(line), "%02u %c", slot + 1,
                 track && track->patterns[slot] ? '*' : '-');
        bold(u, x + 7, y + 8, line, 5, ink);
        if (track && track->queued_pattern == (int8_t)slot)
            text(u, x + 42, y + 3, ">", 1, ink);
    }
    text(u, 4, 180, "WHITE KEYS: SELECT / HOLD TWO: RANGE", 39, 1);
    text(u, 4, 190, "OCT +/-: BANK / KNOB 2: TRACK-SCENE", 39, 1);
    footer(u, "TRACK", "SCOPE", "-", "-", "", u->scene_scope ? "SCENE" : "TRACK", "", "");
}
static void song(FmUi *u)
{
    unsigned count = u->project->song_count,
             selected = u->song_row < count ? u->song_row : (count ? count - 1 : 0);
    unsigned first = first_visible(selected, count, 5);
    char name[48], value[24], detail[48];
    text(u, 4, 39, "SECTIONS / PER-TRACK PATTERNS", 38, 1);
    if (!count)
    {
        text(u, 10, 79, "NO SECTIONS", 30, 1);
        text(u, 10, 95, "SEL: INSERT CURRENT SCENE", 36, 1);
    }
    for (unsigned i = first; i < count && i < first + 5; i++)
    {
        FmSongEntry *entry = &u->project->song[i];
        snprintf(name, sizeof(name), "SECTION %02u", i + 1);
        snprintf(value, sizeof(value), "X%u %.1f B", entry->repeats, entry->bars);
        snprintf(detail, sizeof(detail), "%u TRACK ASSIGNMENTS", entry->count);
        list_row(u, i - first, name, value, detail, i == selected);
    }
    scroll_mark(u, first, count, 5);
    text(u, 4, 184, "SEL: QUEUE / ARP: INSERT-DELETE", 39, 1);
    footer(u, "SECTION", "BARS", "REPEATS", "PATTERN", "", "", "", "");
}
static void commands(FmUi *u)
{
    unsigned selected = u->command % FM_UI_COMMAND_COUNT,
             first = first_visible(selected, FM_UI_COMMAND_COUNT, 5);
    text(u, 4, 39, "SCL/SEL: OPEN SELECTED ACTION", 38, 1);
    for (unsigned i = first; i < FM_UI_COMMAND_COUNT && i < first + 5; i++)
        list_row(u, i - first, fm_ui_command_label(i), ">", "", i == selected);
    scroll_mark(u, first, FM_UI_COMMAND_COUNT, 5);

    text(u, 4, 184, "HOME: PLAYABLE TRACK OVERVIEW", 39, 1);
    footer(u, "COMMAND", "-", "-", "-", "SEL: OPEN", "", "", "");
}
static void creation(FmUi *u, bool source)
{
    FmInstrument *in = selected_instrument(u);
    bool kit = source && in && in->type == FM_MULTI_SAMPLE;
    unsigned count = source && !kit ? 6u : 2u, selected = kit      ? (u->source == 5)
                                                          : source ? u->source % 6
                                                                   : u->source % 2;
    unsigned first = first_visible(selected, count, 5);
    text(u, 4, 39, source ? "SAMPLE SOURCE / SAME TRACK" : "CREATES TRACK + EMPTY PATTERN", 38, 1);
    for (unsigned i = first; i < count && i < first + 5; i++)
    {
        unsigned source_index = kit && i ? 5 : i;
        const char *name = source ? sources[source_index] : i ? "SAMPLE" : "MULTI SAMPLE";
        const char *detail = source ? (i == 0              ? "DIRECT USB AUDIO INPUT"
                                       : source_index == 5 ? "SHARED CAPTURED OR IMPORTED AUDIO"
                                       : i == 1            ? "48 EDITABLE HARMONICS"
                                                           : "ON-DEVICE WAVE SOURCE")
                             : i    ? "PITCHED / OCTAVE SOURCE ANCHORS"
                                    : "16 LOGICAL SLOTS / CHROMATIC KEYS";
        list_row(u, i - first, name, ">", detail, i == selected);
    }
    scroll_mark(u, first, count, 5);
    text(u, 4, 184, source ? "SEL: USE SOURCE / HOME: CANCEL" : "SEL: CREATE / HOME: CANCEL", 39,
         1);
    char shape[20], points[20];
    static const char *const shapes[] = {"SINE", "TRIANGLE", "SAW", "SQUARE"};
    snprintf(shape, sizeof(shape), "%s", shapes[u->wave_shape % 4]);
    snprintf(points, sizeof(points), "%u", u->wave_points);
    footer(u, "CHOICE", source && u->source == 2 ? "SHAPE" : "-",
           source && u->source == 2 ? "POINTS" : "-", "-", "SCL: OPEN",
           source && u->source == 2 ? shape : "", source && u->source == 2 ? points : "", "");
}
static void capture(FmUi *u)
{
    static const char *const labels[] = {"ROOT NOTE",      "INPUT",        "START THRESHOLD",
                                         "STOP THRESHOLD", "SILENCE HOLD", "PRETRIGGER",
                                         "MAX TAKE",       "MINIMUM TAKE", "MANUAL MODE",
                                         "RETRY LAST",     "SKIP SLOT",    "RESUME"};
    static const char *const inputs[] = {"MONO SUM", "LEFT", "RIGHT"};
    static const char *const states[] = {"CONFIGURE", "WAIT INPUT", "PRETRIGGER",   "RECORDING",
                                         "ENDING",    "BUSY",       "PINNED",       "ADVANCE",
                                         "PAUSED",    "RETRY",      "STORAGE FULL", "INPUT LOST",
                                         "FINISHED",  "CANCELLED"};
    const FmCapture *c = u->capture_monitor;
    FmCaptureConfig *cfg = &u->capture_config;
    char line[64], value[32], root[20];
    FmInstrument *in = selected_instrument(u);
    unsigned slot = c ? c->slot : 0;
    if (slot >= FM_ANCHORS)
        slot = FM_ANCHORS - 1;
    bool multi = in && in->type == FM_MULTI_SAMPLE;
    bold(u, 4, 41, "SIMPLE", 6, multi ? FM_UI_MUTED : FM_UI_AMBER);
    bold(u, 84, 41, "MULTI", 5, multi ? FM_UI_AMBER : FM_UI_MUTED);
    rectangle(u, multi ? 84 : 4, 54, multi ? 39 : 47, 2, FM_UI_AMBER);
    snprintf(line, sizeof(line), "%02u/16", in ? in->count : 0);
    bold_right(u, 236, 41, line, 6, FM_UI_TEXT);
    unsigned next = multi ? in->base_note + slot : u->step_note;
    note(root, sizeof(root), next);
    snprintf(line, sizeof(line), "SLOT %02u  %s", slot + 1, root);
    bold(u, 4, 62, line, 28, FM_UI_TEXT);
    for (unsigned n = 0; n < FM_ANCHORS; n++)
    {
        int x = 4 + (int)(n % 8) * 29, y = 79 + (int)(n / 8) * 32;
        bool filled = in && in->regions[n].id;
        rectangle(u, x, y, 26, 28, filled ? FM_UI_GREEN : FM_UI_BLUE);
        if (n == slot)
        {
            rectangle(u, x, y, 26, 3,
                      c && c->state == FM_CAPTURE_CAPTURING ? FM_UI_RED : FM_UI_AMBER);
            outline(u, x, y, 26, 28, FM_UI_AMBER);
        }
        snprintf(value, sizeof(value), "%02u", n + 1);
        bold(u, x + 5, y + 10, value, 2, filled ? FM_UI_BG : FM_UI_TEXT);
    }
    float peak = c ? fminf(1, c->peak) : 0;
    rectangle(u, 4, 146, 230, 6, FM_UI_BLUE);
    rectangle(u, 4, 146, (int)(peak * 230), 6, c && c->clips ? FM_UI_RED : FM_UI_GREEN);
    snprintf(line, sizeof(line), "%s%s", c && c->state < 14 ? states[c->state] : "NO STREAM",
             c && c->clips ? " / CLIP" : "");
    bold(u, 4, 158, line, 28, c && c->state == FM_CAPTURE_CAPTURING ? FM_UI_RED : FM_UI_AMBER);
    unsigned i = u->capture_row % 12;
    {
        switch (i)
        {
        case 0:
            snprintf(value, sizeof(value), "%s", root);
            break;
        case 1:
            snprintf(value, sizeof(value), "%s", inputs[cfg->source]);
            break;
        case 2:
        case 3:
            snprintf(value, sizeof(value), "%.1f DBFS",
                     20 * log10(i == 2 ? cfg->threshold : cfg->stop_threshold));
            break;
        case 4:
            snprintf(value, sizeof(value), "%.0f MS", cfg->silence_frames / 36.0);
            break;
        case 5:
            snprintf(value, sizeof(value), "%.1f MS", cfg->pretrigger_frames / 36.0);
            break;
        case 6:
            snprintf(value, sizeof(value), "%.1f S", (double)cfg->maximum_frames / FM_RATE);
            break;
        case 7:
            snprintf(value, sizeof(value), "%.1f MS", cfg->minimum_frames / 36.0);
            break;
        case 8:
            snprintf(value, sizeof(value), "%s", cfg->manual ? "ON" : "OFF");
            break;
        default:
            snprintf(value, sizeof(value), "SCL >");
            break;
        }
        rectangle(u, 2, 176, 232, 22, FM_UI_PANEL);
        bold(u, 7, 178, labels[i], 15, FM_UI_TEXT);
        bold_right(u, 230, 188, value, 18, FM_UI_AMBER);
    }
    footer(u, "SETTING", "VALUE", "-", "OCT+", "1-12", "EDIT", "", "PLAY");
}
static void workspace(FmUi *u)
{
    char value[32], detail[64];
    FmTrack *t = selected_track(u);
    FmPattern *p = selected_pattern(u);
    unsigned count = 0, selected = u->options_row;
    if (u->screen == FM_SCREEN_LIBRARY)
    {
        count = u->project->asset_count;
        selected = u->library_item;
        unsigned first = first_visible(selected, count, 5), n = 0;
        for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
            if (u->project->assets[i])
            {
                FmAsset *a = u->project->assets[i];
                if (n >= first && n < first + 5)
                {
                    char name[32];
                    snprintf(name, sizeof(name), "ASSET %u %s", a->id, a->cycle ? "CYCLE" : "PCM");
                    snprintf(value, sizeof(value), "%lu FR", (unsigned long)a->frames);
                    snprintf(detail, sizeof(detail), "%u CH / %lu HZ / %lu BYTES", a->channels,
                             (unsigned long)a->rate, (unsigned long)a->frames * a->channels * 2);
                    list_row(u, n - first, name, value, detail, n == selected);
                }
                n++;
            }
        scroll_mark(u, first, count, 5);
        text(u, 4, 184, "SCL: LINK ASSET / HOME: BACK", 39, 1);
    }
    else if (u->screen == FM_SCREEN_TIMING)
    {
        FmSequenceStatus st = fm_sequence_status(u->sequence);
        count = st.candidate_count;
        unsigned first = first_visible(selected, count, 5);
        if (!count)
            text(u, 8, 80, "RECORD OR HOLD REC FIRST", 36, 1);
        for (unsigned i = first; i < count && i < first + 5; i++)
        {
            char name[30];
            snprintf(name, sizeof(name), "%.2f BPM", st.candidates[i].bpm);
            snprintf(value, sizeof(value), "%.0f%%", 100 * st.candidates[i].confidence);
            snprintf(detail, sizeof(detail), "%.1f BEATS / RAW TIMING KEPT",
                     st.candidates[i].beats);
            list_row(u, i - first, name, value, detail, i == selected);
        }
        scroll_mark(u, first, count, 5);
        text(u, 4, 184, "SCL: USE CANDIDATE / HOME: BACK", 39, 1);
    }
    else
    {
        static const char *const steps[] = {"PROBABILITY", "MICROTIME TICKS", "PARAMETER LOCK"};
        static const char *const setup[] = {"LENGTH STEPS",   "PLAY RATE",     "SWING",
                                            "QUANTIZE TICKS", "FIT LAST NOTE", "PROJECT TEMPO"};
        count = u->screen == FM_SCREEN_STEP_DETAIL ? 3 : 6;
        unsigned first = first_visible(selected, count, 5);
        for (unsigned i = first; i < count && i < first + 5; i++)
        {
            value[0] = detail[0] = 0;
            if (u->screen == FM_SCREEN_STEP_DETAIL)
            {
                if (i == 0)
                    snprintf(value, sizeof(value), "%.0f %%", 100 * u->step_probability);
                if (i == 1)
                    snprintf(value, sizeof(value), "%+.0f", u->step_micro);
                if (i == 2)
                {
                    static const char *const params[] = {"CUTOFF", "RES",   "VOLUME", "PAN",
                                                         "SEND1",  "SEND2", "SEND3",  "SEND4"};
                    snprintf(value, sizeof(value), "%s", params[u->lock_parameter]);
                    snprintf(detail, sizeof(detail), "VALUE %.3f / LATCH UNTIL NEXT CHANGE",
                             u->lock_value);
                }
                else
                    snprintf(detail, sizeof(detail), "STEP %u NOTE %u / SCL: APPLY",
                             u->page * 16 + u->selected_step + 1, u->step_note);
            }
            else if (p)
            {
                switch (i)
                {
                case 0:
                    snprintf(value, sizeof(value), "%.0f", p->length_ticks / (FM_PPQN / 4.0));
                    break;
                case 1:
                    snprintf(value, sizeof(value), "%.3f X", p->rate);
                    break;
                case 2:
                    snprintf(value, sizeof(value), "%.0f %%", 100 * p->swing);
                    break;
                case 3:
                    snprintf(value, sizeof(value), "%.0f", p->quantize);
                    break;
                case 4:
                    snprintf(value, sizeof(value), "TURN >");
                    break;
                case 5:
                    snprintf(value, sizeof(value), "%.1f BPM", u->project->bpm);
                    break;
                }
                snprintf(detail, sizeof(detail), "%s",
                         p->frozen_asset ? "FROZEN: UNFREEZE BEFORE EDITING"
                                         : "RAW EVENT TIMING IS RETAINED");
            }
            list_row(u, i - first, u->screen == FM_SCREEN_STEP_DETAIL ? steps[i] : setup[i], value,
                     detail, i == selected);
        }
        scroll_mark(u, first, count, 5);
        text(u, 4, 184,
             u->screen == FM_SCREEN_STEP_DETAIL ? "SCL: APPLY / HOME: STEP" : "HOME: PATTERNS", 39,
             1);
    }
    (void)t;
    footer(u, "ROW", "VALUE", "FINE", "-", "", "", "", "");
}

static void numeric_legend(FmUi *u)
{
    /* Actual physical key indices, including chromatic gaps between whites.
     * White keys 10-14 are BACKSPACE, SIGN, DECIMAL, CANCEL, CONFIRM. */
    static const char *const role[] = {"0", "1", "2",  "3", "4", "5",   "6",  "7",
                                       "8", "9", "BS", "-", ".", "ESC", "OK", "-"};
    for (unsigned i = 0; i < 16; i++)
    {
        int x = 8 + (int)(i % 4) * 57, y = 107 + (int)(i / 4) * 18;
        char item[16];
        snprintf(item, sizeof(item), "%u:%s", fm_ui_white_keys[i], role[i]);
        text(u, x, y, item, 9, 1);
    }
}
static void modal(FmUi *u)
{
    rectangle(u, 0, 48, 240, 163, 0);
    outline(u, 2, 48, 236, 162, FM_UI_AMBER);
    char line[64];
    const char *title = "PICKER";
    switch (u->picker)
    {
    case FM_PICK_NUMBER:
        title = "ENTER NUMBER";
        break;
    case FM_PICK_NOTE:
        title = "PICK NOTE";
        break;
    case FM_PICK_LOW:
        title = "PICK LOW";
        break;
    case FM_PICK_HIGH:
        title = "PICK HIGH";
        break;
    case FM_PICK_SAMPLE:
        title = "PICK SAMPLE";
        break;
    case FM_PICK_NAME:
        title = "NAME CATEGORY";
        break;
    case FM_PICK_CONFIRM:
        title = "CONFIRM DESTRUCTIVE EDIT";
        break;
    default:
        break;
    }
    bold(u, 9, 55, title, 27, FM_UI_AMBER);
    rectangle(u, 8, 69, 224, 1, FM_UI_GRID);
    if (u->picker == FM_PICK_NUMBER)
    {
        text_at(u, 10, 79, u->number_length ? u->number : "_", 18, 2, 1);
        if (u->screen == FM_SCREEN_EDIT)
        {
            unsigned tab = u->tab % 9, row = u->rows[tab] % row_count[tab];
            text(u, 10, 98, edit_rows[tab][row], 36, 1);
        }
        numeric_legend(u);
        text(u, 10, 183, "PHYSICAL INDEX: ROLE", 35, 1);
        text(u, 10, 197, "SEL: APPLY / HOME: CANCEL", 36, 1);
    }
    else if (u->picker == FM_PICK_NAME)
    {
        text(u, 10, 80, "PHYSICAL KEY INDEX -> CATEGORY", 36, 1);
        for (unsigned i = 0; i < 9; i++)
        {
            snprintf(line, sizeof(line), "%u: %s", fm_ui_white_keys[i], categories[i]);
            text(u, 10 + (int)(i % 2) * 114, 99 + (int)(i / 2) * 18, line, 18, 1);
        }
        text(u, 10, 197, "ONE KEY: NAME / HOME: CANCEL", 36, 1);
    }
    else if (u->picker == FM_PICK_CONFIRM)
    {
        if (u->pending_action >= 0)
        {
            text(u, 10, 86, fm_ui_command_label((unsigned)u->pending_action), 36, 1);
            text(u, 10, 104, "PREVIOUS VERSION RETAINED FOR UNDO", 36, 1);
            text(u, 10, 139, "SCL: APPLY / HOME: CANCEL", 36, 1);
            footer(u, "-", "-", "-", "-", "CONFIRM", "", "", "");
            return;
        }
        text(u, 10, 86,
             u->tab == 8 ? "ISOLATED CREATIVE RENDER" : "THIS CREATES OR REPLACES AUDIO.", 36, 1);
        text(u, 10, 104,
             u->tab == 8 ? "SHARED BUS MIX MAY SOUND DIFFERENT"
                         : "PREVIOUS VERSION RETAINED FOR UNDO",
             36, 1);
        if (u->tab == 8)
            text(u, 10, 120, "ALL SENDS / 1S TAIL / USES STORAGE", 36, 1);
        text(u, 10, 139, "SEL: APPLY / HOME: CANCEL", 36, 1);
        text(u, 10, 157, "HOLD SEL: CANCEL", 36, 1);
    }
    else
    {
        const char *prompt = u->picker == FM_PICK_SAMPLE ? "PLAY A KEY TO SELECT ITS REGION"
                                                         : "PRESS A KEY TO ASSIGN ITS NOTE";
        text(u, 10, 89, prompt, 36, 1);
        if (u->picker == FM_PICK_HIGH)
        {
            note(line, sizeof(line), u->pick_low);
            text(u, 10, 111, "LOW:", 8, 1);
            text(u, 40, 111, line, 24, 1);
        }
        snprintf(line, sizeof(line), "OCT +/-: OCTAVE (%+d)", u->octave);
        text(u, 10, 137, line, 36, 1);
        text(u, 10, 161, "PICKER KEYS DO NOT PLAY OR RECORD", 36, 1);
        text(u, 10, 187, "HOME: CANCEL / KEEP PREVIOUS VALUE", 36, 1);
    }
    footer(u, "-", "-", "-", "-", "MODAL", "", "", "");
}
void fm_ui_render(FmUi *u)
{
    if (!u)
        return;
    memset(u->pixels, 0, sizeof(u->pixels));
    if (!u->project)
    {
        text(u, 4, 4, "NO PROJECT", 38, 1);
        return;
    }
    header(u);
    switch (u->screen)
    {
    case FM_SCREEN_HOME:
        home(u, false);
        break;
    case FM_SCREEN_EDIT:
        edit(u);
        break;
    case FM_SCREEN_MIXER:
        home(u, true);
        break;
    case FM_SCREEN_FX:
        effects(u);
        break;
    case FM_SCREEN_STEP:
        steps(u);
        break;
    case FM_SCREEN_PATTERNS:
        patterns(u);
        break;
    case FM_SCREEN_SONG:
        song(u);
        break;
    case FM_SCREEN_CAPTURE:
        capture(u);
        break;
    case FM_SCREEN_COMMANDS:
        commands(u);
        break;
    case FM_SCREEN_NEW:
        creation(u, false);
        break;
    case FM_SCREEN_SOURCE:
        creation(u, true);
        break;
    case FM_SCREEN_STEP_DETAIL:
    case FM_SCREEN_PATTERN_SETTINGS:
    case FM_SCREEN_TIMING:
    case FM_SCREEN_LIBRARY:
        workspace(u);
        break;
    default:
        text(u, 8, 70, "UNKNOWN SCREEN", 36, 1);
        break;
    }
    if (u->picker != FM_PICK_NONE)
        modal(u);
    else
    {
        const char *status = u->status[0]                 ? u->status
                             : u->save_pending            ? "SAVE QUEUED"
                             : u->project->volatile_audio ? "VOLATILE AUDIO - SAVE REQUIRED"
                                                          : "";
        rectangle(u, 0, 199, 240, 12, FM_UI_PANEL);
        text(u, 4, 201, status, 39, u->last_result == FM_OK ? FM_UI_MUTED : FM_UI_RED);
    }
}
