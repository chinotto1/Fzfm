/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fm_capture.h"
#include <math.h>
#include <string.h>

FmCaptureConfig fm_capture_defaults(void)
{
    return (FmCaptureConfig){0.015849f, 0.007943f, 1.0f, 360,   7200,
                             36,        36000,     0,    false, FM_INPUT_MONO_SUM};
}
static void reset_take(FmCapture *c)
{
    c->frames = 0;
    c->pre_count = 0;
    c->pre_write = 0;
    c->quiet = 0;
    c->peak = 0;
}
FmResult fm_capture_begin(FmCapture *c, FmProject *p, FmId tid, uint8_t root,
                          const FmCaptureConfig *config)
{
    FmTrack *t = fm_track(p, tid);
    if (!c || !t || root > 127)
        return FM_ERR_ARGUMENT;
    FmCaptureConfig cfg = config ? *config : fm_capture_defaults();
    if (!isfinite(cfg.threshold) || !isfinite(cfg.stop_threshold) || !isfinite(cfg.input_gain) ||
        cfg.threshold <= 0 || cfg.threshold > 1 || cfg.stop_threshold < 0 ||
        cfg.stop_threshold >= cfg.threshold || cfg.input_gain < 0 || cfg.input_gain > 16 ||
        cfg.pretrigger_frames > FM_PRETRIGGER_MAX || !cfg.maximum_frames ||
        cfg.minimum_frames > cfg.maximum_frames || cfg.maximum_frames <= cfg.pretrigger_frames ||
        cfg.source > FM_INPUT_RIGHT)
        return FM_ERR_RANGE;
    if ((uint64_t)cfg.maximum_frames * sizeof(int16_t) > SIZE_MAX)
        return FM_ERR_RANGE;
    FmInstrument *in = fm_instrument(p, t->instrument);
    if (!in)
        return FM_ERR_NOT_FOUND;
    unsigned slot = 0;
    while (slot < FM_ANCHORS && in->regions[slot].id)
        slot++;
    if (slot == FM_ANCHORS)
        return FM_ERR_STORAGE;
    /* Reserve one bounded staging take, never sixteen equal recording buffers. */
    int16_t *buffer = fm_alloc(p, FM_MEM_AUDIO, (size_t)cfg.maximum_frames * sizeof(*buffer));
    if (!buffer)
        return FM_ERR_MEMORY;
    memset(c, 0, sizeof(*c));
    c->project = p;
    c->track = tid;
    c->instrument = in->id;
    c->config = cfg;
    c->buffer = buffer;
    c->root = root;
    c->slot = (uint8_t)slot;
    c->state = FM_CAPTURE_WAIT_INPUT;
    c->listening = !cfg.manual;
    return FM_OK;
}
void fm_capture_destroy(FmCapture *c)
{
    if (!c)
        return;
    if (c->buffer)
        fm_dealloc(c->project, FM_MEM_AUDIO, c->buffer, (size_t)c->config.maximum_frames * 2);
    memset(c, 0, sizeof(*c));
}
static void trigger(FmCapture *c, FmFrame frame)
{
    c->state = FM_CAPTURE_PRETRIGGER;
    uint32_t n = c->pre_count, cap = c->config.pretrigger_frames;
    for (uint32_t i = 0; i < n; i++)
        c->buffer[i] = c->pretrigger[(c->pre_write + cap - n + i) % cap];
    c->frames = n;
    c->first_frame = frame >= n ? frame - n : 0;
    c->state = FM_CAPTURE_CAPTURING;
}
static void sample(FmCapture *c, int16_t value, FmFrame frame)
{
    float scaled = (float)value * c->config.input_gain;
    if (scaled > 32767)
    {
        scaled = 32767;
        c->clips++;
    }
    if (scaled < -32768)
    {
        scaled = -32768;
        c->clips++;
    }
    int16_t v = (int16_t)scaled;
    float magnitude = fabsf(scaled) / 32768.0f;
    if (magnitude > c->peak)
        c->peak = magnitude;
    if (c->state == FM_CAPTURE_WAIT_INPUT)
    {
        if (!c->config.manual && magnitude >= c->config.threshold)
            trigger(c, frame);
        else
        {
            if (c->config.pretrigger_frames)
            {
                c->pretrigger[c->pre_write] = v;
                c->pre_write = (c->pre_write + 1) % c->config.pretrigger_frames;
                if (c->pre_count < c->config.pretrigger_frames)
                    c->pre_count++;
            }
            return;
        }
    }
    if (c->state != FM_CAPTURE_CAPTURING)
    {
        if (c->state == FM_CAPTURE_FINALIZE)
            c->dropped_busy++;
        return;
    }
    if (c->frames < c->config.maximum_frames)
        c->buffer[c->frames++] = v;
    if (magnitude < c->config.stop_threshold)
        c->quiet++;
    else
        c->quiet = 0;
    if (c->frames == c->config.maximum_frames ||
        (!c->config.manual && c->config.silence_frames && c->quiet >= c->config.silence_frames))
    {
        c->state = FM_CAPTURE_END_DETECTED;
        uint32_t trim = c->config.trim_silence_frames;
        if (trim > c->quiet)
            trim = c->quiet;
        if (trim < c->frames)
            c->frames -= trim;
        c->listening = false;
        c->state = FM_CAPTURE_FINALIZE;
    }
}
void fm_capture_accept(FmCapture *c, const int16_t *mono, size_t n, FmFrame first)
{
    if (!c || !c->buffer || !mono)
        return;
    for (size_t i = 0; i < n; i++)
        sample(c, mono[i], first + i);
    c->clock = first + n;
}
void fm_capture_accept_stereo(FmCapture *c, const float *stereo, size_t n, FmFrame first)
{
    if (!c || !c->buffer || !stereo)
        return;
    for (size_t i = 0; i < n; i++)
    {
        float v = c->config.source == FM_INPUT_LEFT    ? stereo[i * 2]
                  : c->config.source == FM_INPUT_RIGHT ? stereo[i * 2 + 1]
                                                       : (stereo[i * 2] + stereo[i * 2 + 1]) * 0.5f;
        if (!isfinite(v))
            v = 0;
        if (v > 1)
        {
            v = 1;
            c->clips++;
        }
        if (v < -1)
        {
            v = -1;
            c->clips++;
        }
        sample(c, (int16_t)(v >= 1 ? 32767 : v * 32768), first + i);
    }
    c->clock = first + n;
}
FmResult fm_capture_service(FmCapture *c)
{
    if (!c || !c->buffer)
        return FM_ERR_ARGUMENT;
    if (c->state != FM_CAPTURE_FINALIZE)
        return FM_OK;
    if (c->frames < c->config.minimum_frames)
    {
        reset_take(c);
        c->state = c->finish_requested ? FM_CAPTURE_FINISHED : FM_CAPTURE_WAIT_INPUT;
        c->listening = !c->finish_requested && !c->config.manual;
        return FM_OK;
    }
    FmInstrument *in = fm_instrument(c->project, c->instrument);
    if (!in)
        return FM_ERR_NOT_FOUND;
    FmResult r = fm_edit_begin(c->project);
    if (r == FM_OK)
        r = fm_edit_watch(c->project, in, sizeof(*in));
    if (r != FM_OK)
    {
        fm_edit_cancel(c->project);
        c->error = r;
        return r;
    }
    FmId asset = 0, region = 0;
    r = fm_asset_create(c->project, c->buffer, c->frames, 1, FM_RATE, false, FM_SOURCE_CAPTURE,
                        &asset);
    if (r == FM_OK)
        r = fm_region_attach(c->project, in->id, asset, c->root, c->slot, &region);
    if (r != FM_OK)
    {
        fm_edit_cancel(c->project);
        if (asset)
            (void)fm_asset_discard(c->project, asset);
        c->state = FM_CAPTURE_STORAGE_FULL;
        c->error = r;
        return r;
    }
    c->state = FM_CAPTURE_COMMIT_OR_PIN;
    r = fm_edit_commit(c->project);
    c->last_region = region;
    c->state = FM_CAPTURE_ADVANCE;
    c->slot++;
    while (c->slot < FM_ANCHORS && in->regions[c->slot].id)
        c->slot++;
    reset_take(c);
    if (c->finish_requested || in->type == FM_SAMPLE || c->slot == FM_KIT_SLOTS)
        c->state = FM_CAPTURE_FINISHED;
    else
        c->state = FM_CAPTURE_WAIT_INPUT;
    c->listening = c->state == FM_CAPTURE_WAIT_INPUT && !c->config.manual;
    return r;
}
FmResult fm_capture_finish(FmCapture *c)
{
    if (!c || !c->buffer)
        return FM_ERR_ARGUMENT;
    c->finish_requested = true;
    c->listening = false;
    if (c->state == FM_CAPTURE_CAPTURING)
        c->state = FM_CAPTURE_FINALIZE;
    if (c->state == FM_CAPTURE_FINALIZE)
        return fm_capture_service(c);
    c->state = FM_CAPTURE_FINISHED;
    return FM_OK;
}
FmResult fm_capture_resume(FmCapture *c, uint8_t root)
{
    if (!c || !c->buffer || root > 127)
        return FM_ERR_ARGUMENT;
    FmInstrument *in = fm_instrument(c->project, c->instrument);
    if (!in)
        return FM_ERR_NOT_FOUND;
    unsigned slot = 0;
    while (slot < FM_ANCHORS && in->regions[slot].id)
        slot++;
    if (slot == FM_ANCHORS)
        return FM_ERR_STORAGE;
    c->slot = (uint8_t)slot;
    c->root = root;
    reset_take(c);
    c->finish_requested = false;
    c->state = FM_CAPTURE_WAIT_INPUT;
    c->listening = !c->config.manual;
    return FM_OK;
}
FmResult fm_capture_retry(FmCapture *c)
{
    if (!c || !c->buffer || !c->last_region)
        return FM_ERR_NOT_FOUND;
    FmInstrument *in = fm_instrument(c->project, c->instrument);
    if (!in)
        return FM_ERR_NOT_FOUND;
    unsigned slot = 0;
    while (slot < FM_ANCHORS && in->regions[slot].id != c->last_region)
        slot++;
    if (slot == FM_ANCHORS)
        return FM_ERR_NOT_FOUND;
    FmResult r = fm_edit_begin(c->project);
    if (r == FM_OK)
        r = fm_edit_watch(c->project, in, sizeof(*in));
    if (r != FM_OK)
    {
        fm_edit_cancel(c->project);
        return r;
    }
    memset(&in->regions[slot], 0, sizeof(FmRegion));
    in->count--;
    in->revision++;
    fm_map_automatic(in);
    (void)fm_edit_commit(c->project);
    c->slot = (uint8_t)slot;
    c->last_region = 0;
    reset_take(c);
    c->state = FM_CAPTURE_WAIT_INPUT;
    c->finish_requested = false;
    c->listening = !c->config.manual;
    return FM_OK;
}
FmResult fm_capture_skip(FmCapture *c)
{
    if (!c || !c->buffer)
        return FM_ERR_ARGUMENT;
    if (c->state == FM_CAPTURE_CAPTURING || c->state == FM_CAPTURE_FINALIZE)
        return FM_ERR_BUSY;
    FmInstrument *in = fm_instrument(c->project, c->instrument);
    if (!in)
        return FM_ERR_NOT_FOUND;
    c->slot++;
    while (c->slot < FM_ANCHORS && in->regions[c->slot].id)
        c->slot++;
    c->state = c->slot >= FM_ANCHORS ? FM_CAPTURE_FINISHED : FM_CAPTURE_WAIT_INPUT;
    c->listening = c->state == FM_CAPTURE_WAIT_INPUT && !c->config.manual;
    reset_take(c);
    return FM_OK;
}
FmResult fm_capture_manual_start(FmCapture *c)
{
    if (!c || c->state != FM_CAPTURE_WAIT_INPUT)
        return FM_ERR_BUSY;
    trigger(c, c->clock);
    c->listening = true;
    return FM_OK;
}
void fm_capture_input_lost(FmCapture *c)
{
    if (!c)
        return;
    c->listening = false;
    if (c->state == FM_CAPTURE_CAPTURING && c->frames >= c->config.minimum_frames)
    {
        c->state = FM_CAPTURE_FINALIZE;
        c->finish_requested = true;
    }
    else
        c->state = FM_CAPTURE_INPUT_LOST;
}
double fm_capture_seconds_available(const FmCapture *c)
{
    if (!c || !c->project)
        return 0;
    size_t available =
        c->project->budget.limit[FM_MEM_AUDIO] - c->project->budget.used[FM_MEM_AUDIO];
    uint32_t frames = c->config.maximum_frames;
    if (available / 2 < frames)
        frames = (uint32_t)(available / 2);
    return (double)frames / FM_RATE;
}
