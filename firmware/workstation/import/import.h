/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FM_IMPORT_H
#define FM_IMPORT_H
#include "../core/fm.h"
#define FM_IMPORT_CHUNK 256u
/* Strict PCM16 RIFF/WAVE reader; no guessed companding or ROM layout. */
FmResult fm_import_wav_asset(FmProject *, const uint8_t *, size_t, bool cycle, FmId *);
/* Transport-independent backup receiver; USB SysEx framing is a separate adapter.
 * Zero-initialize before first begin. Replacement needs explicit finish approval. */
typedef struct
{
    FmProject *owner;
    uint8_t *bytes;
    size_t length, received;
    uint32_t checksum, next_sequence, previous_crc;
    size_t previous_length;
} FmImportTransfer;
FmResult fm_import_begin(FmImportTransfer *, FmProject *, size_t, uint32_t);
FmResult fm_import_chunk(FmImportTransfer *, uint32_t, const uint8_t *, size_t, uint32_t);
FmResult fm_import_finish(FmImportTransfer *, bool replacement_confirmed);
void fm_import_cancel(FmImportTransfer *);
#endif
