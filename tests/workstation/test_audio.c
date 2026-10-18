/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../firmware/workstation/audio/fm_audio.h"
#include "../../firmware/workstation/audio/fm_src.h"
#include "../../firmware/workstation/dsp/fm_synthesis.h"
#include "test.h"
#include <stdlib.h>
#include <string.h>
static int test_src(void)
{
    enum
    {
        N = 12000,
        OUT = 20000
    };
    float *input = malloc(N * sizeof(float)), *a = malloc(OUT * sizeof(float)),
          *b = malloc(OUT * sizeof(float));
    CHECK(input && a && b);
    for (unsigned i = 0; i < N; i++)
        input[i] = sinf(6.28318530718f * 997.0f * (float)i / 44100.0f);
    FmSrc src;
    CHECK(fm_src_init(&src, 44100, 36000, 1) == FM_OK);
    size_t used = 0, na = fm_src_process(&src, input, N, &used, a, OUT);
    CHECK(used == N);
    CHECK(na > 9700 && na < 9800);
    CHECK(fm_src_init(&src, 44100, 36000, 1) == FM_OK);
    size_t at = 0, nb = 0;
    while (at < N)
    {
        size_t block = (at % 127u) + 1u;
        if (block > N - at)
            block = N - at;
        size_t consumed = 0, done = fm_src_process(&src, input + at, block, &consumed, b + nb, 17);
        at += consumed;
        nb += done;
        CHECK(consumed || done);
    }
    for (;;)
    {
        size_t consumed = 0, done = fm_src_process(&src, NULL, 0, &consumed, b + nb, OUT - nb);
        nb += done;
        if (!done)
            break;
    }
    CHECK(na == nb);
    for (size_t i = 0; i < na; i++)
        NEAR(a[i], b[i], 1e-7);
    CHECK(src.exact_phases && src.phases == 40);
    for (unsigned out = 30; out < 180; out++)
    {
        double position = (double)out * 44100 / 36000;
        int center = (int)floor(position);
        double fraction = position - center, sum = 0, weight = 0, cutoff = .47 * 36000 / 44100;
        for (int offset = -15; offset <= 16; offset++)
        {
            double d = fraction - offset, x = 6.283185307179586 * cutoff * d;
            double c = 2 * cutoff * (fabs(x) < 1e-12 ? 1 : sin(x) / x) *
                       (.5 + .5 * cos(3.141592653589793 * d / 16));
            sum += c * input[center + offset];
            weight += c;
        }
        NEAR(a[out], sum / weight, 1e-7);
    }
    /* Anti-aliasing: a 22 kHz transport tone must be attenuated before 36 kHz. */
    for (unsigned i = 0; i < N; i++)
        input[i] = sinf(6.28318530718f * 22000.0f * (float)i / 48000.0f);
    CHECK(fm_src_init(&src, 48000, 36000, 1) == FM_OK);
    na = fm_src_process(&src, input, N, &used, a, OUT);
    double energy = 0;
    for (size_t i = 100; i < na; i++)
        energy += (double)a[i] * a[i];
    CHECK(sqrt(energy / (double)(na - 100u)) < 0.03);
    free(input);
    free(a);
    free(b);
    return 0;
}
static int test_pitch_and_notes(void)
{
    FmProject project;
    CHECK(fm_project_init(&project, NULL) == FM_OK);
    FmId track_id, region_id;
    CHECK(fm_instrument_create(&project, FM_SAMPLE, &track_id) == FM_OK);
    FmTrack *track = fm_track(&project, track_id);
    CHECK(fm_synthesis_preset(&project, track->instrument, FM_WAVE_SINE, 600, &region_id) == FM_OK);
    FmEngine engine;
    CHECK(fm_engine_init(&engine, &project, 8) == FM_OK);
    FmEvent note = {0};
    note.track = track_id;
    note.kind = FM_EVENT_NOTE_ON;
    note.note = 69;
    note.velocity = 127;
    note.identity = 1;
    note.frame = 23;
    CHECK(fm_engine_event(&engine, &note) == FM_OK);
    float *output = calloc(FM_RATE * 2u, sizeof(float));
    CHECK(output);
    fm_engine_render(&engine, output, FM_RATE);
    for (unsigned i = 0; i < 23; i++)
        NEAR(output[2u * i], 0, 0);
    unsigned first = 0, last = 0, crossings = 0;
    for (unsigned i = 1001; i < FM_RATE; i++)
        if (output[2u * (i - 1u)] <= 0 && output[2u * i] > 0)
        {
            if (!crossings)
                first = i;
            last = i;
            crossings++;
        }
    double frequency = (double)(crossings - 1u) * (double)FM_RATE / (double)(last - first);
    NEAR(frequency, 440, 0.03);
    /* Same target pitch for an independently sized 1024-point cycle. */
    CHECK(fm_synthesis_preset(&project, track->instrument, FM_WAVE_SINE, 1024, &region_id) ==
          FM_OK);
    fm_engine_panic(&engine);
    fm_engine_render(&engine, output, 2000);
    note.frame = engine.frame;
    note.identity = 2;
    CHECK(fm_engine_event(&engine, &note) == FM_OK);
    fm_engine_render(&engine, output, 1);
    unsigned active = 0;
    for (unsigned i = 0; i < engine.max_voices; i++)
        if (engine.voices[i].active)
        {
            NEAR(engine.voices[i].step, 1024.0 * 440.0 / FM_RATE, 1e-10);
            active++;
        }
    CHECK(active == 1);
    note.identity = 3;
    CHECK(fm_engine_event(&engine, &note) == FM_OK);
    fm_engine_render(&engine, output, 1);
    CHECK(fm_engine_active_voices(&engine) == 2);
    FmEvent sustain = {0};
    sustain.kind = FM_EVENT_SUSTAIN;
    sustain.track = track_id;
    sustain.value = 1;
    sustain.frame = engine.frame;
    CHECK(fm_engine_event(&engine, &sustain) == FM_OK);
    note.kind = FM_EVENT_NOTE_OFF;
    note.identity = 2;
    note.frame = engine.frame;
    CHECK(fm_engine_event(&engine, &note) == FM_OK);
    fm_engine_render(&engine, output, 1);
    unsigned held = 0, sustained = 0;
    for (unsigned i = 0; i < engine.max_voices; i++)
    {
        held += engine.voices[i].held ? 1u : 0u;
        sustained += engine.voices[i].sustained ? 1u : 0u;
    }
    CHECK(held == 1 && sustained == 1);
    sustain.value = 0;
    sustain.frame = engine.frame;
    CHECK(fm_engine_event(&engine, &sustain) == FM_OK);
    fm_engine_render(&engine, output, 2000);
    CHECK(fm_engine_active_voices(&engine) == 1);
    note.identity = 3;
    note.frame = engine.frame;
    CHECK(fm_engine_event(&engine, &note) == FM_OK);
    fm_engine_render(&engine, output, 2000);
    CHECK(fm_engine_active_voices(&engine) == 0);
    free(output);
    fm_engine_destroy(&engine);
    fm_project_destroy(&project);
    return 0;
}
static int test_rate_and_loop(void)
{
    FmProject project;
    CHECK(fm_project_init(&project, NULL) == FM_OK);
    FmId track_id, asset_id, region_id;
    CHECK(fm_instrument_create(&project, FM_SAMPLE, &track_id) == FM_OK);
    FmTrack *track = fm_track(&project, track_id);
    int16_t pcm[360];
    for (unsigned i = 0; i < 360; i++)
        pcm[i] = (int16_t)(5000 + i * 30u);
    CHECK(fm_asset_create(&project, pcm, 360, 1, FM_RATE, false, FM_SOURCE_CAPTURE, &asset_id) ==
          FM_OK);
    CHECK(fm_region_attach(&project, track->instrument, asset_id, 60, -1, &region_id) == FM_OK);
    FmEngine engine;
    CHECK(fm_engine_init(&engine, &project, 2) == FM_OK);
    float output[1600];
    FmEvent note = {0};
    note.kind = FM_EVENT_NOTE_ON;
    note.track = track_id;
    note.note = 72;
    note.velocity = 127;
    CHECK(fm_engine_event(&engine, &note) == FM_OK);
    fm_engine_render(&engine, output, 179);
    CHECK(fm_engine_active_voices(&engine) == 1);
    fm_engine_render(&engine, output, 1);
    CHECK(fm_engine_active_voices(&engine) == 0);
    FmRegion *region = fm_region(&project, region_id);
    region->loop_enabled = true;
    region->loop_count = 2;
    region->loops[0] = (FmLoopSection){20, 80, 10, 2, FM_LOOP_CONTINUE, 0, false};
    region->loops[1] = (FmLoopSection){100, 160, 5, 3, FM_LOOP_END, 0, false};
    note.note = 60;
    note.frame = engine.frame;
    CHECK(fm_engine_event(&engine, &note) == FM_OK);
    fm_engine_render(&engine, output, 500);
    CHECK(fm_engine_active_voices(&engine) == 0);
    for (unsigned i = 0; i < 1000; i++)
        CHECK(isfinite(output[i]));
    fm_engine_destroy(&engine);
    fm_project_destroy(&project);
    return 0;
}
static int test_envelopes_filter_fx(void)
{
    FmEnvelope shape = {0};
    shape.count = 8;
    shape.sustain = 5;
    shape.release = 6;
    for (unsigned i = 0; i < 8; i++)
    {
        shape.stage[i].milliseconds = 1;
        shape.stage[i].level = (float)(i + 1u) / 8;
    }
    shape.stage[7].level = 0;
    FmEnvState env;
    fm_env_start(&env, &shape, 60, 127);
    for (unsigned i = 0; i < 90; i++)
        (void)fm_env_tick(&env);
    fm_env_release(&env);
    for (unsigned i = 0; i < 100; i++)
        (void)fm_env_tick(&env);
    CHECK(env.finished);
    FmFilter filter = {0};
    for (unsigned i = 0; i < 100000; i++)
    {
        float output = fm_filter_tick(&filter, (i & 1u) ? 4 : -4, 25.0f + (float)(i % 14975u),
                                      (float)(i % 100u) / 99.0f, 1);
        CHECK(isfinite(output));
        CHECK(fabsf(output) < 32);
    }
    FmProject project;
    CHECK(fm_project_init(&project, NULL) == FM_OK);
    FmEngine engine;
    CHECK(fm_engine_init(&engine, &project, 8) == FM_OK);
    CHECK(project.budget.used[FM_MEM_FX] ==
          fm_fx_memory(FM_FX_DELAY) + fm_fx_memory(FM_FX_REVERB) + fm_fx_memory(FM_FX_CHORUS));
    float silent[2048];
    fm_engine_render(&engine, silent, 1024);
    for (unsigned i = 0; i < 2048; i++)
        NEAR(silent[i], 0, 0);
    float *old = engine.fx[0].buffer;
    size_t used = project.budget.used[FM_MEM_FX];
    project.budget.limit[FM_MEM_FX] = used;
    FmFxConfig config = project.fx[0];
    config.type = FM_FX_REVERB;
    CHECK(fm_engine_fx_prepare(&engine, 0, &config) == FM_ERR_MEMORY);
    CHECK(engine.fx[0].buffer == old);
    CHECK(fm_engine_fx_release(&engine, 0, true) == FM_OK);
    CHECK(project.budget.used[FM_MEM_FX] == used - fm_fx_memory(FM_FX_DELAY));
    fm_engine_destroy(&engine);
    fm_project_destroy(&project);
    return 0;
}
static int test_send_routing_and_frozen(void)
{
    FmProject project;
    CHECK(fm_project_init(&project, NULL) == FM_OK);
    FmId tid, aid, rid;
    CHECK(fm_instrument_create(&project, FM_SAMPLE, &tid) == FM_OK);
    FmTrack *track = fm_track(&project, tid);
    int16_t pcm[1000];
    for (unsigned i = 0; i < 1000; i++)
        pcm[i] = (int16_t)(5000 * sinf(6.2831853f * (float)i / 80.0f));
    CHECK(fm_asset_create(&project, pcm, 1000, 1, FM_RATE, false, FM_SOURCE_CAPTURE, &aid) ==
          FM_OK);
    CHECK(fm_region_attach(&project, track->instrument, aid, 60, -1, &rid) == FM_OK);
    for (unsigned fx = 1; fx < FM_FX_SLOTS; fx++)
        project.fx[fx].enabled = false;
    project.fx[0].parameter[0] = 0.005f;
    track->sends[0] = 1;
    track->volume = 0.2f;
    FmEngine first;
    CHECK(fm_engine_init(&first, &project, 2) == FM_OK);
    FmEvent note = {0};
    note.track = tid;
    note.kind = FM_EVENT_NOTE_ON;
    note.note = 60;
    note.velocity = 127;
    CHECK(fm_engine_event(&first, &note) == FM_OK);
    float a[4000], b[4000];
    fm_engine_render(&first, a, 2000);
    fm_engine_destroy(&first);
    track->volume = 0.4f;
    CHECK(fm_engine_init(&first, &project, 2) == FM_OK);
    CHECK(fm_engine_event(&first, &note) == FM_OK);
    fm_engine_render(&first, b, 2000);
    double wet_energy = 0;
    for (unsigned i = 0; i < 4000; i++)
    {
        NEAR(b[i], 2 * a[i], 1e-6);
        if (i >= 2000)
            wet_energy += (double)b[i] * b[i];
    }
    CHECK(wet_energy > 1e-4);
    fm_engine_destroy(&first);
    /* Per-region trim zero really removes only this source from the send. */
    fm_region(&project, rid)->sends[0] = 0;
    CHECK(fm_engine_init(&first, &project, 2) == FM_OK);
    CHECK(fm_engine_event(&first, &note) == FM_OK);
    fm_engine_render(&first, b, 2000);
    for (unsigned i = 2000; i < 4000; i++)
        NEAR(b[i], 0, 0);
    fm_engine_destroy(&first);
    fm_region(&project, rid)->sends[0] = 1;
    for (unsigned fx = 0; fx < FM_FX_SLOTS; fx++)
    {
        project.fx[fx].enabled = true;
        track->sends[fx] = 1;
    }
    CHECK(fm_engine_init(&first, &project, 8) == FM_OK);
    CHECK(fm_engine_event(&first, &note) == FM_OK);
    for (unsigned block = 0; block < 10; block++)
        fm_engine_render(&first, b, 2000);
    for (unsigned fx = 0; fx < FM_FX_SLOTS; fx++)
        CHECK(first.fx[fx].energy > 0);
    fm_engine_destroy(&first);
    /* Frozen output is final stereo and is never filtered/gained/sent again. */
    FmPattern *pattern = fm_pattern(&project, track->patterns[0]);
    pattern->frozen_asset = aid;
    pattern->frozen_bpm = 120;
    track->volume = 0;
    track->cutoff = 0;
    project.bpm = 240;
    CHECK(fm_engine_init(&first, &project, 2) == FM_OK);
    CHECK(fm_engine_frozen_event(&first, tid, pattern->id, 13, 99) == FM_OK);
    fm_engine_render(&first, b, 2000);
    for (unsigned i = 0; i < 13; i++)
        NEAR(b[2u * i], 0, 0);
    NEAR(b[2u * 14u], (float)pcm[2] / 32768.0f, 1e-7);
    CHECK(fm_engine_active_voices(&first) == 0);
    for (unsigned fx = 0; fx < FM_FX_SLOTS; fx++)
        NEAR(first.fx[fx].energy, 0, 0);
    fm_engine_destroy(&first);
    fm_project_destroy(&project);
    return 0;
}
static int test_fx_safe_reclaim(void)
{
    FmProject project;
    CHECK(fm_project_init(&project, NULL) == FM_OK);
    FmId tid, rid;
    CHECK(fm_instrument_create(&project, FM_SAMPLE, &tid) == FM_OK);
    FmTrack *track = fm_track(&project, tid);
    CHECK(fm_synthesis_preset(&project, track->instrument, FM_WAVE_SINE, 600, &rid) == FM_OK);
    FmPattern *pattern = fm_pattern(&project, track->patterns[0]);
    FmEvent automation = {0};
    automation.track = tid;
    automation.kind = FM_EVENT_CONTROL;
    automation.parameter = FM_PARAM_SEND1;
    automation.value = 0.5f;
    automation.tick = 960;
    CHECK(fm_pattern_append(&project, pattern, &automation) == FM_OK);
    FmEngine engine;
    CHECK(fm_engine_init(&engine, &project, 2) == FM_OK);
    uint8_t released = 0;
    CHECK(fm_engine_fx_reclaim_unused(&engine, 1, &released) == FM_ERR_BUSY && released == 0);
    CHECK(engine.fx[0].buffer != NULL); /* Future pattern input protects slot. */
    pattern->count = 0;
    automation.frame = 100000;
    CHECK(fm_engine_event(&engine, &automation) == FM_OK);
    CHECK(fm_engine_fx_reclaim_unused(&engine, 1, &released) == FM_ERR_BUSY && released == 0);
    engine.event_count = 0;
    track->sends[0] = 0.5f;
    track->mute = true;
    CHECK(fm_engine_fx_reclaim_unused(&engine, 1, &released) == FM_ERR_BUSY && released == 0);
    fm_region(&project, rid)->sends[0] = 0;
    size_t before = project.budget.used[FM_MEM_FX];
    CHECK(fm_engine_fx_reclaim_unused(&engine, 1, &released) == FM_OK && released == 1);
    CHECK(engine.fx[0].buffer == NULL);
    CHECK(project.budget.used[FM_MEM_FX] == before - fm_fx_memory(FM_FX_DELAY));
    CHECK(fm_engine_fx_prepare(&engine, 0, &project.fx[0]) == FM_OK);
    engine.fx[0].energy = 0.1f;
    CHECK(fm_engine_fx_reclaim_unused(&engine, 1, &released) == FM_ERR_BUSY && released == 0);
    fm_engine_destroy(&engine);
    fm_project_destroy(&project);
    return 0;
}
static int test_additive(void)
{
    FmProject project;
    CHECK(fm_project_init(&project, NULL) == FM_OK);
    FmId tid, rid;
    CHECK(fm_instrument_create(&project, FM_SAMPLE, &tid) == FM_OK);
    FmTrack *track = fm_track(&project, tid);
    uint16_t levels[FM_HARMONICS] = {0};
    for (unsigned harmonic = 0; harmonic < FM_HARMONICS; harmonic++)
    {
        memset(levels, 0, sizeof(levels));
        levels[harmonic] = 256;
        CHECK(fm_synthesis_additive(&project, track->instrument, levels, 1024, &rid) == FM_OK);
        FmInstrument *inst = fm_instrument(&project, track->instrument);
        CHECK(inst->harmonics[harmonic] == 256);
        CHECK(inst->count == 1);
        FmAsset *asset = fm_asset(&project, fm_region(&project, rid)->asset);
        double amplitude = 0;
        for (unsigned i = 0; i < 1024; i++)
            amplitude += (double)asset->pcm[i] *
                         sin(6.283185307179586 * (double)(harmonic + 1u) * (double)i / 1024.0);
        CHECK(amplitude / 512.0 > 29998);
    }
    CHECK(fm_undo(&project) == FM_OK);
    FmInstrument *inst = fm_instrument(&project, track->instrument);
    CHECK(inst->harmonics[FM_HARMONICS - 2u] == 256 && inst->harmonics[FM_HARMONICS - 1u] == 0);
    fm_project_destroy(&project);
    return 0;
}
static int test_sample_hold(void)
{
    FmSampleHold state;
    fm_sample_hold_init(&state, 123);
    float initial = state.held;
    for (unsigned i = 0; i < 500; i++)
        CHECK(fm_sample_hold_tick(&state, 0) == initial);
    unsigned changes = 0, last_change = 0;
    float previous = initial;
    for (unsigned frame = 0; frame < FM_RATE / 2; frame++)
    {
        float value = fm_sample_hold_tick(&state, 10);
        CHECK(isfinite(value) && value >= -1 && value <= 1);
        if (value != previous)
        {
            CHECK(frame - last_change >= 3599 && frame - last_change <= 3601);
            last_change = frame;
            changes++;
        }
        previous = value;
    }
    CHECK(changes == 4);
    float low = 1, high = -1;
    for (unsigned cycle = 0; cycle < 4096; cycle++)
    {
        float value = fm_lfo_sample(cycle + .1, FM_LFO_SAMPLE_HOLD, 123);
        CHECK(value == fm_lfo_sample(cycle + .99, FM_LFO_SAMPLE_HOLD, 123));
        CHECK(value >= -1 && value <= 1);
        low = fminf(low, value);
        high = fmaxf(high, value);
    }
    CHECK(low < -.5 && high > .5);
    /* Exercise actual voices: staggered note-ons restart independent clocks
     * in retrigger mode and share the region's global clock in free-run mode. */
    FmProject project;
    CHECK(fm_project_init(&project, NULL) == FM_OK);
    FmId tid, rid;
    CHECK(fm_instrument_create(&project, FM_SAMPLE, &tid) == FM_OK);
    FmTrack *track = fm_track(&project, tid);
    CHECK(fm_synthesis_preset(&project, track->instrument, FM_WAVE_SINE, 600, &rid) == FM_OK);
    FmRegion *region = fm_region(&project, rid);
    region->lfo_rate = 10;
    region->lfo_depth = .5;
    region->lfo_destination = 2;
    for (unsigned free_run = 0; free_run < 2; free_run++)
    {
        region->lfo_shape = FM_LFO_SAMPLE_HOLD | (free_run ? 0x80u : 0);
        FmEngine engine;
        CHECK(fm_engine_init(&engine, &project, 2) == FM_OK);
        FmEvent event = {.kind = FM_EVENT_NOTE_ON,
                         .track = tid,
                         .identity = 1,
                         .origin = FM_ORIGIN_LOCAL,
                         .note = 60,
                         .velocity = 100,
                         .probability = 1};
        CHECK(fm_engine_event(&engine, &event) == FM_OK);
        event.frame = 1800;
        event.identity = 2;
        CHECK(fm_engine_event(&engine, &event) == FM_OK);
        float output[256];
        double energy = 0;
        for (unsigned at = 0; at < 3700;)
        {
            unsigned n = 3700 - at < 128 ? 3700 - at : 128;
            fm_engine_render(&engine, output, n);
            for (unsigned i = 0; i < n * 2; i++)
            {
                CHECK(isfinite(output[i]));
                energy += fabs(output[i]);
            }
            at += n;
        }
        CHECK(energy > 1 && engine.voices[0].sample_hold.cycle == 1);
        CHECK(engine.voices[1].sample_hold.cycle == (free_run ? 1u : 0u));
        CHECK((engine.voices[0].sample_hold.held == engine.voices[1].sample_hold.held) ==
              !!free_run);
        fm_engine_destroy(&engine);
    }
    fm_project_destroy(&project);
    return 0;
}
int test_audio(void)
{
    CHECK(test_src() == 0);
    CHECK(test_pitch_and_notes() == 0);
    CHECK(test_rate_and_loop() == 0);
    CHECK(test_envelopes_filter_fx() == 0);
    CHECK(test_sample_hold() == 0);
    CHECK(test_additive() == 0);
    CHECK(test_send_routing_and_frozen() == 0);
    CHECK(test_fx_safe_reclaim() == 0);
    return 0;
}
