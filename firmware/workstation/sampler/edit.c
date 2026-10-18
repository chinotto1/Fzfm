/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "edit.h"
#include <math.h>
#include <string.h>

static int16_t quantize(double v)
{
    if (v > 32767)
        v = 32767;
    if (v < -32768)
        v = -32768;
    return (int16_t)lrint(v);
}
static FmResult collect(FmProject *p, FmEditScope scope, FmInstrument **instrument,
                        FmRegion **regions, unsigned *count)
{
    if (!p)
        return FM_ERR_ARGUMENT;
    FmInstrument *s = fm_instrument(p, scope.instrument);
    if (!s)
        return FM_ERR_NOT_FOUND;
    unsigned n = 0;
    for (unsigned i = 0; i < FM_ANCHORS; i++)
        if (s->regions[i].id && s->regions[i].asset &&
            (scope.all || s->regions[i].id == scope.region))
            regions[n++] = &s->regions[i];
    if (!n)
        return FM_ERR_NOT_FOUND;
    *instrument = s;
    *count = n;
    return FM_OK;
}
static FmResult publish(FmProject *p, FmInstrument *s, FmRegion **regions, FmRegion *replacements,
                        FmId *new_assets, unsigned count)
{
    FmResult r = fm_edit_begin(p);
    if (r != FM_OK)
        return r;
    r = fm_edit_watch(p, &s->revision, sizeof(s->revision));
    for (unsigned i = 0; i < count && r == FM_OK; i++)
        r = fm_edit_watch(p, regions[i], sizeof(*regions[i]));
    if (r != FM_OK)
    {
        fm_edit_cancel(p);
        return r;
    }
    for (unsigned i = 0; i < count; i++)
    {
        FmAsset *previous = fm_asset(p, regions[i]->asset);
        if (previous && previous->cycle)
            previous->retired = true;
        replacements[i].asset = new_assets[i];
        *regions[i] = replacements[i];
    }
    s->revision++;
    r = fm_edit_commit(p);
    if (r != FM_OK)
        fm_edit_cancel(p);
    return r;
}
FmResult fm_region_markers(FmProject *p, FmId id, uint32_t start, uint32_t end)
{
    FmRegion *r = fm_region(p, id);
    FmAsset *a = r ? fm_asset(p, r->asset) : NULL;
    if (!a || start >= end || end > a->frames)
        return FM_ERR_RANGE;
    for (unsigned i = 0; i < r->loop_count; i++)
        if (r->loops[i].start < start || r->loops[i].end > end)
            return FM_ERR_RANGE;
    FmResult result = fm_edit_begin(p);
    if (result != FM_OK)
        return result;
    result = fm_edit_watch(p, r, sizeof(*r));
    if (result != FM_OK)
    {
        fm_edit_cancel(p);
        return result;
    }
    r->start = start;
    r->end = end;
    result = fm_edit_commit(p);
    if (result != FM_OK)
        fm_edit_cancel(p);
    return result;
}
FmResult fm_sample_edit(FmProject *p, FmEditScope scope, const FmSampleEdit *edit)
{
    if (!edit || (unsigned)edit->operation > FM_EDIT_CLEAR || !isfinite(edit->from) ||
        !isfinite(edit->to) || !isfinite(edit->level) || edit->level < -1 || edit->level > 1)
        return FM_ERR_ARGUMENT;
    FmRegion *regions[FM_ANCHORS];
    FmRegion replacement[FM_ANCHORS];
    FmId assets[FM_ANCHORS] = {0};
    FmInstrument *s;
    unsigned count;
    FmResult result = collect(p, scope, &s, regions, &count);
    if (result != FM_OK)
        return result;
    bool old_dirty = p->dirty, old_volatile = p->volatile_audio;
    for (unsigned n = 0; n < count; n++)
    {
        FmRegion *r = regions[n];
        FmAsset *a = fm_asset(p, r->asset);
        if (!a || r->start >= r->end || r->end > a->frames)
        {
            result = FM_ERR_FORMAT;
            break;
        }
        uint32_t start = r->start, end = r->end;
        bool crop = edit->operation == FM_EDIT_TRUNCATE || edit->operation == FM_EDIT_EXTRACT_CYCLE;
        if (crop)
        {
            if (edit->from < 0 || edit->to > 1 || edit->from >= edit->to)
            {
                result = FM_ERR_RANGE;
                break;
            }
            uint32_t span = end - start;
            start = r->start + (uint32_t)floor(edit->from * span);
            end = r->start + (uint32_t)ceil(edit->to * span);
            if (end > r->end)
                end = r->end;
        }
        uint32_t frames = end - start;
        if (!frames)
        {
            result = FM_ERR_RANGE;
            break;
        }
        uint32_t point = edit->point;
        if (edit->operation == FM_EDIT_DRAW_POINT && scope.all)
        {
            if (edit->from < 0 || edit->from > 1)
            {
                result = FM_ERR_RANGE;
                break;
            }
            point = (uint32_t)llround(edit->from * (frames - 1));
        }
        if ((edit->operation == FM_EDIT_DRAW_POINT && point >= frames) ||
            (edit->operation == FM_EDIT_NORMALIZE && edit->level < 0))
        {
            result = FM_ERR_RANGE;
            break;
        }
        replacement[n] = *r;
        FmRegion *out = &replacement[n];
        out->start = 0;
        out->end = frames;
        out->reverse = false;
        bool reversed = r->reverse != (edit->operation == FM_EDIT_REVERSE_WRITE);
        if (edit->operation == FM_EDIT_EXTRACT_CYCLE)
        {
            out->loop_count = 0;
            out->loop_enabled = false;
            out->one_shot = false;
        }
        else
            for (unsigned j = 0; j < out->loop_count; j++)
            {
                FmLoopSection *l = &out->loops[j];
                if (l->start < start || l->end > end)
                {
                    result = FM_ERR_RANGE;
                    break;
                }
                uint32_t lo = l->start - start, hi = l->end - start;
                l->start = reversed ? frames - hi : lo;
                l->end = reversed ? frames - lo : hi;
            }
        if (result != FM_OK)
            break;
        size_t samples = (size_t)frames * a->channels, bytes = samples * sizeof(int16_t);
        int16_t *pcm = fm_alloc(p, FM_MEM_AUDIO, bytes);
        if (!pcm)
        {
            result = FM_ERR_STORAGE;
            break;
        }
        for (uint32_t i = 0; i < frames; i++)
            for (unsigned ch = 0; ch < a->channels; ch++)
                pcm[(size_t)i * a->channels + ch] =
                    a->pcm[(size_t)(reversed ? end - 1 - i : start + i) * a->channels + ch];
        if (edit->operation == FM_EDIT_NORMALIZE)
        {
            int peak = 0;
            for (size_t i = 0; i < samples; i++)
            {
                int v = pcm[i];
                if (v < 0)
                    v = -v;
                if (v > peak)
                    peak = v;
            }
            double gain = peak ? (double)edit->level * 32767 / peak : 1;
            for (size_t i = 0; i < samples; i++)
                pcm[i] = quantize(pcm[i] * gain);
        }
        else if (edit->operation == FM_EDIT_DRAW_POINT)
            for (unsigned ch = 0; ch < a->channels; ch++)
                pcm[(size_t)point * a->channels + ch] = quantize(edit->level * 32767);
        else if (edit->operation == FM_EDIT_CLEAR)
            memset(pcm, 0, bytes);
        else if (edit->operation == FM_EDIT_SMOOTH)
        { /* Periodic three-point moving average, from immutable source. */
            for (uint32_t i = 0; i < frames; i++)
                for (unsigned ch = 0; ch < a->channels; ch++)
                {
                    uint32_t before = (i + frames - 1) % frames, after = (i + 1) % frames;
                    uint32_t ix[3] = {before, i, after};
                    int sum = 0;
                    for (unsigned k = 0; k < 3; k++)
                        sum += a->pcm[(size_t)(reversed ? end - 1 - ix[k] : start + ix[k]) *
                                          a->channels +
                                      ch];
                    pcm[(size_t)i * a->channels + ch] = (int16_t)(sum / 3);
                }
        }
        FmSource source =
            edit->operation == FM_EDIT_EXTRACT_CYCLE
                ? FM_SOURCE_EXTRACT
                : (edit->operation == FM_EDIT_DRAW_POINT || edit->operation == FM_EDIT_CLEAR ||
                           edit->operation == FM_EDIT_SMOOTH
                       ? FM_SOURCE_DRAW
                       : FM_SOURCE_RENDER);
        result = fm_asset_create(p, pcm, frames, a->channels, a->rate,
                                 a->cycle || edit->operation == FM_EDIT_EXTRACT_CYCLE, source,
                                 &assets[n]);
        fm_dealloc(p, FM_MEM_AUDIO, pcm, bytes);
        if (result != FM_OK)
            break;
    }
    if (result == FM_OK)
        result = publish(p, s, regions, replacement, assets, count);
    if (result != FM_OK)
    {
        for (unsigned i = 0; i < count; i++)
            if (assets[i])
                fm_asset_discard(p, assets[i]);
        p->dirty = old_dirty;
        p->volatile_audio = old_volatile;
    }
    return result;
}
static double sample(const FmAsset *a, const FmRegion *r, double phase, unsigned channel)
{
    if (phase < 0 || phase >= r->end - r->start)
        return 0;
    double x = r->reverse ? r->end - 1 - phase : r->start + phase;
    if (x < r->start)
        x = r->start;
    uint32_t i = (uint32_t)floor(x), j = i + 1;
    if (j >= r->end)
        j = i;
    unsigned ch = a->channels == 1 ? 0 : channel;
    double f = x - i;
    return a->pcm[(size_t)i * a->channels + ch] * (1 - f) +
           a->pcm[(size_t)j * a->channels + ch] * f;
}
FmResult fm_mix_write(FmProject *p, FmId destination, const FmMixWrite *mix)
{
    if (!p || !mix || !isfinite(mix->first_gain) || !isfinite(mix->second_gain) ||
        !isfinite(mix->tuning_cents) || fabsf(mix->first_gain) > 16 ||
        fabsf(mix->second_gain) > 16 || fabsf(mix->tuning_cents) > 9600)
        return FM_ERR_ARGUMENT;
    FmRegion *a = fm_region(p, mix->first), *b = fm_region(p, mix->second),
             *dest = fm_region(p, destination);
    FmAsset *aa = a ? fm_asset(p, a->asset) : NULL, *bb = b ? fm_asset(p, b->asset) : NULL;
    if (!aa || !bb || !dest || a->start >= a->end || a->end > aa->frames || b->start >= b->end ||
        b->end > bb->frames)
        return FM_ERR_NOT_FOUND;
    if (mix->offset_frames < 0)
        return FM_ERR_UNSUPPORTED; /* No hidden clipping of negative source starts. */
    double astep = (double)aa->rate / FM_RATE * exp2(a->tuning / 1200.0),
           bstep = (double)bb->rate / FM_RATE * exp2((b->tuning + mix->tuning_cents) / 1200.0);
    double alength = a->reverse ? floor((a->end - a->start - 1) / astep) + 1
                                : ceil((a->end - a->start) / astep),
           blength = b->reverse ? floor((b->end - b->start - 1) / bstep) + 1
                                : ceil((b->end - b->start) / bstep);
    if (!isfinite(alength) || !isfinite(blength) || alength > UINT32_MAX || blength > UINT32_MAX)
        return FM_ERR_RANGE;
    uint64_t offset = (uint32_t)mix->offset_frames;
    if (mix->crossmix)
    {
        if (!mix->overlap_frames || mix->overlap_frames > alength || mix->overlap_frames > blength)
            return FM_ERR_RANGE;
        offset = (uint32_t)alength - mix->overlap_frames;
    }
    uint64_t length = (uint64_t)blength + offset;
    if (length < (uint64_t)alength)
        length = (uint64_t)alength;
    if (!length || length > UINT32_MAX || length > SIZE_MAX / 4)
        return FM_ERR_RANGE;
    uint8_t channels = aa->channels > bb->channels ? aa->channels : bb->channels;
    size_t bytes = (size_t)length * channels * 2;
    int16_t *pcm = fm_alloc(p, FM_MEM_AUDIO, bytes);
    if (!pcm)
        return FM_ERR_STORAGE;
    for (uint32_t i = 0; i < length; i++)
    {
        double ga = mix->first_gain, gb = mix->second_gain;
        if (mix->crossmix)
        {
            double t = i < offset ? 0
                                  : (i >= offset + mix->overlap_frames
                                         ? 1
                                         : (double)(i - offset) / mix->overlap_frames);
            ga *= mix->equal_power ? cos(t * 1.5707963267948966) : 1 - t;
            gb *= mix->equal_power ? sin(t * 1.5707963267948966) : t;
        }
        for (unsigned ch = 0; ch < channels; ch++)
        {
            double v = sample(aa, a, i * astep, ch) * ga +
                       sample(bb, b, ((double)i - offset) * bstep, ch) * gb;
            pcm[(size_t)i * channels + ch] = quantize(v);
        }
    }
    FmId asset = 0;
    bool dirty = p->dirty, vol = p->volatile_audio;
    FmResult result = fm_asset_create(p, pcm, (uint32_t)length, channels, FM_RATE, false,
                                      FM_SOURCE_RENDER, &asset);
    fm_dealloc(p, FM_MEM_AUDIO, pcm, bytes);
    if (result != FM_OK)
        return result;
    FmInstrument *owner = NULL;
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (p->instruments[i])
            for (unsigned j = 0; j < FM_ANCHORS; j++)
                if (&p->instruments[i]->regions[j] == dest)
                    owner = p->instruments[i];
    FmRegion replacement = *dest;
    replacement.start = 0;
    replacement.end = (uint32_t)length;
    replacement.reverse = false;
    replacement.loop_count = 0;
    replacement.loop_enabled = false;
    replacement.tuning = 0;
    if (owner)
        result = publish(p, owner, &dest, &replacement, &asset, 1);
    else
        result = FM_ERR_NOT_FOUND;
    if (result != FM_OK)
    {
        fm_asset_discard(p, asset);
        p->dirty = dirty;
        p->volatile_audio = vol;
    }
    return result;
}
