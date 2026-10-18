/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "../../firmware/workstation/audio/fm_src.h"
#include "../../firmware/workstation/midi/fm_midi.h"
#include "../../firmware/workstation/sampler/fm_capture.h"
#include "../../firmware/workstation/storage/storage.h"
#include "../../firmware/workstation/ui/fm_ui.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define PI 3.14159265358979323846
/* This executable drives the same input, display, sequencer, capture, storage
 * and 36 kHz engine used by the portable firmware. Host budgets are explicit;
 * these are no claim about the target board's memory or timing. */
typedef struct
{
    FmProject project;
    FmEngine engine;
    FmSequencer *sequence;
    FmUi ui;
    FmCapture capture;
    FmMidi midi;
    FILE *wave;
    uint64_t frames;
    char prefix[512];
} Simulator;
static void put16(FILE *f, uint16_t v)
{
    fputc((int)(v & 255), f);
    fputc((int)(v >> 8), f);
}
static void put32(FILE *f, uint32_t v)
{
    put16(f, (uint16_t)v);
    put16(f, (uint16_t)(v >> 16));
}
static void wave_header(FILE *f, uint32_t bytes)
{
    fwrite("RIFF", 1, 4, f);
    put32(f, bytes + 36);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 2);
    put32(f, FM_RATE);
    put32(f, FM_RATE * 4);
    put16(f, 4);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, bytes);
}
static FmResult save_project(void *user, FmProject *project)
{
    Simulator *s = user;
    return fm_store_save(project, s->prefix);
}
static FmResult capture_command(void *user, unsigned command, FmId track, uint8_t root, uint64_t ms)
{
    Simulator *s = user;
    (void)ms;
    if (command == 0)
    {
        if (s->capture.project && s->capture.track == track)
            return fm_capture_resume(&s->capture, root);
        fm_capture_destroy(&s->capture);
        FmCaptureConfig cfg = s->ui.capture_config;
        return fm_capture_begin(&s->capture, &s->project, track, root, &cfg);
    }
    if (command == 5)
        return fm_capture_skip(&s->capture);
    if (command == 6)
    {
        if (s->capture.project && s->capture.state == FM_CAPTURE_CAPTURING)
            return FM_ERR_BUSY;
        fm_capture_destroy(&s->capture);
        return fm_capture_begin(&s->capture, &s->project, track, root, &s->ui.capture_config);
    }
    if (command == 1)
        return fm_capture_finish(&s->capture);
    if (command == 2 || command == 4)
        return fm_capture_retry(&s->capture);
    if (command == 3)
    {
        if (s->capture.state == FM_CAPTURE_CAPTURING)
            return fm_capture_finish(&s->capture);
        if (s->capture.state != FM_CAPTURE_WAIT_INPUT)
        {
            FmResult r = fm_capture_resume(&s->capture, root);
            if (r != FM_OK)
                return r;
        }
        return fm_capture_manual_start(&s->capture);
    }
    return FM_ERR_ARGUMENT;
}
static int init(Simulator *s, const char *dir)
{
    memset(s, 0, sizeof(*s));
    if (mkdir(dir, 0777) != 0 && errno != EEXIST)
    {
        perror(dir);
        return 1;
    }
    snprintf(s->prefix, sizeof(s->prefix), "%s/project", dir);
    if (fm_project_init(&s->project, NULL) != FM_OK)
        return 1;
    if (fm_engine_init(&s->engine, &s->project, 16) != FM_OK)
        return 1;
    s->sequence = fm_sequence_create(&s->project, 4096, 2048);
    if (!s->sequence)
        return 1;
    fm_ui_init(&s->ui, &s->project, &s->engine, s->sequence);
    fm_ui_callbacks(&s->ui, save_project, capture_command, s);
    s->ui.capture_monitor = &s->capture;
    fm_midi_init(&s->midi, &s->project, &s->engine, s->sequence);
    char path[512];
    snprintf(path, sizeof(path), "%s/audio.wav", dir);
    s->wave = fopen(path, "wb+");
    if (!s->wave)
        return 1;
    wave_header(s->wave, 0);
    return 0;
}
static void render_frames(Simulator *s, uint32_t frames)
{
    float audio[256 * 2];
    while (frames)
    {
        uint32_t n = frames > 256 ? 256 : frames;
        fm_sequence_advance(s->sequence, n, fm_ui_sequence_dispatch, &s->engine);
        fm_engine_render(&s->engine, audio, n);
        for (unsigned i = 0; s->wave && i < n * 2; i++)
        {
            float v = audio[i];
            if (v > 1)
                v = 1;
            if (v < -1)
                v = -1;
            put16(s->wave, (uint16_t)(int16_t)lrintf(v * 32767));
        }
        s->frames += n;
        frames -= n;
    }
    fm_ui_tick(&s->ui, s->frames / 36);
}
static void advance(Simulator *s, uint64_t ms)
{
    uint64_t end = ms * 36;
    if (end < s->frames)
        return;
    while (end > s->frames)
    {
        uint64_t d = end - s->frames;
        render_frames(s, d > 36000 ? 36000 : (uint32_t)d);
    }
}
static void button(Simulator *s, FmButton b, bool down)
{
    fm_ui_button(&s->ui, b, down, s->frames / 36);
}
static void tap(Simulator *s, FmButton b)
{
    button(s, b, true);
    advance(s, s->frames / 36 + 10);
    button(s, b, false);
    advance(s, s->frames / 36 + 10);
}
static void key(Simulator *s, unsigned index)
{
    fm_ui_key(&s->ui, index, true, s->frames / 36);
    advance(s, s->frames / 36 + 10);
    fm_ui_key(&s->ui, index, false, s->frames / 36);
    advance(s, s->frames / 36 + 10);
}
static void encoder(Simulator *s, unsigned index, int d)
{
    fm_ui_encoder(&s->ui, index, d, s->frames / 36);
}
static int screenshot(Simulator *s, const char *path)
{
    fm_ui_render(&s->ui);
    FILE *f = fopen(path, "wb");
    if (!f)
    {
        perror(path);
        return 1;
    }
    fprintf(f, "P5\n%u %u\n255\n", FM_DISPLAY_WIDTH, FM_DISPLAY_HEIGHT);
    for (size_t i = 0; i < sizeof(s->ui.pixels); i++)
        fputc(s->ui.pixels[i] ? 255 : 0, f);
    int r = ferror(f);
    fclose(f);
    return r;
}
static int shot(Simulator *s, const char *dir, const char *name)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.pgm", dir, name);
    return screenshot(s, path);
}
/* Deterministic 44.1 kHz host stereo PCM passes through the actual streaming
 * boundary SRC into capture. Quiet time lets the detector finalize/service. */
