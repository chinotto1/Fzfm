/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../firmware/workstation/core/fm.h"
#include "test.h"
#include <string.h>
int test_core(void)
{
    FmProject p;
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    size_t audio_before = p.budget.used[FM_MEM_AUDIO];
    FmId ids[12];
    for (unsigned i = 0; i < 12; i++)
        CHECK(fm_instrument_create(&p, FM_SAMPLE, &ids[i]) == FM_OK);
    CHECK(p.track_count == 12 && p.pattern_count == 12);
    CHECK(p.budget.used[FM_MEM_AUDIO] == audio_before);
    FmTrack *track = fm_track(&p, ids[0]);
    FmInstrument *in = fm_instrument(&p, track->instrument);
    CHECK(!strcmp(in->name, "SYNTH 1"));
    CHECK(fm_name_category(&p, in->id, 2) == FM_OK);
    CHECK(!strcmp(in->name, "PAD 1"));
    FmId second = fm_track(&p, ids[1])->instrument;
    CHECK(fm_name_category(&p, second, 2) == FM_OK);
    CHECK(!strcmp(fm_instrument(&p, second)->name, "PAD 2"));
    int16_t pcm[] = {0, 12000, 0, -12000};
    FmId asset, r1, r2;
    CHECK(fm_asset_create(&p, pcm, 4, 1, FM_RATE, false, FM_SOURCE_CAPTURE, &asset) == FM_OK);
    CHECK(fm_region_attach(&p, in->id, asset, 48, -1, &r1) == FM_OK);
    CHECK(fm_map_note(in, 0)->id == r1 && fm_map_note(in, 127)->id == r1);
    CHECK(fm_region_attach(&p, in->id, asset, 60, -1, &r2) == FM_OK);
    CHECK(fm_map_note(in, 54)->id == r1 && fm_map_note(in, 55)->id == r2);
    CHECK(fm_asset_discard(&p, asset) == FM_ERR_BUSY);
    CHECK(fm_edit_begin(&p) == FM_OK);
    CHECK(fm_edit_watch(&p, in, sizeof(*in)) == FM_OK);
    in->regions[1].root = 72;
    fm_map_automatic(in);
    CHECK(fm_edit_commit(&p) == FM_OK);
    CHECK(fm_map_note(in, 60)->id == r1);
    CHECK(fm_undo(&p) == FM_OK);
    CHECK(fm_map_note(in, 60)->id == r2);
    CHECK(fm_redo(&p) == FM_OK);
    CHECK(fm_map_note(in, 60)->id == r1);
    CHECK(fm_region_remove(&p, r2) == FM_OK && !fm_region(&p, r2));
    CHECK(fm_map_note(in, 127)->id == r1);
    CHECK(fm_asset(&p, in->regions[0].asset) != NULL);
    CHECK(fm_undo(&p) == FM_OK && fm_region(&p, r2));
    FmPattern *pat = fm_pattern(&p, track->patterns[0]);
    FmEvent e = {0};
    e.kind = FM_EVENT_NOTE_ON;
    e.probability = 1;
    e.tick = 240;
    CHECK(fm_pattern_append(&p, pat, &e) == FM_OK);
    e.tick = 0;
    CHECK(fm_pattern_append(&p, pat, &e) == FM_OK);
    CHECK(pat->events[0].tick == 0 && pat->events[1].tick == 240);
    /* Every meaningful resource refuses admission without a partial object. */
    unsigned count = p.track_count;
    FmBudget before = p.budget;
    p.budget.limit[FM_MEM_METADATA] = p.budget.used[FM_MEM_METADATA];
    FmId no_track = 0;
    CHECK(fm_instrument_create(&p, FM_MULTI_SAMPLE, &no_track) == FM_ERR_MEMORY);
    CHECK(!no_track && p.track_count == count &&
          p.budget.used[FM_MEM_METADATA] == before.used[FM_MEM_METADATA]);
    p.budget = before;
    fm_project_destroy(&p);
    CHECK(!p.allocations);
    return 0;
}
