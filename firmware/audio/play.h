/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#ifndef FZ_PLAY_H
#define FZ_PLAY_H

#include "core/project.h"
#include "audio/engine.h"

struct FzFxRack;

int fz_performance_note(FzEngine *engine, FzProject *project, int note, int velocity, int down);
/* Enables the selected track's filter, then renders. Direct fz_engine_render does not. */
void fz_performance_render(FzEngine *engine, const FzProject *project, int16_t *interleaved_lr, uint32_t frames);
/* Same path, and mixes the rack's wet return. A null rack matches fz_performance_render. */
void fz_performance_render_sends(FzEngine *engine, const FzProject *project, struct FzFxRack *fx,
                                 int16_t *interleaved_lr, uint32_t frames);

#endif
