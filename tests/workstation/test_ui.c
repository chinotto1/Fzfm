/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../firmware/workstation/dsp/fm_synthesis.h"
#include "../../firmware/workstation/ui/fm_ui.h"
#include "test.h"
#include <string.h>
static unsigned captured;
static uint64_t capture_time;
static FmResult capture_command(void *user, unsigned command, FmId id, uint8_t root, uint64_t ms)
{
    (void)user;
    (void)id;
    (void)root;
    if (command == 1)
    {
        captured++;
        capture_time = ms;
    }
    return FM_OK;
}
static void tap_button(FmUi *u, FmButton b, uint64_t *ms)
{
    fm_ui_button(u, b, true, *ms);
    *ms += 10;
    fm_ui_button(u, b, false, *ms);
    *ms += 10;
}
int test_ui(void)
{
    /* T14/T23/T24/T25/T32: actual physical events through central arbiter. */
    FmProject p;
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    FmId tid;
    CHECK(fm_instrument_create(&p, FM_SAMPLE, &tid) == FM_OK);
    FmTrack *t = fm_track(&p, tid);
    FmInstrument *in = fm_instrument(&p, t->instrument);
    FmId rid;
    CHECK(fm_synthesis_preset(&p, in->id, FM_WAVE_SINE, 1024, &rid) == FM_OK);
    FmEngine engine;
    CHECK(fm_engine_init(&engine, &p, 8) == FM_OK);
    FmSequencer *seq = fm_sequence_create(&p, 128, 128);
    CHECK(seq);
    FmUi u;
    fm_ui_init(&u, &p, &engine, seq);
    fm_ui_callbacks(&u, NULL, capture_command, NULL);
    uint64_t ms = 10;
    tap_button(&u, FM_BUTTON_EDIT, &ms);
    CHECK(u.screen == FM_SCREEN_EDIT && u.tab == 0);
    fm_ui_encoder(&u, 2, 5, ms);
    CHECK(u.rows[0] == 5);
    fm_ui_encoder(&u, 0, 1, ms);
    CHECK(u.scope == 1);
    fm_ui_button(&u, FM_BUTTON_EDIT, true, ms);
    uint64_t down = ms;
    ms += 500;
    fm_ui_tick(&u, ms);
    CHECK(u.picker == FM_PICK_NAME && u.tab == 0);
    fm_ui_key(&u, fm_ui_white_keys[2], true, ++ms);
    fm_ui_key(&u, fm_ui_white_keys[2], false, ++ms);
    CHECK(!strcmp(in->name, "PAD 1"));
    fm_ui_button(&u, FM_BUTTON_EDIT, false, ++ms);
    CHECK(u.tab == 0 && u.picker == FM_PICK_NONE);
    CHECK(ms - down >= 500);
    /* Holding across a HOME screen change cannot turn the release into EDIT. */
    ms += 10;
    fm_ui_button(&u, FM_BUTTON_EDIT, true, ms);
    ms += 20;
    tap_button(&u, FM_BUTTON_HOME, &ms);
    CHECK(u.screen == FM_SCREEN_HOME);
    ms += 500;
    fm_ui_tick(&u, ms);
    CHECK(u.picker == FM_PICK_NAME);
    fm_ui_button(&u, FM_BUTTON_EDIT, false, ++ms);
    CHECK(u.screen == FM_SCREEN_HOME && u.tab == 0);
    tap_button(&u, FM_BUTTON_HOME, &ms);
    CHECK(u.picker == FM_PICK_NONE);
    tap_button(&u, FM_BUTTON_EDIT, &ms);
    CHECK(u.rows[0] == 5 && u.scope == 1);
    tap_button(&u, FM_BUTTON_EDIT, &ms);
    CHECK(u.tab == 1);
    tap_button(&u, FM_BUTTON_EDIT, &ms);
    CHECK(u.tab == 2);
    for (unsigned i = 0; i < 7; i++)
        tap_button(&u, FM_BUTTON_EDIT, &ms);
    CHECK(u.tab == 0 && u.rows[0] == 5);
    /* Numeric draft is silent, undo history receives only confirmed edit. */
    unsigned before = engine.event_count;
    FmSequenceStatus old = fm_sequence_status(seq);
    tap_button(&u, FM_BUTTON_SCL, &ms);
    CHECK(u.picker == FM_PICK_NUMBER);
    float original = in->regions[0].tuning;
    fm_ui_key(&u, fm_ui_white_keys[9], true, ++ms);
    fm_ui_key(&u, fm_ui_white_keys[9], false, ++ms);
    CHECK(in->regions[0].tuning == original && engine.event_count == before);
    CHECK(fm_sequence_status(seq).retained_events == old.retained_events);
    tap_button(&u, FM_BUTTON_HOME, &ms);
    CHECK(u.screen == FM_SCREEN_EDIT && u.picker == FM_PICK_NONE &&
          in->regions[0].tuning == original);
    tap_button(&u, FM_BUTTON_SCL, &ms);
    for (unsigned i = 0; i < 8; i++)
    {
        fm_ui_key(&u, fm_ui_white_keys[9], true, ++ms);
        fm_ui_key(&u, fm_ui_white_keys[9], false, ++ms);
    }
    tap_button(&u, FM_BUTTON_SCL, &ms);
    CHECK(u.last_result == FM_ERR_RANGE && u.picker == FM_PICK_NUMBER &&
          in->regions[0].tuning == original);
    tap_button(&u, FM_BUTTON_HOME, &ms);
    /* Relative HOME/GLO controls address the same track fields. */
    tap_button(&u, FM_BUTTON_HOME, &ms);
    float volume = t->volume;
    fm_ui_encoder(&u, 4, 5, ms);
    NEAR(t->volume, volume + .05, .0001);
    tap_button(&u, FM_BUTTON_GLO, &ms);
    fm_ui_encoder(&u, 3, -2, ms);
    NEAR(t->volume, volume + .03, .0001);
    /* Hold SCL performs one undo and never executes its tap mute action. */
    bool mute = t->mute;
    fm_ui_button(&u, FM_BUTTON_SCL, true, ms);
    ms += 1200;
    fm_ui_tick(&u, ms);
    NEAR(t->volume, volume + .05, .0001);
    fm_ui_tick(&u, ++ms);
    fm_ui_button(&u, FM_BUTTON_SCL, false, ++ms);
    CHECK(t->mute == mute);
    NEAR(t->volume, volume + .05, .0001);
    /* Octave finish responds at down time and never shifts the keyboard. */
    u.screen = FM_SCREEN_CAPTURE;
    int octave = u.octave;
    fm_ui_button(&u, FM_BUTTON_OCT_PLUS, true, ++ms);
    CHECK(u.screen == FM_SCREEN_HOME && captured == 1 && capture_time == ms && u.octave == octave);
    fm_ui_button(&u, FM_BUTTON_OCT_PLUS, false, ms += 700);
    CHECK(captured == 1 && u.octave == octave);
    /* Held notes retain their note-on track through selection changes. */
    tap_button(&u, FM_BUTTON_HOME, &ms);
    fm_ui_key(&u, 0, true, ++ms);
    FmId original_track = u.keys[0].track;
    FmId second;
    CHECK(fm_instrument_create(&p, FM_SAMPLE, &second) == FM_OK);
    fm_ui_key(&u, 0, false, ++ms);
    CHECK(engine.events[engine.event_count - 1].track == original_track &&
          engine.events[engine.event_count - 1].kind == FM_EVENT_NOTE_OFF);
    /* More than four tracks are selectable and renderer remains bounded. */
    for (unsigned i = 0; i < 10; i++)
        CHECK(fm_instrument_create(&p, FM_SAMPLE, &second) == FM_OK);
    fm_ui_encoder(&u, 1, -100, ms);
    CHECK(p.selected_track == tid);
    fm_ui_encoder(&u, 1, 100, ms);
    CHECK(p.selected_track == second);
    fm_ui_render(&u);
    for (size_t i = 0; i < sizeof(u.pixels); i++)
        CHECK(u.pixels[i] <= 1);
    fm_sequence_destroy(seq);
    fm_engine_destroy(&engine);
    fm_project_destroy(&p);
    return 0;
}
