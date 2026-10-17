/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R25: burn sends into a new asset. ONE sample zeros only that region's
 * baked trims. Track sends stay. A partial bake stores a wet stem that is
 * not fed back into the remaining sends. Shared nonlinear effects are not
 * treated as a sum of solo bakes. See docs/adr/0003-bake-routing.md. */
#ifndef FZ_BAKE_H
#define FZ_BAKE_H

#include "core/project.h"
#include "dsp/fx.h"

/* 0: F(a)+F(b) is not claimed to equal F(a+b) for a shared nonlinear slot. */
int fz_bake_shared_sum_is_exact(void);

FzStatus fz_bake_region(FzProject *project, FzFxRack *fx, FzId track, FzId region, int mask, int16_t *scratch,
                        uint32_t scratch_cap);
FzStatus fz_bake_pattern(FzProject *project, FzFxRack *fx, FzId track, FzId pattern, FzId region, int mask,
                         int16_t *scratch, uint32_t scratch_cap);

#endif
