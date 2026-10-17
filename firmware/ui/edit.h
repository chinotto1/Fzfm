/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R13, R14, R18, R20: edit tabs. Knob 1 moves the row. Knobs 2-4 edit it.
 * ALL is every region of this instrument. Absolute frame numbers refuse ALL.
 * A batch is one undo group. */
#ifndef FZ_EDIT_H
#define FZ_EDIT_H

#include "core/project.h"
#include "ui/session.h"

enum {
    FZ_TAB_SAMPLE = 0,
    FZ_TAB_LOOP,
    FZ_TAB_MIX,
    FZ_TAB_AMP,
    FZ_TAB_FILTER,
    FZ_TAB_MOD,
    FZ_TAB_WAVE,
    FZ_TAB_MAP,
    FZ_TAB_RENDER,
    FZ_TAB_DRAW
};

int fz_edit_tab_on_cycle(int tab);
int fz_edit_next_tab(int cycle, int tab);

const char *fz_edit_tab_name(int tab);
int fz_edit_row_count(int tab);
const char *fz_edit_row_name(int tab, int row);
void fz_edit_row_value(const FzSession *session, const FzProject *project, int row, char *dst, uint32_t cap);
int fz_edit_row_numeric(int tab, int row);
const char *fz_edit_knob_label(int tab, int row, int knob);
void fz_edit_header(const FzSession *session, const FzProject *project, char *dst, uint32_t cap);
/* Attack, decay, sustain level, and release as the ENV page shows them. */
void fz_edit_shown_env(const FzSession *session, const FzProject *project, int filter, int *attack, int *decay,
                       int *sustain, int *release);

FzStatus fz_edit_knob(FzSession *session, FzProject *project, int knob, int detents);
FzStatus fz_edit_apply_number(FzSession *session, FzProject *project, int value);
int fz_edit_affected(const FzSession *session, const FzProject *project);

FzStatus fz_edit_mix(FzProject *project, FzId region_a, FzId region_b, int gain_a_q15, int gain_b_q15, int32_t offset,
                     int16_t *dst, uint32_t dst_cap, FzId *out_asset);
FzStatus fz_edit_cross(FzProject *project, FzId region_a, FzId region_b, uint32_t overlap, int16_t *dst,
                       uint32_t dst_cap, FzId *out_asset);
FzStatus fz_edit_truncate(FzProject *project, FzId region, uint32_t start, uint32_t end);
FzStatus fz_edit_reverse_write(FzProject *project, FzId region, int16_t *dst, uint32_t dst_cap);
FzStatus fz_edit_cut_cycle(FzProject *project, FzId region, uint32_t start, uint32_t length, int16_t *dst,
                           FzId *out_asset);
FzStatus fz_edit_preset(FzProject *project, FzId region, int square, int16_t *dst, uint32_t points);
/* PRESETS encoder. Sine, saw, square, and triangle, written into the cycle recipe. */
int fz_cycle_preset_count(void);
const char *fz_cycle_preset_name(int index);
FzStatus fz_edit_cycle_preset(FzProject *project, int index);

#endif
