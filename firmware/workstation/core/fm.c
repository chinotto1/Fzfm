/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fm.h"
#include <math.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* All non-RT allocations are budgeted and owned by the project. Retired event
 * buffers/assets remain budgeted while undo/voices may refer to them. */
typedef union Allocation Allocation;
union Allocation
{
    struct
    {
        Allocation *next, *prev;
        size_t bytes;
        FmMemoryKind kind;
    } h;
    max_align_t align;
};
#ifndef FM_UNDO_DEPTH
#define FM_UNDO_DEPTH 8u
#endif
#ifndef FM_UNDO_PATCHES
#define FM_UNDO_PATCHES 64u
#endif
#define UNDO_DEPTH FM_UNDO_DEPTH
#define UNDO_PATCHES FM_UNDO_PATCHES
typedef struct
{
    void *target, *before, *after;
    size_t bytes;
} Patch;
typedef struct
{
    Patch patches[UNDO_PATCHES];
    unsigned count;
    FmId before_assets[FM_OBJECT_LIMIT], after_assets[FM_OBJECT_LIMIT];
    uint16_t before_count, after_count;
} Transaction;
struct FmUndo
{
    Transaction history[UNDO_DEPTH], pending;
    unsigned count, cursor;
    bool editing;
    uint64_t serial;
};

const char *fm_result_string(FmResult r)
{
    static const char *const text[] = {"OK",
                                       "INVALID ARGUMENT",
                                       "WORKING RAM FULL",
                                       "SAMPLE STORAGE FULL",
                                       "INVALID FORMAT",
                                       "OUT OF RANGE",
                                       "BUSY",
                                       "NOT FOUND",
                                       "UNSUPPORTED",
                                       "CANCELLED"};
    return (unsigned)r < sizeof(text) / sizeof(text[0]) ? text[r] : "UNKNOWN ERROR";
}
FmBudget fm_budget_host(void)
{
    FmBudget b = {{4u * 1024u * 1024u, 16u * 1024u * 1024u, 4u * 1024u * 1024u, 2u * 1024u * 1024u,
                   4u * 1024u * 1024u},
                  {0},
                  {0}};
    return b;
}
void *fm_alloc(FmProject *p, FmMemoryKind kind, size_t n)
{
    if (!p || kind >= FM_MEM_COUNT || !n || n > SIZE_MAX - sizeof(Allocation) ||
        p->budget.used[kind] > p->budget.limit[kind] ||
        n > p->budget.limit[kind] - p->budget.used[kind])
        return NULL;
    Allocation *a = calloc(1, sizeof(*a) + n);
    if (!a)
        return NULL;
    a->h.bytes = n;
    a->h.kind = kind;
    a->h.next = p->allocations;
    if (a->h.next)
        a->h.next->h.prev = a;
    p->allocations = a;
    p->budget.used[kind] += n;
    if (p->budget.used[kind] > p->budget.peak[kind])
        p->budget.peak[kind] = p->budget.used[kind];
    return a + 1;
}
void fm_dealloc(FmProject *p, FmMemoryKind kind, void *ptr, size_t bytes)
{
    (void)bytes;
    if (!p || !ptr)
        return;
    Allocation *a = (Allocation *)ptr - 1;
    if (a->h.kind != kind)
        return;
    if (a->h.prev)
        a->h.prev->h.next = a->h.next;
    else
        p->allocations = a->h.next;
    if (a->h.next)
        a->h.next->h.prev = a->h.prev;
    p->budget.used[kind] -= a->h.bytes;
    free(a);
}
FmResult fm_project_init(FmProject *p, const FmBudget *b)
{
    if (!p)
        return FM_ERR_ARGUMENT;
    memset(p, 0, sizeof(*p));
    p->budget = b ? *b : fm_budget_host();
    memset(p->budget.used, 0, sizeof(p->budget.used));
    memset(p->budget.peak, 0, sizeof(p->budget.peak));
    p->next_id = 1;
    p->bpm = 120;
    p->beats_per_bar = 4;
    p->undo = fm_alloc(p, FM_MEM_UNDO, sizeof(*p->undo));
    if (!p->undo)
        return FM_ERR_MEMORY;
    for (unsigned i = 0; i < FM_FX_SLOTS; i++)
    {
        p->fx[i].type = (FmFxType)i;
        p->fx[i].enabled = true;
        p->fx[i].gain = 0.35f;
        p->fx[i].parameter[0] = i == 0 ? 0.25f : 0.5f;
        p->fx[i].parameter[1] = 0.35f;
        p->fx[i].parameter[2] = 0.5f;
        p->fx[i].parameter[3] = 0.5f;
    }
    return FM_OK;
}
void fm_project_destroy(FmProject *p)
{
    if (!p)
        return;
    Allocation *a = p->allocations;
    while (a)
    {
        Allocation *next = a->h.next;
        free(a);
        a = next;
    }
    memset(p, 0, sizeof(*p));
}
#define FIND(name, field, type)                                                                    \
    type *name(FmProject *p, FmId id)                                                              \
    {                                                                                              \
        if (!p || !id)                                                                             \
            return NULL;                                                                           \
        for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)                                             \
            if (p->field[i] && p->field[i]->id == id)                                              \
                return p->field[i];                                                                \
        return NULL;                                                                               \
    }
