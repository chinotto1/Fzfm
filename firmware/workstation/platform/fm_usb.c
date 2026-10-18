/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fm_usb.h"
#include <string.h>

bool fm_usb_rx_init(FmUsbRx *rx, int16_t *storage, uint32_t capacity)
{
    if (!rx || !storage || !capacity || capacity > UINT32_MAX / 4u)
        return false;
    memset(rx, 0, sizeof(*rx));
    rx->pcm = storage;
    rx->capacity = capacity;
    return true;
}

void fm_usb_rx_reset(FmUsbRx *rx)
{
    if (!rx || !rx->pcm)
        return;
    rx->read = rx->write = rx->count = 0;
    rx->generation++;
    rx->resets++;
}

bool fm_usb_rx_stream(FmUsbRx *rx, bool connected, bool suspended, uint8_t alternate, uint32_t rate,
                      uint8_t bits, uint8_t channels)
{
    bool active = connected && !suspended && alternate == 1u && rate == FM_USB_RX_RATE &&
                  bits == 16u && channels == 2u;
    if (!rx || !rx->pcm)
        return false;
    if (rx->active != active)
    {
        fm_usb_rx_reset(rx);
        rx->active = active;
    }
    return active;
}

static int16_t pcm16le(const uint8_t *p)
{
    uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8u);
    return (int16_t)(u < 32768u ? (int32_t)u : (int32_t)u - 65536);
}

bool fm_usb_rx_packet(FmUsbRx *rx, const uint8_t *bytes, size_t length)
{
    uint32_t frames, f;
    if (!rx || !rx->pcm)
        return false;
    if (!rx->active)
    {
        rx->inactive_packets++;
        return false;
    }
    if ((!bytes && length) || length % 4u || length > FM_USB_MAX_PACKET_BYTES)
    {
        rx->bad_packets++;
        return false;
    }
    frames = (uint32_t)(length / 4u);
    if (frames > rx->capacity - rx->count)
    {
        rx->overruns++;
        return false; /* Entire packet rejected; no partial/stale publication. */
    }
    for (f = 0; f < frames; ++f)
    {
        rx->pcm[rx->write * 2u] = pcm16le(bytes + f * 4u);
        rx->pcm[rx->write * 2u + 1u] = pcm16le(bytes + f * 4u + 2u);
        if (++rx->write == rx->capacity)
            rx->write = 0;
    }
    rx->count += frames;
    rx->frames_received += frames;
    return true;
}

uint32_t fm_usb_rx_read(FmUsbRx *rx, int16_t *out, uint32_t requested)
{
    uint32_t frames, f;
    if (!rx || !rx->pcm || !out)
        return 0;
    frames = requested < rx->count ? requested : rx->count;
    if (requested > rx->count)
        rx->underruns++;
    for (f = 0; f < frames; ++f)
    {
        out[f * 2u] = rx->pcm[rx->read * 2u];
        out[f * 2u + 1u] = rx->pcm[rx->read * 2u + 1u];
        if (++rx->read == rx->capacity)
            rx->read = 0;
    }
    rx->count -= frames;
    rx->frames_read += frames;
    return frames;
}

uint32_t fm_usb_rx_available(const FmUsbRx *rx)
{
    return rx && rx->pcm ? rx->count : 0;
}
