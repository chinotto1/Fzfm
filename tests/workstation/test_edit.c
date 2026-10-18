/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../firmware/workstation/sampler/edit.h"
#include "test.h"
#include <string.h>
static int test_baking(void);
int test_edit(void)
{
    FmProject p;
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    FmId t1, t2, asset, r1, r2, r3;
    CHECK(fm_instrument_create(&p, FM_MULTI_SAMPLE, &t1) == FM_OK);
    CHECK(fm_instrument_create(&p, FM_SAMPLE, &t2) == FM_OK);
    FmInstrument *a = fm_instrument(&p, fm_track(&p, t1)->instrument),
                 *b = fm_instrument(&p, fm_track(&p, t2)->instrument);
    int16_t data[] = {-100, 200, 400, -800, 1600, 3200, 6400, -12800};
    CHECK(fm_asset_create(&p, data, 8, 1, FM_RATE, false, FM_SOURCE_CAPTURE, &asset) == FM_OK);
    CHECK(fm_region_attach(&p, a->id, asset, 36, 0, &r1) == FM_OK);
    CHECK(fm_region_attach(&p, a->id, asset, 37, 4, &r2) == FM_OK);
    CHECK(fm_region_attach(&p, b->id, asset, 60, 0, &r3) == FM_OK);
    FmEditScope one = {a->id, r1, false};
    FmSampleEdit reverse = {.operation = FM_EDIT_REVERSE_WRITE};
    CHECK(fm_sample_edit(&p, one, &reverse) == FM_OK);
    FmId reversed = fm_region(&p, r1)->asset;
    CHECK(reversed != asset);
    CHECK(fm_asset(&p, reversed)->pcm[0] == data[7]);
    CHECK(fm_region(&p, r2)->asset == asset);
    CHECK(fm_region(&p, r3)->asset == asset);
    CHECK(!memcmp(fm_asset(&p, asset)->pcm, data, sizeof(data)));
    CHECK(fm_undo(&p) == FM_OK);
    CHECK(fm_region(&p, r1)->asset == asset);
    CHECK(fm_redo(&p) == FM_OK);
    CHECK(fm_region(&p, r1)->asset == reversed);
    FmEditScope all = {a->id, 0, true};
    FmSampleEdit normalize = {.operation = FM_EDIT_NORMALIZE, .level = 0.5f};
    CHECK(fm_sample_edit(&p, all, &normalize) == FM_OK);
    CHECK(fm_region(&p, r1)->asset != reversed);
    CHECK(fm_region(&p, r2)->asset != asset);
    CHECK(fm_region(&p, r3)->asset == asset);
    CHECK(fm_undo(&p) == FM_OK);
    CHECK(fm_region(&p, r1)->asset == reversed);
    CHECK(fm_region(&p, r2)->asset == asset);
    FmSampleEdit crop = {.operation = FM_EDIT_TRUNCATE, .from = 0.25, .to = 0.75};
    CHECK(fm_sample_edit(&p, all, &crop) == FM_OK);
    CHECK(fm_asset(&p, fm_region(&p, r1)->asset)->frames == 4);
    CHECK(fm_asset(&p, fm_region(&p, r2)->asset)->frames == 4);
    CHECK(fm_undo(&p) == FM_OK);
    size_t bytes = p.budget.used[FM_MEM_AUDIO];
    p.budget.limit[FM_MEM_AUDIO] = bytes + 2;
    FmId before1 = fm_region(&p, r1)->asset, before2 = fm_region(&p, r2)->asset;
    CHECK(fm_sample_edit(&p, all, &reverse) == FM_ERR_STORAGE);
    CHECK(fm_region(&p, r1)->asset == before1 && fm_region(&p, r2)->asset == before2);
    CHECK(p.budget.used[FM_MEM_AUDIO] == bytes);
    p.budget.limit[FM_MEM_AUDIO] = 16u * 1024u * 1024u;
    FmMixWrite mix = {.first = r2, .second = r3, .first_gain = 0.5f, .second_gain = 0.5f};
    CHECK(fm_mix_write(&p, r1, &mix) == FM_OK);
    FmAsset *mixed = fm_asset(&p, fm_region(&p, r1)->asset);
    CHECK(mixed->rate == FM_RATE && mixed->frames == 8);
    CHECK(!memcmp(mixed->pcm, data, sizeof(data)));
    mix.crossmix = true;
    mix.overlap_frames = 4;
    CHECK(fm_mix_write(&p, r1, &mix) == FM_OK);
    CHECK(fm_asset(&p, fm_region(&p, r1)->asset)->frames == 12);
    CHECK(fm_undo(&p) == FM_OK);
    CHECK(fm_asset(&p, fm_region(&p, r1)->asset)->frames == 8);
    FmSampleEdit draw = {.operation = FM_EDIT_DRAW_POINT, .point = 3, .level = 0.25f};
    CHECK(fm_sample_edit(&p, one, &draw) == FM_OK);
    CHECK(fm_asset(&p, fm_region(&p, r1)->asset)->pcm[3] == 8192);
    CHECK(fm_asset(&p, asset)->pcm[3] == data[3]);
    FmSampleEdit cycle = {.operation = FM_EDIT_EXTRACT_CYCLE, .from = 0.25, .to = 0.75};
    CHECK(fm_sample_edit(&p, one, &cycle) == FM_OK);
    CHECK(fm_asset(&p, fm_region(&p, r1)->asset)->cycle);
    CHECK(fm_region_markers(&p, r2, 2, 7) == FM_OK);
    CHECK(fm_undo(&p) == FM_OK);
    CHECK(fm_region(&p, r2)->start == 0 && fm_region(&p, r2)->end == 8);
    fm_project_destroy(&p);
    return test_baking();
}

