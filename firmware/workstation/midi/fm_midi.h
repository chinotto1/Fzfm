/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FM_MIDI_H
#define FM_MIDI_H
#include "../audio/fm_audio.h"
#include "../sequence/fm_sequence.h"
#define FM_MIDI_HELD 256u
typedef struct
{
    bool used;
    FmId track;
    uint32_t identity;
    uint8_t note, channel;
    FmOrigin origin;
    uint64_t serial;
} FmMidiHeld;
typedef struct
{
    FmProject *project;
    FmEngine *engine;
    FmSequencer *sequence;
    FmMidiHeld held[FM_MIDI_HELD];
    uint32_t next_identity;
    uint64_t serial;
    uint32_t malformed, dropped, received;
} FmMidi;
void fm_midi_init(FmMidi *, FmProject *, FmEngine *, FmSequencer *);
/* Validates class-compliant USB-MIDI 1.0 event packets. Only cable0 is routed.
 * Incoming notes are never echoed. All callers share the render owner. */
FmResult fm_midi_usb(FmMidi *, const uint8_t *, size_t length, FmFrame);
FmResult fm_midi_message(FmMidi *, FmOrigin, uint8_t status, uint8_t data1, uint8_t data2, FmFrame);
void fm_midi_panic(FmMidi *, FmFrame);
#endif
