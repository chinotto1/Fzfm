/* SPDX-License-Identifier: GPL-3.0-or-later
 * Development board binding, not a qualified device release. R02/R03/R33.
 * BSP sequence references retain their original MPL notices in third_party. */
#include "audio/fm_audio.h"
#include "audio/fm_src.h"
#include "core/fm.h"
#include "midi/fm_midi.h"
#include "platform/fm_heap.h"
#include "platform/fm_usb.h"
#include "sampler/fm_capture.h"
#include "sequence/fm_sequence.h"
#include "ui/fm_ui.h"
#include <math.h>
#include <string.h>
#include <unistd.h>

#define HALF_FRAMES 128u
#define FELUCCA_CDC 0
#define FELUCCA_UAC 1
#define FELUCCA_UAC_OUT 1
#define FELUCCA_UAC_48K 0
#define FELUCCA_OTA 0 /* No flash layout or firmware writing enabled. */
#define RING_PUBLISH() __asm__ volatile("csync" ::: "memory")
#include "fm1_audio.h"
#include "fm1_guard.h"
#include "fm1_input.h"
#include "fm1_irq.h"
#include "fm1_lcd_hw.h"
#include "fm1_sys.h"
#include "fm1_time.h"
#include "fm1_timer.h"
static void raw_packet(const uint8_t *, uint32_t);
static void raw_reset(void);
#define FM1_UAC_RX_PACKET(p, n) raw_packet(p, n)
#define FM1_UAC_RX_STREAM_RESET() raw_reset()
#include "usb.c"

#define POOL __attribute__((section(".pool"), aligned(16)))
#define HEAP_BYTES (224u * 1024u)
static uint8_t heap[HEAP_BYTES] POOL;
static FmHeap allocator;
/* All target allocations use this single-owner bounded heap; no ROM addresses. */
void *malloc(size_t n)
{
    return fm_heap_alloc(&allocator, n);
}
void free(void *p)
{
    fm_heap_free(&allocator, p);
}
void *calloc(size_t n, size_t size)
{
    if (size && n > SIZE_MAX / size)
        return NULL;
    void *p = malloc(n * size);
    if (p)
        memset(p, 0, n * size);
    return p;
}
void *realloc(void *p, size_t n)
{
    return fm_heap_realloc(&allocator, p, n);
}
struct _reent;
void *_malloc_r(struct _reent *r, size_t n)
{
    (void)r;
    return malloc(n);
}
void _free_r(struct _reent *r, void *p)
{
    (void)r;
    free(p);
}
void *_calloc_r(struct _reent *r, size_t n, size_t size)
{
    (void)r;
    return calloc(n, size);
}
void *_realloc_r(struct _reent *r, void *p, size_t n)
{
    (void)r;
    return realloc(p, n);
}
void *_sbrk(int n)
{
    (void)n;
    return (void *)-1;
}
int _write(int fd, const void *p, unsigned n)
{
    (void)fd;
    (void)p;
    (void)n;
    return -1;
}
int _read(int fd, void *p, unsigned n)
{
    (void)fd;
    (void)p;
    (void)n;
    return -1;
}
int _close(int fd)
{
    (void)fd;
    return -1;
}
int _lseek(int fd, int off, int whence)
{
    (void)fd;
    (void)off;
    (void)whence;
    return -1;
}
int _isatty(int fd)
{
    (void)fd;
    return 0;
}
void _exit(int status)
{
    (void)status;
    for (;;)
    {
    }
}

static FmProject project;
static FmEngine engine POOL;
static FmUi ui POOL;
static FmSequencer *sequence;
static FmMidi midi;
static FmCapture capture;
static FmUsbRx receiver;
static int16_t rx_storage[512u * 2u];
static FmSrc input_src, output_src;
static uint32_t rx_generation;
static int32_t dma[2u * HALF_FRAMES * 2u] __attribute__((aligned(16)));
static int32_t ready[2][HALF_FRAMES * 2u] __attribute__((aligned(16)));
static volatile uint32_t ready_write, ready_read, missed_blocks;
typedef struct
{
    uint32_t ms;
    uint8_t index, down, key;
} TargetInput;
static TargetInput input_events[128];
static volatile uint32_t input_write, input_read, input_overflows;
static uint64_t uptime_ms;
static struct
{
    uint32_t magic, pending, failures;
} boot __attribute__((section(".noinit")));
static uint16_t lcd_row[240];
extern void isr_timer5(void), isr_alnk0(void);
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];

