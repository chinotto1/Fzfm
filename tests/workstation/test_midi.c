/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../firmware/workstation/dsp/fm_synthesis.h"
#include "../../firmware/workstation/midi/fm_midi.h"
#include "test.h"
int test_midi(void)
{
    FmProject p;
    CHECK(fm_project_init(&p, NULL) == FM_OK);
    FmId t1, t2;
    CHECK(fm_instrument_create(&p, FM_SAMPLE, &t1) == FM_OK);
    CHECK(fm_instrument_create(&p, FM_SAMPLE, &t2) == FM_OK);
    FmEngine e;
    CHECK(fm_engine_init(&e, &p, 8) == FM_OK);
    FmSequencer *seq = fm_sequence_create(&p, 128, 128);
    CHECK(seq);
    FmMidi midi;
    fm_midi_init(&midi, &p, &e, seq);
    uint8_t on[] = {9, 0x90, 60, 100};
    CHECK(fm_midi_usb(&midi, on, 4, 0) == FM_OK);
    unsigned held = 0;
    for (unsigned i = 0; i < FM_MIDI_HELD; i++)
        held += midi.held[i].used;
    CHECK(held == 1 && fm_sequence_status(seq).retained_events == 1);
    CHECK(fm_midi_usb(&midi, on, 3, 0) == FM_ERR_FORMAT);
    on[0] = 8;
    CHECK(fm_midi_usb(&midi, on, 4, 1) == FM_ERR_FORMAT && midi.malformed == 1);
    on[0] = 9;
    /* No selected-track association: incoming channel binds at note-on. */
    fm_track(&p, t1)->midi_channel = 3;
    p.selected_track = t2;
    uint8_t off[] = {8, 0x80, 60, 0};
    CHECK(fm_midi_usb(&midi, off, 4, 100) == FM_OK);
    for (unsigned i = 0; i < FM_MIDI_HELD; i++)
        CHECK(!midi.held[i].used);
    fm_track(&p, t1)->midi_channel = 0;
    CHECK(fm_midi_usb(&midi, on, 4, 200) == FM_OK);
    CHECK(fm_midi_usb(&midi, on, 4, 201) == FM_OK);
    uint8_t zero[] = {9, 0x90, 60, 0};
    CHECK(fm_midi_usb(&midi, zero, 4, 300) == FM_OK);
    held = 0;
    for (unsigned i = 0; i < FM_MIDI_HELD; i++)
        held += midi.held[i].used;
    CHECK(held == 1);
    CHECK(fm_midi_message(&midi, FM_ORIGIN_PICKER, 0x90, 62, 100, 300) == FM_ERR_ARGUMENT);
    CHECK(fm_midi_message(&midi, FM_ORIGIN_LOOPBACK, 0x90, 62, 100, 300) == FM_ERR_ARGUMENT);
    fm_midi_panic(&midi, 400);
    for (unsigned i = 0; i < FM_MIDI_HELD; i++)
        CHECK(!midi.held[i].used);
    fm_sequence_destroy(seq);
    fm_engine_destroy(&e);
    fm_project_destroy(&p);
    return 0;
}
