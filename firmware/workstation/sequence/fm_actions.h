/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FM_ACTIONS_H
#define FM_ACTIONS_H
#include "fm_sequence.h"
FmResult fm_pattern_copy(FmProject *, FmId source, FmId destination, bool overwrite);
FmResult fm_pattern_clear(FmProject *, FmId);
FmResult fm_step_properties(FmProject *, FmId, uint32_t step, uint8_t note, float probability,
                            double micro_ticks);
FmResult fm_step_tie(FmProject *, FmId, uint32_t step, uint8_t note);
FmResult fm_step_lock(FmProject *, FmId, uint32_t step, FmParameter, float value);
#endif
