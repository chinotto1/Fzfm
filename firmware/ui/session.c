/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "session.h"

#include "ui/edit.h"

#include <stdio.h>
#include <string.h>

static const char *k_categories[FZ_NAME_CATEGORIES] = {
    "DRUMS", "SYNTH", "PAD", "LEAD", "FX", "BASS", "KEYS", "PERC", "LOOP"};

void fz_session_init(FzSession *session)
{
    if (!session)
        return;
    memset(session, 0, sizeof *session);
    session->screen = FZ_SCR_HOME;
    session->step_note = 36;
    session->num_max = 127;
}

int fz_session_confirming(const FzSession *session)
{
    return session && (session->screen == FZ_SCR_NUMERIC || session->screen == FZ_SCR_CONFIRM);
}

int fz_session_wants_play(const FzSession *session)
{
    if (!session)
        return 0;
    return session->screen == FZ_SCR_HOME || session->screen == FZ_SCR_EDIT || session->screen == FZ_SCR_MIXER ||
           session->screen == FZ_SCR_FX;
}

void fz_session_bind_input(FzSession *session, FzInput *input)
{
    if (!session || !input)
        return;
    fz_input_set_screen(input, (int)session->screen);
    fz_input_set_confirming(input, fz_session_confirming(session));
}

const char *fz_name_category(int index)
{
    if (index < 0 || index >= FZ_NAME_CATEGORIES)
        return "";
    return k_categories[index];
}

static void emit(FzCommand *out, FzCommandKind kind, int value, uint32_t at_us)
{
    if (!out)
        return;
    out->kind = kind;
    out->value = value;
    out->at_us = at_us;
}

static int hold_or_cancel(FzActionKind kind)
{
    return kind == FZ_ACT_HOLD || kind == FZ_ACT_CANCEL;
}

static void leave_modal(FzSession *session)
{
    session->numeric_n = 0;
    session->numeric[0] = 0;
    session->pick_have = 0;
    if (session->screen == FZ_SCR_NUMERIC || session->screen == FZ_SCR_CONFIRM || session->screen == FZ_SCR_NAMING ||
        session->screen == FZ_SCR_PICK_NOTE || session->screen == FZ_SCR_PICK_RANGE)
        session->screen = session->back;
}

