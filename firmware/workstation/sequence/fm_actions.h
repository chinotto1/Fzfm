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
/* Note indexes refer to NOTE_ON events in the immutable current event array.
 * A whole gesture publishes one undoable replacement. Raw capture frames stay. */
uint32_t fm_pattern_note_off(const FmPattern *, uint32_t on);
FmResult fm_pattern_note_edit(FmProject *, FmId, unsigned operation, uint32_t on, double tick,
                              double duration, uint8_t note, uint8_t velocity);
FmResult fm_pattern_quantize(FmProject *, FmId, double grid_ticks);
#endif
