/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#ifndef IF_INTERNAL_H
#define IF_INTERNAL_H

#include "if.h"

static inline uint32_t if_step_units(const IfTrack *t, uint32_t idx)
{
    uint32_t base = if_div_units(t->division);
    int32_t sw = (int32_t)t->swing * (int32_t)base / 250;
    int32_t edge = (int32_t)base + ((idx & 1u) ? -sw : sw);
    if (edge < 1)
        edge = 1;
    return (uint32_t)edge;
}

void if_cap_on(IfMachine *m, unsigned track, unsigned note, unsigned vel);
void if_cap_off(IfMachine *m, unsigned track, unsigned note);
void if_route_process(IfMachine *m, const int16_t *usb, uint32_t frames, int16_t *speaker, int16_t *usb_out);

#endif
