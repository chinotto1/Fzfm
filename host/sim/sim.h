/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* Browser and host panel. SELECT turns and does not click. */
#ifndef FZ_SIM_H
#define FZ_SIM_H

#include <stdint.h>

void sim_boot(void);
void sim_button(int button, int down, uint32_t time_us);
void sim_poll(uint32_t time_us);
/* 0 SELECT (no action), 1 ALGORITHM, 2..5 knobs 1..4. */
void sim_encoder(int encoder, int detents);
/* Key index 0 is F3, 26 is G5. */
void sim_key(int index, int down, uint32_t time_us);
/* Interleaved stereo int16 at 36 kHz. Returns the peak absolute sample.
 * sim_mix() is a 1024-frame buffer the page can pass to sim_render. */
int sim_render(int16_t *stereo, int frames);
/* Notes whose voices are still sounding. */
int sim_voices(void);
/* Project file. sim_file_gen() changes when OCT+ asks to download or pick.
 * sim_load() reads len bytes already stored at sim_file_ptr(). */
uint8_t *sim_file_ptr(void);
uint32_t sim_file_cap(void);
uint32_t sim_file_len(void);
uint32_t sim_file_gen(void);
int sim_file_kind(void);
int sim_load(uint32_t len);
/* 36 kHz mono for the open sampling screen. Write sim_capture_buf() first. */
int16_t *sim_capture_buf(void);
int sim_capture_cap(void);
void sim_capture(int frames);
int16_t *sim_mix(void);
uint16_t *sim_lcd(void);
char *sim_status(void);
uint32_t sim_tick(void);

#endif
