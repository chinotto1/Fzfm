/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FM_HEAP_H
#define FM_HEAP_H
#include <stdbool.h>
#include <stddef.h>
typedef struct
{
    void *first;
} FmHeap;
/* Single-owner non-RT heap, bounded by supplied aligned storage. */
bool fm_heap_init(FmHeap *, void *, size_t);
void *fm_heap_alloc(FmHeap *, size_t);
void fm_heap_free(FmHeap *, void *);
void *fm_heap_realloc(FmHeap *, void *, size_t);
#endif
