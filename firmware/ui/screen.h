/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* Draws the session onto the 240x240 panel. This is the host and browser
 * view. It is not a photograph of the device. */
#ifndef FZ_SCREEN_H
#define FZ_SCREEN_H

#include <stdint.h>

#include "core/project.h"
#include "sampler/capture.h"
#include "sequence/seq.h"
#include "ui/home.h"
#include "ui/session.h"

void fz_screen_draw(uint16_t *rgb565, const FzProject *project, const FzSession *session, const FzSeq *seq,
                    const FzCapture *capture);

#endif
