/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fm_audio.h"
#include <math.h>
#include <string.h>
void fm_fx_tick(FmFxState *, float, float, float *, float *);
static float clamp(float x, float a, float b)
{
    return fminf(b, fmaxf(a, x));
}
static FmTrackAudio *track_audio(FmEngine *engine, FmId id)
{
    FmTrackAudio *empty = NULL;
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
    {
        if (engine->tracks[i].track == id)
            return &engine->tracks[i];
        if (!engine->tracks[i].track && !empty)
            empty = &engine->tracks[i];
    }
    if (empty)
    {
        empty->track = id;
        empty->mute_gain = 1;
        FmTrack *track = fm_track(engine->project, id);
        if (track)
        {
            empty->volume = track->volume;
            empty->pan = track->pan;
            empty->cutoff = track->cutoff;
            empty->resonance = track->resonance;
            memcpy(empty->sends, track->sends, sizeof(empty->sends));
        }
    }
    return empty;
}
FmResult fm_engine_init(FmEngine *engine, FmProject *project, unsigned max_voices)
{
    if (!engine || !project || !max_voices || max_voices > FM_VOICE_LIMIT)
        return FM_ERR_ARGUMENT;
    memset(engine, 0, sizeof(*engine));
    engine->project = project;
    engine->max_voices = max_voices;
    engine->voices = fm_alloc(project, FM_MEM_METADATA, (size_t)max_voices * sizeof(FmVoice));
    if (!engine->voices)
    {
        memset(engine, 0, sizeof(*engine));
        return FM_ERR_MEMORY;
    }
    memset(engine->voices, 0, (size_t)max_voices * sizeof(FmVoice));
    for (unsigned i = 0; i < FM_FX_SLOTS; i++)
    {
        if (project->fx[i].enabled)
        {
            FmResult r = fm_engine_fx_prepare(engine, i, &project->fx[i]);
            if (r != FM_OK)
            {
                fm_engine_destroy(engine);
                return r;
            }
        }
    }
    return FM_OK;
}
void fm_engine_destroy(FmEngine *engine)
{
    if (!engine || !engine->project)
        return;
    for (unsigned i = 0; i < FM_FX_SLOTS; i++)
        (void)fm_engine_fx_release(engine, i, true);
    if (engine->voices)
        fm_dealloc(engine->project, FM_MEM_METADATA, engine->voices,
                   (size_t)engine->max_voices * sizeof(FmVoice));
    memset(engine, 0, sizeof(*engine));
}
static unsigned event_priority(const FmEvent *e)
{
    if (e->kind == FM_EVENT_NOTE_OFF)
        return 3;
    if (e->kind == FM_EVENT_CONTROL)
        return e->origin == FM_ORIGIN_LOCAL ? 3 : (e->identity & 0x80000000u) ? 2 : 1;
    return e->kind == FM_EVENT_NOTE_ON ? 3 : 2;
}
FmResult fm_engine_event(FmEngine *engine, const FmEvent *event)
{
    if (!engine || !engine->project || !event || event->channel >= 16 || event->note > 127 ||
        event->velocity > 127 || (unsigned)event->kind > FM_EVENT_BEND ||
        (unsigned)event->origin > FM_ORIGIN_SEQUENCER || !isfinite(event->value))
        return FM_ERR_ARGUMENT;
    if (engine->event_count >= FM_ENGINE_EVENTS)
    {
        engine->rejected_events++;
        return FM_ERR_BUSY;
    }
    unsigned index = engine->event_count++;
    while (index && (engine->events[index - 1u].frame > event->frame ||
                     (engine->events[index - 1].frame == event->frame &&
                      event_priority(&engine->events[index - 1]) > event_priority(event))))
    {
        engine->events[index] = engine->events[index - 1u];
        index--;
    }
    engine->events[index] = *event;
    return FM_OK;
}
FmResult fm_engine_frozen_event(FmEngine *engine, FmId track, FmId pattern, FmFrame frame,
                                uint32_t identity)
{
    if (!engine || !engine->project || !fm_track(engine->project, track))
        return FM_ERR_ARGUMENT;
    FmPattern *pat = fm_pattern(engine->project, pattern);
    if (pattern && (!pat || pat->track != track || !pat->frozen_asset ||
                    (!isfinite(pat->frozen_bpm) || pat->frozen_bpm <= 0)))
        return FM_ERR_ARGUMENT;
    if (engine->frozen_count >= 64)
        return FM_ERR_BUSY;
    unsigned index = engine->frozen_count++;
    while (index && engine->frozen[index - 1u].frame > frame)
    {
        engine->frozen[index] = engine->frozen[index - 1u];
        index--;
    }
    engine->frozen[index] = (FmFrozenTrigger){track, pattern, frame, identity};
    return FM_OK;
}
static void begin_frozen(FmEngine *engine, const FmFrozenTrigger *trigger)
{
    if (!trigger->pattern)
    {
        for (unsigned i = 0; i < engine->max_voices; i++)
            if (engine->voices[i].active && engine->voices[i].frozen &&
                engine->voices[i].track->id == trigger->track)
                engine->voices[i].choke_ramp = 64;
        return;
    }
    FmPattern *pattern = fm_pattern(engine->project, trigger->pattern);
    FmTrack *track = fm_track(engine->project, trigger->track);
    FmAsset *asset = pattern ? fm_asset(engine->project, pattern->frozen_asset) : NULL;
    if (!asset || !track || !asset->frames || !asset->pcm)
        return;
    (void)track_audio(engine, track->id);
    FmVoice *voice = NULL;
    for (unsigned i = 0; i < engine->max_voices; i++)
    {
        FmVoice *candidate = &engine->voices[i];
        if (candidate->active && candidate->frozen && candidate->track == track)
            candidate->choke_ramp = 64;
        if (!candidate->active && !voice)
            voice = candidate;
    }
    float previous[2] = {0, 0};
    if (!voice)
    {
        voice = &engine->voices[0];
        for (unsigned i = 1; i < engine->max_voices; i++)
            if (engine->voices[i].serial < voice->serial)
                voice = &engine->voices[i];
        previous[0] = voice->last[0];
        previous[1] = voice->last[1];
        engine->stolen_voices++;
    }
    memset(voice, 0, sizeof(*voice));
    voice->active = true;
    voice->frozen = true;
    voice->frozen_bpm = (float)pattern->frozen_bpm;
    voice->asset = asset;
    voice->track = track;
    voice->identity = trigger->identity;
    voice->region.start = 0;
    voice->region.end = asset->frames;
    voice->serial = ++engine->serial;
    voice->stolen[0] = previous[0];
    voice->stolen[1] = previous[1];
    if (previous[0] != 0 || previous[1] != 0)
        voice->ramp = 64;
}
static void release_voice(FmVoice *voice)
{
    voice->held = false;
    voice->sustained = false;
    if (voice->region.baked_mask & 0x80u)
    {
        voice->choke_ramp = 64;
        return;
    }
    fm_env_release(&voice->amp);
    fm_env_release(&voice->filter);
}
static double voice_step(const FmVoice *voice, float bend)
{
    double tuning = (double)clamp(voice->region.tuning, -9600, 9600) / 100.0;
    double semitones = (double)voice->note - (double)voice->region.root + tuning + (double)bend;
    if (voice->asset->cycle)
        return (double)(voice->region.end - voice->region.start) * 440.0 *
               exp2(((double)voice->note - 69.0 + tuning + (double)bend) / 12.0) / (double)FM_RATE;
    if (voice->kit)
        semitones = tuning + (double)bend;
    return ((double)voice->asset->rate / (double)FM_RATE) * exp2(semitones / 12.0);
}
static void note_on(FmEngine *engine, const FmEvent *event, FmTrack *track, FmTrackAudio *state)
{
    FmInstrument *instrument = fm_instrument(engine->project, track->instrument);
    FmRegion *region = instrument ? fm_map_note(instrument, event->note) : NULL;
    FmAsset *asset = region ? fm_asset(engine->project, region->asset) : NULL;
    if (!asset || !asset->pcm || !region || region->start >= region->end ||
        region->end > asset->frames || region->loop_count > FM_LOOP_SECTIONS)
        return;
    FmVoice *voice = NULL;
    for (unsigned i = 0; i < engine->max_voices; i++)
    {
        FmVoice *candidate = &engine->voices[i];
        if (region->choke && candidate->active && candidate->track == track &&
            candidate->region.choke == region->choke)
            candidate->choke_ramp = 64;
        if (!candidate->active && !voice)
            voice = candidate;
    }
    float stolen[2] = {0, 0};
    if (!voice)
    {
        voice = &engine->voices[0];
        for (unsigned i = 1; i < engine->max_voices; i++)
        {
            FmVoice *candidate = &engine->voices[i];
            if ((!candidate->held && voice->held) ||
                (candidate->held == voice->held && candidate->serial < voice->serial))
                voice = candidate;
        }
        stolen[0] = voice->last[0];
        stolen[1] = voice->last[1];
        engine->stolen_voices++;
    }
    memset(voice, 0, sizeof(*voice));
    voice->active = true;
    voice->held = true;
    voice->track = track;
    voice->asset = asset;
    voice->region = *region;
    voice->kit = instrument->type == FM_MULTI_SAMPLE;
    voice->origin = event->origin;
    voice->note = event->note;
    voice->channel = event->channel;
    voice->identity = event->identity;
    voice->serial = ++engine->serial;
    voice->velocity = (float)event->velocity / 127.0f;
    voice->phase = region->reverse ? (double)(region->end - 1u) : (double)region->start;
    voice->step = voice_step(voice, state->bend[event->channel]);
    voice->stolen[0] = stolen[0];
    voice->stolen[1] = stolen[1];
    voice->ramp = (region->baked_mask & 0x80u) && stolen[0] == 0 && stolen[1] == 0 ? 0 : 64;
    fm_env_start(&voice->amp, &region->amp, event->note, event->velocity);
    fm_env_start(&voice->filter, &region->filter, event->note, event->velocity);
}
static void apply_event(FmEngine *engine, const FmEvent *event)
{
    FmTrack *track = fm_track(engine->project, event->track);
    if (!track)
        return;
    FmTrackAudio *state = track_audio(engine, event->track);
    if (!state)
        return;
    if (event->kind == FM_EVENT_NOTE_ON && event->velocity)
    {
        note_on(engine, event, track, state);
        return;
    }
    if (event->kind == FM_EVENT_NOTE_OFF || (event->kind == FM_EVENT_NOTE_ON && !event->velocity))
    {
        FmVoice *match = NULL;
        for (unsigned i = 0; i < engine->max_voices; i++)
        {
            FmVoice *voice = &engine->voices[i];
            if (!voice->active || !voice->held || voice->track != track ||
                voice->note != event->note || voice->channel != event->channel ||
                voice->origin != event->origin)
                continue;
            if (event->identity && voice->identity != event->identity)
                continue;
            if (!match || voice->serial < match->serial)
                match = voice;
        }
        if (match)
        {
            match->held = false;
            if (!match->region.one_shot)
            {
                if (state->sustain[event->origin][event->channel])
                    match->sustained = true;
                else
                    release_voice(match);
            }
        }
    }
    else if (event->kind == FM_EVENT_SUSTAIN)
    {
        state->sustain[event->origin][event->channel] = event->value >= 0.5f;
        if (!state->sustain[event->origin][event->channel])
            for (unsigned i = 0; i < engine->max_voices; i++)
            {
                FmVoice *voice = &engine->voices[i];
                if (voice->active && voice->track == track && voice->channel == event->channel &&
                    voice->origin == event->origin && voice->sustained)
                    release_voice(voice);
            }
    }
    else if (event->kind == FM_EVENT_BEND)
    {
        /* Value in semitones: MIDI adapter owns normalized bend/range mapping. */
        state->bend[event->channel] = clamp(event->value, -48, 48);
        for (unsigned i = 0; i < engine->max_voices; i++)
            if (engine->voices[i].active && engine->voices[i].track == track &&
                engine->voices[i].channel == event->channel)
                engine->voices[i].step =
                    voice_step(&engine->voices[i], state->bend[event->channel]);
    }
    else if (event->kind == FM_EVENT_CONTROL)
    {
        float value = event->value;
        switch (event->parameter)
        {
        case FM_PARAM_CUTOFF:
            track->cutoff = clamp(value, 0, 1);
            break;
        case FM_PARAM_RESONANCE:
            track->resonance = clamp(value, 0, 1);
            break;
        case FM_PARAM_VOLUME:
            track->volume = clamp(value, 0, 4);
            break;
        case FM_PARAM_PAN:
            track->pan = clamp(value, -1, 1);
            break;
        default:
            if (event->parameter >= FM_PARAM_SEND1 && event->parameter <= FM_PARAM_SEND4)
                track->sends[event->parameter - FM_PARAM_SEND1] = clamp(value, 0, 1);
            break;
        }
    }
}
static float read_asset(const FmAsset *asset, uint32_t start, uint32_t end, double phase,
                        unsigned channel, bool wrap)
{
    if (phase < start)
        phase = start;
    if (phase >= end)
        phase = end - 1u;
    uint32_t a = (uint32_t)phase, b = a + 1u;
    float fraction = (float)(phase - a);
    if (b >= end)
        b = wrap ? start : a;
    if (asset->channels == 1)
        channel = 0;
    float x = asset->pcm[(size_t)a * asset->channels + channel] / 32768.0f;
    float y = asset->pcm[(size_t)b * asset->channels + channel] / 32768.0f;
    return x + (y - x) * fraction;
}
static float read_at(const FmVoice *voice, double phase, unsigned channel, bool wrap)
{
    return read_asset(voice->asset, voice->region.start, voice->region.end, phase, channel, wrap);
}
/* Immutable table publication is observed at an audio block boundary. Held
 * cycle voices retain phase and crossfade both readers for 64 frames. */
