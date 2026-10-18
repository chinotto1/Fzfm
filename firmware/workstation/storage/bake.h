/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FM_BAKE_H
#define FM_BAKE_H
#include "../sampler/edit.h"
#define FM_BAKED_FINAL_PATH 0x80u
typedef struct
{
    uint8_t sends;
    uint32_t tail_frames;
    uint8_t warmup_loops;
    /* Explicit creative-isolated render: shared nonlinear buses are not
     * decomposable into exact independent contributions. */
    bool isolated_confirmed, cancelled;
} FmBakeOptions;
/* Stereo final-path one-shot; all active sends required. Baked samples bypass
 * filters/envelopes/mixer (these require undo and rerender), retain pitch rate. */
FmResult fm_bake_samples(FmProject *, FmEditScope, const FmBakeOptions *);
/* One bounded steady-state cycle after 1..8 warm-up loops; notes remain saved. */
FmResult fm_bake_pattern(FmProject *, FmId, const FmBakeOptions *);
FmResult fm_unbake_sample(FmProject *, FmId region);
FmResult fm_unfreeze_pattern(FmProject *, FmId);
#endif
