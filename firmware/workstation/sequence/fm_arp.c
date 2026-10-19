/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Original implementation; SLOOP's two ARP pages are the control reference. */
#include "fm_sequence.h"
#include <math.h>
FmArpConfig fm_arp_default(void)
{
    return (FmArpConfig){.division = 3, .octaves = 1, .gate = .5f, .probability = 1};
}
const char *fm_arp_mode_name(unsigned i)
{
    static const char *const names[] = {"OFF",        "UP",     "DOWN",    "UP-DOWN", "RANDOM",
                                        "PLAY ORDER", "REPEAT", "DOWN-UP", "UP+OCT",  "CONVERGE",
                                        "DIVERGE",    "PINKY",  "THUMB",   "WALK",    "CHORD"};
    return names[i < FM_ARP_MODE_COUNT ? i : 0];
}
const char *fm_arp_rate_name(unsigned i)
{
    static const char *const names[] = {"1/4",   "1/8",  "1/8T",  "1/16",
                                        "1/16T", "1/32", "1/32T", "1/64"};
    return names[i < 8 ? i : 3];
}
FmResult fm_arp_set(FmProject *p, FmId id, unsigned field, double value)
{
    FmTrack *t = fm_track(p, id);
    if (!t)
        return FM_ERR_NOT_FOUND;
    if (!isfinite(value) || field > 7)
        return FM_ERR_ARGUMENT;
    FmArpConfig next = t->arp;
    switch (field)
    {
    case 0:
        if (value < 0 || value >= FM_ARP_MODE_COUNT || floor(value) != value)
            return FM_ERR_RANGE;
        next.mode = (uint8_t)value;
        break;
    case 1:
        if (value < 0 || value > 7 || floor(value) != value)
            return FM_ERR_RANGE;
        next.division = (uint8_t)value;
        break;
    case 2:
        if (value < 1 || value > 4 || floor(value) != value)
            return FM_ERR_RANGE;
        next.octaves = (uint8_t)value;
        break;
    case 3:
        if (value < .05 || value > 1)
            return FM_ERR_RANGE;
        next.gate = (float)value;
        break;
    case 4:
        if (value < 0 || value > .49)
            return FM_ERR_RANGE;
        next.swing = (float)value;
        break;
    case 5:
        if (value < 0 || value > 1)
            return FM_ERR_RANGE;
        next.probability = (float)value;
        break;
    case 6:
        if (value != 0 && value != 1)
            return FM_ERR_RANGE;
        next.latch = value != 0;
        break;
    case 7:
        if (value != 0 && value != 1)
            return FM_ERR_RANGE;
        next.play_order = value != 0;
        break;
    }
    FmResult r = fm_edit_begin(p);
    if (r == FM_OK)
        r = fm_edit_watch(p, &t->arp, sizeof(t->arp));
    bool establish =
        field == 0 && next.mode && p->clock_source == FM_CLOCK_INTERNAL && !p->tempo_established;
    if (r == FM_OK && establish)
        r = fm_edit_watch(p, &p->tempo_established, sizeof(p->tempo_established));
    if (r == FM_OK)
    {
        if (establish)
            p->tempo_established = true;
        t->arp = next;
        r = fm_edit_commit(p);
    }
    else
        fm_edit_cancel(p);
    return r;
}
