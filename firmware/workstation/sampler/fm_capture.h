/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FM_CAPTURE_H
#define FM_CAPTURE_H
#include "../core/fm.h"
#define FM_PRETRIGGER_MAX 720u
typedef enum
{
    FM_CAPTURE_CONFIGURE,
    FM_CAPTURE_WAIT_INPUT,
    FM_CAPTURE_PRETRIGGER,
    FM_CAPTURE_CAPTURING,
    FM_CAPTURE_END_DETECTED,
    FM_CAPTURE_FINALIZE,
    FM_CAPTURE_COMMIT_OR_PIN,
    FM_CAPTURE_ADVANCE,
    FM_CAPTURE_PAUSED,
    FM_CAPTURE_RETRY,
    FM_CAPTURE_STORAGE_FULL,
    FM_CAPTURE_INPUT_LOST,
    FM_CAPTURE_FINISHED,
    FM_CAPTURE_CANCELLED
} FmCaptureState;
typedef enum
{
    FM_INPUT_MONO_SUM,
    FM_INPUT_LEFT,
    FM_INPUT_RIGHT
} FmInputSource;
typedef struct
{
    float threshold, stop_threshold, input_gain;
    uint32_t pretrigger_frames, silence_frames, minimum_frames, maximum_frames;
    uint32_t trim_silence_frames;
    bool manual;
    FmInputSource source;
} FmCaptureConfig;
typedef struct
{
    FmProject *project;
    FmId track, instrument, last_region;
    FmCaptureConfig config;
    FmCaptureState state;
    FmResult error;
    int16_t *buffer, pretrigger[FM_PRETRIGGER_MAX];
    uint32_t frames, pre_count, pre_write, quiet, dropped_busy, clips;
    uint8_t slot, root;
    FmFrame first_frame, clock;
    float peak;
    bool finish_requested, listening, replace;
} FmCapture;
typedef struct
{
    size_t limit_bytes, used_bytes, free_bytes, recording_frames;
} FmCaptureCapacity;
/* Mono PCM16 capacity after reserving a staging take. Includes retained undo PCM.
 * Sample counts derived from recording_frames are audio-budget estimates only. */
FmCaptureCapacity fm_capture_capacity(const FmProject *, const FmCapture *,
                                      uint32_t maximum_frames);
FmCaptureConfig fm_capture_defaults(void);
/* begin/destroy/service/finish/retry are non-RT. accept never allocates or
 * persists: FINALIZE is visibly BUSY until service commits a pinned RAM asset. */
FmResult fm_capture_begin(FmCapture *, FmProject *, FmId track, uint8_t root,
                          const FmCaptureConfig *);
/* Explicit slot targeting; replacing retains the old region until commit. */
FmResult fm_capture_begin_slot(FmCapture *, FmProject *, FmId track, uint8_t root,
                               const FmCaptureConfig *, uint8_t slot, bool replace);
void fm_capture_destroy(FmCapture *);
void fm_capture_accept(FmCapture *, const int16_t *mono, size_t frames, FmFrame first_frame);
void fm_capture_accept_stereo(FmCapture *, const float *stereo, size_t frames, FmFrame first_frame);
FmResult fm_capture_service(FmCapture *);
FmResult fm_capture_finish(FmCapture *);
FmResult fm_capture_resume(FmCapture *, uint8_t root);
FmResult fm_capture_retry(FmCapture *);
FmResult fm_capture_skip(FmCapture *);
FmResult fm_capture_manual_start(FmCapture *);
void fm_capture_input_lost(FmCapture *);
double fm_capture_seconds_available(const FmCapture *);
#endif