FIND(fm_asset, assets, FmAsset)
FIND(fm_instrument, instruments, FmInstrument)
FIND(fm_track, tracks, FmTrack)
FIND(fm_pattern, patterns, FmPattern)
FmRegion *fm_region(FmProject *p, FmId id)
{
    if (!p || !id)
        return NULL;
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (p->instruments[i])
            for (unsigned j = 0; j < FM_ANCHORS; j++)
                if (p->instruments[i]->regions[j].id == id)
                    return &p->instruments[i]->regions[j];
    return NULL;
}
uint32_t fm_crc32(const void *data, size_t size)
{
    const uint8_t *b = data;
    uint32_t c = UINT32_MAX;
    for (size_t i = 0; i < size; i++)
    {
        c ^= b[i];
        for (unsigned bit = 0; bit < 8; bit++)
            c = (c >> 1) ^ ((0u - (c & 1u)) & 0xedb88320u);
    }
    return ~c;
}
static unsigned free_slot(void *const *list)
{
    unsigned i = 0;
    while (i < FM_OBJECT_LIMIT && list[i])
        i++;
    return i;
}
FmResult fm_asset_create(FmProject *p, const int16_t *pcm, uint32_t frames, uint8_t channels,
                         uint32_t rate, bool cycle, FmSource source, FmId *out)
{
    if (!p || !pcm || !frames || !out || (channels != 1 && channels != 2) || rate < 1000 ||
        rate > 192000 || source > FM_SOURCE_RENDER)
        return FM_ERR_ARGUMENT;
    if (p->next_id == UINT32_MAX)
        return FM_ERR_RANGE;
    unsigned slot = free_slot((void *const *)p->assets);
    if (slot == FM_OBJECT_LIMIT)
        return FM_ERR_STORAGE;
    if (frames > SIZE_MAX / channels / sizeof(int16_t))
        return FM_ERR_RANGE;
    size_t bytes = (size_t)frames * channels * sizeof(int16_t);
    FmAsset *a = fm_alloc(p, FM_MEM_METADATA, sizeof(*a));
    if (!a)
        return FM_ERR_MEMORY;
    a->pcm = fm_alloc(p, FM_MEM_AUDIO, bytes);
    if (!a->pcm)
    {
        fm_dealloc(p, FM_MEM_METADATA, a, sizeof(*a));
        return FM_ERR_STORAGE;
    }
    memcpy(a->pcm, pcm, bytes);
    a->id = p->next_id++;
    a->frames = frames;
    a->channels = channels;
    a->rate = rate;
    a->cycle = cycle;
    a->source = source;
    a->checksum = fm_crc32(pcm, bytes);
    p->assets[slot] = a;
    p->asset_count++;
    p->dirty = true;
    p->volatile_audio = true;
    *out = a->id;
    return FM_OK;
}
FmResult fm_asset_discard(FmProject *p, FmId id)
{
    FmAsset *a = fm_asset(p, id);
    if (!a)
        return FM_ERR_NOT_FOUND;
    if (a->published)
        return FM_ERR_BUSY;
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (p->instruments[i])
            for (unsigned j = 0; j < FM_ANCHORS; j++)
                if (p->instruments[i]->regions[j].asset == id)
                    return FM_ERR_BUSY;
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (p->patterns[i] && p->patterns[i]->frozen_asset == id)
            return FM_ERR_BUSY;
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (p->assets[i] == a)
            p->assets[i] = NULL;
    fm_dealloc(p, FM_MEM_AUDIO, a->pcm, (size_t)a->frames * a->channels * 2);
    fm_dealloc(p, FM_MEM_METADATA, a, sizeof(*a));
    p->asset_count--;
    return FM_OK;
}
FmEnvelope fm_envelope_default(void)
{
    FmEnvelope e = {0};
    e.count = 3;
    e.sustain = 1;
    e.release = 2;
    e.velocity = 1;
    e.stage[0] = (FmEnvStage){1, 1};
    e.stage[1] = (FmEnvStage){5, 1};
    e.stage[2] = (FmEnvStage){30, 0};
    return e;
}
static const char *const categories[] = {"DRUMS", "SYNTH", "PAD",  "LEAD", "FX",
                                         "BASS",  "KEYS",  "PERC", "LOOP"};
