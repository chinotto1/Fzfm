/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R31: a small project bundle. A bad checksum or a short buffer leaves the
 * destination project untouched. Calibration is not stored here. */
#ifndef FZ_BUNDLE_H
#define FZ_BUNDLE_H

#include <stdint.h>

#include "core/project.h"

struct FzSeq;

FzStatus fz_bundle_write(const FzProject *project, uint8_t *dst, uint32_t cap, uint32_t *out_len);
FzStatus fz_bundle_read(FzProject *project, const uint8_t *src, uint32_t len);
/* Version 2. Samples, mixer, additive recipe, and the sequence. A bad file
 * leaves the destination project and sequence untouched. */
FzStatus fz_bundle_write_all(const FzProject *project, const struct FzSeq *seq, uint8_t *dst, uint32_t cap,
                             uint32_t *out_len);
FzStatus fz_bundle_read_all(FzProject *project, struct FzSeq *seq, const uint8_t *src, uint32_t len);

#endif
