/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R11, R13, R17: one arbiter decides tap versus hold. A hold fires once, at
 * the button-down time plus the threshold, and the action timestamp stays at
 * the down edge. SEL is a logical button: the FM-1 sources inspected for M0
 * have no verified SEL key (see docs/CONTROL_MAP.md). */
#ifndef FZ_INPUT_H
#define FZ_INPUT_H

#include <stdint.h>

#define FZ_HOLD_US 500000u
#define FZ_ACTION_QUEUE 16

typedef enum {
    FZ_BTN_OCT_DOWN = 0,
    FZ_BTN_OCT_UP,
    FZ_BTN_FX,
    FZ_BTN_SCL,
    FZ_BTN_ENV,
    FZ_BTN_LFO,
    FZ_BTN_EDIT,
    FZ_BTN_GLO,
    FZ_BTN_HOME,
    FZ_BTN_SAVE,
    FZ_BTN_ARP,
    FZ_BTN_SEQ,
    FZ_BTN_PLAY,
    FZ_BTN_REC,
    FZ_BTN_SEL, /* logical; no verified physical binding yet */
    FZ_BTN_COUNT
} FzButton;

typedef enum {
    FZ_ACT_NONE = 0,
    FZ_ACT_TAP,
    FZ_ACT_HOLD,
    FZ_ACT_CANCEL /* SEL hold while a confirmation is open */
} FzActionKind;

typedef struct {
    FzActionKind kind;
    FzButton button;
    uint32_t at_us;
} FzAction;

typedef struct {
    uint32_t down_us;
    int down;
    int hold_fired;
} FzButtonTrack;

typedef struct {
    FzButtonTrack btn[FZ_BTN_COUNT];
    uint32_t hold_us;
    int confirming;
    int screen;
    FzAction queue[FZ_ACTION_QUEUE];
    uint32_t q_r, q_n;
    uint32_t dropped;
} FzInput;

void fz_input_init(FzInput *in);
void fz_input_set_screen(FzInput *in, int screen);
void fz_input_set_confirming(FzInput *in, int confirming);
void fz_input_button(FzInput *in, FzButton button, int pressed, uint32_t time_us);
void fz_input_poll(FzInput *in, uint32_t now_us);
int fz_input_pop(FzInput *in, FzAction *out);

#endif
