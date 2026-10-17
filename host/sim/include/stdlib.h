/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FZ_SIM_STDLIB_H
#define FZ_SIM_STDLIB_H

#include <stddef.h>

void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));

#endif
