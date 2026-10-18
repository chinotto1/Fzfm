/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "storage.h"
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
_Static_assert(sizeof(float) == 4 && sizeof(double) == 8 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 &&
                   DBL_MANT_DIG == 53,
               "FM1P requires IEEE binary32/binary64");

/* R29/R31: explicit little-endian scalars, no pointers or struct padding. */
typedef struct
{
    uint8_t *out;
    const uint8_t *in;
    size_t size, pos;
    bool read;
    FmResult error;
} Codec;
static void scalar(Codec *c, uint64_t *v, unsigned n)
{
    if (c->error != FM_OK)
        return;
    if (n > SIZE_MAX - c->pos || (c->read && n > c->size - c->pos))
    {
        c->error = FM_ERR_FORMAT;
        return;
    }
    if (c->read)
    {
        *v = 0;
        for (unsigned j = 0; j < n; j++)
            *v |= (uint64_t)c->in[c->pos + j] << (8 * j);
    }
    else if (c->out)
        for (unsigned j = 0; j < n; j++)
            c->out[c->pos + j] = (uint8_t)(*v >> (8 * j));
    c->pos += n;
}
static void u8(Codec *c, uint8_t *v)
{
    uint64_t q = *v;
    scalar(c, &q, 1);
    *v = (uint8_t)q;
}
static void u16(Codec *c, uint16_t *v)
{
    uint64_t q = *v;
    scalar(c, &q, 2);
    *v = (uint16_t)q;
}
static void u32(Codec *c, uint32_t *v)
{
    uint64_t q = *v;
    scalar(c, &q, 4);
    *v = (uint32_t)q;
}
static void u64(Codec *c, uint64_t *v)
{
    scalar(c, v, 8);
}
static void boolean(Codec *c, bool *v)
{
    uint8_t q = *v ? 1 : 0;
    u8(c, &q);
    if (q > 1)
        c->error = FM_ERR_FORMAT;
    *v = q != 0;
}
static void f32(Codec *c, float *v)
{
    uint32_t q;
    memcpy(&q, v, 4);
    u32(c, &q);
    memcpy(v, &q, 4);
    if (!isfinite(*v))
        c->error = FM_ERR_FORMAT;
}
static void f64(Codec *c, double *v)
{
    uint64_t q;
    memcpy(&q, v, 8);
    u64(c, &q);
    memcpy(v, &q, 8);
    if (!isfinite(*v))
        c->error = FM_ERR_FORMAT;
}
static void enumeration(Codec *c, int *v)
{
    uint32_t q = (uint32_t)*v;
    u32(c, &q);
    if (q > INT_MAX)
        c->error = FM_ERR_FORMAT;
    *v = (int)q;
}
#define U8(x) u8(c, &(x))
#define U16(x) u16(c, &(x))
#define U32(x) u32(c, &(x))
#define U64(x) u64(c, &(x))
#define BOOL(x) boolean(c, &(x))
#define FLOAT(x) f32(c, &(x))
#define DOUBLE(x) f64(c, &(x))
#define ENUM(x)                                                                                    \
    do                                                                                             \
    {                                                                                              \
        int e_ = (int)(x);                                                                         \
        enumeration(c, &e_);                                                                       \
        (x) = e_;                                                                                  \
    } while (0)
