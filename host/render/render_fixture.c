/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* Offline fixtures at the engine rate. Exit status is the pitch check. */
#include "audio/reader.h"
#include "core/types.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static void write_wav(const char *path, const int16_t *mono, uint32_t frames, uint32_t rate)
{
    FILE *f = fopen(path, "wb");
    uint32_t data = frames * 2u;
    uint32_t riff = 36u + data;
    uint16_t audio_format = 1, channels = 1, bits = 16, block = 2;
    uint32_t byte_rate = rate * 2u;
    if (!f) {
        perror(path);
        return;
    }
    fwrite("RIFF", 1, 4, f);
    fwrite(&riff, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    {
        uint32_t fmt = 16;
        fwrite(&fmt, 4, 1, f);
    }
    fwrite(&audio_format, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data, 4, 1, f);
    fwrite(mono, 2, frames, f);
    fclose(f);
}

static int crossings(const int16_t *mono, int frames)
{
    int n = 0, i;
    for (i = 1; i < frames; i++) {
        if (mono[i - 1] < 0 && mono[i] >= 0)
            n++;
    }
    return n;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/fixtures";
    int16_t table[1024];
    int16_t uni[36000], up[36000];
    FzReader reader;
    char path[512];
    int i, zu, zo;
    for (i = 0; i < 1024; i++)
        table[i] = (int16_t)lround(sin(2.0 * M_PI * (double)i / 1024.0) * 14000.0);
    fz_reader_start_cycle(&reader, table, 1024, 69, 0);
    for (i = 0; i < 36000; i++)
        uni[i] = fz_reader_next(&reader);
    fz_reader_start_cycle(&reader, table, 1024, 81, 0);
    for (i = 0; i < 36000; i++)
        up[i] = fz_reader_next(&reader);
    snprintf(path, sizeof path, "%s/a4_cycle.wav", dir);
    write_wav(path, uni, 36000, FZ_ENGINE_RATE_HZ);
    snprintf(path, sizeof path, "%s/a4_plus_octave.wav", dir);
    write_wav(path, up, 36000, FZ_ENGINE_RATE_HZ);
    zu = crossings(uni, 36000);
    zo = crossings(up, 36000);
    printf("A4 crossings %d, +12 crossings %d\n", zu, zo);
    if (zu < 435 || zu > 445)
        return 1;
    if (zo < 870 || zo > 890)
        return 1;
    return 0;
}