/* R25/T35-T39: actual engine output, scoped unsend, atomic failure/undo. */
#include "../../firmware/workstation/audio/fm_audio.h"
#include "../../firmware/workstation/storage/bake.h"
static int test_baking(void)
{
    FmProject p;
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    FmId tid, aid, rid, other;
    CHECK(fm_instrument_create(&p, FM_MULTI_SAMPLE, &tid) == FM_OK);
    FmTrack *t = fm_track(&p, tid);
    FmInstrument *s = fm_instrument(&p, t->instrument);
    int16_t data[256];
    for (unsigned i = 0; i < 256; i++)
        data[i] = (int16_t)(12000 * sin(i * 0.19));
    CHECK(fm_asset_create(&p, data, 256, 1, FM_RATE, false, FM_SOURCE_CAPTURE, &aid) == FM_OK);
    CHECK(fm_region_attach(&p, s->id, aid, 36, 0, &rid) == FM_OK);
    CHECK(fm_region_attach(&p, s->id, aid, 37, 1, &other) == FM_OK);
    t->sends[3] = 0.4f;
    t->pan = 0.2f;
    t->volume = 0.7f;
    FmRegion *r = fm_region(&p, rid);
    r->gain = 0.8f;
    r->pan = -0.1f;
    float expected[512 * 2], actual[512 * 2];
    FmEngine engine;
    CHECK(fm_engine_init(&engine, &p, 8) == FM_OK);
    FmEvent event = {.track = tid,
                     .kind = FM_EVENT_NOTE_ON,
                     .note = 36,
                     .velocity = 127,
                     .identity = 1,
                     .probability = 1};
    CHECK(fm_engine_event(&engine, &event) == FM_OK);
    fm_engine_render(&engine, expected, 512);
    fm_engine_destroy(&engine);
    FmBakeOptions options = {
        .sends = 0, .tail_frames = 256, .warmup_loops = 2, .isolated_confirmed = true};
    FmEditScope scope = {s->id, rid, false};
    CHECK(fm_bake_samples(&p, scope, &options) == FM_ERR_UNSUPPORTED);
    CHECK(r->asset == aid && t->sends[3] == 0.4f);
    options.sends = 8;
    options.cancelled = true;
    CHECK(fm_bake_samples(&p, scope, &options) == FM_ERR_CANCELLED);
    CHECK(r->asset == aid);
    options.cancelled = false;
    CHECK(fm_bake_samples(&p, scope, &options) == FM_OK);
    CHECK(r->asset != aid && (r->baked_mask & FM_BAKED_FINAL_PATH));
    CHECK(r->sends[3] == 0);
    CHECK(fm_region(&p, other)->sends[3] == 1);
    CHECK(t->sends[3] == 0.4f);
    CHECK(fm_engine_init(&engine, &p, 8) == FM_OK);
    CHECK(fm_engine_event(&engine, &event) == FM_OK);
    fm_engine_render(&engine, actual, 512);
    fm_engine_destroy(&engine);
    double energy = 0;
    for (unsigned i = 0; i < 1024; i++)
    {
        NEAR(actual[i], expected[i], 1.1 / 32767);
        energy += actual[i] * actual[i];
    }
    CHECK(energy > 0.01);
    CHECK(fm_undo(&p) == FM_OK);
    CHECK(r->asset == aid && r->sends[3] == 1);
    FmPattern *pat = fm_pattern(&p, t->patterns[0]);
    pat->length_ticks = FM_PPQN;
    CHECK(fm_pattern_append(&p, pat, &event) == FM_OK);
    FmEvent off = event;
    off.tick = FM_PPQN / 4;
    off.kind = FM_EVENT_NOTE_OFF;
    CHECK(fm_pattern_append(&p, pat, &off) == FM_OK);
    CHECK(fm_bake_pattern(&p, pat->id, &options) == FM_OK);
    CHECK(pat->frozen_asset && pat->count == 2 && pat->frozen_sends == 8 && t->sends[3] == 0.4f);
    CHECK(fm_engine_init(&engine, &p, 8) == FM_OK);
    CHECK(fm_engine_frozen_event(&engine, tid, pat->id, 0, 100) == FM_OK);
    fm_engine_render(&engine, actual, 512);
    fm_engine_destroy(&engine);
    FmAsset *frozen = fm_asset(&p, pat->frozen_asset);
    for (unsigned i = 0; i < 1024; i++)
        NEAR(actual[i], frozen->pcm[i] / 32768.0, 0.0001);
    CHECK(fm_unfreeze_pattern(&p, pat->id) == FM_OK);
    CHECK(!pat->frozen_asset && pat->count == 2);
    CHECK(fm_undo(&p) == FM_OK);
    CHECK(pat->frozen_asset && t->sends[3] == 0.4f);
    fm_project_destroy(&p);
    return 0;
}
