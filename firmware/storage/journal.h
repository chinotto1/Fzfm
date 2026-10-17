/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* R29: in-memory stand-in for a commit record. The commit word is written
 * last. A record without it is discarded and the previous complete record
 * stays. This does not program flash. */
#ifndef FZ_JOURNAL_H
#define FZ_JOURNAL_H

#include <stdint.h>

#include "core/types.h"

#define FZ_JOURNAL_MAGIC 0x465A4A52u /* FZJR */
#define FZ_JOURNAL_COMMIT 0x465A4F4Bu /* FZOK */

FzStatus fz_journal_append(uint8_t *buf, uint32_t cap, uint32_t *used, const void *payload, uint32_t len);
/* Drops a trailing record whose commit word or checksum is missing. */
uint32_t fz_journal_recover(const uint8_t *buf, uint32_t used);
int fz_journal_count(const uint8_t *buf, uint32_t used);
FzStatus fz_journal_payload(const uint8_t *buf, uint32_t used, int index, const uint8_t **payload, uint32_t *len);

#endif
