/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "audio/fm_audio.h"
#include "audio/fm_src.h"
#include "dsp/fm_synthesis.h"
#include "sequence/fm_sequence.h"
#include "ui/fm_ui.h"
#include <stdio.h>
#include <time.h>
static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int main(void)
{
    FmProject p;
    FmBudget b = fm_budget_host();
#ifdef FM_PROFILE_SMALL
    size_t limits[] = {65536, 131072, 65536, 98304, 49152};
    for (unsigned i = 0; i < FM_MEM_COUNT; i++)
        b.limit[i] = limits[i];
#endif
    if (fm_project_init(&p, &b) != FM_OK)
        return 1;
    p.fx[0].parameter[0] = .08f;
    FmEngine e;
    if (fm_engine_init(&e, &p, 8) != FM_OK)
        return 2;
    FmSequencer *seq = fm_sequence_create(&p, 128, 128);
    if (!seq)
        return 3;
    for (unsigned t = 0; t < 2; t++)
    {
        FmId id, region;
        if (fm_instrument_create(&p, FM_SAMPLE, &id) != FM_OK)
            return 4;
        FmTrack *track = fm_track(&p, id);
        if (fm_synthesis_preset(&p, track->instrument, FM_WAVE_SAW, 600, &region) != FM_OK)
            return 5;
        for (unsigned fx = 0; fx < 4; fx++)
            track->sends[fx] = .25f;
        for (unsigned n = 0; n < 4; n++)
        {
            FmEvent event = {.track = id,
                             .kind = FM_EVENT_NOTE_ON,
                             .note = 48 + n * 7,
                             .velocity = 100,
                             .identity = t * 4 + n + 1};
            if (fm_engine_event(&e, &event) != FM_OK)
                return 6;
        }
    }
    float audio[256];
    double sum = 0, worst = 0;
    unsigned blocks = 600;
    for (unsigned i = 0; i < blocks; i++)
    {
        double start = now();
        fm_engine_render(&e, audio, 128);
        double elapsed = now() - start;
        sum += elapsed;
        if (elapsed > worst)
            worst = elapsed;
    }
    printf("{\"engine_rate\":36000,\"voices\":8,\"tracks\":2,\"effects\":4,\"blocks\":%u,\"block_"
           "frames\":128,\"host_mean_ms\":%.6f,\"host_max_ms\":%.6f,\"static_engine_bytes\":%zu,"
           "\"static_ui_bytes\":%zu,\"src_state_bytes\":%zu,\"memory_used\":[",
           blocks, sum / blocks * 1000, worst * 1000, sizeof(e), sizeof(FmUi), sizeof(FmSrc));
    for (unsigned i = 0; i < FM_MEM_COUNT; i++)
        printf("%s%zu", i ? "," : "", p.budget.used[i]);
    printf("],\"memory_peak\":[");
    for (unsigned i = 0; i < FM_MEM_COUNT; i++)
        printf("%s%zu", i ? "," : "", p.budget.peak[i]);
    puts("],\"device_measured\":false}");
    fm_sequence_destroy(seq);
    fm_engine_destroy(&e);
    fm_project_destroy(&p);
    return 0;
}
