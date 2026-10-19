/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../firmware/workstation/sequence/fm_sequence.h"
#include "../../firmware/workstation/storage/storage.h"
#include "test.h"
#include <stdio.h>
#include <stdlib.h>
typedef struct
{
    unsigned on, off;
    uint8_t notes[128];
    FmFrame frames[128], offs[128];
} ArpLog;
static void played(void *user, const FmEvent *e, uint32_t offset)
{
    ArpLog *l = user;
    (void)offset;
    if (e->kind == FM_EVENT_NOTE_ON && e->velocity)
    {
        if (l->on < 128)
        {
            l->notes[l->on] = e->note;
            l->frames[l->on] = e->frame;
        }
        l->on++;
    }
    else if (e->kind == FM_EVENT_NOTE_OFF)
    {
        if (l->off < 128)
            l->offs[l->off] = e->frame;
        l->off++;
    }
}
static FmResult input(FmSequencer *s, FmId id, unsigned note, bool on)
{
    FmEvent e = {.kind = on ? FM_EVENT_NOTE_ON : FM_EVENT_NOTE_OFF,
                 .origin = FM_ORIGIN_USB,
                 .note = (uint8_t)note,
                 .velocity = on ? 100 : 0,
                 .identity = note + 1,
                 .track = id,
                 .frame = fm_sequence_frame(s),
                 .probability = 1};
    bool consumed = false;
    FmResult r = fm_sequence_arp_input(s, &e, &consumed);
    return r == FM_OK && !consumed ? FM_ERR_ARGUMENT : r;
}
int test_arp(void)
{
    FmProject p;
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    FmId id;
    CHECK(fm_instrument_create(&p, FM_SAMPLE, &id) == FM_OK);
    FmTrack *t = fm_track(&p, id);
    CHECK(t->arp.mode == FM_ARP_OFF && t->arp.octaves == 1);
    FmSequencer *s = fm_sequence_create(&p, 1024, 1024);
    CHECK(s);
    CHECK(fm_arp_set(&p, id, 0, FM_ARP_UP) == FM_OK);
    CHECK(p.tempo_established);
    CHECK(fm_arp_set(&p, id, 2, 2) == FM_OK);
    CHECK(input(s, id, 67, true) == FM_OK && input(s, id, 60, true) == FM_OK &&
          input(s, id, 64, true) == FM_OK);
    ArpLog l = {0};
    fm_sequence_advance(s, 30001, played, &l);
    uint8_t expected[] = {60, 64, 67, 72, 76, 79, 60};
    CHECK(l.on == 7 && l.off == 7 && !fm_sequence_playing(s));
    for (unsigned i = 0; i < 7; i++)
    {
        CHECK(l.notes[i] == expected[i]);
        CHECK(l.frames[i] == i * 4500 && l.offs[i] == i * 4500 + 2250);
    }
    CHECK(fm_sequence_status(s).retained_events == 14);
    fm_sequence_stop(s, played, &l);
    fm_sequence_destroy(s);
    /* Swing works while transport is stopped; render never allocates. */
    s = fm_sequence_create(&p, 1024, 1024);
    CHECK(s);
    CHECK(fm_arp_set(&p, id, 2, 1) == FM_OK);
    CHECK(fm_arp_set(&p, id, 4, .25) == FM_OK);
    CHECK(input(s, id, 60, true) == FM_OK);
    size_t before = p.budget.used[FM_MEM_EVENTS];
    l = (ArpLog){0};
    fm_sequence_advance(s, 18001, played, &l);
    CHECK(l.on == 5 && l.frames[0] == 0 && l.frames[1] == 6750 && l.frames[2] == 9000 &&
          l.frames[3] == 15750 && l.frames[4] == 18000);
    CHECK(p.budget.used[FM_MEM_EVENTS] == before);
    CHECK(input(s, id, 60, false) == FM_OK);
    fm_sequence_advance(s, 1, played, &l);
    CHECK(l.off == l.on);
    fm_sequence_destroy(s);
    /* Latch keeps released notes, replaces the chord, and Stop releases it. */
    s = fm_sequence_create(&p, 1024, 1024);
    CHECK(s);
    CHECK(fm_arp_set(&p, id, 4, 0) == FM_OK);
    CHECK(fm_arp_set(&p, id, 6, 1) == FM_OK);
    CHECK(input(s, id, 60, true) == FM_OK && input(s, id, 60, false) == FM_OK);
    l = (ArpLog){0};
    fm_sequence_advance(s, 9001, played, &l);
    CHECK(l.on == 3);
    CHECK(input(s, id, 72, true) == FM_OK && input(s, id, 72, false) == FM_OK);
    fm_sequence_advance(s, 1, played, &l);
    CHECK(l.on == 4 && l.notes[3] == 72);
    fm_sequence_stop(s, played, &l);
    CHECK(l.off == l.on);
    unsigned old = l.on;
    fm_sequence_advance(s, 20000, played, &l);
    CHECK(l.on == old);
    /* Every mode stays in the legal pitch range and balances its gates. */
    for (unsigned mode = 1; mode < FM_ARP_MODE_COUNT; mode++)
    {
        CHECK(fm_arp_set(&p, id, 0, mode) == FM_OK);
        CHECK(fm_arp_set(&p, id, 6, 0) == FM_OK);
        CHECK(input(s, id, 60, true) == FM_OK && input(s, id, 64, true) == FM_OK &&
              input(s, id, 67, true) == FM_OK);
        l = (ArpLog){0};
        fm_sequence_advance(s, 24000, played, &l);
        CHECK(l.on > 0);
        for (unsigned i = 0; i < l.on && i < 128; i++)
            CHECK(l.notes[i] >= 60 && l.notes[i] <= 79);
        fm_sequence_stop(s, played, &l);
        CHECK(l.on == l.off);
    }
    CHECK(fm_arp_set(&p, id, 5, 0) == FM_OK);
    CHECK(input(s, id, 60, true) == FM_OK);
    l = (ArpLog){0};
    fm_sequence_advance(s, 18000, played, &l);
    CHECK(!l.on);
    fm_sequence_stop(s, played, &l);
    CHECK(fm_arp_set(&p, id, 3, 0) == FM_ERR_RANGE && fm_arp_set(&p, id, 1, 99) == FM_ERR_RANGE);
    /* Sustain retains source keys while generated gates remain independent. */
    CHECK(fm_arp_set(&p, id, 0, FM_ARP_UP) == FM_OK && fm_arp_set(&p, id, 5, 1) == FM_OK);
    FmEvent pedal = {.kind = FM_EVENT_SUSTAIN, .origin = FM_ORIGIN_USB, .track = id, .value = 1};
    bool consumed = false;
    CHECK(fm_sequence_arp_input(s, &pedal, &consumed) == FM_OK && consumed);
    CHECK(input(s, id, 60, true) == FM_OK && input(s, id, 60, false) == FM_OK);
    l = (ArpLog){0};
    fm_sequence_advance(s, 9001, played, &l);
    CHECK(l.on >= 3 && l.off > 0);
    pedal.value = 0;
    CHECK(fm_sequence_arp_input(s, &pedal, &consumed) == FM_OK && consumed);
    fm_sequence_advance(s, 1, played, &l);
    CHECK(l.on == l.off);
    fm_sequence_stop(s, played, &l);
    /* Settings roundtrip, and real pre-ARP v2 projects migrate with arp Off. */
    CHECK(fm_arp_set(&p, id, 5, .73) == FM_OK);
    CHECK(fm_arp_set(&p, id, 0, FM_ARP_WALK) == FM_OK);
    uint8_t *bytes;
    size_t n;
    CHECK(fm_project_encode(&p, &bytes, &n) == FM_OK);
    FmProject q;
    CHECK(fm_project_init(&q, NULL) == FM_OK);
    CHECK(fm_project_decode(&q, bytes, n) == FM_OK);
    CHECK(fm_track(&q, id)->arp.mode == FM_ARP_WALK);
    NEAR(fm_track(&q, id)->arp.probability, .73, .0001);
    free(bytes);
    fm_project_destroy(&q);
    FILE *f = fopen("tests/fixtures/project-v2-before-arp.fzb", "rb");
    CHECK(f);
    uint8_t legacy[1024];
    size_t size = fread(legacy, 1, sizeof(legacy), f);
    fclose(f);
    CHECK(size == 635 && legacy[4] == 2);
    CHECK(fm_project_init(&q, NULL) == FM_OK);
    CHECK(fm_project_decode(&q, legacy, size) == FM_OK);
    FmTrack *migrated = fm_track(&q, q.selected_track);
    CHECK(migrated && migrated->arp.mode == 0 && migrated->arp.octaves == 1 &&
          migrated->arp.probability == 1);
    fm_project_destroy(&q);
    fm_sequence_destroy(s);
    fm_project_destroy(&p);
    return 0;
}
