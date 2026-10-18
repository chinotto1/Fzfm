/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../firmware/workstation/sampler/fm_capture.h"
#include "test.h"
#include <string.h>
static FmCaptureConfig fixture_config(void)
{
    FmCaptureConfig c = fm_capture_defaults();
    c.pretrigger_frames = 4;
    c.silence_frames = 6;
    c.minimum_frames = 4;
    c.maximum_frames = 64;
    return c;
}
static void hit(FmCapture *c, FmFrame *clock)
{
    int16_t wave[] = {0, 0, 0, 0, 22000, 14000, -16000, 6000, 0, 0, 0, 0, 0, 0};
    fm_capture_accept(c, wave, sizeof(wave) / sizeof(wave[0]), *clock);
    *clock += sizeof(wave) / sizeof(wave[0]);
}
int test_capture(void)
{
    FmProject p;
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    FmId tid;
    CHECK(fm_instrument_create(&p, FM_MULTI_SAMPLE, &tid) == FM_OK);
    FmInstrument *in = fm_instrument(&p, fm_track(&p, tid)->instrument);
    FmCapture c;
    FmCaptureConfig cfg = fixture_config();
    CHECK(fm_capture_begin(&c, &p, tid, 36, &cfg) == FM_OK);
    FmFrame time = 0;
    for (unsigned i = 0; i < 16; i++)
    {
        hit(&c, &time);
        CHECK(c.state == FM_CAPTURE_FINALIZE);
        CHECK(!c.listening);
        CHECK(fm_capture_service(&c) == FM_OK);
        CHECK(in->regions[i].id);
        FmAsset *a = fm_asset(&p, in->regions[i].asset);
        CHECK(a->rate == 36000 && a->channels == 1 && a->pcm[4] == 22000);
        CHECK(in->regions[i].low == 36 + i && in->regions[i].high == 36 + i);
    }
    CHECK(c.state == FM_CAPTURE_FINISHED && in->count == 16 && p.track_count == 1);
    fm_capture_destroy(&c);
    fm_project_destroy(&p);
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    CHECK(fm_instrument_create(&p, FM_MULTI_SAMPLE, &tid) == FM_OK);
    in = fm_instrument(&p, fm_track(&p, tid)->instrument);
    CHECK(fm_capture_begin(&c, &p, tid, 36, &cfg) == FM_OK);
    time = 0;
    for (unsigned i = 0; i < 3; i++)
    {
        hit(&c, &time);
        CHECK(fm_capture_service(&c) == FM_OK);
    }
    CHECK(fm_capture_finish(&c) == FM_OK && in->count == 3);
    CHECK(fm_map_note(in, 38) && !fm_map_note(in, 39));
    CHECK(fm_capture_resume(&c, 36) == FM_OK);
    CHECK(fm_capture_retry(&c) == FM_OK && in->count == 2 && c.slot == 2);
    hit(&c, &time);
    CHECK(fm_capture_service(&c) == FM_OK && in->count == 3);
    CHECK(fm_undo(&p) == FM_OK && in->count == 2);
    CHECK(fm_redo(&p) == FM_OK && in->count == 3);
    CHECK(fm_capture_resume(&c, 36) == FM_OK);
    int16_t long_note[12];
    for (unsigned i = 0; i < 12; i++)
        long_note[i] = 9000;
    fm_capture_accept(&c, long_note, 12, time);
    CHECK(c.state == FM_CAPTURE_CAPTURING);
    CHECK(fm_capture_finish(&c) == FM_OK && in->count == 4 && p.track_count == 1);
    CHECK(fm_asset(&p, in->regions[3].asset)->frames == 12);
    CHECK(fm_capture_resume(&c, 36) == FM_OK);
    fm_capture_input_lost(&c);
    CHECK(c.state == FM_CAPTURE_INPUT_LOST);
    CHECK(fm_capture_finish(&c) == FM_OK && in->count == 4);
    CHECK(fm_capture_resume(&c, 36) == FM_OK);
    p.budget.limit[FM_MEM_AUDIO] = p.budget.used[FM_MEM_AUDIO];
    hit(&c, &time);
    CHECK(fm_capture_service(&c) == FM_ERR_STORAGE && in->count == 4);
    CHECK(c.state == FM_CAPTURE_STORAGE_FULL);
    fm_capture_destroy(&c);
    fm_project_destroy(&p);
    /* Pitched sources stop after one anchor. Mono sum is scaled, antiphase cancels. */
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    CHECK(fm_instrument_create(&p, FM_SAMPLE, &tid) == FM_OK);
    cfg = fixture_config();
    cfg.manual = true;
    CHECK(fm_capture_begin(&c, &p, tid, 60, &cfg) == FM_OK);
    CHECK(fm_capture_manual_start(&c) == FM_OK);
    float stereo[16];
    for (unsigned i = 0; i < 8; i++)
    {
        stereo[i * 2] = 0.8f;
        stereo[i * 2 + 1] = -0.8f;
    }
    fm_capture_accept_stereo(&c, stereo, 8, 0);
    CHECK(c.frames == 8);
    CHECK(fm_capture_finish(&c) == FM_OK);
    in = fm_instrument(&p, fm_track(&p, tid)->instrument);
    FmAsset *asset = fm_asset(&p, in->regions[0].asset);
    CHECK(asset && asset->frames == 8 && asset->pcm[0] == 0);
    fm_capture_destroy(&c);
    fm_project_destroy(&p);
    return 0;
}
