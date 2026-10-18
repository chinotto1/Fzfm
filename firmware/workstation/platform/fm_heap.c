/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fm_heap.h"
#include <stdalign.h>
#include <stdint.h>
#include <string.h>
typedef union Block Block;
union Block
{
    struct
    {
        size_t size;
        Block *next;
        bool free;
    } h;
    max_align_t alignment;
};
bool fm_heap_init(FmHeap *heap, void *buffer, size_t size)
{
    if (!heap || !buffer || (uintptr_t)buffer % alignof(Block) || size <= sizeof(Block))
        return false;
    Block *b = buffer;
    b->h.size = size - sizeof(Block);
    b->h.next = NULL;
    b->h.free = true;
    heap->first = b;
    return true;
}
void *fm_heap_alloc(FmHeap *heap, size_t size)
{
    size_t mask = alignof(Block) - 1;
    if (!heap || !size || size > SIZE_MAX - mask)
        return NULL;
    size = (size + mask) & ~mask;
    for (Block *b = heap->first; b; b = b->h.next)
    {
        if (!b->h.free || b->h.size < size)
            continue;
        if (b->h.size - size > sizeof(Block))
        {
            Block *tail = (Block *)((uint8_t *)(b + 1) + size);
            tail->h.size = b->h.size - size - sizeof(Block);
            tail->h.next = b->h.next;
            tail->h.free = true;
            b->h.next = tail;
            b->h.size = size;
        }
        b->h.free = false;
        return b + 1;
    }
    return NULL;
}
void fm_heap_free(FmHeap *heap, void *ptr)
{
    if (!heap || !ptr)
        return;
    Block *b = heap->first, *previous = NULL;
    while (b && b + 1 != ptr)
    {
        previous = b;
        b = b->h.next;
    }
    if (!b || b->h.free)
        return;
    b->h.free = true;
    if (b->h.next && b->h.next->h.free)
    {
        b->h.size += sizeof(Block) + b->h.next->h.size;
        b->h.next = b->h.next->h.next;
    }
    if (previous && previous->h.free)
    {
        previous->h.size += sizeof(Block) + b->h.size;
        previous->h.next = b->h.next;
    }
}
void *fm_heap_realloc(FmHeap *heap, void *ptr, size_t size)
{
    if (!ptr)
        return fm_heap_alloc(heap, size);
    if (!size)
    {
        fm_heap_free(heap, ptr);
        return NULL;
    }
    Block *b = heap ? heap->first : NULL;
    while (b && b + 1 != ptr)
        b = b->h.next;
    if (!b || b->h.free)
        return NULL;
    if (size <= b->h.size)
        return ptr;
    void *next = fm_heap_alloc(heap, size);
    if (!next)
        return NULL;
    memcpy(next, ptr, b->h.size);
    fm_heap_free(heap, ptr);
    return next;
}
