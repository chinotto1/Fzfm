/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#ifndef FZ_FONT_H
#define FZ_FONT_H

#include <stdint.h>

/* 8x8 glyphs, bit 7 is the left pixel. Unknown characters are blank. */
void fz_font_rows(char c, uint8_t rows[8]);

#endif
