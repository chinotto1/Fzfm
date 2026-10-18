/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "input.h"

#include <string.h>

void fz_input_init(FzInput *in)
{
    memset(in, 0, sizeof *in);
    in->hold_us = FZ_HOLD_US;
}

void fz_input_set_screen(FzInput *in, int screen)
{
    if (in)
        in->screen = screen;
}

void fz_input_set_confirming(FzInput *in, int confirming)
{
    if (in)
        in->confirming = confirming ? 1 : 0;
}

static void push(FzInput *in, FzActionKind kind, FzButton button, uint32_t at_us)
{
    FzAction *a;
    if (in->q_n >= FZ_ACTION_QUEUE) {
        in->dropped++;
        return;
    }
    a = &in->queue[(in->q_r + in->q_n) % FZ_ACTION_QUEUE];
    a->kind = kind;
    a->button = button;
    a->at_us = at_us;
    in->q_n++;
}

static int hold_button(FzButton button)
{
    return button == FZ_BTN_REC || button == FZ_BTN_EDIT || button == FZ_BTN_SEL;
}

void fz_input_button(FzInput *in, FzButton button, int pressed, uint32_t time_us)
{
    FzButtonTrack *b;
    if (!in || button < 0 || button >= FZ_BTN_COUNT)
        return;
    b = &in->btn[button];
    if (pressed) {
        if (b->down)
            return;
        b->down = 1;
        b->down_us = time_us;
        b->hold_fired = 0;
        if (!hold_button(button))
            push(in, FZ_ACT_TAP, button, time_us);
        return;
    }
    if (!b->down)
        return;
    /* A busy UI may not poll between crossing the threshold and release. */
    if (hold_button(button) && !b->hold_fired && time_us - b->down_us >= in->hold_us) {
        b->hold_fired = 1;
        push(in, button == FZ_BTN_SEL && in->confirming ? FZ_ACT_CANCEL : FZ_ACT_HOLD,
             button, b->down_us);
    }
    b->down = 0;
    if (hold_button(button) && !b->hold_fired)
        push(in, FZ_ACT_TAP, button, b->down_us);
}

void fz_input_poll(FzInput *in, uint32_t now_us)
{
    int i;
    if (!in)
        return;
    for (i = 0; i < FZ_BTN_COUNT; i++) {
        FzButtonTrack *b = &in->btn[i];
        uint32_t elapsed;
        if (!b->down || b->hold_fired || !hold_button((FzButton)i))
            continue;
        elapsed = now_us - b->down_us;
        if (elapsed < in->hold_us)
            continue;
        b->hold_fired = 1;
        if (i == FZ_BTN_SEL && in->confirming)
            push(in, FZ_ACT_CANCEL, FZ_BTN_SEL, b->down_us);
        else
            push(in, FZ_ACT_HOLD, (FzButton)i, b->down_us);
    }
}

int fz_input_pop(FzInput *in, FzAction *out)
{
    if (!in || !out || in->q_n == 0)
        return 0;
    *out = in->queue[in->q_r];
    in->q_r = (in->q_r + 1u) % FZ_ACTION_QUEUE;
    in->q_n--;
    return 1;
}
