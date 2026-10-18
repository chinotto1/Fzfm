/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FM_SYNTHESIS_H
#define FM_SYNTHESIS_H
#include "../core/fm.h"
typedef enum
{
    FM_WAVE_SINE,
    FM_WAVE_TRIANGLE,
    FM_WAVE_SAW,
    FM_WAVE_SQUARE
} FmWaveShape;
FmResult fm_synthesis_additive(FmProject *, FmId instrument, const uint16_t levels[FM_HARMONICS],
                               uint32_t points, FmId *region);
FmResult fm_synthesis_preset(FmProject *, FmId instrument, FmWaveShape, uint32_t points,
                             FmId *region);
FmResult fm_synthesis_extract(FmProject *, FmId instrument, FmId asset, uint32_t start,
                              uint32_t points, uint8_t root, FmId *region);
FmResult fm_synthesis_draw(FmProject *, FmId instrument, const int16_t *points, uint32_t count,
                           uint8_t root, FmId *region);
#endif
