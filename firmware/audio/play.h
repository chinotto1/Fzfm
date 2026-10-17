/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#ifndef FZ_PLAY_H
#define FZ_PLAY_H

#include "core/project.h"
#include "audio/engine.h"

int fz_performance_note(FzEngine *engine, FzProject *project, int note, int velocity, int down);

#endif
