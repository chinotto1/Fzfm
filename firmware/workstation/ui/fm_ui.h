/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FM_UI_H
#define FM_UI_H
#include "../audio/fm_audio.h"
#include "../sampler/fm_capture.h"
#include "../sequence/fm_sequence.h"
#define FM_DISPLAY_WIDTH 240u
#define FM_DISPLAY_HEIGHT 240u
#define FM_UI_BUTTONS 14u
#define FM_UI_KEYS 27u
#define FM_UI_HOLD_MS 500u
/* Physical FM-1 board indexes: SCL is the documented logical SEL. Encoders
 * never have invented push actions. Pixels index the shared RGB palette;
 * the board converts one scanline to RGB565 without another framebuffer. */
typedef enum
{
    FM_UI_BG,
    FM_UI_TEXT,
    FM_UI_PANEL,
    FM_UI_BLUE,
    FM_UI_GREEN,
    FM_UI_AMBER,
    FM_UI_MUTED,
    FM_UI_RED,
    FM_UI_GRID,
    FM_UI_COLOR_COUNT
} FmUiColor;
extern const uint32_t fm_ui_palette[FM_UI_COLOR_COUNT];
static inline uint16_t fm_ui_rgb565(uint8_t index)
{
    uint32_t rgb = fm_ui_palette[index < FM_UI_COLOR_COUNT ? index : FM_UI_BG];
    return (uint16_t)(((rgb >> 8) & 0xf800u) | ((rgb >> 5) & 0x07e0u) | ((rgb >> 3) & 0x001fu));
}
static inline void fm_ui_rgb565_bytes(uint8_t index, uint8_t out[2])
{
    uint16_t color = fm_ui_rgb565(index);
    out[0] = (uint8_t)(color >> 8);
    out[1] = (uint8_t)color;
}
typedef enum
{
    FM_BUTTON_OCT_MINUS,
    FM_BUTTON_OCT_PLUS,
    FM_BUTTON_FX,
    FM_BUTTON_SCL,
    FM_BUTTON_ENV,
    FM_BUTTON_LFO,
    FM_BUTTON_EDIT,
    FM_BUTTON_GLO,
    FM_BUTTON_HOME,
    FM_BUTTON_SAVE,
    FM_BUTTON_ARP,
    FM_BUTTON_SEQ,
    FM_BUTTON_PLAY,
    FM_BUTTON_REC
} FmButton;
typedef enum
{
    FM_SCREEN_HOME,
    FM_SCREEN_EDIT,
    FM_SCREEN_MIXER,
    FM_SCREEN_FX,
    FM_SCREEN_STEP,
    FM_SCREEN_PATTERNS,
    FM_SCREEN_SONG,
    FM_SCREEN_CAPTURE,
    FM_SCREEN_COMMANDS,
    FM_SCREEN_NEW,
    FM_SCREEN_SOURCE,
    FM_SCREEN_STEP_DETAIL,
    FM_SCREEN_PATTERN_SETTINGS,
    FM_SCREEN_TIMING,
    FM_SCREEN_LIBRARY,
    FM_SCREEN_ARP,
    FM_SCREEN_ROLL
} FmScreen;
typedef enum
{
    FM_PICK_NONE,
    FM_PICK_NUMBER,
    FM_PICK_NOTE,
    FM_PICK_LOW,
    FM_PICK_HIGH,
    FM_PICK_SAMPLE,
    FM_PICK_NAME,
    FM_PICK_CONFIRM
} FmPicker;
typedef struct
{
    bool down, held;
    uint64_t down_ms;
    FmScreen screen;
    FmPicker picker;
    FmId track;
} FmButtonState;
typedef struct
{
    bool down, sounding;
    FmId track;
    uint8_t note;
    uint32_t identity;
} FmKeyState;
typedef FmResult (*FmUiSave)(void *user, FmProject *project);
typedef FmResult (*FmUiCaptureCommand)(void *user, unsigned command, FmId track, uint8_t root,
                                       uint64_t time_ms);
