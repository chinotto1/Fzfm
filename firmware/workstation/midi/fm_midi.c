/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fm_midi.h"
#include <string.h>
static FmResult emit(FmMidi *m, FmEvent *e)
{
    FmResult r = m->engine ? fm_engine_event(m->engine, e) : FM_OK;
    if (r != FM_OK)
    {
        m->dropped++;
        return r;
    }
    if (m->sequence)
    {
        FmResult recorded = fm_sequence_live(m->sequence, e);
        if (recorded != FM_OK)
        {
            m->dropped++;
            if (!m->engine)
                return recorded;
        }
    }
    /* A full recording buffer must not orphan an admitted audible note. */
    return FM_OK;
}
void fm_midi_init(FmMidi *m, FmProject *p, FmEngine *e, FmSequencer *s)
{
    memset(m, 0, sizeof(*m));
    m->project = p;
    m->engine = e;
    m->sequence = s;
    m->next_identity = 1;
}
FmResult fm_midi_message(FmMidi *m, FmOrigin origin, uint8_t status, uint8_t a, uint8_t b,
                         FmFrame time)
{
    if (!m || !m->project || origin == FM_ORIGIN_PICKER || origin == FM_ORIGIN_SEQUENCER ||
        origin == FM_ORIGIN_LOOPBACK)
        return FM_ERR_ARGUMENT;
    if (a > 127 || b > 127 || status < 0x80)
        return FM_ERR_FORMAT;
    if (status >= 0xf0)
    {
        if (!m->sequence)
            return FM_ERR_UNSUPPORTED;
        FmClockSource source = origin == FM_ORIGIN_USB ? FM_CLOCK_USB : FM_CLOCK_TRS;
        if (m->project->clock_source != source)
            return FM_OK;
        if (status == 0xf8)
            return fm_sequence_clock_pulse(m->sequence, source, time);
        if (status == 0xfa || status == 0xfb)
        {
            if (status == 0xfa)
            {
                if (m->engine)
                    fm_engine_panic(m->engine);
                fm_sequence_start(m->sequence);
            }
            else
                fm_sequence_continue(m->sequence);
            return FM_OK;
        }
        if (status == 0xfc)
        {
            fm_sequence_stop(m->sequence, NULL, NULL);
            if (m->engine)
                fm_engine_panic(m->engine);
            return FM_OK;
        }
        return FM_ERR_UNSUPPORTED;
    }
    uint8_t channel = status & 15u, kind = status & 0xf0u;
    m->received++;
    FmEvent e = {0};
    e.frame = time;
    e.channel = channel;
    e.origin = origin;
    e.note = a;
    e.velocity = b;
    e.probability = 1;
    if (kind == 0x80 || (kind == 0x90 && b == 0))
    {
        /* Bind note-off to note-on track despite later routing/track changes.
         * For each track release its oldest matching repeated-note instance. */
        FmResult result = FM_OK;
        for (unsigned t = 0; t < FM_OBJECT_LIMIT; t++)
            if (m->project->tracks[t])
            {
                FmId tid = m->project->tracks[t]->id;
                FmMidiHeld *found = NULL;
                for (unsigned j = 0; j < FM_MIDI_HELD; j++)
                {
                    FmMidiHeld *h = &m->held[j];
                    if (h->used && h->track == tid && h->note == a && h->channel == channel &&
                        h->origin == origin && (!found || h->serial < found->serial))
                        found = h;
                }
                if (found)
                {
                    e.track = tid;
                    e.kind = FM_EVENT_NOTE_OFF;
                    e.identity = found->identity;
                    FmResult r = emit(m, &e);
                    if (r == FM_OK)
                        found->used = false;
                    else
                        result = r;
                }
            }
        return result;
    }
    FmResult result = FM_OK;
    for (unsigned t = 0; t < FM_OBJECT_LIMIT; t++)
        if (m->project->tracks[t] && m->project->tracks[t]->midi_channel == channel)
        {
            e.track = m->project->tracks[t]->id;
            e.identity = 0;
            if (kind == 0x90)
            {
                unsigned slot = 0;
                while (slot < FM_MIDI_HELD && m->held[slot].used)
                    slot++;
                if (slot == FM_MIDI_HELD)
                {
                    m->dropped++;
                    result = FM_ERR_MEMORY;
                    continue;
                }
                e.kind = FM_EVENT_NOTE_ON;
                e.identity = m->next_identity++;
                if (!m->next_identity)
                    m->next_identity = 1;
                FmResult r = emit(m, &e);
                if (r == FM_OK)
                    m->held[slot] =
                        (FmMidiHeld){true, e.track, e.identity, a, channel, origin, ++m->serial};
                else
                    result = r;
            }
            else if (kind == 0xe0)
            {
                e.kind = FM_EVENT_BEND;
                e.value = ((float)((unsigned)a + 128u * b) - 8192.0f) * 2.0f / 8192.0f;
                FmResult r = emit(m, &e);
                if (r != FM_OK)
                    result = r;
            }
            else if (kind == 0xb0)
            {
                e.kind = FM_EVENT_CONTROL;
                e.value = (float)b / 127.0f;
                if (a == 64)
                {
                    e.kind = FM_EVENT_SUSTAIN;
                    e.value = b >= 64 ? 1 : 0;
                }
                else if (a == 7)
                    e.parameter = FM_PARAM_VOLUME;
                else if (a == 10)
                {
                    e.parameter = FM_PARAM_PAN;
                    e.value = ((float)b - 64) / 64.0f;
                }
                else if (a == 74)
                    e.parameter = FM_PARAM_CUTOFF;
                else if (a == 71)
                    e.parameter = FM_PARAM_RESONANCE;
                else if (a >= 91 && a <= 94)
                    e.parameter = (uint8_t)(FM_PARAM_SEND1 + a - 91);
                else if (a == 120 || a == 123)
                {
                    fm_midi_panic(m, time);
                    return FM_OK;
                }
                else
                    continue;
                FmResult r = emit(m, &e);
                if (r != FM_OK)
                    result = r;
            }
        }
    return result;
}
FmResult fm_midi_usb(FmMidi *m, const uint8_t *bytes, size_t length, FmFrame time)
{
    if (!m || (!bytes && length) || length % 4)
        return FM_ERR_FORMAT;
    FmResult result = FM_OK;
    for (size_t i = 0; i < length; i += 4)
    {
        uint8_t cin = bytes[i] & 15u, st = bytes[i + 1];
        if (bytes[i] >> 4)
            continue;
        bool valid = (cin >= 8 && cin <= 14 && ((st >> 4) == cin)) || (cin == 15 && st >= 0xf8);
        if (!valid || bytes[i + 2] > 127 ||
            ((cin != 12 && cin != 13 && cin != 15) && bytes[i + 3] > 127))
        {
            m->malformed++;
            result = FM_ERR_FORMAT;
            continue;
        }
        FmResult r = fm_midi_message(m, FM_ORIGIN_USB, st, cin == 15 ? 0 : bytes[i + 2],
                                     cin == 15 ? 0 : bytes[i + 3], time);
        if (r != FM_OK && r != FM_ERR_UNSUPPORTED)
            result = r;
    }
    return result;
}
void fm_midi_panic(FmMidi *m, FmFrame time)
{
    if (!m)
        return;
    for (unsigned i = 0; i < FM_MIDI_HELD; i++)
        if (m->held[i].used)
        {
            FmMidiHeld *h = &m->held[i];
            FmEvent e = {0};
            e.track = h->track;
            e.identity = h->identity;
            e.note = h->note;
            e.channel = h->channel;
            e.origin = h->origin;
            e.kind = FM_EVENT_NOTE_OFF;
            e.frame = time;
            e.probability = 1;
            (void)emit(m, &e);
            h->used = false;
        }
    if (m->engine)
        fm_engine_panic(m->engine);
}