FmResult fm_name_category(FmProject *p, FmId id, uint8_t category)
{
    FmInstrument *in = fm_instrument(p, id);
    if (!in || category >= 9)
        return FM_ERR_ARGUMENT;
    char name[FM_NAME_LENGTH] = {0};
    for (unsigned n = 1; n <= FM_OBJECT_LIMIT + 1; n++)
    {
        snprintf(name, sizeof(name), "%s %u", categories[category], n);
        bool exists = false;
        for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
            if (p->instruments[i] && p->instruments[i] != in &&
                !strcmp(p->instruments[i]->name, name))
                exists = true;
        if (!exists)
        {
            memcpy(in->name, name, sizeof(in->name));
            p->dirty = true;
            return FM_OK;
        }
    }
    return FM_ERR_RANGE;
}
FmResult fm_pattern_create(FmProject *p, FmId tid, uint8_t slot, FmId *out)
{
    FmTrack *t = fm_track(p, tid);
    if (!t || slot >= FM_PATTERN_SLOTS || !out)
        return FM_ERR_ARGUMENT;
    if (t->patterns[slot])
    {
        *out = t->patterns[slot];
        return FM_OK;
    }
    unsigned index = free_slot((void *const *)p->patterns);
    if (index == FM_OBJECT_LIMIT || p->next_id == UINT32_MAX)
        return FM_ERR_MEMORY;
    FmPattern *pat = fm_alloc(p, FM_MEM_METADATA, sizeof(*pat));
    if (!pat)
        return FM_ERR_MEMORY;
    pat->id = p->next_id++;
    pat->track = tid;
    pat->length_ticks = FM_PPQN * 4;
    pat->rate = 1;
    p->patterns[index] = pat;
    p->pattern_count++;
    t->patterns[slot] = pat->id;
    *out = pat->id;
    p->dirty = true;
    return FM_OK;
}
FmResult fm_instrument_create(FmProject *p, FmInstrumentType type, FmId *out)
{
    if (!p || !out || type > FM_SAMPLE || p->next_id > UINT32_MAX - 3)
        return FM_ERR_ARGUMENT;
    unsigned is = free_slot((void *const *)p->instruments),
             ts = free_slot((void *const *)p->tracks), ps = free_slot((void *const *)p->patterns);
    if (is == FM_OBJECT_LIMIT || ts == FM_OBJECT_LIMIT || ps == FM_OBJECT_LIMIT)
        return FM_ERR_MEMORY;
    FmInstrument *in = fm_alloc(p, FM_MEM_METADATA, sizeof(*in));
    if (!in)
        return FM_ERR_MEMORY;
    FmTrack *t = fm_alloc(p, FM_MEM_METADATA, sizeof(*t));
    if (!t)
    {
        fm_dealloc(p, FM_MEM_METADATA, in, sizeof(*in));
        return FM_ERR_MEMORY;
    }
    FmPattern *pat = fm_alloc(p, FM_MEM_METADATA, sizeof(*pat));
    if (!pat)
    {
        fm_dealloc(p, FM_MEM_METADATA, t, sizeof(*t));
        fm_dealloc(p, FM_MEM_METADATA, in, sizeof(*in));
        return FM_ERR_MEMORY;
    }
    in->id = p->next_id++;
    in->type = type;
    in->base_note = 36;
    t->id = p->next_id++;
    t->instrument = in->id;
    t->volume = 0.8f;
    t->arp = fm_arp_default();
    t->cutoff = 1;
    t->queued_pattern = -1;
    t->midi_channel = (uint8_t)(p->track_count % 16);
    pat->id = p->next_id++;
    pat->track = t->id;
    pat->length_ticks = FM_PPQN * 4;
    pat->rate = 1;
    t->patterns[0] = pat->id;
    p->instruments[is] = in;
    p->tracks[ts] = t;
    p->patterns[ps] = pat;
    p->instrument_count++;
    p->track_count++;
    p->pattern_count++;
    p->selected_track = t->id;
    (void)fm_name_category(p, in->id, type == FM_MULTI_SAMPLE ? 0 : 1);
    *out = t->id;
    p->dirty = true;
    return FM_OK;
}
void fm_map_automatic(FmInstrument *in)
{
    if (!in)
        return;
    if (in->type == FM_MULTI_SAMPLE)
    {
        for (unsigned i = 0; i < FM_KIT_SLOTS; i++)
            if (in->regions[i].id && !in->regions[i].manual_zone)
                in->regions[i].low = in->regions[i].high = (uint8_t)(in->base_note + i);
        return;
    }
    for (unsigned i = 0; i < FM_ANCHORS; i++)
        if (in->regions[i].id && !in->regions[i].manual_zone)
        {
            FmRegion *r = &in->regions[i];
            int low = 0, high = 127;
            for (unsigned j = 0; j < FM_ANCHORS; j++)
                if (j != i && in->regions[j].id)
                {
                    int root = in->regions[j].root;
                    if (root < r->root)
                    {
                        int bound = (root + r->root) / 2 + 1;
                        if (bound > low)
                            low = bound;
                    }
                    if (root > r->root)
                    {
                        int bound = (root + r->root) / 2;
                        if (bound < high)
                            high = bound;
                    }
                }
            r->low = (uint8_t)low;
            r->high = (uint8_t)high;
        }
}
FmRegion *fm_map_note(FmInstrument *in, uint8_t note)
{
    if (!in || note > 127)
        return NULL;
    for (unsigned i = 0; i < FM_ANCHORS; i++)
        if (in->regions[i].id && in->regions[i].manual_zone && note >= in->regions[i].low &&
            note <= in->regions[i].high)
            return &in->regions[i];
    for (unsigned i = 0; i < FM_ANCHORS; i++)
        if (in->regions[i].id && note >= in->regions[i].low && note <= in->regions[i].high)
            return &in->regions[i];
    return NULL;
}
FmResult fm_region_attach(FmProject *p, FmId iid, FmId aid, uint8_t root, int slot, FmId *out)
{
    FmInstrument *in = fm_instrument(p, iid);
    FmAsset *a = fm_asset(p, aid);
    if (!in || !a || !out || root > 127 || slot >= (int)FM_ANCHORS)
        return FM_ERR_ARGUMENT;
    if (slot < 0)
    {
        slot = 0;
        while (slot < (int)FM_ANCHORS && in->regions[slot].id)
            slot++;
    }
    if (slot == (int)FM_ANCHORS)
        return FM_ERR_MEMORY;
    if (in->regions[slot].id)
        return FM_ERR_BUSY;
    if (in->type == FM_SAMPLE)
        for (unsigned i = 0; i < FM_ANCHORS; i++)
            if (in->regions[i].id && in->regions[i].root == root)
                return FM_ERR_RANGE;
    if (p->next_id == UINT32_MAX)
        return FM_ERR_RANGE;
    FmRegion *r = &in->regions[slot];
    memset(r, 0, sizeof(*r));
    r->id = p->next_id++;
    r->asset = aid;
    r->end = a->frames;
    r->root = root;
    r->gain = 1;
    r->cutoff = 1;
    r->one_shot = in->type == FM_MULTI_SAMPLE;
    for (unsigned i = 0; i < FM_FX_SLOTS; i++)
        r->sends[i] = 1;
    r->amp = fm_envelope_default();
    r->filter = fm_envelope_default();
    a->published = true;
    in->count++;
    in->revision++;
    fm_map_automatic(in);
    *out = r->id;
    p->dirty = true;
    return FM_OK;
}
FmResult fm_pattern_append(FmProject *p, FmPattern *pat, const FmEvent *e)
{
    if (!p || !pat || !e || !isfinite(e->tick) || e->tick < 0 || e->note > 127 ||
        e->velocity > 127 || e->kind > FM_EVENT_BEND)
        return FM_ERR_ARGUMENT;
    if (pat->count == pat->capacity)
    {
        uint32_t cap = pat->capacity ? pat->capacity * 2 : 16;
        if (cap < pat->capacity || cap > 65536)
            return FM_ERR_MEMORY;
        FmEvent *events = fm_alloc(p, FM_MEM_EVENTS, (size_t)cap * sizeof(*events));
        if (!events)
            return FM_ERR_MEMORY;
        if (pat->count)
            memcpy(events, pat->events, (size_t)pat->count * sizeof(*events));
        pat->events = events;
        pat->capacity = cap; /* retired arrays remain charged for undo */
    }
    uint32_t at = pat->count;
    while (at && pat->events[at - 1].tick > e->tick)
    {
        pat->events[at] = pat->events[at - 1];
        at--;
    }
    pat->events[at] = *e;
    pat->count++;
    p->dirty = true;
    return FM_OK;
}
static void transaction_free(FmProject *p, Transaction *t)
{
    for (unsigned i = 0; i < t->count; i++)
    {
        fm_dealloc(p, FM_MEM_UNDO, t->patches[i].before, t->patches[i].bytes);
        fm_dealloc(p, FM_MEM_UNDO, t->patches[i].after, t->patches[i].bytes);
    }
    memset(t, 0, sizeof(*t));
}
static bool id_in(const FmId *ids, unsigned count, FmId id)
{
    for (unsigned i = 0; i < count; i++)
        if (ids[i] == id)
            return true;
    return false;
}
static void asset_ref(FmId *ids, uint16_t *count, FmId id)
{
    if (id && *count < FM_OBJECT_LIMIT && !id_in(ids, *count, id))
        ids[(*count)++] = id;
}
/* Snapshot actual dependencies, not every registry entry. Before/after sets
 * protect generic pointer-based transactions without scanning arbitrary bytes. */