void fz_session_handle(FzSession *session, const FzAction *action, FzCommand *out)
{
    int oct, sel, modal;
    emit(out, FZ_CMD_NONE, 0, 0);
    if (!session || !action)
        return;
    emit(out, FZ_CMD_NONE, 0, action->at_us);
    oct = action->button == FZ_BTN_OCT_UP;
    sel = action->button == FZ_BTN_SEL;
    modal = session->screen == FZ_SCR_NUMERIC || session->screen == FZ_SCR_CONFIRM || session->screen == FZ_SCR_NAMING ||
            session->screen == FZ_SCR_PICK_NOTE || session->screen == FZ_SCR_PICK_RANGE;

    if ((oct || sel) && hold_or_cancel(action->kind)) {
        if (session->screen == FZ_SCR_NUMERIC || session->screen == FZ_SCR_CONFIRM || session->screen == FZ_SCR_NAMING ||
            session->screen == FZ_SCR_PICK_NOTE || session->screen == FZ_SCR_PICK_RANGE) {
            leave_modal(session);
            emit(out, FZ_CMD_CANCEL, 0, action->at_us);
            return;
        }
        if (oct && session->screen == FZ_SCR_CAPTURE) {
            emit(out, FZ_CMD_CAPTURE_UNDO, 0, action->at_us);
            return;
        }
        emit(out, FZ_CMD_UNDO, 0, action->at_us);
        return;
    }

    if ((oct || sel) && action->kind == FZ_ACT_TAP) {
        if (session->screen == FZ_SCR_NUMERIC || session->screen == FZ_SCR_CONFIRM) {
            emit(out, FZ_CMD_CONFIRM, 0, action->at_us);
            return;
        }
        if (oct && session->screen == FZ_SCR_CAPTURE) {
            session->screen = FZ_SCR_HOME;
            emit(out, FZ_CMD_CAPTURE_FINISH, session->octave, action->at_us);
            return;
        }
        if (oct && (session->screen == FZ_SCR_STEP || session->screen == FZ_SCR_PATTERNS)) {
            if (session->page < 7)
                session->page++;
            emit(out, FZ_CMD_PAGE, session->page, action->at_us);
            return;
        }
        if (oct && session->screen == FZ_SCR_HOME) {
            if (session->octave < 4)
                session->octave++;
            emit(out, FZ_CMD_OCTAVE_UP, session->octave, action->at_us);
            return;
        }
        if (oct && (session->screen == FZ_SCR_PICK_NOTE || session->screen == FZ_SCR_PICK_RANGE)) {
            if (session->octave < 4)
                session->octave++;
            emit(out, FZ_CMD_OCTAVE_UP, session->octave, action->at_us);
            return;
        }
        if (oct && session->screen == FZ_SCR_EDIT && fz_edit_row_numeric(session->edit_tab, session->row[session->edit_tab])) {
            fz_session_numeric_open(session, session->num_min, session->num_max);
            emit(out, FZ_CMD_CONFIRM, 1, action->at_us);
            return;
        }
        emit(out, FZ_CMD_CONFIRM, 0, action->at_us);
        return;
    }

    if (action->button == FZ_BTN_OCT_DOWN && action->kind == FZ_ACT_TAP) {
        if (session->screen == FZ_SCR_STEP || session->screen == FZ_SCR_PATTERNS) {
            if (session->page > 0)
                session->page--;
            emit(out, FZ_CMD_PAGE, session->page, action->at_us);
            return;
        }
        if (session->screen == FZ_SCR_HOME || session->screen == FZ_SCR_PICK_NOTE || session->screen == FZ_SCR_PICK_RANGE) {
            if (session->octave > -4)
                session->octave--;
            emit(out, FZ_CMD_OCTAVE_DOWN, session->octave, action->at_us);
        }
        return;
    }

    if (action->button == FZ_BTN_HOME && action->kind == FZ_ACT_TAP) {
        if (modal) {
            leave_modal(session);
            emit(out, FZ_CMD_CANCEL, 1, action->at_us);
            return;
        }
        session->gesture = 0;
        session->screen = FZ_SCR_HOME;
        emit(out, FZ_CMD_HOME, 0, action->at_us);
        return;
    }

    if (modal)
        return;

    if (action->button == FZ_BTN_EDIT && action->kind == FZ_ACT_HOLD) {
        int tab = session->edit_tab;
        session->back = session->screen;
        session->screen = FZ_SCR_NAMING;
        session->edit_tab = tab;
        emit(out, FZ_CMD_NAMING, tab, action->at_us);
        return;
    }
    if (action->button == FZ_BTN_EDIT && action->kind == FZ_ACT_TAP) {
        if (session->screen != FZ_SCR_EDIT) {
            session->screen = FZ_SCR_EDIT;
        } else {
            session->edit_tab = (session->edit_tab + 1) % FZ_TAB_COUNT;
            session->gesture = 0;
        }
        emit(out, FZ_CMD_EDIT_TAB, session->edit_tab, action->at_us);
        return;
    }
    if (action->button == FZ_BTN_SEQ && action->kind == FZ_ACT_TAP) {
        if (session->screen == FZ_SCR_STEP)
            session->screen = FZ_SCR_PATTERNS;
        else if (session->screen == FZ_SCR_PATTERNS)
            session->screen = FZ_SCR_SONG;
        else if (session->screen == FZ_SCR_SONG)
            session->screen = FZ_SCR_STEP;
        else
            session->screen = FZ_SCR_STEP;
        session->gesture = 0;
        emit(out, FZ_CMD_PAGE, (int)session->screen, action->at_us);
        return;
    }
    if (action->button == FZ_BTN_GLO && action->kind == FZ_ACT_TAP) {
        if (session->screen != FZ_SCR_MIXER) {
            session->screen = FZ_SCR_MIXER;
            session->send_slot = 0;
        } else {
            session->send_slot = (session->send_slot + 1) % 4;
        }
        emit(out, FZ_CMD_PAGE, session->send_slot, action->at_us);
        return;
    }
    if (action->button == FZ_BTN_FX && action->kind == FZ_ACT_TAP) {
        if (session->screen != FZ_SCR_FX)
            session->screen = FZ_SCR_FX;
        else
            session->fx_slot = (session->fx_slot + 1) % 4;
        emit(out, FZ_CMD_PAGE, session->fx_slot, action->at_us);
        return;
    }
    if (action->button == FZ_BTN_PLAY && action->kind == FZ_ACT_TAP) {
        emit(out, FZ_CMD_PLAY, 0, action->at_us);
        return;
    }
    if (action->button == FZ_BTN_REC && action->kind == FZ_ACT_TAP) {
        emit(out, FZ_CMD_REC_TAP, 0, action->at_us);
        return;
    }
    if (action->button == FZ_BTN_REC && action->kind == FZ_ACT_HOLD) {
        emit(out, FZ_CMD_REC_HOLD, 0, action->at_us);
        return;
    }
    if (action->button == FZ_BTN_SAVE && action->kind == FZ_ACT_TAP)
        emit(out, FZ_CMD_SAVE, 0, action->at_us);
}

