/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R21, R33: HOME at the panel's 240x240 RGB565 resolution. */
#ifndef FZ_HOME_H
#define FZ_HOME_H

#include <stdint.h>

#include "core/project.h"

#define FZ_LCD_W 240
#define FZ_LCD_H 240
#define FZ_HOME_ROW_H 16
#define FZ_HOME_LIST_Y 24
#define FZ_HOME_BAR_Y 208
#define FZ_HOME_BAR_MAX 48

#define FZ_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))

void fz_home_draw(uint16_t *rgb565, const FzProject *project);

#endif
