/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Native mixer and MIDI-event roll. Helpers come from render.c. */
static float voice_peak(FmUi *u, FmTrack *t)
{
    float peak = 0;
    if (!u->engine)
        return 0;
    for (unsigned i = 0; i < u->engine->max_voices && i < FM_VOICE_LIMIT; i++)
    {
        FmVoice *v = &u->engine->voices[i];
        if (v->active && v->track == t)
            peak += fmaxf(fabsf(v->last[0]), fabsf(v->last[1]));
    }
    return fminf(1, peak);
}
static void mixer(FmUi *u)
{
    FmTrack *list[FM_OBJECT_LIMIT];
    unsigned selected, count = track_list(u, list, &selected), first = selected / 4 * 4;
    FmInstrument *in = selected_instrument(u);
    char out[48], a[16] = "", b[16] = "", c[16] = "", d[16] = "";
    bold(u, 4, 39, in ? in->name : "NO TRACKS", 28, FM_UI_TEXT);
    for (unsigned i = first; i < count && i < first + 4; i++)
    {
        FmTrack *t = list[i];
        int x = 3 + (int)(i - first) * 59;
        rectangle(u, x, 54, 56, 132, i == selected ? FM_UI_BLUE : FM_UI_PANEL);
        if (i == selected)
            outline(u, x, 54, 56, 132, FM_UI_AMBER);
        snprintf(out, sizeof(out), "%02u", i + 1);
        bold(u, x + 5, 58, out, 2, FM_UI_TEXT);
        if (t->mute)
            bold(u, x + 29, 58, "M", 1, FM_UI_RED);
        if (t->solo)
            bold(u, x + 42, 58, "S", 1, FM_UI_AMBER);
        FmInstrument *sound = fm_instrument(u->project, t->instrument);
        text(u, x + 3, 73, sound ? sound->name : "MISSING", 8, FM_UI_MUTED);
        rectangle(u, x + 18, 86, 2, 70, FM_UI_GRID);
        int y = 151 - (int)(fminf(2, t->volume) / 2 * 60);
        rectangle(u, x + 8, y, 23, 6, t->mute ? FM_UI_MUTED : FM_UI_GREEN);
        rectangle(u, x + 41, 86, 5, 70, FM_UI_BG);
        int meter = (int)(70 * voice_peak(u, t));
        rectangle(u, x + 41, 156 - meter, 5, meter, FM_UI_GREEN);
        snprintf(out, sizeof(out), "%.2f", (double)t->volume);
        bold(u, x + 5, 163, out, 6, FM_UI_TEXT);
        snprintf(out, sizeof(out), "%+.2f", (double)t->pan);
        text(u, x + 5, 177, out, 7, FM_UI_MUTED);
    }
    FmTrack *focused = selected_track(u);
    if (focused)
        for (unsigned i = 0; i < 4; i++)
        {
            snprintf(out, sizeof(out), "%u %.2f", i + 1, (double)focused->sends[i]);
            text(u, 3 + (int)i * 60, 190, out, 9, i == u->mixer_send ? FM_UI_AMBER : FM_UI_MUTED);
        }
    FmTrack *t = selected_track(u);
    if (t)
    {
        snprintf(a, sizeof(a), "%u/%u", selected + 1, count);
        snprintf(b, sizeof(b), "%.2f", (double)t->volume);
        snprintf(c, sizeof(c), "%+.2f", (double)t->pan);
        snprintf(d, sizeof(d), "%.2f", (double)t->sends[u->mixer_send]);
    }
    snprintf(out, sizeof(out), "SEND %u", u->mixer_send + 1);
    footer(u, "TRACK", "LEVEL", "PAN", out, a, b, c, d);
}
static void arpeggiator(FmUi *u)
{
    FmTrack *t = selected_track(u);
    FmInstrument *in = selected_instrument(u);
    char value[4][24], line[48];
    bold(u, 4, 39, in ? in->name : "NO TRACK", 28, FM_UI_TEXT);
    snprintf(line, sizeof(line), "ARP %u/2 / PLAY HELD NOTES", u->arp_page + 1);
    text(u, 4, 54, line, 38, FM_UI_MUTED);
    if (!t)
        return;
    const char *labels[2][4] = {{"MODE", "RATE", "OCTAVES", "GATE"},
                                {"SWING", "PROBABILITY", "HOLD / LATCH", "ORDER"}};
    if (!u->arp_page)
    {
        snprintf(value[0], 24, "%s", fm_arp_mode_name(t->arp.mode));
        snprintf(value[1], 24, "%s", fm_arp_rate_name(t->arp.division));
        snprintf(value[2], 24, "%u", t->arp.octaves);
        snprintf(value[3], 24, "%.0f %%", t->arp.gate * 100.0);
    }
    else
    {
        snprintf(value[0], 24, "%.0f %%", t->arp.swing * 100.0);
        snprintf(value[1], 24, "%.0f %%", t->arp.probability * 100.0);
        snprintf(value[2], 24, "%s", t->arp.latch ? "ON" : "OFF");
        snprintf(value[3], 24, "%s", t->arp.play_order ? "PLAY" : "NOTE");
    }
    for (unsigned i = 0; i < 4; i++)
    {
        int y = 72 + (int)i * 27;
        rectangle(u, 3, y, 232, 24, i % 2 ? FM_UI_PANEL : FM_UI_BLUE);
        snprintf(line, sizeof(line), "%u %s", i + 1, labels[u->arp_page][i]);
        bold(u, 8, y + 6, line, 16, FM_UI_TEXT);
        bold_right(u, 230, y + 6, value[i], 14, FM_UI_AMBER);
    }
    text(u, 4, 187, "ARP: NEXT PAGE / KNOBS 1-4: EDIT", 38, FM_UI_MUTED);
    footer(u, labels[u->arp_page][0], labels[u->arp_page][1], labels[u->arp_page][2],
           labels[u->arp_page][3], value[0], value[1], value[2], value[3]);
}
static bool roll_pair(const FmEvent *a, const FmEvent *b)
{
    return a->origin == b->origin && a->channel == b->channel &&
           (b->identity ? a->identity == b->identity : a->note == b->note);
}
static bool roll_note(FmUi *u, FmPattern *p, const FmEvent *on, double end, double from, double to)
{
    if (on->note < u->roll_low || on->note >= u->roll_low + (u->roll_span ? u->roll_span : 12))
        return false;
    double raw_duration = end - on->tick;
    double start = fm_pattern_view_tick(p, on->tick);
    end = fm_pattern_view_tick(p, end);
    if (end <= start)
        end = start + fmax(1, raw_duration);
    if (end <= from || start >= to)
        return false;
    int x = 25 + (int)(208 * (fmax(from, start) - from) / (4 * FM_PPQN));
    int right = 25 + (int)(208 * (fmin(to, end) - from) / (4 * FM_PPQN));
    unsigned span = u->roll_span ? u->roll_span : 12;
    int y = 66 + (int)((u->roll_low + span - 1 - on->note) * 120 / span);
    bool selected = on->note == u->step_note && start <= u->roll_tick + .001 && end > u->roll_tick;
    rectangle(u, x, y + 1, right - x > 2 ? right - x : 2, (int)(120 / span) - 1,
              selected ? FM_UI_AMBER : FM_UI_GREEN);
    return selected;
}
static void piano_roll(FmUi *u)
{
    FmPattern *p = selected_pattern(u);
    FmTrack *t = selected_track(u);
    char out[64], n[16], length[16], velocity[16], position[16];
    if (!p)
    {
        bold(u, 4, 60, "CREATE A TRACK FIRST", 26, FM_UI_TEXT);
        return;
    }
    unsigned page = (unsigned)(u->roll_tick / (4 * FM_PPQN));
    double from = page * 4 * FM_PPQN, to = fmin(p->length_ticks, from + 4 * FM_PPQN);
    snprintf(out, sizeof(out), "P%02u BAR %u %s", t->selected_pattern + 1, page + 1,
             u->roll_move ? "MOVE" : "SCL ADD/DEL");
    text(u, 4, 39, out, 38, FM_UI_MUTED);
    for (unsigned i = 0; i < 4; i++)
    {
        snprintf(out, sizeof(out), "%u", page * 4 + i + 1);
        text(u, 27 + (int)i * 52, 54, out, 4, FM_UI_MUTED);
    }
    unsigned span = u->roll_span ? u->roll_span : 12;
    for (unsigned i = 0; i < span; i++)
    {
        unsigned pitch = u->roll_low + span - 1 - i;
        int y = 66 + (int)(i * 120 / span), height = (int)((i + 1) * 120 / span - i * 120 / span);
        bool black = pitch % 12 == 1 || pitch % 12 == 3 || pitch % 12 == 6 || pitch % 12 == 8 ||
                     pitch % 12 == 10;
        rectangle(u, 25, y, 208, height, black ? FM_UI_BG : FM_UI_PANEL);
        note(out, sizeof(out), pitch);
        char *slash = strchr(out, '/');
        if (slash)
            *slash = 0;
        if (height >= 8 || pitch % 12 == 0)
            text(u, 1, y + 1, out, 4, pitch == u->step_note ? FM_UI_AMBER : FM_UI_MUTED);
        rectangle(u, 25, y + height - 1, 208, 1, FM_UI_GRID);
    }
    for (unsigned i = 0; i <= 16; i++)
        rectangle(u, 25 + (int)i * 13, 66, 1, 120, i % 4 ? FM_UI_GRID : FM_UI_MUTED);
    const FmEvent *focus = NULL;
    double focus_length = u->step_length;
    const FmEvent *held[128];
    unsigned count = 0;
    for (unsigned i = 0; i < p->count; i++)
    {
        FmEvent *e = &p->events[i];
        if (fm_pattern_view_tick(p, e->tick) > to)
            break;
        if (e->kind == FM_EVENT_NOTE_ON && e->velocity)
        {
            if (count < 128)
                held[count++] = e;
        }
        else if (e->kind == FM_EVENT_NOTE_OFF || (e->kind == FM_EVENT_NOTE_ON && !e->velocity))
        {
            for (unsigned j = 0; j < count; j++)
                if (roll_pair(held[j], e))
                {
                    if (roll_note(u, p, held[j], e->tick, from, to) && (!focus || held[j] < focus))
                    {
                        focus = held[j];
                        focus_length = fmax(1, e->tick - focus->tick);
                    }
                    memmove(held + j, held + j + 1, (--count - j) * sizeof(*held));
                    break;
                }
        }
    }
    for (unsigned i = 0; i < count; i++)
        if (roll_note(u, p, held[i], p->length_ticks, from, to) && (!focus || held[i] < focus))
        {
            focus = held[i];
            focus_length = fmax(1, p->length_ticks - focus->tick);
        }
    if (u->step_note >= u->roll_low &&
        u->step_note < u->roll_low + (u->roll_span ? u->roll_span : 12))
    {
        int x = 25 + (int)(208 * (u->roll_tick - from) / (4 * FM_PPQN)),
            y = 66 + (int)((u->roll_low + span - 1 - u->step_note) * 120 / span);
        outline(u, x, y, 13, (int)(120 / span), FM_UI_AMBER);
    }
    double head = fm_sequence_pattern_tick(u->sequence, p->id);
    if (head >= from && head < to)
        rectangle(u, 25 + (int)(208 * (head - from) / (4 * FM_PPQN)), 66, 1, 120, FM_UI_RED);
    text(u, 4, 190, "PRESETS: NOTES / EDIT: MOVE", 39, FM_UI_MUTED);
    note(n, sizeof(n), u->step_note);
    snprintf(position, sizeof(position), "%.2f", u->roll_tick / FM_PPQN);
    snprintf(length, sizeof(length), "%.2f", focus_length / FM_PPQN);
    snprintf(velocity, sizeof(velocity), "%u", focus ? focus->velocity : u->step_velocity);
    footer(u, u->roll_move ? "MOVE" : "BEAT", "NOTE", "LENGTH", "VEL", position, n, length,
           velocity);
}
