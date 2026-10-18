/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FM_STORAGE_H
#define FM_STORAGE_H
#include "../core/fm.h"
#define FM_FORMAT_VERSION 2u
/* Complete portable backups include every live/undo-retained audio dependency.
 * These APIs are non-real-time; decode publishes only after full validation. */
FmResult fm_project_validate(FmProject *);
FmResult fm_project_encode(FmProject *, uint8_t **bytes, size_t *length);
FmResult fm_project_decode(FmProject *, const uint8_t *bytes, size_t length);
/* Host durable two-generation journal. path is a prefix, not a device address. */
FmResult fm_store_save(FmProject *, const char *path);
FmResult fm_store_load(FmProject *, const char *path);
/* Fail immediately before step N; SIZE_MAX disables. Steps include all writes,
 * verification and durability barriers. For deterministic power-cut tests. */
FmResult fm_store_save_fault(FmProject *, const char *, size_t fail_before, size_t *steps);
#endif