/* capture command: 0 begin/resume; 1 finish at press; 2 undo last; 3 manual
 * start/end; 4 retry; 5 skip; 6 apply settings. User supplies the real adapter. */
typedef struct
{
    FmProject *project;
    FmEngine *engine;
    FmSequencer *sequence;
    FmScreen screen;
    FmPicker picker;
    FmButtonState buttons[FM_UI_BUTTONS];
    FmKeyState keys[FM_UI_KEYS];
    uint64_t now_ms;
    uint32_t next_identity;
    int octave;
    unsigned tab, rows[9], scope;
    unsigned mixer_send, fx_slot, page, selected_step, song_row, command;
    unsigned arp_page, roll_low, roll_span;
    double roll_tick;
    bool roll_move;
    unsigned harmonic, stage, loop_section, source, point;
    unsigned mix_source;
    float mix_gain_a, mix_gain_b, mix_tuning;
    int32_t mix_offset;
    uint32_t mix_overlap;
    bool mix_equal_power;
    unsigned pattern_keys;
    bool pattern_chained, scene_scope;
    uint8_t step_note, step_velocity, pick_low;
    double step_length;
    char number[24];
    unsigned number_length;
    char status[64];
    FmResult last_result;
    bool save_pending;
    FmUiSave save, load;
    FmScreen return_screen;
    unsigned options_row, library_item, capture_row, wave_shape, wave_points;
    float step_probability, lock_value;
    double step_micro;
    FmParameter lock_parameter;
    FmId clipboard_pattern, pending_instrument;
    int pending_action;
    FmCaptureConfig capture_config;
    const FmCapture *capture_monitor;
    struct
    {
        FmId id;
        unsigned tab, scope, rows[9];
    } views[FM_OBJECT_LIMIT];
    FmId view_instrument, last_capture;
    uint64_t gesture_ms, gesture_serial;
    unsigned gesture_key;

    FmUiCaptureCommand capture;
    void *user;
    uint8_t pixels[FM_DISPLAY_WIDTH * FM_DISPLAY_HEIGHT];
} FmUi;
extern const uint8_t fm_ui_white_keys[16];
void fm_ui_init(FmUi *, FmProject *, FmEngine *, FmSequencer *);
void fm_ui_callbacks(FmUi *, FmUiSave, FmUiCaptureCommand, void *user);
void fm_ui_button(FmUi *, FmButton, bool down, uint64_t time_ms);
void fm_ui_key(FmUi *, unsigned physical_index, bool down, uint64_t time_ms);
void fm_ui_encoder(FmUi *, unsigned physical_index, int delta, uint64_t time_ms);
void fm_ui_tick(FmUi *, uint64_t time_ms);
void fm_ui_render(FmUi *);
/* Absolute values from the accessible host editor use the same transactions
 * and ALL/ONE scope as the physical encoders. Selectors are zero based. */
FmResult fm_ui_set_parameter(FmUi *, unsigned tab, unsigned row, unsigned field, double value);
FmResult fm_ui_use_source(FmUi *, unsigned source);
FmResult fm_ui_history(FmUi *, bool redo);
#define FM_UI_COMMAND_COUNT 28u
const char *fm_ui_command_label(unsigned);
FmResult fm_ui_workspace_action(FmUi *, unsigned, bool confirmed);
FmResult fm_ui_workspace_confirm(FmUi *);
bool fm_ui_workspace_encoder(FmUi *, unsigned knob, int delta);
FmResult fm_ui_capture_setting(FmUi *, int delta);
void fm_ui_sync_view(FmUi *);
FmResult fm_ui_roll_edit(FmUi *, unsigned knob, int delta);
FmResult fm_ui_roll_toggle(FmUi *);
void fm_ui_roll_open(FmUi *);
void fm_ui_roll_next(FmUi *, int delta);
const char *fm_ui_screen_name(FmScreen);
/* Board/simulator sequencing dispatch, preserving absolute frame timestamp. */
void fm_ui_sequence_dispatch(void *engine, const FmEvent *, uint32_t offset);
#endif