static FmResult tone(Simulator *s, double frequency, unsigned milliseconds)
{
    FmSrc src;
    FmResult r = fm_src_init(&src, 44100, FM_RATE, 2);
    if (r != FM_OK)
        return r;
    size_t total = (size_t)milliseconds * 44100 / 1000;
    size_t done = 0;
    FmFrame first = s->frames;
    while (done < total)
    {
        float input[98], converted[128];
        size_t n = total - done;
        if (n > 49)
            n = 49;
        for (size_t j = 0; j < n; j++)
        {
            double t = (double)(done + j) / 44100;
            float value = t < .08f ? (float)(sin(2 * PI * frequency * t) * .65 * exp(-t * 25)) : 0;
            input[2 * j] = input[2 * j + 1] = value;
        }
        size_t used = 0;
        size_t out = fm_src_process(&src, input, n, &used, converted, 64);
        if (used != n)
            return FM_ERR_BUSY;
        fm_capture_accept_stereo(&s->capture, converted, out, first);
        first += out;
        if (s->capture.state == FM_CAPTURE_FINALIZE || s->capture.state == FM_CAPTURE_END_DETECTED)
        {
            r = fm_capture_service(&s->capture);
            if (r != FM_OK && r != FM_ERR_BUSY)
                return r;
        }
        render_frames(s, (uint32_t)out);
        done += n;
    }
    return FM_OK;
}
static int demo(Simulator *s, const char *dir)
{
    /* Real physical controls create MULTI SAMPLE, three-hit partial then sixteen
     * slots, and OCT+ immediately returns to playable HOME without octave jump. */
    tap(s, FM_BUTTON_SCL);
    tap(s, FM_BUTTON_SCL);
    key(s, fm_ui_white_keys[0]);
    tap(s, FM_BUTTON_SCL);
    if (s->ui.last_result != FM_OK)
        return 1;
    for (unsigned hit = 0; hit < 16; hit++)
    {
        FmResult r = tone(s, 110 + hit * 31, 400);
        if (r != FM_OK)
        {
            fprintf(stderr, "capture %u: %s\n", hit, fm_result_string(r));
            return 1;
        }
        if (hit == 2 && shot(s, dir, "capture-three"))
            return 1;
    }
    if (shot(s, dir, "capture-sixteen"))
        return 1;
    button(s, FM_BUTTON_OCT_PLUS, true);
    advance(s, s->frames / 36 + 10);
    button(s, FM_BUTTON_OCT_PLUS, false);
    if (shot(s, dir, "home-kit"))
        return 1;
    for (unsigned i = 0; i < 4; i++)
        key(s, i);
    tap(s, FM_BUTTON_HOME);
    tap(s, FM_BUTTON_SCL);
    tap(s, FM_BUTTON_SCL);
    key(s, fm_ui_white_keys[1]);
    encoder(s, 2, 1);
    tap(s, FM_BUTTON_SCL);
    if (s->ui.last_result != FM_OK)
        return 1;
    button(s, FM_BUTTON_EDIT, true);
    advance(s, s->frames / 36 + 501);
    key(s, fm_ui_white_keys[2]);
    button(s, FM_BUTTON_EDIT, false);
    tap(s, FM_BUTTON_EDIT);
    for (unsigned i = 0; i < 6; i++)
        tap(s, FM_BUTTON_EDIT);
    encoder(s, 2, 1);
    encoder(s, 3, 1);
    encoder(s, 4, 96);
    encoder(s, 2, 2);
    tap(s, FM_BUTTON_SCL);
    if (shot(s, dir, "wave-additive"))
        return 1;
    tap(s, FM_BUTTON_HOME);
    tap(s, FM_BUTTON_SEQ);
    for (unsigned i = 0; i < 16; i += 4)
        key(s, fm_ui_white_keys[i]);
    if (shot(s, dir, "step"))
        return 1;
    tap(s, FM_BUTTON_SEQ);
    if (shot(s, dir, "patterns"))
        return 1;
    tap(s, FM_BUTTON_SEQ);
    tap(s, FM_BUTTON_SCL);
    if (shot(s, dir, "song"))
        return 1;
    tap(s, FM_BUTTON_PLAY);
    advance(s, s->frames / 36 + 4000);
    tap(s, FM_BUTTON_SAVE);
    if (shot(s, dir, "save-queued"))
        return 1;
    tap(s, FM_BUTTON_PLAY);
    advance(s, s->frames / 36 + 20);
    if (s->ui.last_result != FM_OK)
    {
        fprintf(stderr, "save: %s\n", s->ui.status);
        return 1;
    }
    tap(s, FM_BUTTON_HOME);
    tap(s, FM_BUTTON_GLO);
    encoder(s, 5, 30);
    if (shot(s, dir, "mixer"))
        return 1;
    tap(s, FM_BUTTON_FX);
    if (shot(s, dir, "fx"))
        return 1;
    tap(s, FM_BUTTON_HOME);
    if (shot(s, dir, "home"))
        return 1;
    for (unsigned tab = 0; tab < 9; tab++)
    {
        s->ui.screen = FM_SCREEN_EDIT;
        s->ui.tab = tab;
        char name[40];
        snprintf(name, sizeof(name), "edit-%u", tab);
        if (shot(s, dir, name))
            return 1;
    }
    printf("Host simulation: %u tracks, %u assets, %u captured kit regions; %.3f s at 36000 Hz.\n",
           s->project.track_count, s->project.asset_count, s->project.instruments[0]->count,
           (double)s->frames / FM_RATE);
    return 0;
}
static int button_name(const char *name)
{
    static const char *const names[] = {"OCT-", "OCT+", "FX",   "SCL", "ENV", "LFO",  "EDIT",
                                        "GLO",  "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"};
    for (unsigned i = 0; i < FM_UI_BUTTONS; i++)
        if (!strcmp(names[i], name))
            return (int)i;
    return -1;
}
static int script(Simulator *s, FILE *f)
{
    char line[1024];
    unsigned lineno = 0;
    while (fgets(line, sizeof(line), f))
    {
        lineno++;
        if (line[0] == '#' || line[0] == '\n')
            continue;
        unsigned long long ms;
        char cmd[40], arg[512], state[40];
        int value;
        if (sscanf(line, "%llu %39s", &ms, cmd) != 2)
            goto malformed;
        if (ms * 36 < s->frames)
            goto malformed;
        advance(s, (uint64_t)ms);
        if (!strcmp(cmd, "button"))
        {
            if (sscanf(line, "%*u %*s %511s %39s", arg, state) != 2)
                goto malformed;
            int b = button_name(arg);
            if (b < 0 || (strcmp(state, "down") && strcmp(state, "up")))
                goto malformed;
            button(s, (FmButton)b, !strcmp(state, "down"));
        }
        else if (!strcmp(cmd, "key"))
        {
            unsigned index;
            if (sscanf(line, "%*u %*s %u %39s", &index, state) != 2 || index >= 27 ||
                (strcmp(state, "down") && strcmp(state, "up")))
                goto malformed;
            fm_ui_key(&s->ui, index, !strcmp(state, "down"), s->frames / 36);
        }
        else if (!strcmp(cmd, "encoder"))
        {
            unsigned index;
            if (sscanf(line, "%*u %*s %u %d", &index, &value) != 2 || index > 6)
                goto malformed;
            encoder(s, index, value);
        }
        else if (!strcmp(cmd, "screen"))
        {
            if (sscanf(line, "%*u %*s %511s", arg) != 1 || screenshot(s, arg))
                return 1;
        }
        else if (!strcmp(cmd, "tone"))
        {
            double hz;
            unsigned duration;
            if (sscanf(line, "%*u %*s %lf %u", &hz, &duration) != 2 || hz <= 0 || hz > 18000 ||
                duration > 60000)
                goto malformed;
            FmResult r = tone(s, hz, duration);
            if (r != FM_OK)
            {
                fprintf(stderr, "tone: %s\n", fm_result_string(r));
                return 1;
            }
        }
        else if (!strcmp(cmd, "midi"))
        {
            unsigned status, d1, d2;
            if (sscanf(line, "%*u %*s %x %u %u", &status, &d1, &d2) != 3 || status > 255 ||
                d1 > 127 || d2 > 127)
                goto malformed;
            FmResult r = fm_midi_message(&s->midi, FM_ORIGIN_USB, (uint8_t)status, (uint8_t)d1,
                                         (uint8_t)d2, s->frames);
            if (r != FM_OK)
            {
                fprintf(stderr, "midi: %s\n", fm_result_string(r));
                return 1;
            }
        }
        else if (strcmp(cmd, "wait"))
            goto malformed;
        continue;
    malformed:
        fprintf(stderr, "Invalid/nonmonotonic script line %u\n", lineno);
        return 1;
    }
    return ferror(f) ? 1 : 0;
}
static void destroy(Simulator *s)
{
    if (s->wave)
    {
        uint64_t bytes = s->frames * 4;
        if (bytes <= UINT32_MAX - 36)
        {
            rewind(s->wave);
            wave_header(s->wave, (uint32_t)bytes);
        }
        fclose(s->wave);
    }
    fm_capture_destroy(&s->capture);
    fm_sequence_destroy(s->sequence);
    fm_engine_destroy(&s->engine);
    fm_project_destroy(&s->project);
}
int main(int argc, char **argv)
{
    if (argc < 3 || (strcmp(argv[1], "--demo") && strcmp(argv[1], "--script")))
    {
        fprintf(stderr, "usage: %s --demo OUTDIR | --script EVENTS.txt [OUTDIR]\n", argv[0]);
        return 2;
    }
    bool is_demo = !strcmp(argv[1], "--demo");
    const char *dir = is_demo ? argv[2] : argc > 3 ? argv[3] : "build/simulation";
    Simulator s;
    if (init(&s, dir))
    {
        fprintf(stderr, "Could not initialize host simulation\n");
        destroy(&s);
        return 1;
    }
    int r;
    if (is_demo)
        r = demo(&s, dir);
    else
    {
        FILE *f = fopen(argv[2], "r");
        if (!f)
        {
            perror(argv[2]);
            destroy(&s);
            return 1;
        }
        r = script(&s, f);
        fclose(f);
    }
    destroy(&s);
    return r;
}
