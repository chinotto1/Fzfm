/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Native bridge for the interactive simulator. This reuses the CLI owner and
 * the real C framebuffer/audio engine; JavaScript neither models nor edits DSP. */
#define main fm_cli_entry
#include "main.c"
#undef main
#include "../../firmware/workstation/import/import.h"
#include "../../firmware/workstation/platform/fm_usb.h"
static FmBudget replacement_budget(const FmProject *p)
{
    FmBudget b = p->budget;
    for (unsigned i = 0; i < FM_MEM_COUNT; i++)
        b.limit[i] -= b.used[i];
    return b;
}
static void replacement_limits(FmProject *next, const FmProject *old)
{
    for (unsigned i = 0; i < FM_MEM_COUNT; i++)
    {
        next->budget.limit[i] = old->budget.limit[i];
        size_t peak = next->budget.peak[i] + old->budget.used[i];
        next->budget.peak[i] = peak > old->budget.peak[i] ? peak : old->budget.peak[i];
    }
}
static FmResult load_project(void *user, FmProject *old)
{
    Simulator *s = user;
    FmProject next;
    FmBudget available = replacement_budget(old);
    FmResult r = fm_project_init(&next, &available);
    if (r == FM_OK)
        r = fm_store_load(&next, s->prefix);
    if (r != FM_OK)
    {
        fm_project_destroy(&next);
        return r;
    }
    FmEngine prepared;
    FmSequencer *sequence = NULL;
    r = fm_engine_init(&prepared, &next, 16);
    if (r == FM_OK)
    {
        sequence = fm_sequence_create(&next, 4096, 2048);
        if (!sequence)
            r = FM_ERR_MEMORY;
    }
    if (r != FM_OK)
    {
        fm_project_destroy(&next);
        return r;
    }
    replacement_limits(&next, &s->project);
    fm_capture_destroy(&s->capture);
    fm_sequence_destroy(s->sequence);
    fm_engine_destroy(&s->engine);
    fm_project_destroy(old);
    s->project = next;
    s->engine = prepared;
    s->engine.project = &s->project;
    s->sequence = sequence;
    fm_sequence_rebind(sequence, &s->project);
    fm_ui_init(&s->ui, &s->project, &s->engine, sequence);
    fm_ui_callbacks(&s->ui, save_project, capture_command, s);
    s->ui.load = load_project;
    s->ui.capture_monitor = &s->capture;
    fm_midi_init(&s->midi, &s->project, &s->engine, sequence);
    s->frames = 0;
    s->capture_targeted = s->capture_replace = false;
    memset(&s->audition, 0, sizeof(s->audition));
    memset(s->performed, 0, sizeof(s->performed));
    return FM_OK;
}
void *fm_sim_create(const char *directory)
{
    Simulator *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    if (init(s, directory))
    {
        destroy(s);
        free(s);
        return NULL;
    }
    if (s->wave)
        fclose(s->wave);
    s->wave = NULL;
    s->ui.load = load_project;
    return s;
}
void fm_sim_destroy(void *handle)
{
    Simulator *s = handle;
    if (s)
    {
        destroy(s);
        free(s);
    }
}
int fm_sim_command(void *handle, unsigned type, unsigned index, int value)
{
    Simulator *s = handle;
    if (!s)
        return FM_ERR_ARGUMENT;
    if (type == 0)
    {
        if (index >= FM_UI_BUTTONS)
            return FM_ERR_RANGE;
        if (!value && s->ui.buttons[index].down &&
            s->frames / 36 < s->ui.buttons[index].down_ms + 6)
            render_frames(s, (uint32_t)((s->ui.buttons[index].down_ms + 6) * 36 - s->frames));
        fm_ui_button(&s->ui, (FmButton)index, value != 0, s->frames / 36);
    }
    else if (type == 1)
    {
        if (index >= FM_UI_KEYS)
            return FM_ERR_RANGE;
        fm_ui_key(&s->ui, index, value != 0, s->frames / 36);
    }
    else if (type == 2)
    {
        if (index > 6 || value < -10000 || value > 10000)
            return FM_ERR_RANGE;
        fm_ui_encoder(&s->ui, index, value, s->frames / 36);
    }
    else if (type == 3)
    {
        if (value < 0 || value > 36000)
            return FM_ERR_RANGE;
        render_frames(s, (uint32_t)value);
    }
    else if (type == 4)
    {
        FmResult r = tone(s, 110 + index * 31, 400);
        s->ui.last_result = r;
        return r;
    }
    else if (type == 5)
        return save_project(s, &s->project);
    else if (type == 6)
        return load_project(s, &s->project);
    else if (type == 7)
    {
        fm_engine_panic(&s->engine);
        return FM_OK;
    }
    else
        return FM_ERR_UNSUPPORTED;
    return s->ui.last_result;
}
int fm_sim_render(void *handle, float *stereo, unsigned frames)
{
    Simulator *s = handle;
    if (!s || !stereo || frames > 8192)
        return FM_ERR_ARGUMENT;
    unsigned done = 0;
    while (done < frames)
    {
        unsigned n = frames - done;
        if (n > 128)
            n = 128;
        fm_sequence_advance(s->sequence, n, fm_ui_sequence_dispatch, &s->engine);
        fm_engine_render(&s->engine, stereo + 2 * done, n);
        s->frames += n;
        done += n;
    }
    fm_ui_tick(&s->ui, s->frames / 36);
    return FM_OK;
}
const uint8_t *fm_sim_pixels(void *handle)
{
    Simulator *s = handle;
    fm_ui_render(&s->ui);
    return s->ui.pixels;
}
const char *fm_sim_text(void *handle, unsigned field)
{
    Simulator *s = handle;
    if (field == 0)
        return fm_ui_screen_name(s->ui.screen);
    if (field == 1)
        return s->ui.status;
    FmTrack *t = fm_track(&s->project, s->project.selected_track);
    FmInstrument *in = t ? fm_instrument(&s->project, t->instrument) : NULL;
    return in ? in->name : "NO INSTRUMENT";
}
double fm_sim_metric(void *handle, unsigned field)
{
    Simulator *s = handle;
    FmTrack *t = fm_track(&s->project, s->project.selected_track);
    FmInstrument *in = t ? fm_instrument(&s->project, t->instrument) : NULL;
    FmSequenceStatus st = fm_sequence_status(s->sequence);
    switch (field)
    {
    case 0:
        return s->frames;
    case 1:
        return s->project.track_count;
    case 2:
        return s->project.asset_count;
    case 3:
        return s->project.bpm;
    case 4:
        return fm_sequence_playing(s->sequence);
    case 5:
        return s->project.dirty;
    case 6:
        return s->ui.picker;
    case 7:
        return s->ui.screen;
    case 8:
        return s->ui.tab;
    case 9:
        return s->ui.scope;
    case 10:
        return t ? t->cutoff : 0;
    case 11:
        return t ? t->resonance : 0;
    case 12:
        return t ? t->volume : 0;
    case 13:
        return t ? t->pan : 0;
    case 14:
        return in ? in->count : 0;
    case 15:
        return s->capture.state;
    case 16:
        return s->capture.peak;
    case 17:
        return s->capture.slot;
    case 18:
        return s->ui.last_result;
    case 19:
        return s->project.song_count;
    case 20:
        return t ? t->selected_pattern : 0;
    case 21:
        return fm_engine_active_voices(&s->engine);
    case 22:
        return st.recording;
    case 23:
        return st.armed;
    case 24:
        return st.candidate_count;
    case 25:
        return st.retained_frames / (double)FM_RATE;
    case 26:
        return fm_sequence_arranging(s->sequence);
    case 27:
        return s->project.budget.used[FM_MEM_AUDIO];
    case 28:
        return s->project.budget.used[FM_MEM_FX];
    case 29:
        return s->engine.rejected_events;
    case 30:
        return s->capture.dropped_busy;
    case 31:
        return in ? in->type : 1;
    case 32:
        return in ? in->base_note : 36;
    case 33:
        return s->ui.octave;
    case 34:
        return s->project.generation;
    default:
        return 0;
    }
}
int fm_sim_import_wave(void *handle, const uint8_t *bytes, size_t length, int cycle)
{
    Simulator *s = handle;
    if (!s || length > 32u * 1024u * 1024u || s->project.next_id > UINT32_MAX - 5)
        return FM_ERR_RANGE;
    bool dirty = s->project.dirty, vol = s->project.volatile_audio;
    FmId next_id = s->project.next_id;
    FmId aid = 0, tid = 0, rid = 0;
    FmResult r = fm_import_wav_asset(&s->project, bytes, length, cycle != 0, &aid);
    if (r != FM_OK)
        return r;
    r = fm_instrument_create(&s->project, FM_SAMPLE, &tid);
    if (r != FM_OK)
    {
        fm_asset_discard(&s->project, aid);
        s->project.dirty = dirty;
        s->project.volatile_audio = vol;
        s->project.next_id = next_id;
        return r;
    }
    FmTrack *t = fm_track(&s->project, tid);
    r = fm_region_attach(&s->project, t->instrument, aid, 60, -1, &rid);
    if (r == FM_OK)
    {
        s->ui.screen = FM_SCREEN_HOME;
        fm_ui_tick(&s->ui, s->frames / 36);
    }
    return r;
}
int fm_sim_backup(void *handle, uint8_t **bytes, size_t *length)
{
    Simulator *s = handle;
    return fm_project_encode(&s->project, bytes, length);
}
void fm_sim_free(void *bytes)
{
    free(bytes);
}
int fm_sim_restore(void *handle, const uint8_t *bytes, size_t length)
{
    Simulator *s = handle;
    FmProject next;
    FmBudget available = replacement_budget(&s->project);
    FmResult r = fm_project_init(&next, &available);
    if (r == FM_OK)
        r = fm_project_decode(&next, bytes, length);
    if (r != FM_OK)
    {
        fm_project_destroy(&next);
        return r;
    }
    /* Validate and prepare every runtime allocation before replacing anything. */
    FmEngine prepared;
    r = fm_engine_init(&prepared, &next, 16);
    FmSequencer *sequence = r == FM_OK ? fm_sequence_create(&next, 4096, 2048) : NULL;
    if (r != FM_OK || !sequence)
    {
        fm_project_destroy(&next);
        return r == FM_OK ? FM_ERR_MEMORY : r;
    }
    replacement_limits(&next, &s->project);
    fm_capture_destroy(&s->capture);
    fm_sequence_destroy(s->sequence);
    fm_engine_destroy(&s->engine);
    fm_project_destroy(&s->project);
    s->project = next;
    s->engine = prepared;
    s->engine.project = &s->project;
    s->sequence = sequence;
    fm_sequence_rebind(sequence, &s->project);
    fm_ui_init(&s->ui, &s->project, &s->engine, sequence);
    fm_ui_callbacks(&s->ui, save_project, capture_command, s);
    s->ui.load = load_project;
    s->ui.capture_monitor = &s->capture;
    fm_midi_init(&s->midi, &s->project, &s->engine, sequence);
    s->frames = 0;
    s->capture_targeted = s->capture_replace = false;
    memset(&s->audition, 0, sizeof(s->audition));
    memset(s->performed, 0, sizeof(s->performed));
    return FM_OK;
}
int fm_sim_demo(void *handle)
{
    Simulator *s = handle;
    if (s->project.track_count)
        return FM_ERR_BUSY;
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", s->prefix);
    char *slash = strrchr(dir, '/');
    if (!slash)
        return FM_ERR_ARGUMENT;
    *slash = 0;
    int r = demo(s, dir);
    s->ui.screen = FM_SCREEN_HOME;
    snprintf(s->ui.status, sizeof(s->ui.status), "DEMO READY - PLAY / EDIT / SEQ");
    return r ? FM_ERR_FORMAT : FM_OK;
}

/* Browser durability is confirmed only after IndexedDB's async commit. */
void fm_sim_persistence_error(void *handle)
{
    Simulator *s = handle;
    if (!s)
        return;
    s->project.dirty = true;
    s->project.volatile_audio = true;
    s->ui.last_result = FM_ERR_STORAGE;
    snprintf(s->ui.status, sizeof(s->ui.status), "LOCAL SAVE FAILED - EXPORT BACKUP");
}

#include "editor.inc.c"
#include "workflow.inc.c"