void fz_session_did(FzSession *session, int undo_kind)
{
    if (session)
        session->undo_kind = undo_kind;
}

FzStatus fz_session_undo(FzSession *session, FzProject *project, FzSeq *seq)
{
    if (session && session->undo_kind == FZ_UNDO_SEQ && seq && seq->undo_n > 0)
        return fz_seq_undo(seq);
    if (!project)
        return FZ_ERR_STATE;
    return fz_project_undo(project);
}

void fz_session_apply(FzSession *session, FzProject *project, FzCapture *capture, FzSeq *seq, const FzCommand *command)
{
    if (!session || !command)
        return;
    if (command->kind == FZ_CMD_CAPTURE_FINISH && capture)
        fz_capture_oct_plus(capture);
    else if (command->kind == FZ_CMD_CAPTURE_UNDO && capture)
        fz_capture_undo_last(capture);
    else if (command->kind == FZ_CMD_UNDO)
        fz_session_undo(session, project, seq);
    else if (command->kind == FZ_CMD_PLAY && seq)
        fz_seq_play(seq, !seq->playing);
    else if (command->kind == FZ_CMD_CONFIRM && session->screen == FZ_SCR_NUMERIC && project) {
        int value = 0;
        if (fz_session_numeric_take(session, &value) == FZ_OK)
            fz_edit_apply_number(session, project, value);
    } else if (command->kind == FZ_CMD_REC_TAP && seq && project) {
        int pattern = seq->current[0] >= 0 ? seq->current[0] : 0;
        fz_seq_rec_tap(seq, project, pattern, seq->frame);
    } else if (command->kind == FZ_CMD_REC_HOLD) {
        /* The history ring is owned by the caller. Retrospective import is
         * fz_seq_capture_history, not this router. */
        (void)seq;
        (void)project;
    }
}

FzStatus fz_session_name_key(FzSession *session, FzProject *project, int key)
{
    FzTrack *track;
    FzStatus st;
    if (!session || session->screen != FZ_SCR_NAMING)
        return FZ_ERR_STATE;
    if (key < 0 || key >= FZ_NAME_CATEGORIES)
        return FZ_ERR_RANGE;
    if (!project || project->selected_track < 0 || project->selected_track >= FZ_MAX_TRACKS)
        return FZ_ERR_BAD_ID;
    track = &project->track[project->selected_track];
    if (!track->alive)
        return FZ_ERR_BAD_ID;
    st = fz_project_rename_category(project, track->instrument, fz_name_category(key));
    if (st != FZ_OK)
        return st;
    session->screen = session->back == FZ_SCR_NAMING ? FZ_SCR_EDIT : session->back;
    return FZ_OK;
}

void fz_session_numeric_open(FzSession *session, int min_value, int max_value)
{
    if (!session)
        return;
    if (session->screen != FZ_SCR_NUMERIC)
        session->back = session->screen;
    session->screen = FZ_SCR_NUMERIC;
    session->num_min = min_value;
    session->num_max = max_value;
    session->numeric_n = 0;
    session->numeric[0] = 0;
    session->numeric_neg = 0;
}

FzStatus fz_session_numeric_key(FzSession *session, int key)
{
    if (!session || session->screen != FZ_SCR_NUMERIC)
        return FZ_ERR_STATE;
    if (key == -1) {
        if (session->numeric_n > 0)
            session->numeric[--session->numeric_n] = 0;
        return FZ_OK;
    }
    if (key < 0 || key > 9)
        return FZ_ERR_RANGE;
    if (session->numeric_n >= 6)
        return FZ_ERR_RANGE;
    session->numeric[session->numeric_n++] = (char)('0' + key);
    session->numeric[session->numeric_n] = 0;
    return FZ_OK;
}

