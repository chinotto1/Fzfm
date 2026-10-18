/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FM_CORE_H
#define FM_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FM_RATE 36000u
#define FM_HARMONICS 48u
#define FM_KIT_SLOTS 16u
#define FM_ANCHORS 16u
#define FM_FX_SLOTS 4u
#define FM_ENV_STAGES 8u
#define FM_LOOP_SECTIONS 8u
/* Corruption/safety ceilings, not preallocated track/audio banks. */
#ifndef FM_OBJECT_LIMIT
#define FM_OBJECT_LIMIT 256u
#endif
#define FM_PATTERN_SLOTS 32u
#define FM_NAME_LENGTH 32u
#define FM_PPQN 960u
typedef uint32_t FmId;
typedef uint64_t FmFrame;
typedef enum
{
    FM_OK,
    FM_ERR_ARGUMENT,
    FM_ERR_MEMORY,
    FM_ERR_STORAGE,
    FM_ERR_FORMAT,
    FM_ERR_RANGE,
    FM_ERR_BUSY,
    FM_ERR_NOT_FOUND,
    FM_ERR_UNSUPPORTED,
    FM_ERR_CANCELLED
} FmResult;
const char *fm_result_string(FmResult result);
typedef enum
{
    FM_MEM_METADATA,
    FM_MEM_AUDIO,
    FM_MEM_EVENTS,
    FM_MEM_FX,
    FM_MEM_UNDO,
    FM_MEM_COUNT
} FmMemoryKind;
typedef struct
{
    size_t limit[FM_MEM_COUNT], used[FM_MEM_COUNT], peak[FM_MEM_COUNT];
} FmBudget;
typedef struct
{
    float milliseconds, level;
} FmEnvStage;
typedef struct
{
    uint8_t count, sustain, release;
    float key_rate, velocity;
    FmEnvStage stage[FM_ENV_STAGES];
} FmEnvelope;
typedef enum
{
    FM_LFO_SINE,
    FM_LFO_TRIANGLE,
    FM_LFO_SQUARE,
    FM_LFO_SAW,
    FM_LFO_SAMPLE_HOLD
} FmLfoShape;
typedef enum
{
    FM_LOOP_CONTINUE,
    FM_LOOP_JUMP,
    FM_LOOP_END
} FmLoopAction;
typedef struct
{
    uint32_t start, end, crossfade;
    uint16_t repeats;
    uint8_t action, jump;
    bool until_release;
} FmLoopSection;
typedef enum
{
    FM_MULTI_SAMPLE,
    FM_SAMPLE
} FmInstrumentType;
typedef enum
{
    FM_SOURCE_CAPTURE,
    FM_SOURCE_ADDITIVE,
    FM_SOURCE_PRESET,
    FM_SOURCE_DRAW,
    FM_SOURCE_EXTRACT,
    FM_SOURCE_IMPORT,
    FM_SOURCE_RENDER
} FmSource;
typedef struct
{
    FmId id;
    uint32_t frames, rate, checksum;
    uint8_t channels;
    bool cycle, published, retired;
    FmSource source;
    int16_t *pcm;
} FmAsset;
typedef struct
{
    FmId id, asset;
    uint32_t start, end;
    uint8_t root, low, high, choke;
    bool manual_zone, one_shot, reverse, loop_enabled;
    float tuning, gain, pan, sends[FM_FX_SLOTS];
    uint8_t baked_mask;
    float cutoff, resonance, drive, filter_env, keytrack;
    float lfo_rate, lfo_depth;
    uint8_t lfo_shape, lfo_destination;
    FmEnvelope amp, filter;
    uint8_t loop_count;
    FmLoopSection loops[FM_LOOP_SECTIONS];
} FmRegion;
typedef struct
{
    FmId id;
    FmInstrumentType type;
    char name[FM_NAME_LENGTH];
    uint8_t base_note, anchor_class;
    uint16_t count;
    FmRegion regions[FM_ANCHORS];
    uint16_t harmonics[FM_HARMONICS];
    bool additive;
    uint32_t revision;
} FmInstrument;
typedef struct
{
    FmId region;
    FmRegion original;
    uint8_t cleared_sends;
    float track_sends[FM_FX_SLOTS];
} FmBakeRecipe;
typedef enum
{
    FM_EVENT_NOTE_ON,
    FM_EVENT_NOTE_OFF,
    FM_EVENT_CONTROL,
    FM_EVENT_SUSTAIN,
    FM_EVENT_BEND
} FmEventKind;
typedef enum
{
    FM_ORIGIN_LOCAL,
    FM_ORIGIN_USB,
    FM_ORIGIN_TRS,
    FM_ORIGIN_SEQUENCER,
    FM_ORIGIN_PICKER,
    FM_ORIGIN_LOOPBACK
} FmOrigin;
typedef enum
{
    FM_PARAM_CUTOFF,
    FM_PARAM_RESONANCE,
    FM_PARAM_VOLUME,
    FM_PARAM_PAN,
    FM_PARAM_SEND1,
    FM_PARAM_SEND2,
    FM_PARAM_SEND3,
    FM_PARAM_SEND4
} FmParameter;
typedef struct
{
    double tick;
    FmFrame frame;
    FmId track;
    uint32_t identity;
    FmEventKind kind;
    FmOrigin origin;
    uint8_t note, velocity, channel, parameter;
    float value;
    float probability;
} FmEvent;
typedef struct
{
    FmId id, track;
    FmEvent *events;
    uint32_t count, capacity;
    double length_ticks, rate, swing, quantize;
    FmId frozen_asset;
    uint8_t frozen_sends;
    double frozen_bpm;
} FmPattern;
typedef struct
{
    FmId id, instrument;
    FmId patterns[FM_PATTERN_SLOTS];
    uint8_t selected_pattern, playing_pattern;
    uint8_t chain[64], chain_count;
    int8_t queued_pattern;
    float volume, pan, cutoff, resonance, sends[FM_FX_SLOTS];
    bool mute, solo;
    uint8_t midi_channel;
} FmTrack;
typedef enum
{
    FM_FX_DELAY,
    FM_FX_REVERB,
    FM_FX_CHORUS,
    FM_FX_CHARACTER
} FmFxType;
typedef struct
{
    FmFxType type;
    bool enabled;
    float parameter[4], gain, pan;
    bool return_muted;
} FmFxConfig;
typedef struct
{
    FmId tracks[FM_OBJECT_LIMIT];
    uint8_t patterns[FM_OBJECT_LIMIT];
    bool muted[FM_OBJECT_LIMIT];
    uint16_t count, repeats;
    double bars;
} FmSongEntry;
typedef enum
{
    FM_CLOCK_INTERNAL,
    FM_CLOCK_USB,
    FM_CLOCK_TRS
} FmClockSource;
struct FmUndo;
typedef struct FmProject
{
    FmBudget budget;
    FmId next_id, selected_track;
    uint32_t generation;
    FmAsset *assets[FM_OBJECT_LIMIT];
    FmInstrument *instruments[FM_OBJECT_LIMIT];
    FmTrack *tracks[FM_OBJECT_LIMIT];
    FmPattern *patterns[FM_OBJECT_LIMIT];
    uint16_t asset_count, instrument_count, track_count, pattern_count;
    FmBakeRecipe *bake_recipes[FM_OBJECT_LIMIT];
    uint16_t bake_count;
    FmFxConfig fx[FM_FX_SLOTS];
    FmSongEntry *song;
    uint16_t song_count;
    double bpm;
    uint8_t beats_per_bar;
    FmClockSource clock_source;
    bool tempo_established, dirty, volatile_audio;
    struct FmUndo *undo;
    void *allocations;
} FmProject;