static uint16_t asset_refs(FmProject *p, FmId *ids)
{
    uint16_t count = 0;
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
    {
        if (p->instruments[i])
            for (unsigned j = 0; j < FM_ANCHORS; j++)
                if (p->instruments[i]->regions[j].id)
                    asset_ref(ids, &count, p->instruments[i]->regions[j].asset);
        if (p->patterns[i])
            asset_ref(ids, &count, p->patterns[i]->frozen_asset);
        if (p->bake_recipes[i])
            asset_ref(ids, &count, p->bake_recipes[i]->original.asset);
    }
    return count;
}
size_t fm_assets_collect_cycles(FmProject *p, bool (*pinned)(void *, FmId), void *user)
{
    if (!p || !p->undo || p->undo->editing || !pinned)
        return 0;
    FmId live[FM_OBJECT_LIMIT];
    unsigned count = asset_refs(p, live);
    size_t freed = 0;
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
    {
        FmAsset *a = p->assets[i];
        if (!a || !a->cycle || !a->retired || id_in(live, count, a->id) || pinned(user, a->id))
            continue;
        bool retained = false;
        for (unsigned j = 0; j < p->undo->count && !retained; j++)
        {
            Transaction *t = &p->undo->history[j];
            retained = id_in(t->before_assets, t->before_count, a->id) ||
                       id_in(t->after_assets, t->after_count, a->id);
        }
        if (retained)
            continue;
        size_t bytes = (size_t)a->frames * a->channels * sizeof(int16_t);
        fm_dealloc(p, FM_MEM_AUDIO, a->pcm, bytes);
        fm_dealloc(p, FM_MEM_METADATA, a, sizeof(*a));
        p->assets[i] = NULL;
        p->asset_count--;
        freed += bytes;
    }
    return freed;
}
FmResult fm_edit_begin(FmProject *p)
{
    if (!p || !p->undo)
        return FM_ERR_ARGUMENT;
    if (p->undo->editing)
        return FM_ERR_BUSY;
    p->undo->editing = true;
    p->undo->pending.before_count = asset_refs(p, p->undo->pending.before_assets);
    return FM_OK;
}
FmResult fm_edit_watch(FmProject *p, void *target, size_t bytes)
{
    if (!p || !p->undo || !p->undo->editing || !target || !bytes)
        return FM_ERR_ARGUMENT;
    Transaction *t = &p->undo->pending;
    for (unsigned i = 0; i < t->count; i++)
        if (t->patches[i].target == target && t->patches[i].bytes == bytes)
            return FM_OK;
    if (t->count == UNDO_PATCHES)
        return FM_ERR_MEMORY;
    void *before = fm_alloc(p, FM_MEM_UNDO, bytes), *after = fm_alloc(p, FM_MEM_UNDO, bytes);
    if (!before || !after)
    {
        fm_dealloc(p, FM_MEM_UNDO, before, bytes);
        fm_dealloc(p, FM_MEM_UNDO, after, bytes);
        return FM_ERR_MEMORY;
    }
    memcpy(before, target, bytes);
    t->patches[t->count++] = (Patch){target, before, after, bytes};
    return FM_OK;
}
FmResult fm_edit_commit(FmProject *p)
{
    if (!p || !p->undo || !p->undo->editing)
        return FM_ERR_ARGUMENT;
    struct FmUndo *u = p->undo;
    Transaction *t = &u->pending;
    for (unsigned i = 0; i < t->count; i++)
        memcpy(t->patches[i].after, t->patches[i].target, t->patches[i].bytes);
    t->after_count = asset_refs(p, t->after_assets);
    while (u->count > u->cursor)
        transaction_free(p, &u->history[--u->count]);
    if (u->count == UNDO_DEPTH)
    {
        transaction_free(p, &u->history[0]);
        memmove(u->history, u->history + 1, (UNDO_DEPTH - 1) * sizeof(Transaction));
        u->count--;
    }
    if (t->count)
        u->history[u->count++] = *t;
    u->cursor = u->count;
    memset(t, 0, sizeof(*t));
    u->editing = false;
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
    {
        if (p->instruments[i])
            for (unsigned j = 0; j < FM_ANCHORS; j++)
            {
                FmAsset *asset = fm_asset(p, p->instruments[i]->regions[j].asset);
                if (asset)
                    asset->published = true;
            }
        if (p->patterns[i])
        {
            FmAsset *asset = fm_asset(p, p->patterns[i]->frozen_asset);
            if (asset)
                asset->published = true;
        }
    }
    p->dirty = true;
    u->serial++;
    return FM_OK;
}
void fm_edit_cancel(FmProject *p)
{
    if (!p || !p->undo || !p->undo->editing)
        return;
    Transaction *t = &p->undo->pending;
    for (unsigned i = t->count; i > 0; i--)
        memcpy(t->patches[i - 1].target, t->patches[i - 1].before, t->patches[i - 1].bytes);
    transaction_free(p, t);
    p->undo->editing = false;
}
FmResult fm_undo(FmProject *p)
{
    if (!p || !p->undo)
        return FM_ERR_ARGUMENT;
    struct FmUndo *u = p->undo;
    if (u->editing)
    {
        fm_edit_cancel(p);
        return FM_OK;
    }
    if (!u->cursor)
        return FM_ERR_NOT_FOUND;
    Transaction *t = &u->history[--u->cursor];
    for (unsigned i = t->count; i > 0; i--)
        memcpy(t->patches[i - 1].target, t->patches[i - 1].before, t->patches[i - 1].bytes);
    p->dirty = true;
    return FM_OK;
}
FmResult fm_redo(FmProject *p)
{
    if (!p || !p->undo)
        return FM_ERR_ARGUMENT;
    struct FmUndo *u = p->undo;
    if (u->editing)
        return FM_ERR_BUSY;
    if (u->cursor == u->count)
        return FM_ERR_NOT_FOUND;
    Transaction *t = &u->history[u->cursor++];
    for (unsigned i = 0; i < t->count; i++)
        memcpy(t->patches[i].target, t->patches[i].after, t->patches[i].bytes);
    p->dirty = true;
    return FM_OK;
}

