/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../firmware/workstation/audio/fm_audio.h"
#include "../../firmware/workstation/audio/fm_src.h"
#include "../../firmware/workstation/platform/fm_usb.h"
#include "../../firmware/workstation/sampler/fm_capture.h"
#include "../../firmware/workstation/storage/storage.h"
#include "test.h"
#include <stdlib.h>
#include <string.h>
int test_system(void)
{
    FmProject p;
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    FmId tid;
    CHECK(fm_instrument_create(&p, FM_MULTI_SAMPLE, &tid) == FM_OK);
    FmCaptureConfig cfg = fm_capture_defaults();
    cfg.pretrigger_frames = 36;
    cfg.silence_frames = 360;
    cfg.minimum_frames = 100;
    cfg.maximum_frames = 3600;
    FmCapture capture;
    CHECK(fm_capture_begin(&capture, &p, tid, 36, &cfg) == FM_OK);
    FmUsbRx rx;
    int16_t ring[256 * 2];
    CHECK(fm_usb_rx_init(&rx, ring, 256));
    CHECK(fm_usb_rx_stream(&rx, true, false, 1, 44100, 16, 2));
    FmSrc src;
    CHECK(fm_src_init(&src, 44100, 36000, 2) == FM_OK);
    uint32_t input_frame = 0, output_frame = 0;
    for (unsigned packet = 0; packet < 300; packet++)
    {
        unsigned frames = 44 + (packet % 10 == 9);
        uint8_t bytes[49 * 4];
        for (unsigned j = 0; j < frames; j++, input_frame++)
        {
            unsigned phase = input_frame % 4410;
            int16_t sample =
                phase >= 441 && phase < 1764
                    ? (int16_t)(12000 * sin(6.283185307179586 * 880.0 * input_frame / 44100.0))
                    : 0;
            uint16_t value = (uint16_t)sample;
            bytes[j * 4] = (uint8_t)value;
            bytes[j * 4 + 1] = (uint8_t)(value >> 8);
            bytes[j * 4 + 2] = (uint8_t)value;
            bytes[j * 4 + 3] = (uint8_t)(value >> 8);
        }
        CHECK(fm_usb_rx_packet(&rx, bytes, frames * 4));
        int16_t pcm[49 * 2];
        CHECK(fm_usb_rx_read(&rx, pcm, frames) == frames);
        float in[49 * 2], out[64 * 2];
        for (unsigned j = 0; j < frames * 2; j++)
            in[j] = (float)pcm[j] / 32768.0f;
        size_t consumed = 0;
        size_t produced = fm_src_process(&src, in, frames, &consumed, out, 64);
        CHECK(consumed == frames);
        fm_capture_accept_stereo(&capture, out, produced, output_frame);
        output_frame += (uint32_t)produced;
        CHECK(fm_capture_service(&capture) == FM_OK);
    }
    CHECK(fm_capture_finish(&capture) == FM_OK);
    FmInstrument *in = fm_instrument(&p, fm_track(&p, tid)->instrument);
    CHECK(in->count == 3);
    CHECK(output_frame > 10780 && output_frame <= 10800);
    CHECK(rx.overruns == 0 && rx.bad_packets == 0);
    /* Complete actual project roundtrip, then play all three captured regions. */
    uint8_t *bundle = NULL;
    size_t n = 0;
    CHECK(fm_project_encode(&p, &bundle, &n) == FM_OK);
    FmProject reloaded;
    CHECK(fm_project_init(&reloaded, NULL) == FM_OK);
    CHECK(fm_project_decode(&reloaded, bundle, n) == FM_OK);
    free(bundle);
    FmEngine engine;
    CHECK(fm_engine_init(&engine, &reloaded, 8) == FM_OK);
    float stereo[512 * 2];
    double energy = 0;
    for (unsigned i = 0; i < 3; i++)
    {
        FmEvent e = {0};
        e.track = tid;
        e.kind = FM_EVENT_NOTE_ON;
        e.note = (uint8_t)(36 + i);
        e.velocity = 127;
        e.frame = engine.frame;
        e.identity = i + 1;
        e.probability = 1;
        CHECK(fm_engine_event(&engine, &e) == FM_OK);
        fm_engine_render(&engine, stereo, 512);
        for (unsigned j = 0; j < 512 * 2; j++)
        {
            CHECK(isfinite(stereo[j]));
            energy += stereo[j] * stereo[j];
        }
    }
    CHECK(energy > 0.1);
    fm_engine_destroy(&engine);
    fm_project_destroy(&reloaded);
    fm_capture_destroy(&capture);
    fm_project_destroy(&p);
    return 0;
}