FmBudget fm_budget_host(void);
FmResult fm_project_init(FmProject *project, const FmBudget *budget);
void fm_project_destroy(FmProject *project);
void *fm_alloc(FmProject *project, FmMemoryKind kind, size_t bytes);
void fm_dealloc(FmProject *project, FmMemoryKind kind, void *ptr, size_t bytes);
FmAsset *fm_asset(FmProject *project, FmId id);
FmInstrument *fm_instrument(FmProject *project, FmId id);
FmTrack *fm_track(FmProject *project, FmId id);
FmPattern *fm_pattern(FmProject *project, FmId id);
FmRegion *fm_region(FmProject *project, FmId id);
FmResult fm_asset_create(FmProject *, const int16_t *, uint32_t frames, uint8_t channels,
                         uint32_t rate, bool cycle, FmSource, FmId *out);
/* Only unpublished failed output; never a live/undo asset garbage collector. */
FmResult fm_asset_discard(FmProject *, FmId);
FmResult fm_instrument_create(FmProject *, FmInstrumentType, FmId *track);
FmResult fm_region_attach(FmProject *, FmId instrument, FmId asset, uint8_t root, int slot,
                          FmId *region);
void fm_map_automatic(FmInstrument *);
FmRegion *fm_map_note(FmInstrument *, uint8_t note);
FmResult fm_name_category(FmProject *, FmId instrument, uint8_t category);
FmResult fm_pattern_create(FmProject *, FmId track, uint8_t slot, FmId *pattern);
FmResult fm_pattern_append(FmProject *, FmPattern *, const FmEvent *);
FmEnvelope fm_envelope_default(void);
uint32_t fm_crc32(const void *, size_t);
/* Non-RT transactions watch only touched metadata/event ranges. Objects/assets
 * are retained until project disposal, so undo and active voices keep valid IDs. */
FmResult fm_edit_begin(FmProject *);
FmResult fm_edit_watch(FmProject *, void *, size_t);
FmResult fm_edit_commit(FmProject *);
void fm_edit_cancel(FmProject *);
FmResult fm_undo(FmProject *);
FmResult fm_redo(FmProject *);
uint64_t fm_edit_serial(FmProject *);
FmResult fm_edit_coalesce(FmProject *);
FmResult fm_remove_empty_instrument(FmProject *, FmId track);
FmResult fm_region_remove(FmProject *, FmId region);
/* Collect only superseded cycle revisions, with current/undo/redo and external
 * readers pinned. Callback is mandatory; single owner, outside render/edit. */
size_t fm_assets_collect_cycles(FmProject *, bool (*pinned)(void *, FmId), void *);
/* Host is a single owner: mutation and render never run concurrently. A target
 * adapter must hand commands over at an audio block boundary. */
#endif
