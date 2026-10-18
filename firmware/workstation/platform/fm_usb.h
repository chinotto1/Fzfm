/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FM_USB_H
#define FM_USB_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* R02/R03: transport only. One owner or an externally held IRQ critical
 * section must protect ALL calls; volatile is not synchronization. The board
 * adapter must call ssync before delivering a DMA buffer here. No allocation,
 * monitoring, flash write or SRC occurs in this bounded interface. */
#define FM_USB_RX_RATE 44100u
#define FM_USB_MAX_PACKET_FRAMES 49u
#define FM_USB_MAX_PACKET_BYTES (FM_USB_MAX_PACKET_FRAMES * 4u)
typedef struct
{
    int16_t *pcm;
    uint32_t capacity, read, write, count;
    uint32_t generation;
    uint64_t frames_received, frames_read;
    uint32_t bad_packets, overruns, underruns, inactive_packets, resets;
    bool active;
} FmUsbRx;

bool fm_usb_rx_init(FmUsbRx *, int16_t *stereo_storage, uint32_t frame_capacity);
/* Only 44.1 kHz, stereo, PCM16LE alt1 is implemented. Inactive/unsupported
 * transitions discard stale audio and increment generation exactly once. */
bool fm_usb_rx_stream(FmUsbRx *, bool connected, bool suspended, uint8_t alternate, uint32_t rate,
                      uint8_t bits, uint8_t channels);
bool fm_usb_rx_packet(FmUsbRx *, const uint8_t *, size_t bytes);
uint32_t fm_usb_rx_read(FmUsbRx *, int16_t *stereo, uint32_t max_frames);
uint32_t fm_usb_rx_available(const FmUsbRx *);
void fm_usb_rx_reset(FmUsbRx *);
#endif
