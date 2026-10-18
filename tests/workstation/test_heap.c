/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "platform/fm_heap.h"
#include "test.h"
#include <stdalign.h>
#include <stdint.h>
#include <string.h>
int test_heap(void)
{
    alignas(max_align_t) uint8_t storage[8192];
    FmHeap heap = {0};
    CHECK(!fm_heap_init(&heap, storage + 1, sizeof(storage) - 1));
    CHECK(fm_heap_init(&heap, storage, sizeof(storage)));
    CHECK(!fm_heap_alloc(&heap, SIZE_MAX));
    CHECK(!fm_heap_alloc(&heap, 0));
    void *p[64] = {0};
    size_t sizes[64] = {0};
    uint32_t rng = 1234;
    for (unsigned n = 0; n < 5000; n++)
    {
        rng = rng * 1664525u + 1013904223u;
        unsigned i = rng % 64;
        if (p[i])
        {
            for (size_t j = 0; j < sizes[i]; j++)
                CHECK(((uint8_t *)p[i])[j] == i);
            if (rng & 128)
            {
                void *next = fm_heap_realloc(&heap, p[i], sizes[i] + 17);
                if (next)
                {
                    for (size_t j = 0; j < sizes[i]; j++)
                        CHECK(((uint8_t *)next)[j] == i);
                    p[i] = next;
                    sizes[i] += 17;
                    memset(p[i], i, sizes[i]);
                }
            }
            else
            {
                fm_heap_free(&heap, p[i]);
                p[i] = NULL;
            }
        }
        else
        {
            sizes[i] = 1 + (rng >> 16) % 257;
            p[i] = fm_heap_alloc(&heap, sizes[i]);
            if (p[i])
            {
                CHECK((uintptr_t)p[i] % alignof(max_align_t) == 0);
                memset(p[i], i, sizes[i]);
            }
        }
    }
    for (unsigned i = 0; i < 64; i++)
        fm_heap_free(&heap, p[i]);
    void *full = fm_heap_alloc(&heap, 8000);
    CHECK(full); /* all split blocks coalesce back to the original pool. */
    memset(full, 7, 8000);
    CHECK(!fm_heap_realloc(&heap, full, 9000));
    for (unsigned i = 0; i < 8000; i++)
        CHECK(((uint8_t *)full)[i] == 7);
    fm_heap_free(&heap, full);
    return 0;
}
