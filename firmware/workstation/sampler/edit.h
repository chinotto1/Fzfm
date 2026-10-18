/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FM_SAMPLE_EDIT_H
#define FM_SAMPLE_EDIT_H
#include "../core/fm.h"
typedef struct
{
    FmId instrument;
    FmId region;
    bool all;
} FmEditScope;
typedef enum
{
    FM_EDIT_REVERSE_WRITE,
    FM_EDIT_NORMALIZE,
    FM_EDIT_TRUNCATE,
    FM_EDIT_EXTRACT_CYCLE,
    FM_EDIT_DRAW_POINT,
    FM_EDIT_SMOOTH,
    FM_EDIT_CLEAR
} FmSampleOperation;
typedef struct
{
    FmSampleOperation operation;
    /* ALL marker ranges are normalized fractions, never absolute frame offsets. */
    /* ALL drawing uses from as normalized position; ONE uses absolute point. */
    double from, to;
    float level;
    uint32_t point;
} FmSampleEdit;
typedef struct
{
    FmId first, second;
    float first_gain, second_gain;
    int32_t offset_frames;
    float tuning_cents;
    bool crossmix, equal_power;
    uint32_t overlap_frames;
} FmMixWrite;
FmResult fm_sample_edit(FmProject *, FmEditScope, const FmSampleEdit *);
FmResult fm_mix_write(FmProject *, FmId destination_region, const FmMixWrite *);
/* Metadata marker edits remain reversible without copying PCM. */
FmResult fm_region_markers(FmProject *, FmId region, uint32_t start, uint32_t end);
#endif
