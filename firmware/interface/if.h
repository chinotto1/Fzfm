/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* Host-tested audio interface. Stereo routing, limiter, sequencer and
 * arpeggiator. No equalizer: the 5-band filter and its delay are not
 * allocated. Processing is 36 kHz. This is not a USB or Bluetooth device
 * stack and it is not the sampler image. */
#ifndef IF_H
#define IF_H

#include <stdint.h>

#define IF_TRACKS 8u
#define IF_STEPS 64u
#define IF_CAP_N 128u
#define IF_REC_MAX 128u
#define IF_HELD 16u
#define IF_RATE 36000u
#define IF_BEAT_UNITS 24576u
#define IF_BPM_MIN 40
#define IF_BPM_MAX 240
#define IF_EQ_BANDS 0
#define IF_FREE_REST_MS 8000u
#define IF_UI_W 240
#define IF_UI_H 240
#define IF_ARP_LOG 64u
#define IF_STEP_X0 8
#define IF_STEP_Y 120
#define IF_STEP_S 12
#define IF_STEP_G 2
#define IF_METER_X 72
#define IF_METER_Y 78
#define IF_METER_W 120
#define IF_REC_X 200
#define IF_REC_Y 44

enum {
    IF_DIV_1_4,
    IF_DIV_1_8,
    IF_DIV_1_16,
    IF_DIV_1_32,
    IF_DIV_8T,
    IF_DIV_16T,
    IF_DIV_1_2,
    IF_DIV_1_1,
    IF_DIV_2BAR,
    IF_DIV_4BAR,
    IF_DIV_COUNT
};

enum {
    IF_ARP_OFF,
    IF_ARP_UP,
    IF_ARP_DOWN,
    IF_ARP_UPDOWN,
    IF_ARP_RANDOM,
    IF_ARP_ORDER,
    IF_ARP_REPEAT
};

enum { IF_OUT_LOOP, IF_OUT_MIX, IF_OUT_LOCAL };

enum { IF_PAGE_MIX, IF_PAGE_SEQ, IF_PAGE_ARP, IF_PAGE_COUNT };

enum { IF_CAP_OK, IF_CAP_EMPTY, IF_CAP_RECORDING, IF_CAP_FULL };

enum {
    IF_COL_BG,
    IF_COL_TEXT,
    IF_COL_PANEL,
    IF_COL_BLUE,
    IF_COL_GREEN,
    IF_COL_AMBER,
    IF_COL_MUTED,
    IF_COL_RED,
    IF_COL_COUNT
};

/* Quarter-note = 24576 musical units, the same grid the nightly tempo
 * function divides by. Audio samples are not part of this unit. */
static inline uint32_t if_div_units(unsigned div)
{
    uint32_t quarter = IF_BEAT_UNITS;
    if (div >= IF_DIV_COUNT)
        div = IF_DIV_1_16;
    switch (div) {
    case IF_DIV_1_4:
        return quarter;
    case IF_DIV_1_8:
        return quarter / 2u;
    case IF_DIV_1_16:
        return quarter / 4u;
    case IF_DIV_1_32:
        return quarter / 8u;
    case IF_DIV_8T:
        return quarter / 3u;
    case IF_DIV_16T:
        return quarter / 6u;
    case IF_DIV_1_2:
        return quarter * 2u;
    case IF_DIV_1_1:
        return quarter * 4u;
    case IF_DIV_2BAR:
        return quarter * 8u;
    default:
        return quarter * 16u;
    }
}

/* Milliseconds times BPM for `steps` at `div_units`. Integer order matches
 * Melodee nightly 3fe570d285d1d8da5438d93963c6048734e9e4d2. */
static inline uint32_t if_cap_loop_bpm_ms(uint32_t steps, uint32_t div_units)
{
    return (((steps * div_units / 96u) * 15000u) / 64u);
}

typedef struct {
    uint8_t n, vel, on;
    uint8_t note[4];
} IfStep;

typedef struct {
    uint16_t step, dur;
    uint8_t on, note, vel, used;
} IfRec;

typedef struct {
    IfStep step[IF_STEPS];
    IfRec rec[IF_REC_MAX];
    uint8_t rec_n, length, division, swing, gate;
    uint8_t arp_mode, arp_oct, arp_rate, arp_gate, arp_prob, arp_hold;
    int8_t arp_swing;
    uint8_t held[IF_HELD];
    uint8_t nheld, arp_phys, arp_note, sound_on, sound_note;
    uint16_t seq_idx, cap_step;
    uint32_t seq_pos, arp_pos, arp_idx, arp_off, sound_left, steps_entered;
    uint8_t level;
    int8_t pan;
    uint8_t mute;
    uint16_t phase;
} IfTrack;

typedef struct {
    uint16_t step, dur;
    uint8_t on, note, vel, tr;
} IfCapEv;

typedef struct {
    int valid, redo;
    uint8_t track, length, rec_n;
    IfStep step[IF_STEPS];
    IfRec rec[IF_REC_MAX];
} IfUndo;

typedef struct {
    uint8_t note, vel, down, track;
} IfArpEvent;

typedef struct {
    int status, free_take;
    unsigned steps, bpm;
    char text[40];
} IfCaptureInfo;

typedef struct {
    IfTrack track[IF_TRACKS];
    IfCapEv cap[IF_CAP_N];
    IfUndo undo;
    IfArpEvent arp_log[IF_ARP_LOG];
    uint8_t arp_log_n;
    uint32_t cap_w, cap_from, cap_free_t0, cap_free_last, ms, clock_rem, rng;
    uint16_t bpm;
    uint8_t cap_track, cap_free, sel, playing, clock_ext, rec, page, focus;
    uint8_t usb_level, usb_mute;
    int8_t usb_pan;
    uint8_t out_mode, clipped;
    int32_t ceiling, lim_env;
    uint16_t meter_l, meter_r;
    char message[40];
} IfMachine;

extern const uint32_t if_palette[IF_COL_COUNT];

void if_init(IfMachine *m);
void if_select(IfMachine *m, unsigned track);
void if_set_bpm(IfMachine *m, unsigned bpm);
void if_clock_external(IfMachine *m, int on);
void if_arm(IfMachine *m, int on);
void if_play(IfMachine *m, int on);
void if_note(IfMachine *m, unsigned track, unsigned note, unsigned vel, uint32_t ms);
int if_set_step(IfMachine *m, unsigned track, unsigned step, const uint8_t *notes, unsigned n, unsigned vel);
int if_track_empty(const IfMachine *m, unsigned track);
void if_advance(IfMachine *m, uint32_t frames, const int16_t *usb, int16_t *speaker, int16_t *usb_out);
IfCaptureInfo if_capture(IfMachine *m, uint32_t now_ms, uint32_t hold_age_ms);
int if_undo(IfMachine *m);
int if_redo(IfMachine *m);
void if_ui_draw(const IfMachine *m, uint8_t *fb);
const char *if_page_name(unsigned page);
int if_eq_bands(void);

#endif
