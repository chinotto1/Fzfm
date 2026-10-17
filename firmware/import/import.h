/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R26: optional import. FZTEST is a synthetic profile, not a commercial ROM.
 * Unknown sizes are rejected. A cycle's pitch comes from its point count;
 * the WAV rate is returned and is not a root note. */
#ifndef FZ_IMPORT_H
#define FZ_IMPORT_H

#include <stdint.h>

#include "core/project.h"

typedef struct {
    uint32_t count;
    uint32_t frames_each;
    uint32_t rate;
    uint8_t root0;
    int unsigned8; /* 1 = unsigned 8-bit, 0 = signed 8-bit */
} FzPcmImport;

FzStatus fz_import_rom(FzProject *project, FzId instrument, const char *profile, const uint8_t *data, uint32_t len);
FzStatus fz_import_pcm(FzProject *project, FzId instrument, const FzPcmImport *cfg, const uint8_t *data, uint32_t len);
FzStatus fz_import_wav_cycle(const uint8_t *wav, uint32_t len, int16_t *dst, uint32_t dst_cap, uint32_t *points,
                             uint32_t *file_rate);

#endif
