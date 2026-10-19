/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fm_synthesis.h"
#include <math.h>
#include <string.h>
#define PI 3.14159265358979323846
static FmResult attach(FmProject *project, FmId instrument, const int16_t *pcm, uint32_t frames,
                       uint8_t root, FmSource source, const uint16_t *recipe, FmId *region)
{
    FmInstrument *inst = fm_instrument(project, instrument);
    if (!inst || inst->type == FM_MULTI_SAMPLE || !pcm || !frames || frames > 65536u ||
        root > 127 || !region)
        return FM_ERR_ARGUMENT;
    FmRegion *existing = NULL;
    for (unsigned i = 0; i < FM_ANCHORS; i++)
        if (inst->regions[i].id && inst->regions[i].root == root)
            existing = &inst->regions[i];
    if (existing && (existing->baked_mask & 0x80))
        return FM_ERR_BUSY;
    if (!existing && inst->count >= FM_ANCHORS)
        return FM_ERR_RANGE;
    FmResult result = fm_edit_begin(project);
    if (result != FM_OK)
        return result;
    result = fm_edit_watch(project, inst, sizeof(*inst));
    if (result != FM_OK)
    {
        fm_edit_cancel(project);
        return result;
    }
    FmId asset = 0;
    result = fm_asset_create(project, pcm, frames, 1, FM_RATE, true, source, &asset);
    if (result != FM_OK)
    {
        fm_edit_cancel(project);
        return result;
    }
    if (existing)
    {
        FmAsset *previous = fm_asset(project, existing->asset);
        if (previous && previous->cycle)
            previous->retired = true;
        existing->asset = asset;
        existing->start = 0;
        existing->end = frames;
        existing->loop_count = 0;
        existing->loop_enabled = false;
        *region = existing->id;
        inst->revision++;
    }
    else
        result = fm_region_attach(project, instrument, asset, root, -1, region);
    if (result == FM_OK && recipe)
    {
        memcpy(inst->harmonics, recipe, sizeof(inst->harmonics));
        inst->additive = true;
    }
    else if (result == FM_OK)
        inst->additive = false;
    if (result == FM_OK)
        result = fm_edit_commit(project);
    if (result != FM_OK)
    {
        fm_edit_cancel(project);
        (void)fm_asset_discard(project, asset);
    }
    return result;
}
FmResult fm_synthesis_draw(FmProject *project, FmId instrument, const int16_t *points,
                           uint32_t count, uint8_t root, FmId *region)
{
    return attach(project, instrument, points, count, root, FM_SOURCE_DRAW, NULL, region);
}
FmResult fm_synthesis_additive(FmProject *project, FmId instrument,
                               const uint16_t levels[FM_HARMONICS], uint32_t points, FmId *region)
{
    FmInstrument *inst = fm_instrument(project, instrument);
    if (!inst || !levels || points < FM_HARMONICS * 2u + 1u || points > 65536u)
        return FM_ERR_ARGUMENT;
    for (unsigned h = 0; h < FM_HARMONICS; h++)
        if (levels[h] > 256)
            return FM_ERR_RANGE;
    size_t bytes = (size_t)points * sizeof(int16_t);
    int16_t *pcm = fm_alloc(project, FM_MEM_AUDIO, bytes);
    if (!pcm)
        return FM_ERR_MEMORY;
    /* L1 normalization guarantees headroom for all 48 simultaneous partials.
     * A silent recipe generates a valid silent cycle. No per-partial clip. */
    double scale = 0;
    for (unsigned h = 0; h < FM_HARMONICS; h++)
        scale += (double)levels[h];
    if (scale == 0)
        scale = 1;
    for (uint32_t n = 0; n < points; n++)
    {
        double value = 0;
        for (unsigned h = 0; h < FM_HARMONICS; h++)
            value +=
                (double)levels[h] * sin(2 * PI * (double)(h + 1u) * (double)n / (double)points);
        pcm[n] = (int16_t)lrint(value * 30000.0 / scale);
    }
    FmResult result =
        attach(project, instrument, pcm, points, 60, FM_SOURCE_ADDITIVE, levels, region);
    fm_dealloc(project, FM_MEM_AUDIO, pcm, bytes);
    return result;
}
FmResult fm_synthesis_preset(FmProject *project, FmId instrument, FmWaveShape shape,
                             uint32_t points, FmId *region)
{
    if (points < 2 || points > 65536u || shape > FM_WAVE_SQUARE)
        return FM_ERR_ARGUMENT;
    size_t bytes = (size_t)points * sizeof(int16_t);
    int16_t *pcm = fm_alloc(project, FM_MEM_AUDIO, bytes);
    if (!pcm)
        return FM_ERR_MEMORY;
    for (uint32_t n = 0; n < points; n++)
    {
        double phase = (double)n / (double)points, value;
        switch (shape)
        {
        case FM_WAVE_TRIANGLE:
            value = 1 - 4 * fabs(phase - 0.5);
            break;
        case FM_WAVE_SAW:
            value = 2 * phase - 1;
            break;
        case FM_WAVE_SQUARE:
            value = phase < 0.5 ? 1 : -1;
            break;
        default:
            value = sin(2 * PI * phase);
            break;
        }
        pcm[n] = (int16_t)lrint(value * 30000.0);
    }
    FmResult result = attach(project, instrument, pcm, points, 60, FM_SOURCE_PRESET, NULL, region);
    fm_dealloc(project, FM_MEM_AUDIO, pcm, bytes);
    return result;
}
FmResult fm_synthesis_extract(FmProject *project, FmId instrument, FmId asset_id, uint32_t start,
                              uint32_t points, uint8_t root, FmId *region)
{
    FmAsset *asset = fm_asset(project, asset_id);
    if (!asset || !points || points > 65536u || start >= asset->frames ||
        points > asset->frames - start)
        return FM_ERR_ARGUMENT;
    size_t bytes = (size_t)points * sizeof(int16_t);
    int16_t *pcm = fm_alloc(project, FM_MEM_AUDIO, bytes);
    if (!pcm)
        return FM_ERR_MEMORY;
    for (uint32_t i = 0; i < points; i++)
    {
        int32_t value = asset->pcm[(size_t)(start + i) * asset->channels];
        if (asset->channels == 2)
            value = (value + asset->pcm[(size_t)(start + i) * 2u + 1u]) / 2;
        pcm[i] = (int16_t)value;
    }
    FmResult result =
        attach(project, instrument, pcm, points, root, FM_SOURCE_EXTRACT, NULL, region);
    fm_dealloc(project, FM_MEM_AUDIO, pcm, bytes);
    return result;
}
