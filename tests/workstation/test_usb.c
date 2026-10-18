/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "platform/fm_usb.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int test_usb(void)
{
    FmUsbRx rx;
    int16_t storage[8], out[12];
    const uint8_t packet[] = {0, 128, 255, 127, 255, 255, 1, 0};
    uint8_t oversize[FM_USB_MAX_PACKET_BYTES + 4u];
    uint32_t generation;
    assert(!fm_usb_rx_init(NULL, storage, 4));
    assert(!fm_usb_rx_init(&rx, storage, 0));
    assert(fm_usb_rx_init(&rx, storage, 4));
    assert(!fm_usb_rx_packet(&rx, packet, sizeof(packet)));
    assert(rx.inactive_packets == 1);
    assert(!fm_usb_rx_stream(&rx, true, false, 1, 48000, 16, 2));
    assert(!fm_usb_rx_stream(&rx, true, false, 1, 44100, 24, 2));
    assert(fm_usb_rx_stream(&rx, true, false, 1, 44100, 16, 2));
    assert(fm_usb_rx_packet(&rx, packet, sizeof(packet)));
    assert(!fm_usb_rx_packet(&rx, packet, 3));
    memset(oversize, 0, sizeof(oversize));
    assert(!fm_usb_rx_packet(&rx, oversize, sizeof(oversize)));
    assert(rx.bad_packets == 2 && fm_usb_rx_available(&rx) == 2);
    assert(fm_usb_rx_packet(&rx, packet, sizeof(packet)));
    assert(!fm_usb_rx_packet(&rx, packet, sizeof(packet)));
    assert(rx.overruns == 1 && fm_usb_rx_available(&rx) == 4);
    assert(fm_usb_rx_read(&rx, out, 3) == 3);
    assert(out[0] == -32768 && out[1] == 32767 && out[2] == -1 && out[3] == 1);
    assert(fm_usb_rx_packet(&rx, packet, sizeof(packet))); /* wraps */
    assert(fm_usb_rx_read(&rx, out, 6) == 3 && rx.underruns == 1);
    assert(out[0] == -1 && out[1] == 1 && out[2] == -32768 && out[3] == 32767);
    assert(fm_usb_rx_packet(&rx, packet, sizeof(packet)));
    generation = rx.generation;
    assert(!fm_usb_rx_stream(&rx, true, true, 1, 44100, 16, 2));
    assert(fm_usb_rx_available(&rx) == 0 && rx.generation == generation + 1u);
    assert(!fm_usb_rx_stream(&rx, false, false, 0, 44100, 16, 2));
    assert(rx.generation == generation + 1u); /* repeated inactive event */
    assert(fm_usb_rx_stream(&rx, true, false, 1, 44100, 16, 2));
    assert(fm_usb_rx_available(&rx) == 0);
    assert(fm_usb_rx_packet(&rx, NULL, 0)); /* legal zero-frame USB packet */
    assert(!fm_usb_rx_packet(&rx, NULL, 4));
    assert(rx.frames_received == 8 && rx.frames_read == 6);
    puts("USB packet/lifecycle bounds: PASS (T04 transport-host coverage)");
    return 0;
}