uint64_t fm_edit_serial(FmProject *p)
{
    return p && p->undo ? p->undo->serial : 0;
}
FmResult fm_edit_coalesce(FmProject *p)
{
    if (!p || !p->undo || p->undo->editing)
        return FM_ERR_BUSY;
    struct FmUndo *u = p->undo;
    if (u->count < 2 || u->cursor != u->count)
        return FM_ERR_NOT_FOUND;
    Transaction *old = &u->history[u->count - 2], *now = &u->history[u->count - 1];
    if (old->count != now->count)
        return FM_ERR_ARGUMENT;
    for (unsigned i = 0; i < old->count; i++)
        if (old->patches[i].target != now->patches[i].target ||
            old->patches[i].bytes != now->patches[i].bytes)
            return FM_ERR_ARGUMENT;
    for (unsigned i = 0; i < old->count; i++)
        memcpy(old->patches[i].after, now->patches[i].after, old->patches[i].bytes);
    old->after_count = now->after_count;
    memcpy(old->after_assets, now->after_assets, sizeof(old->after_assets));
    transaction_free(p, now);
    u->count--;
    u->cursor = u->count;
    return FM_OK;
}
FmResult fm_remove_empty_instrument(FmProject *p, FmId id)
{
    FmTrack *t = fm_track(p, id);
    FmInstrument *in = t ? fm_instrument(p, t->instrument) : NULL;
    if (!in)
        return FM_ERR_NOT_FOUND;
    if (in->count)
        return FM_ERR_BUSY;
    for (unsigned i = 0; i < FM_PATTERN_SLOTS; i++)
    {
        FmPattern *pat = fm_pattern(p, t->patterns[i]);
        if (pat && pat->count)
            return FM_ERR_BUSY;
    }
    if (p->song_count)
        return FM_ERR_BUSY;
    FmResult r = fm_edit_begin(p);
    if (r != FM_OK)
        return r;
#define KEEP(x)                                                                                    \
    do                                                                                             \
    {                                                                                              \
        r = fm_edit_watch(p, &(x), sizeof(x));                                                     \
        if (r != FM_OK)                                                                            \
        {                                                                                          \
            fm_edit_cancel(p);                                                                     \
            return r;                                                                              \
        }                                                                                          \
    } while (0)
    KEEP(p->tracks);
    KEEP(p->instruments);
    KEEP(p->patterns);
    KEEP(p->track_count);
    KEEP(p->instrument_count);
    KEEP(p->pattern_count);
    KEEP(p->selected_track);
#undef KEEP
    unsigned write = 0;
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (p->patterns[i] && p->patterns[i]->track != id)
            p->patterns[write++] = p->patterns[i];
    p->pattern_count = (uint16_t)write;
    while (write < FM_OBJECT_LIMIT)
        p->patterns[write++] = NULL;
    write = 0;
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (p->tracks[i] && p->tracks[i] != t)
            p->tracks[write++] = p->tracks[i];
    p->track_count = (uint16_t)write;
    while (write < FM_OBJECT_LIMIT)
        p->tracks[write++] = NULL;
    write = 0;
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (p->instruments[i] && p->instruments[i] != in)
            p->instruments[write++] = p->instruments[i];
    p->instrument_count = (uint16_t)write;
    while (write < FM_OBJECT_LIMIT)
        p->instruments[write++] = NULL;
    if (p->selected_track == id)
        p->selected_track = p->track_count ? p->tracks[0]->id : 0;
    return fm_edit_commit(p);
}

/* Region metadata is removed atomically; immutable PCM/readers remain valid. */
FmResult fm_region_remove(FmProject *p, FmId id)
{
    if (!p)
        return FM_ERR_ARGUMENT;
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (p->instruments[i])
        {
            FmInstrument *in = p->instruments[i];
            for (unsigned j = 0; j < FM_ANCHORS; j++)
                if (in->regions[j].id == id && id)
                {
                    if (in->regions[j].baked_mask & 0x80)
                        return FM_ERR_BUSY;
                    FmResult r = fm_edit_begin(p);
                    if (r == FM_OK)
                        r = fm_edit_watch(p, in, sizeof(*in));
                    if (r != FM_OK)
                    {
                        fm_edit_cancel(p);
                        return r;
                    }
                    memset(&in->regions[j], 0, sizeof(in->regions[j]));
                    in->count--;
                    in->revision++;
                    fm_map_automatic(in);
                    return fm_edit_commit(p);
                }
        }
    return FM_ERR_NOT_FOUND;
}
