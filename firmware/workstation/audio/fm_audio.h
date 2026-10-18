/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FM_AUDIO_H
#define FM_AUDIO_H
#include "../core/fm.h"
#include "../dsp/fm_dsp.h"
#ifndef FM_ENGINE_EVENTS
#define FM_ENGINE_EVENTS 512u
#endif
#define FM_VOICE_LIMIT 128u
/* Reference host configuration only, never a declaration of FM-1 capacity. */
typedef struct
{
    bool active, held, sustained, frozen, kit;
    FmOrigin origin;
    float frozen_bpm;
    FmTrack *track;
    const FmAsset *asset, *previous_asset;
    double previous_phase, previous_step;
    uint32_t previous_start, previous_end;
    uint16_t table_fade;
    FmRegion region;
    uint8_t note, channel, loop;
    uint32_t identity, repetitions;
    uint64_t serial;
    double phase, step;
    float velocity, lfo_phase;
    FmFilter filters[2];
    FmEnvState amp, filter;
    float last[2], stolen[2];
    uint16_t ramp, choke_ramp;
} FmVoice;
typedef struct
{
    FmId track;
    bool sustain[FM_ORIGIN_LOOPBACK + 1][16];
    float bend[16], mute_gain;
    float volume, pan, cutoff, resonance, sends[FM_FX_SLOTS];
} FmTrackAudio;
typedef struct
{
    FmFxConfig config;
    float *buffer;
    size_t frames, bytes, position;
    float phase, state[16], energy, last[2], previous[2];
    uint16_t transition;
} FmFxState;
typedef struct
{
    FmId track, pattern;
    FmFrame frame;
    uint32_t identity;
} FmFrozenTrigger;
typedef struct FmEngine
{
    FmProject *project;
    FmVoice *voices;
    unsigned max_voices;
    FmFrame frame;
    uint64_t serial;
    unsigned stolen_voices, rejected_events;
    FmEvent events[FM_ENGINE_EVENTS];
    unsigned event_count;
    FmFrozenTrigger frozen[64];
    unsigned frozen_count;
    FmTrackAudio tracks[FM_OBJECT_LIMIT];
    FmFxState fx[FM_FX_SLOTS];
} FmEngine;
FmResult fm_engine_init(FmEngine *, FmProject *, unsigned max_voices);
void fm_engine_destroy(FmEngine *);
/* Absolute audio frame timestamps, including intra-block offsets. Bounded queue.
 * Single owner only: call at boundaries, or from that same render owner. */
FmResult fm_engine_event(FmEngine *, const FmEvent *);
void fm_engine_render(FmEngine *, float *stereo, size_t frames);
/* Fully rendered pattern contains final stereo gain/pan/filter/envelope/FX.
 * Playback bypasses those controls and all live sends; unfreeze to edit them.
 * A zero pattern schedules a 64-frame release of frozen voices on that track. */
FmResult fm_engine_frozen_event(FmEngine *, FmId track, FmId pattern, FmFrame, uint32_t identity);
unsigned fm_engine_active_voices(const FmEngine *);
void fm_engine_panic(FmEngine *);
/* Non-real-time atomic preparation. A failure leaves prior slot valid.
 * Disabled slots retain tails; release requires explicit discard or silence. */
FmResult fm_engine_fx_prepare(FmEngine *, unsigned slot, const FmFxConfig *);
/* Prepare all changed slots before publishing any (undo/redo). */
FmResult fm_engine_fx_sync(FmEngine *, uint8_t mask);
/* Low-level release: caller owns admission/lifetime proof. discard_tail=true
 * is deliberately destructive; automatic reclamation must use scanner below. */
FmResult fm_engine_fx_release(FmEngine *, unsigned slot, bool discard_tail);
/* Non-RT conservative scan includes stored/queued automation and held readers.
 * BUSY may accompany partial success, exactly reported in released_mask. */
FmResult fm_engine_fx_reclaim_unused(FmEngine *, uint8_t requested_mask, uint8_t *released_mask);
size_t fm_fx_memory(FmFxType);
float fm_fx_max_delay(void);
size_t fm_engine_collect_cycles(FmEngine *);
#endif
