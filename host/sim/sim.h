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
int16_t *sim_mix(void);
uint16_t *sim_lcd(void);
char *sim_status(void);

#endif