static void raw_reset(void)
{
    fm_usb_rx_reset(&receiver);
}
static void input_edge(unsigned index, bool down, bool key)
{
    if (input_write - input_read >= 128u)
    {
        input_overflows++;
        return;
    }
    TargetInput *event = &input_events[input_write & 127u];
    event->ms = fm1_ms;
    event->index = (uint8_t)index;
    event->down = (uint8_t)down;
    event->key = (uint8_t)key;
    RING_PUBLISH();
    input_write++;
}
static void raw_packet(const uint8_t *p, uint32_t n)
{
    (void)fm_usb_rx_stream(&receiver, usb.config != 0, usb.suspended != 0, uo.alt, 44100u, 16, 2);
    (void)fm_usb_rx_packet(&receiver, p, n);
}
void fm1_timer5_irq(void)
{
    static uint32_t sub, last, remainder, old_buttons, old_notes;
    uint32_t now = fm1_ticks(), delta = now - last;
    fm1_timer5_ack();
    last = now;
    remainder += delta;
    fm1_ms += remainder / 24000u;
    remainder %= 24000u;
    fm1_input_tick();
    uint32_t buttons = fm1_in.buttons, notes = fm1_in.notes;
    for (unsigned i = 0; i < 14; i++)
        if ((buttons ^ old_buttons) & (1u << i))
            input_edge(i, (buttons & (1u << i)) != 0, false);
    for (unsigned i = 0; i < 27; i++)
        if ((notes ^ old_notes) & (1u << i))
            input_edge(i, (notes & (1u << i)) != 0, true);
    old_buttons = buttons;
    old_notes = notes;
    if (++sub == 5u)
    {
        sub = 0;
        usb_poll();
    }
}
void fm1_alnk0_irq(void)
{
    uint8_t pending = fm1_audio_pending();
    fm1_audio_ack_aux(pending);
    if (pending & FM1_AUDIO_HALF)
    {
        uint32_t half = fm1_audio_free_half();
        int32_t *out = dma + half * HALF_FRAMES * 2u;
        if (ready_read != ready_write)
        {
            uint32_t slot = ready_read & 1u;
            RING_PUBLISH();
            for (unsigned i = 0; i < HALF_FRAMES * 2u; i++)
                out[i] = ready[slot][i];
            RING_PUBLISH();
            ready_read++;
        }
        else
        {
            for (unsigned i = 0; i < HALF_FRAMES * 2u; i++)
                out[i] = 0;
            missed_blocks++;
        }
        RING_PUBLISH();
        fm1_audio_ack_half();
    }
}
static void fm1_fault(const fm1_crash_t *fault)
{
    (void)fault;
    fm1_audio_stop();
    fm1_reboot();
}

