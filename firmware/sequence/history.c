/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "history.h"

#include <string.h>

void fz_history_init(FzHistory *h)
{
    memset(h, 0, sizeof *h);
}

void fz_history_push(FzHistory *h, FzHistEvent event)
{
    if (!h)
        return;
    if (h->count == FZ_HIST_CAP)
        h->dropped++;
    h->ev[h->next] = event;
    h->next = (h->next + 1u) % FZ_HIST_CAP;
    if (h->count < FZ_HIST_CAP)
        h->count++;
}

int fz_history_slice(const FzHistory *h, uint32_t end_us, uint32_t window_us, FzHistEvent *out, int max_out)
{
    uint32_t start = end_us - window_us;
    uint32_t i, n;
    int written = 0;
    if (!h || !out || max_out <= 0)
        return 0;
    if (window_us > end_us)
        start = 0;
    n = h->count;
    i = (h->next + FZ_HIST_CAP - n) % FZ_HIST_CAP;
    while (n--) {
        const FzHistEvent *e = &h->ev[i];
        int live = e->source == FZ_SRC_KEYBOARD || e->source == FZ_SRC_EXTERNAL;
        int in_window = window_us >= end_us ? e->time_us <= end_us
                                            : (e->time_us <= end_us && e->time_us > start);
        if (live && in_window) {
            if (written < max_out)
                out[written] = *e;
            written++;
        }
        i = (i + 1u) % FZ_HIST_CAP;
    }
    return written > max_out ? max_out : written;
}