static size_t record_begin(Codec *c)
{
    size_t start = c->pos;
    uint32_t n = 0;
    u32(c, &n);
    if (c->read)
    {
        if (n > c->size - c->pos)
            c->error = FM_ERR_FORMAT;
        return c->pos + n;
    }
    return start;
}
static void record_end(Codec *c, size_t mark)
{
    if (c->read)
    {
        if (c->pos != mark)
            c->error = FM_ERR_FORMAT;
    }
    else if (c->pos - mark - 4 > UINT32_MAX)
        c->error = FM_ERR_RANGE;
    else if (c->out)
    {
        uint32_t n = (uint32_t)(c->pos - mark - 4);
        for (unsigned j = 0; j < 4; j++)
            c->out[mark + j] = (uint8_t)(n >> (8 * j));
    }
}
static void envelope(Codec *c, FmEnvelope *e)
{
    U8(e->count);
    U8(e->sustain);
    U8(e->release);
    FLOAT(e->key_rate);
    FLOAT(e->velocity);
    for (unsigned j = 0; j < FM_ENV_STAGES; j++)
    {
        FLOAT(e->stage[j].milliseconds);
        FLOAT(e->stage[j].level);
    }
}
static void region(Codec *c, FmRegion *r)
{
    size_t m = record_begin(c);
    U32(r->id);
    U32(r->asset);
    U32(r->start);
    U32(r->end);
    U8(r->root);
    U8(r->low);
    U8(r->high);
    U8(r->choke);
    BOOL(r->manual_zone);
    BOOL(r->one_shot);
    BOOL(r->reverse);
    BOOL(r->loop_enabled);
    FLOAT(r->tuning);
    FLOAT(r->gain);
    FLOAT(r->pan);
    for (unsigned j = 0; j < FM_FX_SLOTS; j++)
        FLOAT(r->sends[j]);
    U8(r->baked_mask);
    FLOAT(r->cutoff);
    FLOAT(r->resonance);
    FLOAT(r->drive);
    FLOAT(r->filter_env);
    FLOAT(r->keytrack);
    FLOAT(r->lfo_rate);
    FLOAT(r->lfo_depth);
    U8(r->lfo_shape);
    U8(r->lfo_destination);
    envelope(c, &r->amp);
    envelope(c, &r->filter);
    U8(r->loop_count);
    for (unsigned j = 0; j < FM_LOOP_SECTIONS; j++)
    {
        FmLoopSection *l = &r->loops[j];
        U32(l->start);
        U32(l->end);
        U32(l->crossfade);
        U16(l->repeats);
        U8(l->action);
        U8(l->jump);
        BOOL(l->until_release);
    }
    record_end(c, m);
}
static void event(Codec *c, FmEvent *e)
{
    DOUBLE(e->tick);
    U64(e->frame);
    U32(e->track);
    U32(e->identity);
    ENUM(e->kind);
    ENUM(e->origin);
    U8(e->note);
    U8(e->velocity);
    U8(e->channel);
    U8(e->parameter);
    FLOAT(e->value);
    FLOAT(e->probability);
}
static uint32_t pcm_crc(const int16_t *pcm, size_t count)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < count; i++)
        for (unsigned j = 0; j < 2; j++)
        {
            crc ^= ((uint16_t)pcm[i] >> (8 * j)) & 255u;
            for (unsigned k = 0; k < 8; k++)
                crc = (crc >> 1) ^ ((0u - (crc & 1u)) & 0xedb88320u);
        }
    return ~crc;
}
static void payload(Codec *c, FmProject *p)
{
    U32(p->next_id);
    U32(p->selected_track);
    U32(p->generation);
    DOUBLE(p->bpm);
    U8(p->beats_per_bar);
    ENUM(p->clock_source);
    BOOL(p->tempo_established);
    /* A successful load is durable; dirty/volatile are runtime status. */
    U16(p->asset_count);
    U16(p->instrument_count);
    U16(p->track_count);
    U16(p->pattern_count);
    U16(p->song_count);
    U16(p->bake_count);
    if (p->asset_count > FM_OBJECT_LIMIT || p->instrument_count > FM_OBJECT_LIMIT ||
        p->track_count > FM_OBJECT_LIMIT || p->pattern_count > FM_OBJECT_LIMIT ||
        p->song_count > FM_OBJECT_LIMIT || p->bake_count > FM_OBJECT_LIMIT)
    {
        c->error = FM_ERR_FORMAT;
        return;
    }
    for (unsigned i = 0; i < FM_FX_SLOTS; i++)
    {
        FmFxConfig *f = &p->fx[i];
        ENUM(f->type);
        BOOL(f->enabled);
        BOOL(f->return_muted);
        for (unsigned j = 0; j < 4; j++)
            FLOAT(f->parameter[j]);
        FLOAT(f->gain);
        FLOAT(f->pan);
    }
    unsigned assets_cursor = 0;
    for (unsigned i = 0; i < p->asset_count && c->error == FM_OK; i++)
    {
        size_t m = record_begin(c);
        if (c->read)
        {
            p->assets[i] = fm_alloc(p, FM_MEM_METADATA, sizeof(FmAsset));
            if (!p->assets[i])
            {
                c->error = FM_ERR_MEMORY;
                break;
            }
            memset(p->assets[i], 0, sizeof(FmAsset));
        }
        while (!c->read && assets_cursor < FM_OBJECT_LIMIT && !p->assets[assets_cursor])
            assets_cursor++;
        FmAsset *a = p->assets[c->read ? i : assets_cursor++];
        U32(a->id);
        U32(a->frames);
        U32(a->rate);
        U8(a->channels);
        BOOL(a->cycle);
        BOOL(a->retired);
        ENUM(a->source);
        uint32_t crc = c->read ? 0 : pcm_crc(a->pcm, (size_t)a->frames * a->channels);
        U32(crc);
        if (a->channels < 1 || a->channels > 2 || !a->frames || a->frames > UINT32_MAX / 4u ||
            a->rate < 1000 || a->rate > 384000)
        {
            c->error = FM_ERR_FORMAT;
            break;
        }
        size_t samples = (size_t)a->frames * a->channels;
        if (c->read)
        {
            if (samples > (c->size - c->pos) / 2)
            {
                c->error = FM_ERR_FORMAT;
                break;
            }
            a->pcm = fm_alloc(p, FM_MEM_AUDIO, samples * 2);
            if (!a->pcm)
            {
                c->error = FM_ERR_MEMORY;
                break;
            }
        }
        for (size_t j = 0; j < samples && c->error == FM_OK; j++)
        {
            uint16_t v = (uint16_t)a->pcm[j];
            U16(v);
            a->pcm[j] = (int16_t)v;
        }
        if (c->read)
        {
            if (pcm_crc(a->pcm, samples) != crc)
                c->error = FM_ERR_FORMAT;
            a->checksum = fm_crc32(a->pcm, samples * 2);
            a->published = true;
        }
        record_end(c, m);
    }
    unsigned instruments_cursor = 0;
    for (unsigned i = 0; i < p->instrument_count && c->error == FM_OK; i++)
    {
        size_t m = record_begin(c);
        if (c->read)
        {
            p->instruments[i] = fm_alloc(p, FM_MEM_METADATA, sizeof(FmInstrument));
            if (!p->instruments[i])
            {
                c->error = FM_ERR_MEMORY;
                break;
            }
            memset(p->instruments[i], 0, sizeof(FmInstrument));
        }
        while (!c->read && instruments_cursor < FM_OBJECT_LIMIT &&
               !p->instruments[instruments_cursor])
            instruments_cursor++;
        FmInstrument *s = p->instruments[c->read ? i : instruments_cursor++];
        U32(s->id);
        ENUM(s->type);
        for (unsigned j = 0; j < FM_NAME_LENGTH; j++)
        {
            uint8_t v = (uint8_t)s->name[j];
            U8(v);
            s->name[j] = (char)v;
        }
        U8(s->base_note);
        U8(s->anchor_class);
        U16(s->count);
        BOOL(s->additive);
        U32(s->revision);
        for (unsigned j = 0; j < FM_HARMONICS; j++)
            U16(s->harmonics[j]);
        if (s->count > FM_ANCHORS)
        {
            c->error = FM_ERR_FORMAT;
            break;
        }
        unsigned region_cursor = 0;
        for (unsigned j = 0; j < s->count; j++)
        {
            while (!c->read && region_cursor < FM_ANCHORS && !s->regions[region_cursor].id)
                region_cursor++;
            uint8_t slot = c->read ? 0 : (uint8_t)region_cursor++;
            U8(slot);
            if (slot >= FM_ANCHORS || (c->read && s->regions[slot].id))
            {
                c->error = FM_ERR_FORMAT;
                break;
            }
            region(c, &s->regions[slot]);
        }
        record_end(c, m);
    }
    unsigned tracks_cursor = 0;
    for (unsigned i = 0; i < p->track_count && c->error == FM_OK; i++)
    {
        size_t m = record_begin(c);
        if (c->read)
        {
            p->tracks[i] = fm_alloc(p, FM_MEM_METADATA, sizeof(FmTrack));
            if (!p->tracks[i])
            {
                c->error = FM_ERR_MEMORY;
                break;
            }
            memset(p->tracks[i], 0, sizeof(FmTrack));
        }
        while (!c->read && tracks_cursor < FM_OBJECT_LIMIT && !p->tracks[tracks_cursor])
            tracks_cursor++;
        FmTrack *t = p->tracks[c->read ? i : tracks_cursor++];
        U32(t->id);
        U32(t->instrument);
        for (unsigned j = 0; j < FM_PATTERN_SLOTS; j++)
            U32(t->patterns[j]);
        U8(t->selected_pattern);
        U8(t->playing_pattern);
        U8(t->chain_count);
        for (unsigned j = 0; j < 64; j++)
            U8(t->chain[j]);
        uint8_t q = (uint8_t)t->queued_pattern;
        U8(q);
        t->queued_pattern = (int8_t)q;
        FLOAT(t->volume);
        FLOAT(t->pan);
        FLOAT(t->cutoff);
        FLOAT(t->resonance);
        for (unsigned j = 0; j < FM_FX_SLOTS; j++)
            FLOAT(t->sends[j]);
        BOOL(t->mute);
        BOOL(t->solo);
        U8(t->midi_channel);
        record_end(c, m);
    }
    unsigned patterns_cursor = 0;
    for (unsigned i = 0; i < p->pattern_count && c->error == FM_OK; i++)
    {
        size_t m = record_begin(c);
        if (c->read)
        {
            p->patterns[i] = fm_alloc(p, FM_MEM_METADATA, sizeof(FmPattern));
            if (!p->patterns[i])
            {
                c->error = FM_ERR_MEMORY;
                break;
            }
            memset(p->patterns[i], 0, sizeof(FmPattern));
        }
        while (!c->read && patterns_cursor < FM_OBJECT_LIMIT && !p->patterns[patterns_cursor])
            patterns_cursor++;
        FmPattern *r = p->patterns[c->read ? i : patterns_cursor++];
        U32(r->id);
        U32(r->track);
        U32(r->count);
        DOUBLE(r->length_ticks);
        DOUBLE(r->rate);
        DOUBLE(r->swing);
        DOUBLE(r->quantize);
        U32(r->frozen_asset);
        U8(r->frozen_sends);
        DOUBLE(r->frozen_bpm);
        if (c->read)
        {
            if (r->count > (c->size - c->pos) / 44u ||
                (r->count && sizeof(FmEvent) > SIZE_MAX / r->count))
            {
                c->error = FM_ERR_FORMAT;
                break;
            }
            r->capacity = r->count;
            if (r->count)
            {
                r->events = fm_alloc(p, FM_MEM_EVENTS, (size_t)r->count * sizeof(FmEvent));
                if (!r->events)
                {
                    c->error = FM_ERR_MEMORY;
                    break;
                }
                memset(r->events, 0, (size_t)r->count * sizeof(FmEvent));
            }
        }
        for (unsigned j = 0; j < r->count && c->error == FM_OK; j++)
            event(c, &r->events[j]);
        record_end(c, m);
    }
    if (c->read && p->song_count)
    {
        p->song = fm_alloc(p, FM_MEM_METADATA, (size_t)p->song_count * sizeof(FmSongEntry));
        if (!p->song)
        {
            c->error = FM_ERR_MEMORY;
            return;
        }
        memset(p->song, 0, (size_t)p->song_count * sizeof(FmSongEntry));
    }
    for (unsigned i = 0; i < p->song_count && c->error == FM_OK; i++)
    {
        size_t m = record_begin(c);
        FmSongEntry *s = &p->song[i];
        U16(s->count);
        U16(s->repeats);
        DOUBLE(s->bars);
        if (s->count > FM_OBJECT_LIMIT)
        {
            c->error = FM_ERR_FORMAT;
            break;
        }
        for (unsigned j = 0; j < s->count; j++)
        {
            U32(s->tracks[j]);
            U8(s->patterns[j]);
            BOOL(s->muted[j]);
        }
        record_end(c, m);
    }
    unsigned cursor = 0;
    for (unsigned i = 0; i < p->bake_count && c->error == FM_OK; i++)
    {
        size_t m = record_begin(c);
        if (c->read)
        {
            p->bake_recipes[i] = fm_alloc(p, FM_MEM_METADATA, sizeof(FmBakeRecipe));
            if (!p->bake_recipes[i])
            {
                c->error = FM_ERR_MEMORY;
                break;
            }
        }
        while (!c->read && cursor < FM_OBJECT_LIMIT && !p->bake_recipes[cursor])
            cursor++;
        FmBakeRecipe *recipe = p->bake_recipes[c->read ? i : cursor++];
        U32(recipe->region);
        U8(recipe->cleared_sends);
        region(c, &recipe->original);
        for (unsigned j = 0; j < 4; j++)
            FLOAT(recipe->track_sends[j]);
        record_end(c, m);
    }
}
static bool bounded(float x, float low, float high)
{
    return isfinite(x) && x >= low && x <= high;
}
static bool envelope_valid(const FmEnvelope *e)
{
    if (!e->count || e->count > FM_ENV_STAGES || e->sustain >= e->count || e->release >= e->count ||
        !bounded(e->key_rate, -16, 16) || !bounded(e->velocity, 0, 4))
        return false;
    for (unsigned j = 0; j < e->count; j++)
        if (!bounded(e->stage[j].milliseconds, 0, 3600000) || !bounded(e->stage[j].level, -4, 4))
            return false;
    return true;
}
static bool region_valid(FmProject *p, const FmRegion *r)
{
    FmAsset *a = fm_asset(p, r->asset);
    if (!a || r->start >= r->end || r->end > a->frames || r->root > 127 || r->high > 127 ||
        r->low > r->high || r->loop_count > FM_LOOP_SECTIONS || (r->baked_mask & 0x70u) != 0 ||
        !bounded(r->gain, 0, 16) || !bounded(r->pan, -1, 1) || !bounded(r->tuning, -9600, 9600) ||
        !bounded(r->cutoff, 0, 200000) || !bounded(r->resonance, 0, 8) ||
        !bounded(r->drive, 0, 64) || !bounded(r->filter_env, -32, 32) ||
        !bounded(r->keytrack, -16, 16) || !bounded(r->lfo_rate, 0, 1000) ||
        !bounded(r->lfo_depth, -128, 128) || (r->lfo_shape & 0x7f) > 3 || r->lfo_destination > 8 ||
        !envelope_valid(&r->amp) || !envelope_valid(&r->filter))
        return false;
    for (unsigned k = 0; k < FM_FX_SLOTS; k++)
        if (!bounded(r->sends[k], 0, 8))
            return false;
    for (unsigned k = 0; k < r->loop_count; k++)
    {
        const FmLoopSection *l = &r->loops[k];
        if (l->start < r->start || l->start >= l->end || l->end > r->end ||
            l->crossfade > l->end - l->start || l->action > FM_LOOP_END ||
            (l->action == FM_LOOP_JUMP && l->jump >= r->loop_count))
            return false;
    }
    return true;
}
FmResult fm_project_validate(FmProject *p)
{
    if (!p || p->asset_count > FM_OBJECT_LIMIT || p->instrument_count > FM_OBJECT_LIMIT ||
        p->track_count > FM_OBJECT_LIMIT || p->pattern_count > FM_OBJECT_LIMIT ||
        p->song_count > FM_OBJECT_LIMIT || p->bake_count > FM_OBJECT_LIMIT)
        return FM_ERR_FORMAT;
    unsigned actual[4] = {0};
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
    {
        actual[0] += p->assets[i] != NULL;
        actual[1] += p->instruments[i] != NULL;
        actual[2] += p->tracks[i] != NULL;
        actual[3] += p->patterns[i] != NULL;
    }
    if (actual[0] != p->asset_count || actual[1] != p->instrument_count ||
        actual[2] != p->track_count || actual[3] != p->pattern_count)
        return FM_ERR_FORMAT;
    FmId ids[FM_OBJECT_LIMIT * (4 + FM_ANCHORS)];
    size_t n = 0;
    FmId max = 0;
#define ID(x)                                                                                      \
    do                                                                                             \
    {                                                                                              \
        FmId v_ = (x);                                                                             \
        if (!v_)                                                                                   \
            return FM_ERR_FORMAT;                                                                  \
        for (size_t k_ = 0; k_ < n; k_++)                                                          \
            if (ids[k_] == v_)                                                                     \
                return FM_ERR_FORMAT;                                                              \
        ids[n++] = v_;                                                                             \
        if (v_ > max)                                                                              \
            max = v_;                                                                              \
    } while (0)
    if (!isfinite(p->bpm) || p->bpm < 10 || p->bpm > 1000 || !p->beats_per_bar ||
        p->beats_per_bar > 32 || (unsigned)p->clock_source > FM_CLOCK_TRS)
        return FM_ERR_FORMAT;
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (p->assets[i])
        {
            FmAsset *a = p->assets[i];
            if (!a || !a->pcm || !a->frames || a->frames > UINT32_MAX / 4u || a->channels < 1 ||
                a->channels > 2 || a->rate < 1000 || a->rate > 384000 ||
                (unsigned)a->source > FM_SOURCE_RENDER)
                return FM_ERR_FORMAT;
            ID(a->id);
        }
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (p->instruments[i])
        {
            FmInstrument *s = p->instruments[i];
            if (!s || (unsigned)s->type > FM_SAMPLE || s->count > FM_ANCHORS ||
                s->base_note > 127 || s->anchor_class > 11 || !memchr(s->name, 0, FM_NAME_LENGTH))
                return FM_ERR_FORMAT;
            ID(s->id);
            for (unsigned h = 0; h < FM_HARMONICS; h++)
                if (s->harmonics[h] > 256)
                    return FM_ERR_FORMAT;
            unsigned actual_regions = 0;
            for (unsigned j = 0; j < FM_ANCHORS; j++)
                actual_regions += s->regions[j].id != 0;
            if (actual_regions != s->count)
                return FM_ERR_FORMAT;
            for (unsigned j = 0; j < FM_ANCHORS; j++)
                if (s->regions[j].id)
                {
                    FmRegion *r = &s->regions[j];
                    ID(r->id);
                    if (!region_valid(p, r))
                        return FM_ERR_FORMAT;
                }
        }
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (p->tracks[i])
        {
            FmTrack *t = p->tracks[i];
            if (!t || !fm_instrument(p, t->instrument) || t->selected_pattern >= FM_PATTERN_SLOTS ||
                t->playing_pattern >= FM_PATTERN_SLOTS || t->chain_count > 64 ||
                t->queued_pattern < -1 || t->queued_pattern >= (int)FM_PATTERN_SLOTS ||
                t->midi_channel > 15 || !bounded(t->volume, 0, 16) || !bounded(t->pan, -1, 1) ||
                !bounded(t->cutoff, 0, 200000) || !bounded(t->resonance, 0, 8))
                return FM_ERR_FORMAT;
            ID(t->id);
            for (unsigned j = 0; j < t->chain_count; j++)
                if (t->chain[j] >= FM_PATTERN_SLOTS || !fm_pattern(p, t->patterns[t->chain[j]]))
                    return FM_ERR_FORMAT;
            for (unsigned k = 0; k < FM_FX_SLOTS; k++)
                if (!bounded(t->sends[k], 0, 8))
                    return FM_ERR_FORMAT;
            for (unsigned k = 0; k < FM_PATTERN_SLOTS; k++)
                if (t->patterns[k])
                {
                    FmPattern *r = fm_pattern(p, t->patterns[k]);
                    if (!r || r->track != t->id)
                        return FM_ERR_FORMAT;
                }
        }
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (p->patterns[i])
        {
            FmPattern *r = p->patterns[i];
            if (!r || !fm_track(p, r->track) || r->count > r->capacity ||
                (r->count && !r->events) || !isfinite(r->length_ticks) || r->length_ticks <= 0 ||
                r->length_ticks > 1e12 || !isfinite(r->rate) || r->rate < 0.03125 || r->rate > 32 ||
                !isfinite(r->swing) || r->swing < 0 || r->swing > 1 || !isfinite(r->quantize) ||
                r->quantize < 0 || r->quantize > FM_PPQN * 16 || r->frozen_sends > 15 ||
                !isfinite(r->frozen_bpm) ||
                (r->frozen_asset &&
                 (!fm_asset(p, r->frozen_asset) || r->frozen_bpm < 10 || r->frozen_bpm > 1000)))
                return FM_ERR_FORMAT;
            ID(r->id);
            for (unsigned j = 0; j < r->count; j++)
            {
                FmEvent *e = &r->events[j];
                if (!isfinite(e->tick) || e->tick < 0 || e->tick > 1e12 || e->track != r->track ||
                    (unsigned)e->kind > FM_EVENT_BEND || (unsigned)e->origin > FM_ORIGIN_LOOPBACK ||
                    e->note > 127 || e->velocity > 127 || e->channel > 15 ||
                    e->parameter > FM_PARAM_SEND4 || !isfinite(e->value) ||
                    !bounded(e->probability, 0, 1))
                    return FM_ERR_FORMAT;
            }
        }
    for (unsigned i = 0; i < FM_FX_SLOTS; i++)
    {
        FmFxConfig *f = &p->fx[i];
        if ((unsigned)f->type > FM_FX_CHARACTER || !bounded(f->gain, 0, 16) ||
            !bounded(f->pan, -1, 1))
            return FM_ERR_FORMAT;
        for (unsigned j = 0; j < 4; j++)
            if (!bounded(f->parameter[j], -1000000, 1000000))
                return FM_ERR_FORMAT;
    }
    if (p->song_count && !p->song)
        return FM_ERR_FORMAT;
    for (unsigned i = 0; i < p->song_count; i++)
    {
        FmSongEntry *s = &p->song[i];
        if (!s->count || s->count > FM_OBJECT_LIMIT || !s->repeats || !isfinite(s->bars) ||
            s->bars <= 0 || s->bars > 1e6)
            return FM_ERR_FORMAT;
        for (unsigned j = 0; j < s->count; j++)
        {
            FmTrack *t = fm_track(p, s->tracks[j]);
            if (!t || s->patterns[j] >= FM_PATTERN_SLOTS || !t->patterns[s->patterns[j]])
                return FM_ERR_FORMAT;
            for (unsigned k = 0; k < j; k++)
                if (s->tracks[k] == s->tracks[j])
                    return FM_ERR_FORMAT;
        }
    }
    unsigned recipes = 0;
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (p->bake_recipes[i])
        {
            FmBakeRecipe *recipe = p->bake_recipes[i];
            FmRegion *live = fm_region(p, recipe->region);
            if (recipe->cleared_sends > 15 || !live || !(live->baked_mask & 0x80) ||
                recipe->original.id != recipe->region || !region_valid(p, &recipe->original))
                return FM_ERR_FORMAT;
            for (unsigned j = 0; j < i; j++)
                if (p->bake_recipes[j] && p->bake_recipes[j]->region == recipe->region)
                    return FM_ERR_FORMAT;
            for (unsigned j = 0; j < 4; j++)
                if (!bounded(recipe->track_sends[j], 0, 8))
                    return FM_ERR_FORMAT;
            recipes++;
        }
    if (recipes != p->bake_count)
        return FM_ERR_FORMAT;
    if (p->next_id <= max || (p->selected_track && !fm_track(p, p->selected_track)))
        return FM_ERR_FORMAT;
#undef ID
    return FM_OK;
}
static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void write32(uint8_t *p, uint32_t v)
{
    for (unsigned j = 0; j < 4; j++)
        p[j] = (uint8_t)(v >> (8 * j));
}
FmResult fm_project_encode(FmProject *p, uint8_t **bytes, size_t *length)
{
    if (!p || !bytes || !length)
        return FM_ERR_ARGUMENT;
    FmResult r = fm_project_validate(p);
    if (r != FM_OK)
        return r;
    Codec c = {0};
    payload(&c, p);
    if (c.error != FM_OK)
        return c.error;
    if (c.pos > UINT32_MAX - 24u)
        return FM_ERR_RANGE;
    uint8_t *b = malloc(c.pos + 24);
    if (!b)
        return FM_ERR_MEMORY;
    memset(b, 0, 24);
    memcpy(b, "FM1P", 4);
    b[4] = FM_FORMAT_VERSION;
    b[6] = 24;
    write32(b + 8, (uint32_t)c.pos);
    write32(b + 16, p->generation);
    c.out = b + 24;
    c.pos = 0;
    payload(&c, p);
    if (c.error != FM_OK)
    {
        free(b);
        return c.error;
    }
    write32(b + 12, fm_crc32(b + 24, c.pos));
    write32(b + 20, fm_crc32(b, 20));
    *bytes = b;
    *length = c.pos + 24;
    return FM_OK;
}
FmResult fm_project_decode(FmProject *p, const uint8_t *b, size_t length)
{
    if (!p || !b)
        return FM_ERR_ARGUMENT;
    if (length < 24 || memcmp(b, "FM1P", 4) || b[4] != FM_FORMAT_VERSION || b[5] || b[6] != 24 ||
        b[7] || read32(b + 8) != length - 24 || read32(b + 20) != fm_crc32(b, 20) ||
        read32(b + 12) != fm_crc32(b + 24, length - 24))
        return FM_ERR_FORMAT;
    FmBudget staging = p->budget;
    for (unsigned i = 0; i < FM_MEM_COUNT; i++)
    {
        if (staging.used[i] > staging.limit[i])
            return FM_ERR_MEMORY;
        staging.limit[i] -= staging.used[i];
    }
    FmProject temp;
    FmResult r = fm_project_init(&temp, &staging);
    if (r != FM_OK)
        return r;
    Codec c = {.in = b + 24, .size = length - 24, .read = true};
    payload(&c, &temp);
    r = c.error;
    if (r == FM_OK && (c.pos != c.size || temp.generation != read32(b + 16)))
        r = FM_ERR_FORMAT;
    if (r == FM_OK)
        r = fm_project_validate(&temp);
    if (r != FM_OK)
    {
        fm_project_destroy(&temp);
        return r;
    }
    for (unsigned i = 0; i < FM_MEM_COUNT; i++)
    {
        temp.budget.limit[i] = p->budget.limit[i];
        size_t peak = p->budget.used[i] + temp.budget.peak[i];
        temp.budget.peak[i] = peak > p->budget.peak[i] ? peak : p->budget.peak[i];
    }
    temp.dirty = false;
    temp.volatile_audio = false;
    fm_project_destroy(p);
    *p = temp;
    return FM_OK;
}
