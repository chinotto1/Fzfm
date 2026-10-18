/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fm_src.h"
#include <math.h>
#include <string.h>
FmResult fm_src_init(FmSrc *src, uint32_t input_rate, uint32_t output_rate, unsigned channels)
{
    if (!src || (channels != 1 && channels != 2) || input_rate < 8000 || input_rate > 96000 ||
        output_rate < 8000 || output_rate > 96000)
        return FM_ERR_ARGUMENT;
    memset(src, 0, sizeof(*src));
    src->input_rate = input_rate;
    src->output_rate = output_rate;
    src->channels = channels;
    uint32_t a = input_rate, b = output_rate;
    while (b)
    {
        uint32_t next = a % b;
        a = b;
        b = next;
    }
    src->divisor = a;
    unsigned phases = output_rate / a;
    src->exact_phases = phases <= FM_SRC_PHASE_LIMIT;
    src->phases = src->exact_phases ? phases : FM_SRC_PHASE_LIMIT;
    double cutoff = .47 * fmin(1.0, (double)output_rate / input_rate);
    for (unsigned phase = 0; phase < src->phases; phase++)
    {
        double fraction = (double)phase / src->phases, weight = 0;
        double coefficients[FM_SRC_TAPS];
        for (int offset = -15; offset <= 16; offset++)
        {
            double distance = fraction - offset;
            double x = 6.28318530717958647692 * cutoff * distance;
            double sinc = fabs(x) < 1e-12 ? 1 : sin(x) / x;
            double c = 2 * cutoff * sinc * (.5 + .5 * cos(3.14159265358979323846 * distance / 16));
            coefficients[offset + 15] = c;
            weight += c;
        }
        for (unsigned tap = 0; tap < FM_SRC_TAPS; tap++)
            src->coefficients[phase][tap] = (float)(coefficients[tap] / weight);
    }
    return FM_OK;
}
size_t fm_src_process(FmSrc *src, const float *input, size_t input_frames, size_t *consumed,
                      float *output, size_t capacity)
{
    if (consumed)
        *consumed = 0;
    if (!src || !consumed || !src->input_rate || !src->output_rate || !output ||
        (!input && input_frames))
        return 0;
    size_t produced = 0, used = 0;
    while (produced < capacity)
    {
        uint64_t center = src->next_numerator / src->output_rate;
        if (src->received > center + 16u)
        {
            uint32_t remainder = src->next_numerator % src->output_rate;
            unsigned phase = src->exact_phases
                                 ? remainder / src->divisor
                                 : (unsigned)((uint64_t)remainder * src->phases / src->output_rate);
            double sum[2] = {0, 0};
            for (int offset = -15; offset <= 16; offset++)
            {
                float coefficient = src->coefficients[phase][offset + 15];
                int64_t index = (int64_t)center + offset;
                if (index >= 0 && (uint64_t)index < src->received &&
                    src->received - (uint64_t)index <= FM_SRC_HISTORY)
                {
                    for (unsigned channel = 0; channel < src->channels; channel++)
                        sum[channel] +=
                            coefficient *
                            (double)src->history[(uint64_t)index % FM_SRC_HISTORY][channel];
                }
            }
            for (unsigned channel = 0; channel < src->channels; channel++)
                output[produced * src->channels + channel] = (float)sum[channel];
            produced++;
            src->next_numerator += src->input_rate;
        }
        else if (used < input_frames)
        {
            for (unsigned channel = 0; channel < src->channels; channel++)
                src->history[src->received % FM_SRC_HISTORY][channel] =
                    input[used * src->channels + channel];
            src->received++;
            used++;
        }
        else
            break;
    }
    *consumed = used;
    return produced;
}
