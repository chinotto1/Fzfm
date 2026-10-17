/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R08, R11, R13, R16, R17: one screen router. SELECT does not click.
 * OCT+ is the physical tap and hold:
 *   HOME tap raises the octave; HOME hold undoes and does not.
 *   Capture tap finishes to HOME; capture hold undoes the last slot.
 *   Numeric and confirm: tap confirms, hold cancels, and never both.
 *   STEP and PATTERNS: tap turns the page.
 *   EDIT: tap confirms the row; hold undoes.
 * A logical SEL tap/hold still maps to confirm/undo for host tests.
 */
#ifndef FZ_SESSION_H
#define FZ_SESSION_H

#include "core/project.h"
#include "sampler/capture.h"
#include "sequence/history.h"
#include "sequence/seq.h"
#include "ui/input.h"

#define FZ_TAB_COUNT 9
#define FZ_NAME_CATEGORIES 9

typedef enum {
    FZ_SCR_HOME = 0,
    FZ_SCR_CAPTURE,
    FZ_SCR_EDIT,
    FZ_SCR_STEP,
    FZ_SCR_PATTERNS,
    FZ_SCR_SONG,
    FZ_SCR_MIXER,
    FZ_SCR_FX,
    FZ_SCR_NUMERIC,
    FZ_SCR_NAMING,
    FZ_SCR_CONFIRM,
    FZ_SCR_PICK_NOTE,
    FZ_SCR_PICK_RANGE
} FzScreen;

typedef enum {
    FZ_CMD_NONE = 0,
    FZ_CMD_OCTAVE_UP,
    FZ_CMD_OCTAVE_DOWN,
    FZ_CMD_UNDO,
    FZ_CMD_CONFIRM,
    FZ_CMD_CANCEL,
    FZ_CMD_CAPTURE_FINISH,
    FZ_CMD_CAPTURE_UNDO,
    FZ_CMD_PAGE,
    FZ_CMD_EDIT_TAB,
    FZ_CMD_NAMING,
    FZ_CMD_REC_TAP,
    FZ_CMD_REC_HOLD,
    FZ_CMD_PLAY,
    FZ_CMD_HOME,
    FZ_CMD_SAVE
} FzCommandKind;

typedef enum { FZ_UNDO_REGION = 0, FZ_UNDO_SEQ = 1 } FzUndoKind;

typedef struct {
    FzCommandKind kind;
    int value;
    uint32_t at_us;
} FzCommand;

typedef struct {
    FzScreen screen;
    FzScreen back;
    int octave;
    int edit_tab;
    int scope_all;
    int region_sel;
    int row[FZ_TAB_COUNT];
    int page;
    int send_slot;
    int fx_slot;
    int gesture;
    int undo_kind;
    int num_min;
    int num_max;
    int numeric_neg;
    char numeric[8];
    int numeric_n;
    int pick_note;
    int pick_low;
    int pick_high;
    int pick_have;
    int step_note;
} FzSession;

void fz_session_init(FzSession *session);
int fz_session_confirming(const FzSession *session);
int fz_session_wants_play(const FzSession *session);
void fz_session_bind_input(FzSession *session, FzInput *input);
void fz_session_handle(FzSession *session, const FzAction *action, FzCommand *out);
void fz_session_apply(FzSession *session, FzProject *project, FzCapture *capture, FzSeq *seq, const FzCommand *command);
void fz_session_did(FzSession *session, int undo_kind);
FzStatus fz_session_undo(FzSession *session, FzProject *project, FzSeq *seq);

const char *fz_name_category(int index);
FzStatus fz_session_name_key(FzSession *session, FzProject *project, int key);
void fz_session_numeric_open(FzSession *session, int min_value, int max_value);
FzStatus fz_session_numeric_key(FzSession *session, int key);
FzStatus fz_session_numeric_take(FzSession *session, int *out);
void fz_session_pick_open(FzSession *session, int range);
FzStatus fz_session_pick_key(FzSession *session, int note);
void fz_session_history_key(FzSession *session, FzHistory *history, FzHistEvent event);
void fz_session_knob(FzSession *session, FzProject *project, int knob, int detents);
FzStatus fz_session_grid_key(FzSession *session, FzSeq *seq, int key);

#endif