FzStatus fz_session_numeric_take(FzSession *session, int *out)
{
    int value = 0, i;
    if (!session || !out || session->screen != FZ_SCR_NUMERIC)
        return FZ_ERR_STATE;
    if (session->numeric_n <= 0)
        return FZ_ERR_STATE;
    for (i = 0; i < session->numeric_n; i++)
        value = value * 10 + (session->numeric[i] - '0');
    if (session->numeric_neg)
        value = -value;
    if (value < session->num_min || value > session->num_max)
        return FZ_ERR_RANGE;
    *out = value;
    session->screen = session->back;
    session->numeric_n = 0;
    return FZ_OK;
}

void fz_session_pick_open(FzSession *session, int range)
{
    if (!session)
        return;
    session->back = session->screen;
    session->screen = range ? FZ_SCR_PICK_RANGE : FZ_SCR_PICK_NOTE;
    session->pick_have = 0;
}

FzStatus fz_session_pick_key(FzSession *session, int note)
{
    int low, high;
    if (!session || note < 0 || note > 127)
        return FZ_ERR_ARGS;
    if (session->screen == FZ_SCR_PICK_NOTE) {
        session->pick_note = note;
        session->screen = session->back;
        return FZ_OK;
    }
    if (session->screen != FZ_SCR_PICK_RANGE)
        return FZ_ERR_STATE;
    if (!session->pick_have) {
        session->pick_low = note;
        session->pick_have = 1;
        return FZ_OK;
    }
    low = session->pick_low;
    high = note;
    if (high < low) {
        int swap = low;
        low = high;
        high = swap;
    }
    session->pick_low = low;
    session->pick_high = high;
    session->pick_have = 0;
    session->screen = session->back;
    return FZ_OK;
}

void fz_session_history_key(FzSession *session, FzHistory *history, FzHistEvent event)
{
    if (!session || !history || !fz_session_wants_play(session))
        return;
    event.source = FZ_SRC_KEYBOARD;
    fz_history_push(history, event);
}

static int clamp7(int v)
{
    if (v < 0)
        return 0;
    if (v > 127)
        return 127;
    return v;
}

static void mixer_turn(FzProject *project, int send_slot, int knob, int detents)
{
    FzTrack *track;
    if (!project)
        return;
    if (knob == 0) {
        fz_home_select_track(project, detents);
        return;
    }
    if (project->selected_track < 0 || project->selected_track >= FZ_MAX_TRACKS)
        return;
    track = &project->track[project->selected_track];
    if (!track->alive)
        return;
    if (knob == 1)
        track->volume = (int16_t)clamp7(track->volume + detents);
    else if (knob == 2)
        track->pan = (int16_t)clamp7(track->pan + detents);
    else if (knob == 3 && send_slot >= 0 && send_slot < 4)
        track->send[send_slot] = (int16_t)clamp7(track->send[send_slot] + detents);
}

void fz_session_knob(FzSession *session, FzProject *project, int knob, int detents)
{
    if (!session || fz_session_confirming(session) || session->screen == FZ_SCR_NAMING)
        return;
    if (session->screen == FZ_SCR_HOME)
        fz_home_turn(project, knob, detents);
    else if (session->screen == FZ_SCR_MIXER)
        mixer_turn(project, session->send_slot, knob, detents);
    else if (session->screen == FZ_SCR_EDIT)
        fz_edit_knob(session, project, knob, detents);
    else if (session->screen == FZ_SCR_SONG && project && knob == 1 && session->row[0] >= 0 &&
             session->row[0] < FZ_SONG_ROWS) {
        /* Repeats are edited on the sequencer song, not from this knob path. */
        (void)detents;
    }
}

FzStatus fz_session_grid_key(FzSession *session, FzSeq *seq, int key)
{
    int pattern;
    if (!session || !seq || key < 0 || key > 15)
        return FZ_ERR_ARGS;
    pattern = seq->current[0] >= 0 ? seq->current[0] : 0;
    if (session->screen == FZ_SCR_STEP) {
        session->undo_kind = FZ_UNDO_SEQ;
        return fz_seq_toggle_step(seq, pattern, session->page * 16 + key, session->step_note, 100);
    }
    if (session->screen == FZ_SCR_PATTERNS)
        return fz_seq_select_pattern(seq, 0, session->page * 16 + key, 0);
    return FZ_ERR_STATE;
}