static void lcd_command(uint8_t cmd, const uint8_t *bytes, unsigned count)
{
    static uint8_t data[4];
    fm1_lcd_send_cmd(cmd);
    fm1_lcd_wait();
    fm1_lcd_deselect();
    if (count)
    {
        for (unsigned i = 0; i < count; i++)
            data[i] = bytes[i];
        fm1_lcd_send_data(data, count);
        fm1_lcd_wait();
        fm1_lcd_deselect();
    }
}
static void display_init(void)
{
    uint8_t format = 0x55, orient = 0;
    fm1_lcd_hw_init();
    fm1_delay_ms(50);
    lcd_command(0x01, 0, 0);
    fm1_delay_ms(150);
    lcd_command(0x11, 0, 0);
    fm1_delay_ms(120);
    lcd_command(0x3a, &format, 1);
    lcd_command(0x36, &orient, 1);
    lcd_command(0x21, 0, 0);
    lcd_command(0x13, 0, 0);
    lcd_command(0x29, 0, 0);
}
static void display_row(unsigned row)
{
    uint8_t x[4] = {0, 0, 0, 239}, y[4] = {0, (uint8_t)row, 0, (uint8_t)row};
    for (unsigned i = 0; i < 240; i++)
        lcd_row[i] = ui.pixels[row * 240u + i] ? 0xffff : 0;
    lcd_command(0x2a, x, 4);
    lcd_command(0x2b, y, 4);
    lcd_command(0x2c, 0, 0);
    fm1_lcd_send_data(lcd_row, sizeof(lcd_row));
    fm1_lcd_wait();
    fm1_lcd_deselect();
}
static FmResult save_unavailable(void *user, FmProject *p)
{
    (void)user;
    (void)p;
    return FM_ERR_UNSUPPORTED; /* Never pretend RAM is durable. */
}
static FmResult capture_command(void *user, unsigned cmd, FmId track, uint8_t root, uint64_t ms)
{
    (void)user;
    (void)ms;
    if (cmd == 5)
        return fm_capture_skip(&capture);
    if (cmd == 0 || cmd == 6)
    {
        if (cmd == 0 && capture.project && capture.track == track)
            return fm_capture_resume(&capture, root);
        if (capture.state == FM_CAPTURE_CAPTURING)
            return FM_ERR_BUSY;
        fm_capture_destroy(&capture);
        return fm_capture_begin(&capture, &project, track, root, &ui.capture_config);
    }
    if (cmd == 1)
        return fm_capture_finish(&capture);
    if (cmd == 2 || cmd == 4)
        return fm_capture_retry(&capture);
    if (cmd == 3)
    {
        if (capture.state == FM_CAPTURE_CAPTURING)
            return fm_capture_finish(&capture);
        if (capture.state != FM_CAPTURE_WAIT_INPUT)
        {
            FmResult r = fm_capture_resume(&capture, root);
            if (r != FM_OK)
                return r;
        }
        return fm_capture_manual_start(&capture);
    }
    return FM_ERR_ARGUMENT;
}
static void capture_service(void)
{
    int16_t pcm[49u * 2u];
    float input[49u * 2u], converted[64u * 2u];
    uint32_t count, generation;
    bool active;
    fm1_irq_off();
    active = fm_usb_rx_stream(&receiver, usb.config != 0, usb.suspended != 0, uo.alt, 44100, 16, 2);
    generation = receiver.generation;
    count = fm_usb_rx_available(&receiver);
    if (count > 49u)
        count = 49u;
    if (count)
        count = fm_usb_rx_read(&receiver, pcm, count);
    fm1_irq_on();
    if (generation != rx_generation)
    {
        rx_generation = generation;
        (void)fm_src_init(&input_src, 44100, FM_RATE, 2);
        if (capture.project && (!active || capture.frames))
            fm_capture_input_lost(&capture);
    }
    for (unsigned i = 0; i < count * 2u; i++)
        input[i] = (float)pcm[i] / 32768.0f;
    size_t used = 0, frames = fm_src_process(&input_src, input, count, &used, converted, 64);
    if (used != count)
    { /* 49 input frames produce at most 40 output frames. */
        receiver.overruns++;
        if (capture.project)
            fm_capture_input_lost(&capture);
    }
    if (capture.project && frames)
        fm_capture_accept_stereo(&capture, converted, frames, capture.clock);
    if (capture.project)
        (void)fm_capture_service(&capture);
}
static void output_service(void)
{
    static float core[128u * 2u];
    static size_t position, remaining;
    float converted[HALF_FRAMES * 2u];
    size_t total = 0;
    if (ready_write - ready_read >= 2u)
        return;
    /* Bounded SRC lookahead needs at most two 128-frame input blocks here. */
    for (unsigned attempt = 0; attempt < 4u && total < HALF_FRAMES; attempt++)
    {
        if (!remaining)
        {
            fm_sequence_advance(sequence, 128, fm_ui_sequence_dispatch, &engine);
            fm_engine_render(&engine, core, 128);
            position = 0;
            remaining = 128;
        }
        size_t used = 0;
        size_t n = fm_src_process(&output_src, core + position * 2u, remaining, &used,
                                  converted + total * 2u, HALF_FRAMES - total);
        position += used;
        remaining -= used;
        total += n;
    }
    uint32_t slot = ready_write & 1u;
    for (unsigned i = 0; i < HALF_FRAMES * 2u; i++)
    {
        float v = i < total * 2u ? converted[i] : 0;
        if (v > 0.999999f)
            v = 0.999999f;
        if (v < -1.0f)
            v = -1.0f;
        ready[slot][i] = (int32_t)(v * 8388607.0f) * 256;
    }
    RING_PUBLISH();
    ready_write++;
}
static void midi_service(void)
{
    for (unsigned limit = 0; limit < 16u; limit++)
    {
        uint32_t packet;
        fm1_irq_off();
        if (midi_in_overflow)
        {
            mi_r = mi_w;
            midi_in_overflow = 0;
            fm1_irq_on();
            fm_midi_panic(&midi, engine.frame);
            break;
        }
        if (mi_r == mi_w)
        {
            fm1_irq_on();
            break;
        }
        packet = midi_in_q[mi_r % MQ];
        mi_r++;
        fm1_irq_on();
        uint8_t bytes[4] = {(uint8_t)packet, (uint8_t)(packet >> 8), (uint8_t)(packet >> 16),
                            (uint8_t)(packet >> 24)};
        (void)fm_midi_usb(&midi, bytes, sizeof(bytes), engine.frame);
    }
}
static void input_service(uint32_t now)
{
    static uint32_t seen_overflows;
    if (input_overflows != seen_overflows)
    {
        seen_overflows = input_overflows;
        fm1_irq_off();
        input_read = input_write;
        fm1_irq_on();
        memset(ui.buttons, 0, sizeof(ui.buttons));
        memset(ui.keys, 0, sizeof(ui.keys));
        fm_midi_panic(&midi, engine.frame);
        strcpy(ui.status, "INPUT QUEUE FULL");
        ui.last_result = FM_ERR_BUSY;
    }
    for (unsigned limit = 0; limit < 32u; limit++)
    {
        TargetInput event;
        fm1_irq_off();
        if (input_read == input_write)
        {
            fm1_irq_on();
            break;
        }
        event = input_events[input_read & 127u];
        input_read++;
        fm1_irq_on();
        uint32_t age = now - event.ms;
        uint64_t at = age > uptime_ms ? 0 : uptime_ms - age;
        if (event.key)
            fm_ui_key(&ui, event.index, event.down != 0, at);
        else
            fm_ui_button(&ui, (FmButton)event.index, event.down != 0, at);
    }
}
static bool initialize(void)
{
    if (!fm_heap_init(&allocator, heap, sizeof(heap)))
        return false;
    FmBudget budgets = {
        {64u * 1024u, 128u * 1024u, 64u * 1024u, 96u * 1024u, 48u * 1024u}, {0}, {0}};
    if (fm_project_init(&project, &budgets) != FM_OK)
        return false;
    project.fx[0].parameter[0] = .08f; /* Admitted short-delay development profile. */
    if (fm_engine_init(&engine, &project, 8) != FM_OK)
        return false;
    sequence = fm_sequence_create(&project, 128, 128);
    if (!sequence)
        return false;
    fm_midi_init(&midi, &project, &engine, sequence);
    FmId track, asset, region;
    if (fm_instrument_create(&project, FM_SAMPLE, &track) != FM_OK)
        return false;
    int16_t cycle[256];
    for (unsigned i = 0; i < 256; i++)
        cycle[i] = (int16_t)(sinf(6.28318530718f * i / 256.0f) * 16000);
    if (fm_asset_create(&project, cycle, 256, 1, FM_RATE, true, FM_SOURCE_PRESET, &asset) != FM_OK)
        return false;
    if (fm_region_attach(&project, fm_track(&project, track)->instrument, asset, 69, 0, &region) !=
        FM_OK)
        return false;
    (void)region;
    fm_ui_init(&ui, &project, &engine, sequence);
    fm_ui_callbacks(&ui, save_unavailable, capture_command, 0);
    ui.capture_monitor = &capture;
    ui.capture_config.maximum_frames = 4096;
    return fm_usb_rx_init(&receiver, rx_storage, 512) &&
           fm_src_init(&input_src, 44100, FM_RATE, 2) == FM_OK &&
           fm_src_init(&output_src, FM_RATE, 44100, 2) == FM_OK;
}
void fm1_cstart(void)
{
    uint32_t *s, *d;
    fm1_time_init();
    fm1_wdt_arm(0x0d);
    if (boot.magic != 0x465a4231u)
    {
        boot.magic = 0x465a4231u;
        boot.pending = boot.failures = 0;
    }
    if (boot.pending)
        boot.failures++;
    boot.pending = 1;
    if (boot.failures >= 2)
    {
        boot.pending = boot.failures = 0;
        fm1_enter_uboot();
    }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    display_init();
    if (!initialize())
        for (;;)
        {
        } /* watchdog resets; boot guard then enters UBOOT */
    fm1_input_init();
    usb_start();
    output_service();
    output_service();
    fm1_audio_init(dma, HALF_FRAMES * 2u, isr_alnk0, 3);
    fm1_timer5_start(isr_timer5, 4);
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    uint32_t last = 0, draw_ms = 0;
    unsigned row = 240;
    for (;;)
    {
        uint32_t now = fm1_ms;
        uptime_ms += (uint32_t)(now - last);
        last = now;
        fm1_wdt_feed();
        usb_retry(now);
        output_service();
        capture_service();
        midi_service();
        now = fm1_ms;
        uptime_ms += (uint32_t)(now - last);
        last = now;
        input_service(now);
        static const uint8_t enc[7] = {0, 1, 2, 3, 4, 5, 6};
        for (unsigned i = 0; i < 7; i++)
        {
            int delta = fm1_enc_take(enc[i]);
            if (delta)
                fm_ui_encoder(&ui, i, delta, uptime_ms);
        }
        fm_ui_tick(&ui, uptime_ms);
        fm1_led_key(1, capture.project && capture.listening && ((now / 250u) & 1u));
        output_service();
        if (row >= 240 && now - draw_ms >= 100u)
        {
            fm_ui_render(&ui);
            draw_ms = now;
            row = 0;
        }
        if (row < 240 && ready_write - ready_read >= 1u)
            display_row(row++);
        if (now > 30000u)
        {
            boot.pending = boot.failures = 0;
        }
    }
}