static void sync_cycle_edits(FmEngine *engine)
{
    for (unsigned i = 0; i < engine->max_voices; i++)
    {
        FmVoice *v = &engine->voices[i];
        if (!v->active || v->frozen || !v->asset->cycle || v->region.baked_mask & 0x80)
            continue;
        FmRegion *r = fm_region(engine->project, v->region.id);
        const FmAsset *a = r ? fm_asset(engine->project, r->asset) : NULL;
        if (!a || !a->cycle || a == v->asset || r->end <= r->start || r->end > a->frames)
            continue;
        v->previous_asset = v->asset;
        v->previous_phase = v->phase;
        v->previous_step = v->step;
        v->previous_start = v->region.start;
        v->previous_end = v->region.end;
        double fraction = (v->phase - v->region.start) / (v->region.end - v->region.start);
        v->asset = a;
        v->region = *r;
        v->phase = r->start + fraction * (r->end - r->start);
        FmTrackAudio *state = track_audio(engine, v->track->id);
        v->step = voice_step(v, state ? state->bend[v->channel] : 0);
        v->table_fade = 64;
    }
}
static float sample_voice(const FmVoice *voice, unsigned channel)
{
    float value = read_at(voice, voice->phase, channel, voice->asset->cycle);
    if (voice->table_fade && voice->previous_asset)
    {
        float before = read_asset(voice->previous_asset, voice->previous_start, voice->previous_end,
                                  voice->previous_phase, channel, true);
        float t = voice->table_fade / 64.0f;
        value = value * (1 - t) + before * t;
    }
    if (voice->region.loop_enabled && !voice->region.reverse &&
        voice->loop < voice->region.loop_count)
    {
        const FmLoopSection *loop = &voice->region.loops[voice->loop];
        if (loop->crossfade && loop->end > loop->start &&
            loop->crossfade <= loop->end - loop->start &&
            voice->phase >= (double)(loop->end - loop->crossfade) &&
            voice->phase < (double)loop->end)
        {
            float blend = (float)(voice->phase - (double)(loop->end - loop->crossfade)) /
                          (float)loop->crossfade;
            float other = read_at(
                voice, (double)loop->start + voice->phase - (double)(loop->end - loop->crossfade),
                channel, false);
            value = value * (1 - blend) + other * blend;
        }
    }
    return value;
}
static void advance_voice(FmVoice *voice, double step)
{
    if (voice->table_fade)
    {
        double start_old = voice->previous_start, length_old = voice->previous_end - start_old;
        voice->previous_phase = start_old + fmod(voice->previous_phase - start_old +
                                                     (voice->region.reverse ? -voice->previous_step
                                                                            : voice->previous_step),
                                                 length_old);
        if (voice->previous_phase < start_old)
            voice->previous_phase += length_old;
        if (!--voice->table_fade)
            voice->previous_asset = NULL;
    }
    double start = (double)voice->region.start, end = (double)voice->region.end;
    voice->phase += voice->region.reverse ? -step : step;
    if (voice->asset->cycle)
    {
        double length = end - start;
        voice->phase = start + fmod(voice->phase - start, length);
        if (voice->phase < start)
            voice->phase += length;
        return;
    }
    if (voice->region.loop_enabled && !voice->region.reverse)
    {
        for (unsigned transitions = 0; transitions < FM_LOOP_SECTIONS; transitions++)
        {
            if (voice->loop >= voice->region.loop_count)
                break;
            const FmLoopSection *loop = &voice->region.loops[voice->loop];
            if (loop->start < voice->region.start || loop->end > voice->region.end ||
                loop->end <= loop->start || loop->crossfade >= loop->end - loop->start)
            {
                voice->active = false;
                return;
            }
            if (voice->phase < (double)loop->end)
                break;
            double overshoot = voice->phase - (double)loop->end;
            if ((loop->until_release && !voice->amp.released) || voice->repetitions < loop->repeats)
            {
                double begin = (double)(loop->start + loop->crossfade),
                       length = (double)loop->end - begin;
                if (length <= 0)
                {
                    voice->active = false;
                    return;
                }
                if (loop->until_release && !voice->amp.released)
                    overshoot = fmod(overshoot, length);
                else
                {
                    uint32_t available = (uint32_t)loop->repeats - voice->repetitions;
                    uint32_t traversals =
                        1u + (uint32_t)fmin(floor(overshoot / length), (double)(available - 1u));
                    voice->repetitions += traversals;
                    overshoot -= (double)(traversals - 1u) * length;
                }
                voice->phase = begin + overshoot;
            }
            else
            {
                voice->repetitions = 0;
                if (loop->action == FM_LOOP_END)
                {
                    voice->active = false;
                    return;
                }
                unsigned next =
                    loop->action == FM_LOOP_JUMP ? loop->jump : (unsigned)voice->loop + 1u;
                if (next >= voice->region.loop_count)
                {
                    voice->loop = voice->region.loop_count;
                    break;
                }
                voice->loop = (uint8_t)next;
                voice->phase = (double)voice->region.loops[next].start + overshoot;
            }
        }
    }
    if (voice->phase < start || voice->phase >= end)
        voice->active = false;
}
void fm_engine_render(FmEngine *engine, float *stereo, size_t frames)
{
    if (!engine || !engine->voices || !stereo)
        return;
    sync_cycle_edits(engine);
    bool any_solo = false;
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        if (engine->project->tracks[i] && engine->project->tracks[i]->solo)
            any_solo = true;
    for (size_t frame = 0; frame < frames; frame++, engine->frame++)
    {
        while (engine->event_count && engine->events[0].frame <= engine->frame)
        {
            FmEvent event = engine->events[0];
            engine->event_count--;
            memmove(engine->events, engine->events + 1u,
                    (size_t)engine->event_count * sizeof(FmEvent));
            apply_event(engine, &event);
        }
        while (engine->frozen_count && engine->frozen[0].frame <= engine->frame)
        {
            FmFrozenTrigger trigger = engine->frozen[0];
            engine->frozen_count--;
            memmove(engine->frozen, engine->frozen + 1u,
                    (size_t)engine->frozen_count * sizeof(*engine->frozen));
            begin_frozen(engine, &trigger);
        }
        float dry[2] = {0, 0}, sends[FM_FX_SLOTS][2] = {{0}};
        for (unsigned t = 0; t < FM_OBJECT_LIMIT; t++)
            if (engine->tracks[t].track)
            {
                FmTrack *track = fm_track(engine->project, engine->tracks[t].track);
                float target = track && !track->mute && (!any_solo || track->solo) ? 1 : 0;
                FmTrackAudio *state = &engine->tracks[t];
                state->mute_gain += 0.01f * (target - state->mute_gain);
                if (track)
                {
                    state->volume += 0.01f * (track->volume - state->volume);
                    state->pan += 0.01f * (track->pan - state->pan);
                    state->cutoff += 0.01f * (track->cutoff - state->cutoff);
                    state->resonance += 0.01f * (track->resonance - state->resonance);
                    for (unsigned fx = 0; fx < FM_FX_SLOTS; fx++)
                        state->sends[fx] += 0.01f * (track->sends[fx] - state->sends[fx]);
                }
            }
        for (unsigned i = 0; i < engine->max_voices; i++)
        {
            FmVoice *voice = &engine->voices[i];
            if (!voice->active)
                continue;
            if (voice->frozen || (voice->region.baked_mask & 0x80u))
            {
                FmTrackAudio *frozen_state = track_audio(engine, voice->track->id);
                float gain = frozen_state ? frozen_state->mute_gain : 1;
                if (voice->choke_ramp)
                {
                    gain *= (float)voice->choke_ramp / 64.0f;
                    if (!--voice->choke_ramp)
                        voice->active = false;
                }
                for (unsigned channel = 0; channel < 2; channel++)
                {
                    float value = read_at(voice, voice->phase, channel, false) * gain;
                    if (voice->ramp)
                    {
                        float blend = (float)voice->ramp / 64.0f;
                        value = value * (1 - blend) + voice->stolen[channel] * blend;
                    }
                    voice->last[channel] = value;
                    dry[channel] += value;
                }
                if (voice->ramp)
                    voice->ramp--;
                if (voice->frozen)
                {
                    voice->phase += (double)voice->asset->rate / (double)FM_RATE *
                                    (double)clamp((float)engine->project->bpm, 1, 1000) /
                                    (double)voice->frozen_bpm;
                    if (voice->phase >= (double)voice->region.end)
                        voice->active = false;
                }
                else
                    advance_voice(voice, voice->step);
                continue;
            }
            FmTrack *track = voice->track;
            FmTrackAudio *state = track_audio(engine, track->id);
            float amp = fm_env_tick(&voice->amp), env = fm_env_tick(&voice->filter);
            if (voice->amp.finished)
            {
                voice->active = false;
                continue;
            }
            if (voice->region.lfo_shape & 0x80u)
                voice->lfo_phase = (float)fmod(
                    (double)engine->frame / (double)FM_RATE * (double)voice->region.lfo_rate, 1);
            float lfo = fm_lfo_tick(&voice->lfo_phase, voice->region.lfo_rate,
                                    voice->region.lfo_shape & 0x7fu) *
                        voice->region.lfo_depth;
            float cutoff =
                (state ? state->cutoff : track->cutoff) + voice->region.cutoff - 1 +
                voice->region.filter_env * env +
                voice->region.keytrack * ((float)voice->note - (float)voice->region.root) / 120.0f;
            if (voice->region.lfo_destination == 0)
                cutoff += lfo;
            float frequency = 30 * powf(500, clamp(cutoff, 0, 1));
            float velocity = (1 - clamp(voice->region.amp.velocity, 0, 1)) +
                             clamp(voice->region.amp.velocity, 0, 1) * voice->velocity;
            float gain = amp * velocity * voice->region.gain *
                         (state ? state->volume : track->volume) * (state ? state->mute_gain : 1);
            if (voice->region.lfo_destination == 1)
                gain *= clamp(1 + lfo, 0, 2);
            if (voice->choke_ramp)
            {
                gain *= (float)voice->choke_ramp / 64.0f;
                if (!--voice->choke_ramp)
                    voice->active = false;
            }
            float pan = clamp((state ? state->pan : track->pan) + voice->region.pan +
                                  (voice->region.lfo_destination == 3 ? lfo : 0),
                              -1, 1),
                  pans[2] = {sqrtf((1 - pan) * 0.5f), sqrtf((1 + pan) * 0.5f)};
            for (unsigned channel = 0; channel < 2; channel++)
            {
                float value = sample_voice(voice, channel);
                value = fm_filter_tick(&voice->filters[channel], value, frequency,
                                       (state ? state->resonance : track->resonance) +
                                           voice->region.resonance,
                                       voice->region.drive);
                value *= gain * pans[channel];
                if (voice->ramp)
                    value = value * (1 - (float)voice->ramp / 64.0f) +
                            voice->stolen[channel] * (float)voice->ramp / 64.0f;
                voice->last[channel] = value;
                dry[channel] += value;
                for (unsigned fx = 0; fx < FM_FX_SLOTS; fx++)
                    if (!(voice->region.baked_mask & (1u << fx)))
                        sends[fx][channel] += value *
                                              (state ? state->sends[fx] : track->sends[fx]) *
                                              voice->region.sends[fx];
            }
            if (voice->ramp)
                voice->ramp--;
            advance_voice(voice, voice->step * (voice->region.lfo_destination == 2
                                                    ? exp2((double)lfo / 12.0)
                                                    : 1.0));
        }
        for (unsigned fx = 0; fx < FM_FX_SLOTS; fx++)
        {
            FmFxState *effect = &engine->fx[fx];
            float l, r;
            if (!effect->buffer && effect->config.type != FM_FX_CHARACTER)
                continue;
            fm_fx_tick(effect, sends[fx][0], sends[fx][1], &l, &r);
            dry[0] += l;
            dry[1] += r;
        }
        stereo[2u * frame] = clamp(dry[0], -1, 1);
        stereo[2u * frame + 1u] = clamp(dry[1], -1, 1);
    }
}
unsigned fm_engine_active_voices(const FmEngine *engine)
{
    unsigned count = 0;
    if (!engine)
        return 0;
    for (unsigned i = 0; i < engine->max_voices; i++)
        count += engine->voices[i].active ? 1u : 0u;
    return count;
}
void fm_engine_panic(FmEngine *engine)
{
    if (!engine)
        return;
    for (unsigned i = 0; i < engine->max_voices; i++)
        if (engine->voices[i].active)
        {
            if (engine->voices[i].frozen)
                engine->voices[i].choke_ramp = 64;
            else
                release_voice(&engine->voices[i]);
        }
    for (unsigned i = 0; i < FM_OBJECT_LIMIT; i++)
        memset(engine->tracks[i].sustain, 0, sizeof(engine->tracks[i].sustain));
    engine->event_count = 0;
    engine->frozen_count = 0;
}

static bool cycle_pinned(void *user, FmId id)
{
    FmEngine *engine = user;
    for (unsigned i = 0; i < engine->max_voices; i++)
    {
        const FmVoice *v = &engine->voices[i];
        if (v->active && ((v->asset && v->asset->id == id) ||
                          (v->previous_asset && v->previous_asset->id == id)))
            return true;
    }
    return false;
}
size_t fm_engine_collect_cycles(FmEngine *engine)
{
    return engine && engine->project
               ? fm_assets_collect_cycles(engine->project, cycle_pinned, engine)
               : 0;
}
