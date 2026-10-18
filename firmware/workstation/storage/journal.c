/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "storage.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Host model only. The target adapter must implement actual flash geometry,
 * barriers, safe execution placement, and erase/program failure handling. */
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(uint8_t *p, uint32_t n)
{
    for (unsigned i = 0; i < 4; i++)
        p[i] = (uint8_t)(n >> (8 * i));
}
static char *slot_path(const char *path, unsigned slot)
{
    size_t n = strlen(path);
    if (n > SIZE_MAX - 3)
        return NULL;
    char *s = malloc(n + 3);
    if (s)
        snprintf(s, n + 3, "%s.%u", path, slot);
    return s;
}
static FmResult read_slot(const char *path, unsigned slot, uint8_t **bytes, size_t *length,
                          uint32_t *generation, size_t max_bytes)
{
    char *s = slot_path(path, slot);
    if (!s)
        return FM_ERR_MEMORY;
    FILE *f = fopen(s, "rb");
    free(s);
    if (!f)
        return FM_ERR_NOT_FOUND;
    struct stat st;
    if (fstat(fileno(f), &st) || st.st_size < 36 || (uintmax_t)st.st_size > max_bytes ||
        (uintmax_t)st.st_size > SIZE_MAX)
    {
        fclose(f);
        return FM_ERR_FORMAT;
    }
    size_t n = (size_t)st.st_size;
    uint8_t *b = malloc(n);
    if (!b)
    {
        fclose(f);
        return FM_ERR_MEMORY;
    }
    bool ok = fread(b, 1, n, f) == n;
    if (fclose(f))
        ok = false;
    if (!ok || memcmp(b + n - 12, "CMIT", 4) || get32(b + n - 8) != get32(b + 16) ||
        get32(b + n - 4) != fm_crc32(b, n - 12))
    {
        free(b);
        return FM_ERR_FORMAT;
    }
    *bytes = b;
    *length = n - 12;
    *generation = get32(b + 16);
    return FM_OK;
}
static size_t maximum_backup(FmProject *p)
{
    size_t n = 1024 * 1024;
    for (unsigned i = 0; i < FM_MEM_COUNT; i++)
    {
        if (p->budget.limit[i] > SIZE_MAX - n)
            return SIZE_MAX;
        n += p->budget.limit[i];
    }
    return n;
}
FmResult fm_store_load(FmProject *p, const char *path)
{
    if (!p || !path || !*path)
        return FM_ERR_ARGUMENT;
    uint8_t *b[2] = {NULL, NULL};
    size_t n[2] = {0, 0};
    uint32_t gen[2] = {0, 0};
    FmResult r[2];
    for (unsigned i = 0; i < 2; i++)
        r[i] = read_slot(path, i, &b[i], &n[i], &gen[i], maximum_backup(p));
    unsigned first = (r[1] == FM_OK && (r[0] != FM_OK || gen[1] > gen[0])) ? 1 : 0;
    FmResult result = FM_ERR_NOT_FOUND;
    for (unsigned k = 0; k < 2; k++)
    {
        unsigned i = k ? 1 - first : first;
        if (r[i] == FM_OK)
        {
            result = fm_project_decode(p, b[i], n[i]);
            if (result == FM_OK)
                break;
        }
        else if (r[i] != FM_ERR_NOT_FOUND)
            result = r[i];
    }
    free(b[0]);
    free(b[1]);
    return result;
}
static bool step(size_t *counter, size_t fail)
{
    return (*counter)++ != fail;
}
static int sync_parent(const char *path)
{
    const char *slash = strrchr(path, '/');
    char *dir = NULL;
    if (slash)
    {
        size_t n = (size_t)(slash - path);
        if (!n)
            n = 1;
        dir = malloc(n + 1);
        if (!dir)
            return -1;
        memcpy(dir, path, n);
        dir[n] = 0;
    }
    int fd = open(dir ? dir : ".", O_RDONLY);
    free(dir);
    if (fd < 0)
        return -1;
    int result = fsync(fd);
    if (close(fd))
        result = -1;
    return result;
}
FmResult fm_store_save_fault(FmProject *p, const char *path, size_t fail, size_t *steps)
{
    if (!p || !path || !*path)
        return FM_ERR_ARGUMENT;
    uint8_t *old[2] = {NULL, NULL};
    size_t sizes[2] = {0, 0};
    uint32_t gen[2] = {0, 0};
    FmResult existing[2];
    for (unsigned i = 0; i < 2; i++)
        existing[i] = read_slot(path, i, &old[i], &sizes[i], &gen[i], maximum_backup(p));
    /* Structural validity matters: a valid footer alone must not evict the last
     * decodable generation. Validate each candidate without publishing. */
    for (unsigned i = 0; i < 2; i++)
        if (existing[i] == FM_OK)
        {
            FmProject t;
            FmResult r = fm_project_init(&t, &p->budget);
            if (r == FM_OK)
            {
                r = fm_project_decode(&t, old[i], sizes[i]);
                fm_project_destroy(&t);
            }
            if (r != FM_OK)
                existing[i] = r;
        }
    unsigned latest = (existing[1] == FM_OK && (existing[0] != FM_OK || gen[1] > gen[0])) ? 1 : 0;
    uint32_t last = existing[latest] == FM_OK ? gen[latest] : p->generation;
    free(old[0]);
    free(old[1]);
    if (last == UINT32_MAX)
        return FM_ERR_RANGE;
    unsigned target = existing[latest] == FM_OK ? 1 - latest : 0;
    uint32_t original = p->generation;
    p->generation = last + 1;
    uint8_t *b = NULL;
    size_t n = 0;
    FmResult result = fm_project_encode(p, &b, &n);
    p->generation = original;
    if (result != FM_OK)
        return result;
    char *s = slot_path(path, target);
    if (!s)
    {
        free(b);
        return FM_ERR_MEMORY;
    }
    FILE *f = NULL;
    size_t count = 0;
    result = FM_ERR_STORAGE;
    if (!step(&count, fail))
        goto done;
    f = fopen(s, "wb+");
    if (!f)
        goto done;
    for (size_t offset = 0; offset < n;)
    {
        size_t amount = n - offset;
        if (amount > 256)
            amount = 256;
        if (!step(&count, fail) || fwrite(b + offset, 1, amount, f) != amount)
            goto done;
        offset += amount;
    }
    if (!step(&count, fail) || fflush(f) || fsync(fileno(f)))
        goto done;
    if (!step(&count, fail) || fseek(f, 0, SEEK_SET))
        goto done;
    {
        uint8_t verify[256];
        for (size_t offset = 0; offset < n;)
        {
            size_t amount = n - offset;
            if (amount > sizeof verify)
                amount = sizeof verify;
            if (!step(&count, fail) || fread(verify, 1, amount, f) != amount ||
                memcmp(b + offset, verify, amount))
                goto done;
            offset += amount;
        }
    }
    if (!step(&count, fail) || fseek(f, 0, SEEK_END))
        goto done;
    {
        uint8_t marker[12];
        memcpy(marker, "CMIT", 4);
        put32(marker + 4, last + 1);
        put32(marker + 8, fm_crc32(b, n));
        /* Each marker byte is a distinct power-loss boundary. */
        for (unsigned j = 0; j < sizeof marker; j++)
            if (!step(&count, fail) || fwrite(marker + j, 1, 1, f) != 1)
                goto done;
    }
    if (!step(&count, fail) || fflush(f) || fsync(fileno(f)))
        goto done;
    if (!step(&count, fail) || sync_parent(path))
        goto done;
    p->generation = last + 1;
    p->dirty = false;
    p->volatile_audio = false;
    result = FM_OK;
done:
    if (f && fclose(f) && result == FM_OK)
        result = FM_ERR_STORAGE;
    if (steps)
        *steps = count;
    free(s);
    free(b);
    return result;
}
FmResult fm_store_save(FmProject *p, const char *path)
{
    return fm_store_save_fault(p, path, SIZE_MAX, NULL);
}
