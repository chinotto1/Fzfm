/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "import.h"
#include "../storage/storage.h"
#include <string.h>
static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)((unsigned)p[0] | (unsigned)p[1] << 8);
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
FmResult fm_import_wav_asset(FmProject *p, const uint8_t *b, size_t length, bool cycle, FmId *out)
{
    if (!p || !b || !out)
        return FM_ERR_ARGUMENT;
    if (length < 12 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4) ||
        get32(b + 4) != (uint64_t)length - 8)
        return FM_ERR_FORMAT;
    bool have_format = false, have_data = false;
    uint16_t channels = 0, alignment = 0;
    uint32_t rate = 0;
    const uint8_t *data = NULL;
    size_t data_length = 0;
    for (size_t at = 12; at < length;)
    {
        if (length - at < 8)
            return FM_ERR_FORMAT;
        uint32_t n = get32(b + at + 4);
        const uint8_t *chunk = b + at;
        at += 8;
        if (n > length - at)
            return FM_ERR_FORMAT;
        if (!memcmp(chunk, "fmt ", 4))
        {
            if (have_format || n < 16)
                return FM_ERR_FORMAT;
            have_format = true;
            if (get16(b + at) != 1 || get16(b + at + 14) != 16)
                return FM_ERR_UNSUPPORTED;
            channels = get16(b + at + 2);
            rate = get32(b + at + 4);
            alignment = get16(b + at + 12);
            if ((channels != 1 && channels != 2) || rate < 1000 || rate > 192000 ||
                alignment != channels * 2 || get32(b + at + 8) != (uint64_t)rate * alignment)
                return FM_ERR_FORMAT;
        }
        else if (!memcmp(chunk, "data", 4))
        {
            if (have_data)
                return FM_ERR_FORMAT;
            have_data = true;
            data = b + at;
            data_length = n;
        }
        at += n;
        if (n & 1u)
        {
            if (at == length)
                return FM_ERR_FORMAT;
            at++;
        }
    }
    if (!have_format || !have_data || !data_length || data_length % alignment ||
        data_length / alignment > UINT32_MAX)
        return FM_ERR_FORMAT;
    uint32_t frames = (uint32_t)(data_length / alignment);
    if (cycle && (channels != 1 || frames < 2))
        return FM_ERR_FORMAT;
    int16_t *pcm = fm_alloc(p, FM_MEM_AUDIO, data_length);
    if (!pcm)
        return FM_ERR_STORAGE;
    for (size_t i = 0; i < data_length / 2; i++)
        pcm[i] = (int16_t)get16(data + i * 2);
    FmResult result =
        fm_asset_create(p, pcm, frames, (uint8_t)channels, rate, cycle, FM_SOURCE_IMPORT, out);
    fm_dealloc(p, FM_MEM_AUDIO, pcm, data_length);
    return result;
}
void fm_import_cancel(FmImportTransfer *t)
{
    if (!t)
        return;
    if (t->owner && t->bytes)
        fm_dealloc(t->owner, FM_MEM_AUDIO, t->bytes, t->length);
    memset(t, 0, sizeof(*t));
}
FmResult fm_import_begin(FmImportTransfer *t, FmProject *p, size_t length, uint32_t crc)
{
    if (!t || !p || length < 24)
        return FM_ERR_ARGUMENT;
    if (t->bytes)
        return FM_ERR_BUSY;
    uint8_t *bytes = fm_alloc(p, FM_MEM_AUDIO, length);
    if (!bytes)
        return FM_ERR_STORAGE;
    memset(t, 0, sizeof(*t));
    t->owner = p;
    t->bytes = bytes;
    t->length = length;
    t->checksum = crc;
    return FM_OK;
}
FmResult fm_import_chunk(FmImportTransfer *t, uint32_t sequence, const uint8_t *b, size_t n,
                         uint32_t crc)
{
    if (!t || !t->bytes || !b || !n || n > FM_IMPORT_CHUNK)
        return FM_ERR_ARGUMENT;
    if (crc != fm_crc32(b, n))
        return FM_ERR_FORMAT;
    if (t->next_sequence && sequence == t->next_sequence - 1)
    {
        if (n != t->previous_length || crc != t->previous_crc ||
            memcmp(t->bytes + t->received - n, b, n))
            return FM_ERR_FORMAT;
        return FM_OK;
    }
    if (sequence != t->next_sequence || n > t->length - t->received ||
        t->next_sequence == UINT32_MAX)
        return FM_ERR_RANGE;
    memcpy(t->bytes + t->received, b, n);
    t->received += n;
    t->previous_crc = crc;
    t->previous_length = n;
    t->next_sequence++;
    return FM_OK;
}
FmResult fm_import_finish(FmImportTransfer *t, bool confirmed)
{
    if (!t || !t->bytes || !t->owner)
        return FM_ERR_ARGUMENT;
    if (!confirmed)
        return FM_ERR_CANCELLED;
    if (t->received != t->length || fm_crc32(t->bytes, t->length) != t->checksum)
        return FM_ERR_FORMAT;
    FmResult result = fm_project_decode(t->owner, t->bytes, t->length);
    if (result == FM_OK)
        memset(t, 0, sizeof(*t));
    return result;
}
