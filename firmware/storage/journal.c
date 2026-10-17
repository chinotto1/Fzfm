/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "journal.h"

#include <string.h>

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t fnv(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u;
    uint32_t i;
    for (i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static int record_at(const uint8_t *buf, uint32_t used, uint32_t at, uint32_t *next, int *complete)
{
    uint32_t len, sum, commit;
    if (at + 16u > used)
        return 0;
    if (rd32(buf + at) != FZ_JOURNAL_MAGIC)
        return 0;
    len = rd32(buf + at + 4);
    if (len > used || at + 16u + len > used)
        return 0;
    sum = rd32(buf + at + 8);
    commit = rd32(buf + at + 12);
    *complete = commit == FZ_JOURNAL_COMMIT && sum == fnv(buf + at + 16, len);
    *next = at + 16u + len;
    return 1;
}

FzStatus fz_journal_append(uint8_t *buf, uint32_t cap, uint32_t *used, const void *payload, uint32_t len)
{
    uint32_t at;
    if (!buf || !used || (!payload && len > 0))
        return FZ_ERR_ARGS;
    if (*used > cap || len > cap || *used + 16u + len > cap)
        return FZ_ERR_FULL;
    at = *used;
    wr32(buf + at, FZ_JOURNAL_MAGIC);
    wr32(buf + at + 4, len);
    wr32(buf + at + 8, fnv((const uint8_t *)payload, len));
    wr32(buf + at + 12, 0);
    if (len)
        memcpy(buf + at + 16, payload, len);
    wr32(buf + at + 12, FZ_JOURNAL_COMMIT);
    *used = at + 16u + len;
    return FZ_OK;
}

uint32_t fz_journal_recover(const uint8_t *buf, uint32_t used)
{
    uint32_t at = 0;
    uint32_t good = 0;
    if (!buf)
        return 0;
    while (at < used) {
        uint32_t next = 0;
        int complete = 0;
        if (!record_at(buf, used, at, &next, &complete) || !complete)
            break;
        good = next;
        at = next;
    }
    return good;
}

int fz_journal_count(const uint8_t *buf, uint32_t used)
{
    uint32_t at = 0;
    int n = 0;
    used = fz_journal_recover(buf, used);
    while (at < used) {
        uint32_t next = 0;
        int complete = 0;
        if (!record_at(buf, used, at, &next, &complete) || !complete)
            break;
        n++;
        at = next;
    }
    return n;
}

FzStatus fz_journal_payload(const uint8_t *buf, uint32_t used, int index, const uint8_t **payload, uint32_t *len)
{
    uint32_t at = 0;
    int n = 0;
    if (!buf || !payload || !len || index < 0)
        return FZ_ERR_ARGS;
    used = fz_journal_recover(buf, used);
    while (at < used) {
        uint32_t next = 0;
        int complete = 0;
        if (!record_at(buf, used, at, &next, &complete) || !complete)
            break;
        if (n == index) {
            *payload = buf + at + 16;
            *len = rd32(buf + at + 4);
            return FZ_OK;
        }
        n++;
        at = next;
    }
    return FZ_ERR_RANGE;
}
