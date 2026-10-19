/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../firmware/workstation/dsp/fm_synthesis.h"
#include "../../firmware/workstation/sequence/fm_actions.h"
#include "../../firmware/workstation/ui/fm_ui.h"
#include "test.h"
#include <string.h>
static unsigned captured;
static unsigned capture_started;
static uint64_t capture_time;
static FmResult capture_command(void *user, unsigned command, FmId id, uint8_t root, uint64_t ms)
{
    (void)user;
    (void)id;
    (void)root;
    if (command == 0)
        capture_started++;
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
static unsigned graph_hash(FmUi *u)
{
    fm_ui_render(u);
    unsigned hash = 2166136261u;
    for (unsigned y = 53; y < 117; y++)
        for (unsigned x = 7; x < 233; x++)
            hash = (hash ^ u->pixels[y * FM_DISPLAY_WIDTH + x]) * 16777619u;
    return hash;
}
static unsigned pixels_with(FmUi *u, unsigned x, unsigned y, unsigned w, unsigned h, unsigned color)
{
    unsigned count = 0;
    for (unsigned row = y; row < y + h; row++)
        for (unsigned col = x; col < x + w; col++)
            count += u->pixels[row * FM_DISPLAY_WIDTH + col] == color;
    return count;
}
static int test_display_style(FmUi *u)
{
    /* Full-color indexed output remains one byte/pixel. Primary labels reach
     * ten rows, rather than the old tiny seven-row face. */
    CHECK(sizeof(u->pixels) == 240 * 240);
    CHECK(fm_ui_palette[FM_UI_BLUE] == 0x294f88);
    CHECK(fm_ui_palette[FM_UI_AMBER] == 0xf4bf35);
    CHECK(fm_ui_rgb565(FM_UI_GREEN) == 0x5650);
    uint8_t wire[2];
    fm_ui_rgb565_bytes(FM_UI_GREEN, wire);
    CHECK(wire[0] == 0x56 && wire[1] == 0x50); /* LCD sends high byte first. */
    CHECK(fm_ui_rgb565(255) == fm_ui_rgb565(FM_UI_BG));
    u->screen = FM_SCREEN_HOME;
    fm_ui_render(u);
    CHECK(pixels_with(u, 2, 52, 232, 23, FM_UI_BLUE) > 1000);
    CHECK(pixels_with(u, 7, 63, 100, 1, FM_UI_TEXT) > 0);
    CHECK(pixels_with(u, 3, 236, 55, 1, FM_UI_AMBER) > 0);
    /* HOME keeps numeric knob values without parameter bars or routine OK. */
    char saved_status[sizeof(u->status)];
    memcpy(saved_status, u->status, sizeof(saved_status));
    FmResult saved_result = u->last_result;
    strcpy(u->status, "OK");
    u->last_result = FM_OK;
    fm_ui_render(u);
    CHECK(pixels_with(u, 0, 194, 240, 18, FM_UI_BG) == 240 * 18);
    CHECK(pixels_with(u, 3, 227, 55, 10, FM_UI_AMBER) > 0);
    strcpy(u->status, "STORAGE FULL");
    u->last_result = FM_ERR_STORAGE;
    fm_ui_render(u);
    CHECK(pixels_with(u, 4, 201, 230, 7, FM_UI_RED) > 0);
    memcpy(u->status, saved_status, sizeof(u->status));
    u->last_result = saved_result;
    FmCapture capture = {.slot = 1, .state = FM_CAPTURE_CAPTURING, .peak = .5f};
    u->capture_monitor = &capture;
    u->screen = FM_SCREEN_CAPTURE;
    fm_ui_render(u);
    CHECK(u->pixels[84 * 240 + 5] == FM_UI_GREEN);  /* Filled slot. */
    CHECK(u->pixels[85 * 240 + 34] == FM_UI_BLUE);  /* Empty slot. */
    CHECK(u->pixels[79 * 240 + 33] == FM_UI_AMBER); /* Target border. */
    CHECK(u->pixels[77 * 240 + 34] == FM_UI_RED);   /* Recording band. */
    CHECK(u->pixels[159 * 240 + 50] == FM_UI_GREEN);
    CHECK(u->pixels[159 * 240 + 220] == FM_UI_BLUE);
    CHECK(pixels_with(u, 4, 132, 230, 10, FM_UI_GREEN) > 50); /* Free memory. */
    CHECK(pixels_with(u, 4, 144, 230, 10, FM_UI_AMBER) > 50); /* Time/count estimate. */
    u->capture_monitor = NULL;
    for (unsigned screen = FM_SCREEN_HOME; screen <= FM_SCREEN_LIBRARY; screen++)
    {
        u->screen = (FmScreen)screen;
        fm_ui_render(u);
        for (size_t i = 0; i < sizeof(u->pixels); i++)
            CHECK(u->pixels[i] < FM_UI_COLOR_COUNT);
    }
    u->screen = FM_SCREEN_HOME;
    return 0;
}
static int test_graphical_parameters(FmUi *u, FmInstrument *in)
{
    /* Graphs must change with the real envelope/LFO/partial parameters. */
    CHECK(fm_ui_set_parameter(u, 3, 0, 0, 0) == FM_OK);
    unsigned old = graph_hash(u);
    CHECK(fm_ui_set_parameter(u, 3, 1, 1, 700) == FM_OK);
    CHECK(fm_ui_set_parameter(u, 3, 2, 1, .6) == FM_OK);
    CHECK(graph_hash(u) != old);
    NEAR(in->regions[0].amp.stage[0].milliseconds, 700, .001);
    NEAR(in->regions[0].amp.stage[0].level, .6, .001);
    CHECK(fm_ui_set_parameter(u, 4, 6, 1, 420) == FM_OK);
    NEAR(in->regions[0].filter.stage[0].milliseconds, 420, .001);
    NEAR(in->regions[0].amp.stage[0].milliseconds, 700, .001);
    CHECK(fm_ui_set_parameter(u, 5, 0, 1, 2.5) == FM_OK);
    CHECK(fm_ui_set_parameter(u, 5, 1, 1, .75) == FM_OK);
    old = graph_hash(u);
    CHECK(fm_ui_set_parameter(u, 5, 2, 1, 2) == FM_OK);
    CHECK(graph_hash(u) != old);
    CHECK(in->regions[0].lfo_shape == 2);
    CHECK(fm_ui_set_parameter(u, 5, 2, 1, FM_LFO_SAMPLE_HOLD) == FM_OK);
    CHECK(in->regions[0].lfo_shape == FM_LFO_SAMPLE_HOLD);
    CHECK(fm_ui_set_parameter(u, 5, 4, 1, 1) == FM_OK);
    CHECK(in->regions[0].lfo_shape == (0x80u | FM_LFO_SAMPLE_HOLD));
    CHECK(fm_ui_set_parameter(u, 5, 4, 1, 0) == FM_OK);
    CHECK(in->regions[0].lfo_shape == FM_LFO_SAMPLE_HOLD);
    CHECK(fm_ui_set_parameter(u, 3, 0, 0, 3) == FM_OK);
    CHECK(fm_ui_set_parameter(u, 3, 0, 0, 1) == FM_OK && u->stage == 1);
    CHECK(fm_ui_set_parameter(u, 5, 1, 1, NAN) == FM_ERR_ARGUMENT);
    CHECK(fm_ui_set_parameter(u, 3, 5, 1, 8) == FM_OK);
    CHECK(fm_ui_set_parameter(u, 3, 0, 0, 7) == FM_OK);
    CHECK(fm_ui_set_parameter(u, 3, 5, 1, 3) == FM_OK && u->stage == 2);
    CHECK(fm_ui_use_source(u, 0) == FM_OK && u->step_note == 72);
    return 0;
}
static int test_usb_sampling_workflow(void)
{
    FmProject p;
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    FmUi u;
    fm_ui_init(&u, &p, NULL, NULL);
    fm_ui_callbacks(&u, NULL, capture_command, NULL);
    uint64_t ms = 10;
    for (unsigned type = FM_MULTI_SAMPLE; type <= FM_SAMPLE; type++)
    {
        u.screen = FM_SCREEN_NEW;
        u.source = type;
        unsigned started = capture_started;
        tap_button(&u, FM_BUTTON_SCL, &ms);
        FmTrack *t = fm_track(&p, p.selected_track);
        FmInstrument *in = t ? fm_instrument(&p, t->instrument) : NULL;
        CHECK(in && (unsigned)in->type == type && in->count == 0);
        CHECK(fm_pattern(&p, t->patterns[0]));
        CHECK(u.screen == FM_SCREEN_CAPTURE && u.source == 0 && !u.pending_instrument);
        CHECK(capture_started == started + 1);
        /* Neither captured type can select synthesized/shared sources via API. */
        for (unsigned source = 1; source <= 5; source++)
        {
            CHECK(fm_ui_use_source(&u, source) == FM_ERR_UNSUPPORTED);
            CHECK(u.screen == FM_SCREEN_CAPTURE && u.source == 0 && !in->count);
        }
        CHECK(fm_ui_set_parameter(&u, 6, 0, 0, 2) == FM_ERR_UNSUPPORTED);
        u.screen = FM_SCREEN_HOME;
        CHECK(fm_ui_workspace_action(&u, 24, false) == FM_OK);
        CHECK(u.screen == FM_SCREEN_CAPTURE && capture_started == started + 2);
        /* Stale view state cannot reopen the oscillator on a sample. */
        u.tab = 6;
        fm_ui_sync_view(&u);
        CHECK(u.tab == 0);
    }
    fm_project_destroy(&p);
    return 0;
}
static int test_synth_workflow(void)
{
    FmProject p;
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    FmEngine engine;
    CHECK(fm_engine_init(&engine, &p, 4) == FM_OK);
    FmUi u;
    fm_ui_init(&u, &p, &engine, NULL);
    fm_ui_callbacks(&u, NULL, capture_command, NULL);
    uint64_t ms = 10;
    tap_button(&u, FM_BUTTON_SCL, &ms);
    CHECK(u.screen == FM_SCREEN_NEW);
    fm_ui_encoder(&u, 2, 2, ms);
    CHECK(u.source == 2);
    fm_ui_render(&u);
    tap_button(&u, FM_BUTTON_SCL, &ms);
    FmTrack *track = fm_track(&p, p.selected_track);
    FmInstrument *in = track ? fm_instrument(&p, track->instrument) : NULL;
    CHECK(in && in->type == FM_SYNTH && in->count == 1);
    CHECK(u.screen == FM_SCREEN_EDIT && u.tab == 6 && !u.pending_instrument);
    CHECK(!in->regions[0].one_shot && fm_asset(&p, in->regions[0].asset)->cycle);
    /* A newly created synth can be heard without visiting any capture wizard. */
    fm_ui_key(&u, 7, true, ++ms);
    float audio[8192];
    fm_engine_render(&engine, audio, 4096);
    float peak = 0;
    for (unsigned i = 0; i < 8192; i++)
        peak = fmaxf(peak, fabsf(audio[i]));
    CHECK(peak > .001f);
    ms += 120;
    fm_ui_key(&u, 7, false, ++ms);
    CHECK(fm_ui_set_parameter(&u, 6, 1, 0, 47) == FM_OK);
    CHECK(u.harmonic == 47);
    CHECK(fm_ui_set_parameter(&u, 6, 1, 1, 128) == FM_OK);
    CHECK(in->harmonics[47] == 128 && in->additive);
    unsigned old = graph_hash(&u);
    CHECK(fm_ui_set_parameter(&u, 6, 1, 1, 256) == FM_OK);
    CHECK(graph_hash(&u) != old);
    unsigned tabs[] = {3, 4, 5, 6, 3, 4, 5, 6};
    for (unsigned i = 0; i < sizeof(tabs) / sizeof(tabs[0]); i++)
    {
        tap_button(&u, FM_BUTTON_EDIT, &ms);
        CHECK(u.tab == tabs[i]);
        fm_ui_render(&u);
    }
    fm_ui_encoder(&u, 2, 100, ms);
    CHECK(u.rows[6] == 6); /* No CUT SAMPLE row on an oscillator. */
    CHECK(fm_ui_set_parameter(&u, 0, 0, 0, 12) == FM_ERR_UNSUPPORTED);
    CHECK(fm_ui_set_parameter(&u, 1, 0, 0, 1) == FM_ERR_UNSUPPORTED);
    CHECK(fm_ui_set_parameter(&u, 2, 1, 0, 1) == FM_ERR_UNSUPPORTED);
    CHECK(fm_ui_set_parameter(&u, 7, 0, 0, 60) == FM_ERR_UNSUPPORTED);
    CHECK(fm_ui_set_parameter(&u, 8, 0, 0, 0) == FM_ERR_UNSUPPORTED);
    CHECK(fm_ui_set_parameter(&u, 6, 7, 0, 0) == FM_ERR_UNSUPPORTED);
    CHECK(fm_ui_use_source(&u, 0) == FM_ERR_UNSUPPORTED);
    CHECK(fm_ui_use_source(&u, 3) == FM_ERR_UNSUPPORTED);
    CHECK(fm_ui_use_source(&u, 5) == FM_ERR_UNSUPPORTED);
    CHECK(fm_ui_use_source(&u, 1) == FM_OK && in->additive);
    CHECK(fm_ui_use_source(&u, 4) == FM_OK && !in->additive);
    FmCapture capture = {0};
    CHECK(fm_capture_begin(&capture, &p, track->id, 72, NULL) == FM_ERR_UNSUPPORTED);
    CHECK(!capture.buffer && in->type == FM_SYNTH);
    CHECK(fm_ui_set_parameter(&u, 6, 0, 0, 0) == FM_ERR_UNSUPPORTED);
    CHECK(fm_ui_set_parameter(&u, 6, 0, 0, 1) == FM_OK);
    tap_button(&u, FM_BUTTON_SCL, &ms);
    CHECK(u.screen == FM_SCREEN_SOURCE && u.source == 1);
    fm_ui_encoder(&u, 2, -100, ms);
    CHECK(u.source == 1);
    fm_ui_encoder(&u, 2, 100, ms);
    CHECK(u.source == 4);
    /* Sample tracks still retain trim/loops/mix/map/render and input capture. */
    FmId id;
    CHECK(fm_instrument_create(&p, FM_SAMPLE, &id) == FM_OK);
    fm_ui_sync_view(&u);
    u.screen = FM_SCREEN_HOME;
    tap_button(&u, FM_BUTTON_EDIT, &ms);
    CHECK(u.tab == 0);
    const unsigned sample_tabs[] = {1, 2, 3, 4, 5, 7, 8, 0};
    for (unsigned i = 0; i < sizeof(sample_tabs) / sizeof(sample_tabs[0]); i++)
    {
        tap_button(&u, FM_BUTTON_EDIT, &ms);
        CHECK(u.tab == sample_tabs[i]);
    }
    CHECK(fm_ui_use_source(&u, 0) == FM_OK && u.screen == FM_SCREEN_CAPTURE);
    fm_engine_destroy(&engine);
    fm_project_destroy(&p);
    return 0;
}
static int test_performance_screens(void)
{
    FmProject p;
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    FmId id;
    CHECK(fm_instrument_create(&p, FM_SAMPLE, &id) == FM_OK);
    FmTrack *track = fm_track(&p, id);
    FmPattern *pat = fm_pattern(&p, track->patterns[0]);
    FmEngine e;
    CHECK(fm_engine_init(&e, &p, 4) == FM_OK);
    FmSequencer *seq = fm_sequence_create(&p, 256, 256);
    CHECK(seq);
    FmUi u;
    fm_ui_init(&u, &p, &e, seq);
    uint64_t ms = 10;
    tap_button(&u, FM_BUTTON_ARP, &ms);
    CHECK(u.screen == FM_SCREEN_ARP && !u.arp_page);
    fm_ui_encoder(&u, 2, 1, ms);
    CHECK(track->arp.mode == FM_ARP_UP);
    tap_button(&u, FM_BUTTON_ARP, &ms);
    CHECK(u.arp_page == 1);
    fm_ui_encoder(&u, 4, 1, ms);
    CHECK(track->arp.latch);
    tap_button(&u, FM_BUTTON_ARP, &ms);
    fm_ui_encoder(&u, 2, 1, ms);
    CHECK(track->arp.mode == FM_ARP_DOWN);
    tap_button(&u, FM_BUTTON_ARP, &ms);
    fm_ui_encoder(&u, 2, 5, ms);
    NEAR(track->arp.swing, .05, .0001);
    CHECK(fm_ui_history(&u, false) == FM_OK);
    CHECK(track->arp.mode == FM_ARP_DOWN && track->arp.swing == 0);
    fm_ui_button(&u, FM_BUTTON_HOME, true, ms);
    ms += 501;
    fm_ui_tick(&u, ms);
    CHECK(u.screen == FM_SCREEN_COMMANDS);
    fm_ui_button(&u, FM_BUTTON_HOME, false, ++ms);
    CHECK(u.screen == FM_SCREEN_COMMANDS);
    tap_button(&u, FM_BUTTON_HOME, &ms);
    CHECK(u.screen == FM_SCREEN_HOME);
    tap_button(&u, FM_BUTTON_GLO, &ms);
    CHECK(u.screen == FM_SCREEN_MIXER);
    fm_ui_render(&u);
    CHECK(pixels_with(&u, 10, 86, 25, 70, FM_UI_GREEN) > 0);
    fm_ui_encoder(&u, 3, -20, ms);
    NEAR(track->volume, .6, .0001);
    tap_button(&u, FM_BUTTON_SCL, &ms);
    CHECK(track->mute);
    unsigned send = u.mixer_send;
    fm_ui_button(&u, FM_BUTTON_GLO, true, ms);
    ms += 501;
    fm_ui_tick(&u, ms);
    fm_ui_button(&u, FM_BUTTON_GLO, false, ++ms);
    CHECK(track->solo && u.mixer_send == send);
    CHECK(fm_arp_set(&p, id, 0, FM_ARP_OFF) == FM_OK);
    tap_button(&u, FM_BUTTON_SEQ, &ms);
    CHECK(u.screen == FM_SCREEN_ROLL);
    u.step_note = 60;
    tap_button(&u, FM_BUTTON_SCL, &ms);
    CHECK(pat->count == 2);
    fm_ui_encoder(&u, 3, 2, ms);
    fm_ui_encoder(&u, 4, 1, ms);
    fm_ui_encoder(&u, 5, -20, ms);
    CHECK(pat->events[0].note == 62 && pat->events[0].velocity == 80);
    NEAR(pat->events[1].tick, 480, .001);
    tap_button(&u, FM_BUTTON_EDIT, &ms);
    CHECK(u.screen == FM_SCREEN_ROLL && u.roll_move);
    fm_ui_encoder(&u, 2, 2, ms);
    NEAR(pat->events[0].tick, 480, .001);
    NEAR(pat->events[1].tick, 960, .001);
    tap_button(&u, FM_BUTTON_SCL, &ms);
    CHECK(!pat->count);
    CHECK(fm_ui_history(&u, false) == FM_OK && pat->count == 2);
    CHECK(fm_pattern_note_edit(&p, pat->id, 0, 0, 355.2, 80, 70, 100) == FM_OK);
    fm_ui_encoder(&u, 6, -1, ms);
    NEAR(u.roll_tick, 355.2, .001);
    CHECK(u.step_note == 70);
    int octave = u.octave;
    tap_button(&u, FM_BUTTON_OCT_PLUS, &ms);
    CHECK(u.octave == octave + 1 && u.screen == FM_SCREEN_ROLL && u.step_note == 82);
    tap_button(&u, FM_BUTTON_OCT_MINUS, &ms);
    CHECK(u.octave == octave && u.step_note == 70);
    fm_ui_encoder(&u, 4, 1, ms);
    CHECK(pat->events[0].note == 70);
    NEAR(pat->events[fm_pattern_note_off(pat, 0)].tick, 675.2, .001);
    fm_ui_encoder(&u, 6, 1, ms);
    CHECK(u.step_note == 62);
    fm_ui_encoder(&u, 4, 1, ms);
    CHECK(fm_ui_history(&u, false) == FM_OK);
    NEAR(pat->events[fm_pattern_note_off(pat, 0)].tick, 675.2, .001);
    tap_button(&u, FM_BUTTON_SEQ, &ms);
    CHECK(u.screen == FM_SCREEN_STEP);
    tap_button(&u, FM_BUTTON_SEQ, &ms);
    CHECK(u.screen == FM_SCREEN_PATTERNS);
    tap_button(&u, FM_BUTTON_SEQ, &ms);
    CHECK(u.screen == FM_SCREEN_SONG);
    tap_button(&u, FM_BUTTON_SEQ, &ms);
    CHECK(u.screen == FM_SCREEN_ROLL);
    for (unsigned screen = FM_SCREEN_ARP; screen <= FM_SCREEN_ROLL; screen++)
    {
        u.screen = (FmScreen)screen;
        fm_ui_render(&u);
        for (size_t i = 0; i < sizeof(u.pixels); i++)
            CHECK(u.pixels[i] < FM_UI_COLOR_COUNT);
    }
    fm_sequence_destroy(seq);
    fm_engine_destroy(&e);
    fm_project_destroy(&p);
    return 0;
}
int test_ui(void)
{
    CHECK(test_performance_screens() == 0);
    CHECK(test_synth_workflow() == 0);
    CHECK(test_usb_sampling_workflow() == 0);
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
    CHECK(test_display_style(&u) == 0);
    CHECK(test_graphical_parameters(&u, in) == 0);
    /* Restore the initial page for the physical-control regressions below. */
    u.screen = FM_SCREEN_HOME;
    u.tab = u.stage = u.harmonic = 0;
    u.step_note = 60;
    memset(u.rows, 0, sizeof(u.rows));
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
    for (unsigned i = 0; i < 6; i++)
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
        CHECK(u.pixels[i] < FM_UI_COLOR_COUNT);
    fm_sequence_destroy(seq);
    fm_engine_destroy(&engine);
    fm_project_destroy(&p);
    return 0;
}
