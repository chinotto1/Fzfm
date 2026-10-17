/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* Tempo-locked arpeggiator. The page layout follows the public SLOOP guide
 * (mode, rate, octaves, gate, swing, probability, hold, note order). This
 * file is not SLOOP source. */
#ifndef FZ_ARP_H
#define FZ_ARP_H

#include <stdint.h>

enum {
    FZ_ARP_OFF = 0,
    FZ_ARP_UP,
    FZ_ARP_DN,
    FZ_ARP_UPDN,
    FZ_ARP_RND,
    FZ_ARP_ORD,
    FZ_ARP_MODE_COUNT
};

enum {
    FZ_ARP_1_4 = 0,
    FZ_ARP_1_8,
    FZ_ARP_1_8T,
    FZ_ARP_1_16,
    FZ_ARP_16T,
    FZ_ARP_1_32,
    FZ_ARP_RATE_COUNT
};

#define FZ_ARP_NOTES 8

typedef struct {
    int note;
    int on; /* 1 note-on, 0 note-off */
} FzArpEvent;

typedef struct {
    int mode;
    int rate;
    int octaves; /* 1..4 */
    int gate;    /* percent of the rate, 20..100 */
    int swing;   /* 0..100, delays every other step */
    int prob;    /* 0..100 */
    int hold;    /* latch the chord after the keys come up */
    int order_play; /* ORD mode: 0 pitch order, 1 the order the keys went down */
    uint8_t chord[FZ_ARP_NOTES];
    uint8_t down[FZ_ARP_NOTES];
    int chord_n;
    int step;
    int cursor; /* index in fz_arp_pattern, or -1 */
    int live;   /* MIDI note sounding, or -1 */
    int armed;
    uint32_t tick;
    uint64_t acc;
    uint32_t origin;
    uint32_t next_on;
    uint32_t gate_off; /* tick of the pending note-off; 0 is a real tick */
    int gate_pending;
    uint32_t rng;
} FzArp;

void fz_arp_init(FzArp *arp);
void fz_arp_key(FzArp *arp, int note, int down);
void fz_arp_turn(FzArp *arp, int page, int knob, int detents);
int fz_arp_advance(FzArp *arp, uint32_t frames, uint32_t bpm_milli, FzArpEvent *out, int cap);
/* One cycle in playback order. cursor is the last note that sounded. */
int fz_arp_pattern(const FzArp *arp, int *dst, int cap, int *cursor);

const char *fz_arp_mode_name(int mode);
const char *fz_arp_rate_name(int rate);

#endif
