/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Included by fm_sequence.c. Bounded original scheduler; no allocation or I/O. */
static void arp_end(FmSequencer *s, ArpRun *r, unsigned offset, FmSequenceDispatch cb, void *user)
{
    for (unsigned i = 0; i < r->sounding; i++)
    {
        FmEvent e = r->output[i];
        e.kind = FM_EVENT_NOTE_OFF;
        e.velocity = 0;
        e.frame = s->frame;
        (void)fm_sequence_live(s, &e);
        e.origin = FM_ORIGIN_SEQUENCER;
        send(cb, user, &e, offset);
    }
    r->sounding = 0;
}
static void arp_clear(FmSequencer *s, unsigned offset, FmSequenceDispatch cb, void *user)
{
    for (unsigned i = 0; i < FM_ARP_RUNS; i++)
        if (s->arp[i].track)
        {
            arp_end(s, &s->arp[i], offset, cb, user);
            memset(&s->arp[i], 0, sizeof(s->arp[i]));
        }
}
static void arp_pause(FmSequencer *s, unsigned offset, FmSequenceDispatch cb, void *user)
{
    for (unsigned i = 0; i < FM_ARP_RUNS; i++)
        if (s->arp[i].track)
        {
            arp_end(s, &s->arp[i], offset, cb, user);
            s->arp[i].next = s->arp_tick;
        }
}
FmResult fm_sequence_arp_input(FmSequencer *s, const FmEvent *e, bool *consumed)
{
    if (!consumed)
        return FM_ERR_ARGUMENT;
    *consumed = false;
    if (!s || !e || !live_origin(e->origin))
        return FM_OK;
    FmTrack *t = fm_track(s->project, e->track);
    if (!t || !t->arp.mode)
        return FM_OK;
    if (!note_on(e) && !note_off(e) && e->kind != FM_EVENT_SUSTAIN)
        return FM_OK;
    if (e->note > 127 || e->channel > 15 || !isfinite(e->value))
        return FM_ERR_ARGUMENT;
    *consumed = true;
    ArpRun *r = NULL, *empty = NULL;
    for (unsigned i = 0; i < FM_ARP_RUNS; i++)
    {
        if (s->arp[i].track == t->id)
            r = &s->arp[i];
        if (!s->arp[i].track && !empty)
            empty = &s->arp[i];
    }
    if (!r && note_off(e))
    {
        *consumed = false;
        return FM_OK;
    }
    if (!r)
    {
        if (!empty)
        {
            s->status.overflow = true;
            return FM_ERR_MEMORY;
        }
        r = empty;
        memset(r, 0, sizeof(*r));
        r->track = t->id;
        r->owner = t;
        r->next = s->arp_tick;
    }
    if (e->kind == FM_EVENT_SUSTAIN)
    {
        uint16_t mask = (uint16_t)(1u << e->channel);
        if (e->value >= .5f)
            r->pedals[e->origin] |= mask;
        else
            r->pedals[e->origin] &= (uint16_t)~mask;
        return FM_OK;
    }
    if (note_on(e))
    {
        bool down = false;
        for (unsigned i = 0; i < r->count; i++)
            down |= r->keys[i].down;
        if (t->arp.latch && !down)
            r->count = 0;
        if (r->count == FM_ARP_KEYS)
        {
            s->status.overflow = true;
            return FM_ERR_MEMORY;
        }
        if (!r->count)
        {
            r->next = s->arp_tick;
            r->step = 0;
            r->walk = 0;
        }
        r->keys[r->count++] = (ArpKey){.identity = e->identity,
                                       .origin = e->origin,
                                       .channel = e->channel,
                                       .note = e->note,
                                       .velocity = e->velocity,
                                       .down = true};
    }
    else
    {
        bool matched = false;
        for (unsigned i = 0; i < r->count; i++)
        {
            ArpKey *k = &r->keys[i];
            if (k->down && k->origin == e->origin && k->channel == e->channel &&
                (e->identity ? k->identity == e->identity : k->note == e->note))
            {
                k->down = false;
                matched = true;
                break;
            }
        }
        if (!matched)
            *consumed = false;
    }
    return FM_OK;
}
static unsigned arp_pick(FmSequencer *s, ArpRun *r, unsigned mode, unsigned count)
{
    unsigned n = r->step;
    if (count < 2)
        return 0;
    switch (mode)
    {
    case FM_ARP_DOWN:
        return count - 1 - n % count;
    case FM_ARP_UP_DOWN:
    case FM_ARP_DOWN_UP:
    {
        unsigned k = n % (2 * count - 2);
        if (k >= count)
            k = 2 * count - 2 - k;
        return mode == FM_ARP_DOWN_UP ? count - 1 - k : k;
    }
    case FM_ARP_RANDOM:
        return random_next(s) % count;
    case FM_ARP_CONVERGE:
    {
        unsigned k = n % count;
        return k % 2 ? count - 1 - k / 2 : k / 2;
    }
    case FM_ARP_DIVERGE:
    {
        unsigned k = n % count;
        return (unsigned)((int)(count - 1) / 2 + (k % 2 ? (int)(k + 1) / 2 : -(int)(k / 2)));
    }
    case FM_ARP_PINKY:
        return n % 2 ? count - 1 : (n / 2) % (count - 1);
    case FM_ARP_THUMB:
        return n % 2 ? 1 + (n / 2) % (count - 1) : 0;
    case FM_ARP_WALK:
        if (n)
        {
            int step = (random_next(s) & 1) ? 1 : -1;
            r->walk = (uint8_t)((r->walk + count + step) % count);
        }
        return r->walk % count;
    default:
        return n % count;
    }
}
static void arp_tick(FmSequencer *s, double add, unsigned offset, FmSequenceDispatch cb, void *user)
{
    static const double ticks[] = {960, 480, 320, 240, 160, 120, 80, 60};
    for (unsigned ri = 0; ri < FM_ARP_RUNS; ri++)
    {
        ArpRun *r = &s->arp[ri];
        if (!r->track)
            continue;
        FmTrack *t = r->owner;
        if (!t || !t->arp.mode)
        {
            arp_end(s, r, offset, cb, user);
            memset(r, 0, sizeof(*r));
            continue;
        }
        FmArpConfig *a = &t->arp;
        if (!a->latch)
            for (unsigned i = 0; i < r->count;)
            {
                ArpKey *k = &r->keys[i];
                if (!k->down && !(r->pedals[k->origin] & (1u << k->channel)))
                {
                    memmove(k, k + 1, (--r->count - i) * sizeof(*k));
                }
                else
                    i++;
            }
        if (r->sounding && (s->arp_tick + 1e-8 >= r->off || !r->count))
            arp_end(s, r, offset, cb, user);
        if (!r->count)
        {
            if (!r->sounding)
                memset(r, 0, sizeof(*r));
            continue;
        }
        if (s->arp_tick + 1e-8 < r->next)
            continue;
        arp_end(s, r, offset, cb, user);
        uint8_t order[FM_ARP_KEYS];
        for (unsigned i = 0; i < r->count; i++)
            order[i] = (uint8_t)i;
        if (!a->play_order && a->mode != FM_ARP_ORDER && a->mode != FM_ARP_REPEAT)
            for (unsigned i = 1; i < r->count; i++)
            {
                uint8_t key = order[i];
                unsigned j = i;
                while (j && r->keys[order[j - 1]].note > r->keys[key].note)
                {
                    order[j] = order[j - 1];
                    j--;
                }
                order[j] = key;
            }
        struct
        {
            uint8_t key, note;
        } list[FM_ARP_KEYS * 4 + 1];
        unsigned count = 0;
        for (unsigned oct = 0; oct < a->octaves; oct++)
            for (unsigned i = 0; i < r->count; i++)
            {
                unsigned pitch = r->keys[order[i]].note + oct * 12;
                if (pitch < 128)
                {
                    list[count].key = order[i];
                    list[count++].note = (uint8_t)pitch;
                }
            }
        if (a->mode == FM_ARP_UP_OCTAVE && count && count < FM_ARP_KEYS * 4 + 1)
        {
            unsigned pitch = list[0].note + 12;
            while (pitch <= list[count - 1].note)
                pitch += 12;
            if (pitch < 128)
            {
                list[count].key = list[0].key;
                list[count++].note = (uint8_t)pitch;
            }
        }
        double length = ticks[a->division < 8 ? a->division : 3],
               interval = length * (1 + (r->step % 2 ? -2 : 2) * a->swing);
        r->next += interval;
        r->off = s->arp_tick + fmax(1, fmin(length * a->gate, interval));
        if (count && (double)random_next(s) / 4294967296.0 < a->probability)
        {
            unsigned times = a->mode == FM_ARP_CHORD ? (r->count < 4 ? r->count : 4) : 1;
            for (unsigned chord = 0; chord < times; chord++)
            {
                unsigned picked = arp_pick(s, r, a->mode, count);
                uint8_t key = list[picked].key, note = list[picked].note;
                if (a->mode == FM_ARP_REPEAT)
                {
                    key = (uint8_t)(r->count - 1);
                    note = r->keys[key].note;
                }
                if (a->mode == FM_ARP_CHORD)
                {
                    key = order[chord];
                    unsigned pitch = r->keys[key].note + 12 * (r->step % a->octaves);
                    if (pitch > 127)
                        continue;
                    note = (uint8_t)pitch;
                }
                ArpKey *source = &r->keys[key];
                FmEvent e = {.frame = s->frame,
                             .kind = FM_EVENT_NOTE_ON,
                             .origin = source->origin,
                             .track = r->track,
                             .channel = source->channel,
                             .note = note,
                             .velocity = source->velocity,
                             .identity = 0x40000000u | (++s->arp_identity & 0x3fffffffu),
                             .probability = 1};
                r->output[r->sounding++] = e;
                (void)fm_sequence_live(s, &e);
                e.origin = FM_ORIGIN_SEQUENCER;
                send(cb, user, &e, offset);
            }
        }
        r->step++;
    }
    s->arp_tick += add;
}
