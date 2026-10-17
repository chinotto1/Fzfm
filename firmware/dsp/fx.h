/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R23, R24: four shared send effects. Returns are wet only. Sends are
 * post-fader: track send 0 or region trim 0 contributes nothing. A muted
 * track stops new dry audio and new sends; it does not clear a running tail.
 * Buffers here are host ceilings, not a measured FM-1 budget. */
#ifndef FZ_FX_H
#define FZ_FX_H

#include <stdint.h>

#include "core/project.h"

typedef enum { FZ_FX_OFF = 0, FZ_FX_DELAY = 1, FZ_FX_REVERB = 2, FZ_FX_CHORUS = 3, FZ_FX_DRIVE = 4 } FzFxKind;

#define FZ_FX_SLOTS 4

typedef struct {
    FzFxKind kind;
    int enabled;
    int ret_gain; /* 0..127 */
    int param_a;  /* delay samples, reverb decay, chorus rate, drive amount */
    int param_b;  /* delay feedback, reverb damp, chorus depth */
    int tail;
    uint32_t pos;
    uint32_t phase;
    int32_t damp;
    int16_t delay[4096];
    int16_t comb[4][640];
    int16_t chorus[1024];
} FzFxSlot;

typedef struct FzFxRack {
    FzFxSlot slot[FZ_FX_SLOTS];
} FzFxRack;

void fz_fx_init(FzFxRack *rack);
FzStatus fz_fx_set(FzFxRack *rack, int slot, FzFxKind kind);
void fz_fx_enable(FzFxRack *rack, int slot, int enabled);
uint32_t fz_fx_ram_used(const FzFxRack *rack);
/* `in` is one post-fader send per slot. Outputs are wet only. */
void fz_fx_process(FzFxRack *rack, const int32_t in[FZ_FX_SLOTS], int32_t *wet_l, int32_t *wet_r);

/* Post-fader sample, applied once. track_send 0 or trim 0 yields 0. */
int32_t fz_send_amount(int32_t post_fader, int track_send, int trim);
/* Mute forces dry and sends to 0. A baked pattern mask removes only that
 * pattern's slots. A wet stem never feeds a send. */
void fz_voice_sends(const FzTrack *track, const FzPattern *pattern, const FzRegion *region, int32_t post_fader,
                    int wet_stem, int32_t *dry, int32_t send[FZ_FX_SLOTS]);

#endif
