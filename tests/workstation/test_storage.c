/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "../../firmware/workstation/import/import.h"
#include "../../firmware/workstation/storage/storage.h"
#include "test.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned j = 0; j < 4; j++)
        p[j] = (uint8_t)(v >> (j * 8));
}
static void repair(uint8_t *p, size_t n)
{
    put32(p + 12, fm_crc32(p + 24, n - 24));
    put32(p + 20, fm_crc32(p, 20));
}
static void remove_slots(const char *prefix)
{
    char path[256];
    for (unsigned i = 0; i < 2; i++)
    {
        snprintf(path, sizeof path, "%s.%u", prefix, i);
        unlink(path);
    }
}
int test_storage(void)
{
    FmProject p, q;
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    CHECK(fm_project_init(&q, NULL) == FM_OK);
    FmId tid, aid, rid;
    CHECK(fm_instrument_create(&p, FM_SAMPLE, &tid) == FM_OK);
    FmTrack *t = fm_track(&p, tid);
    FmInstrument *s = fm_instrument(&p, t->instrument);
    int16_t pcm[] = {-32768, 100, 32767, 200, -2, 4, 7, 8};
    CHECK(fm_asset_create(&p, pcm, 8, 1, FM_RATE, true, FM_SOURCE_ADDITIVE, &aid) == FM_OK);
    CHECK(fm_region_attach(&p, s->id, aid, 60, 7, &rid) == FM_OK);
    s->additive = true;
    for (unsigned i = 0; i < 48; i++)
        s->harmonics[i] = (uint16_t)(256 - i);
    FmRegion *r = fm_region(&p, rid);
    r->sends[2] = 0.125f;
    r->pan = -0.25f;
    r->lfo_shape = 0x80u | FM_LFO_SAMPLE_HOLD;
    r->lfo_rate = 7.5f;
    r->lfo_depth = .75f;
    r->baked_mask = 2;
    r->loop_enabled = true;
    r->loop_count = 2;
    r->loops[0] = (FmLoopSection){0, 4, 1, 3, FM_LOOP_CONTINUE, 0, false};
    r->loops[1] = (FmLoopSection){4, 8, 1, 0, FM_LOOP_JUMP, 0, true};
    FmPattern *pat = fm_pattern(&p, t->patterns[0]);
    for (unsigned i = 0; i < 80; i++)
    {
        FmEvent e = {.tick = i * 10.125,
                     .frame = i * 97,
                     .track = tid,
                     .identity = i + 1,
                     .kind = i % 2 ? FM_EVENT_NOTE_OFF : FM_EVENT_NOTE_ON,
                     .origin = FM_ORIGIN_LOCAL,
                     .note = 60,
                     .velocity = 100,
                     .probability = 0.9f};
        CHECK(fm_pattern_append(&p, pat, &e) == FM_OK);
    }
    pat->swing = 0.125;
    pat->rate = 0.75;
    pat->quantize = 0;
    p.bpm = 123.456;
    p.tempo_established = true;
    p.song = fm_alloc(&p, FM_MEM_METADATA, sizeof(FmSongEntry));
    CHECK(p.song);
    p.song_count = 1;
    p.song[0].count = 1;
    p.song[0].tracks[0] = tid;
    p.song[0].patterns[0] = 0;
    p.song[0].repeats = 3;
    p.song[0].bars = 2.5;
    CHECK(fm_project_validate(&p) == FM_OK);
    uint8_t *b = NULL, *round = NULL;
    size_t n = 0, nround = 0;
    CHECK(fm_project_encode(&p, &b, &n) == FM_OK);
    CHECK(!memcmp(b, "FM1P\4\0\30\0", 8));
    CHECK(fm_project_decode(&q, b, n) == FM_OK);
    CHECK(fm_project_encode(&q, &round, &nround) == FM_OK);
    CHECK(nround == n && !memcmp(round, b, n));
    CHECK(fm_region(&q, rid)->sends[2] == 0.125f);
    CHECK(fm_region(&q, rid)->lfo_shape == (0x80u | FM_LFO_SAMPLE_HOLD));
    CHECK(fm_region(&q, rid)->lfo_rate == 7.5f && fm_region(&q, rid)->lfo_depth == .75f);
    r->lfo_shape = FM_LFO_SAMPLE_HOLD + 1;
    CHECK(fm_project_validate(&p) == FM_ERR_FORMAT);
    r->lfo_shape = 0x80u | FM_LFO_SAMPLE_HOLD;
    CHECK(fm_instrument(&q, s->id)->harmonics[47] == 209);
    CHECK(!memcmp(fm_asset(&q, aid)->pcm, pcm, sizeof pcm));
    free(round);
    /* v3 used the same payload layout, but had no explicit synth type. */
    b[4] = 3;
    repair(b, n);
    CHECK(fm_project_decode(&q, b, n) == FM_OK);
    CHECK(fm_instrument(&q, s->id)->type == FM_SYNTH);
    CHECK(!memcmp(fm_asset(&q, aid)->pcm, pcm, sizeof pcm));
    fm_asset(&p, aid)->cycle = false;
    free(b);
    CHECK(fm_project_encode(&p, &b, &n) == FM_OK);
    b[4] = 3;
    repair(b, n);
    CHECK(fm_project_decode(&q, b, n) == FM_OK);
    CHECK(fm_instrument(&q, s->id)->type == FM_SAMPLE);
    fm_asset(&p, aid)->cycle = true;
    s->type = FM_SYNTH;
    free(b);
    CHECK(fm_project_encode(&p, &b, &n) == FM_OK);
    CHECK(fm_project_decode(&q, b, n) == FM_OK);
    CHECK(fm_instrument(&q, s->id)->type == FM_SYNTH);
    s->type = FM_SAMPLE;
    free(b);
    CHECK(fm_project_encode(&p, &b, &n) == FM_OK);
    CHECK(fm_project_decode(&q, b, n) == FM_OK);
    FmId retained = q.selected_track;
    for (size_t length = 0; length < n; length += 17)
    {
        CHECK(fm_project_decode(&q, b, length) != FM_OK);
        CHECK(q.selected_track == retained);
    }
    uint8_t *bad = malloc(n);
    CHECK(bad);
    /* Malicious scalar/count/reference mutations with valid outer CRCs exercise
     * validation beyond ordinary checksum rejection. */
    uint32_t random = 1234567;
    for (unsigned i = 0; i < 600; i++)
    {
        memcpy(bad, b, n);
        random = random * 1664525u + 1013904223u;
        size_t at = 24 + random % (n - 24);
        bad[at] ^= (uint8_t)(1u << (random % 8));
        repair(bad, n);
        FmResult result = fm_project_decode(&q, bad, n);
        if (result == FM_OK)
            CHECK(fm_project_decode(&q, b, n) == FM_OK);
        else
            CHECK(q.selected_track == retained);
    }
    memcpy(bad, b, n);
    bad[4] = 99;
    put32(bad + 20, fm_crc32(bad, 20));
    CHECK(fm_project_decode(&q, bad, n) == FM_ERR_FORMAT);
    memcpy(bad, b, n);
    memset(bad + 24, 0, 4);
    repair(bad, n);
    CHECK(fm_project_decode(&q, bad, n) == FM_ERR_FORMAT);
    free(bad);
    free(b);
    char dir[] = "/tmp/fm-storage-XXXXXX";
    CHECK(mkdtemp(dir));
    char prefix[256];
    snprintf(prefix, sizeof prefix, "%s/project", dir);
    strcpy(s->name, "OLD");
    CHECK(fm_store_save(&p, prefix) == FM_OK);
    size_t steps = 0;
    strcpy(s->name, "NEW");
    CHECK(fm_store_save_fault(&p, prefix, SIZE_MAX, &steps) == FM_OK);
    CHECK(steps > 20);
    remove_slots(prefix);
    for (size_t fail = 0; fail < steps; fail++)
    {
        strcpy(s->name, "OLD");
        CHECK(fm_store_save(&p, prefix) == FM_OK);
        strcpy(s->name, "NEW");
        p.dirty = true;
        FmResult result = fm_store_save_fault(&p, prefix, fail, NULL);
        CHECK(result == FM_ERR_STORAGE);
        CHECK(fm_store_load(&q, prefix) == FM_OK);
        const char *name = fm_instrument(&q, t->instrument)->name;
        CHECK(!strcmp(name, "OLD") || !strcmp(name, "NEW"));
        CHECK(fm_project_validate(&q) == FM_OK);
        CHECK(!memcmp(fm_asset(&q, aid)->pcm, pcm, sizeof pcm));
        remove_slots(prefix);
    }
    CHECK(rmdir(dir) == 0);
    uint8_t wav[60] = {0};
    memcpy(wav, "RIFF", 4);
    put32(wav + 4, sizeof(wav) - 8);
    memcpy(wav + 8, "WAVEfmt ", 8);
    put32(wav + 16, 16);
    wav[20] = 1;
    wav[22] = 1;
    put32(wav + 24, FM_RATE);
    put32(wav + 28, FM_RATE * 2);
    wav[32] = 2;
    wav[34] = 16;
    memcpy(wav + 36, "data", 4);
    put32(wav + 40, 16);
    for (unsigned i = 0; i < 8; i++)
    {
        wav[44 + i * 2] = (uint8_t)pcm[i];
        wav[45 + i * 2] = (uint8_t)((uint16_t)pcm[i] >> 8);
    }
    FmId imported = 0;
    CHECK(fm_import_wav_asset(&q, wav, sizeof(wav), true, &imported) == FM_OK);
    CHECK(fm_asset(&q, imported)->cycle);
    CHECK(!memcmp(fm_asset(&q, imported)->pcm, pcm, sizeof pcm));
    unsigned assets = q.asset_count;
    put32(wav + 40, UINT32_MAX);
    CHECK(fm_import_wav_asset(&q, wav, sizeof(wav), false, &imported) == FM_ERR_FORMAT);
    CHECK(q.asset_count == assets);
    CHECK(fm_project_encode(&p, &b, &n) == FM_OK);
    FmImportTransfer transfer = {0};
    CHECK(fm_import_begin(&transfer, &q, n, fm_crc32(b, n)) == FM_OK);
    CHECK(fm_import_finish(&transfer, true) == FM_ERR_FORMAT);
    uint32_t sequence = 0;
    for (size_t at = 0; at < n;)
    {
        size_t amount = n - at;
        if (amount > FM_IMPORT_CHUNK)
            amount = FM_IMPORT_CHUNK;
        CHECK(fm_import_chunk(&transfer, sequence, b + at, amount, fm_crc32(b + at, amount)) ==
              FM_OK);
        CHECK(fm_import_chunk(&transfer, sequence, b + at, amount, fm_crc32(b + at, amount)) ==
              FM_OK);
        at += amount;
        sequence++;
    }
    CHECK(fm_import_finish(&transfer, false) == FM_ERR_CANCELLED);
    CHECK(q.asset_count == assets);
    CHECK(fm_import_finish(&transfer, true) == FM_OK);
    CHECK(q.asset_count == p.asset_count);
    CHECK(!transfer.bytes);
    free(b);
    fm_project_destroy(&p);
    fm_project_destroy(&q);
    return 0;
}
