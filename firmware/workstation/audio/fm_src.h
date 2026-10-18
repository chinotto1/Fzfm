/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FM_SRC_H
#define FM_SRC_H
#include "../core/fm.h"
#define FM_SRC_TAPS 32u
#define FM_SRC_HISTORY 64u
#ifndef FM_SRC_PHASE_LIMIT
#define FM_SRC_PHASE_LIMIT 128u
#endif
typedef struct
{
    uint32_t input_rate, output_rate;
    unsigned channels, phases;
    uint32_t divisor;
    bool exact_phases;
    float coefficients[FM_SRC_PHASE_LIMIT][FM_SRC_TAPS];
    uint64_t received, next_numerator;
    float history[FM_SRC_HISTORY][2];
} FmSrc;
FmResult fm_src_init(FmSrc *, uint32_t input_rate, uint32_t output_rate, unsigned channels);
/* Streaming 32-tap windowed-sinc, 16 input-frame lookahead. The caller retains
 * unconsumed input and may call again with zero input to drain ready output.
 * Fixed rational clock, no per-packet rounding, no process allocation. */
size_t fm_src_process(FmSrc *, const float *input, size_t input_frames, size_t *consumed,
                      float *output, size_t output_capacity);
#endif
