/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* Selected-track performance. HOME cutoff/resonance are not applied: the
 * filter model is not implemented. Volume and pan are the track's mix state. */
#include "play.h"

#include <string.h>

int fz_performance_note(FzEngine *engine, FzProject *project, int note, int velocity, int down)
{
    FzTrack *tr;
    FzInstrument *inst;
    FzRegion *region;
    FzAsset *asset;
    FzId id;

    if (!engine || !project)
        return -1;
    if (!down) {
        fz_engine_note_off(engine, note);
        return 0;
    }
    if (project->selected_track < 0)
        return -1;
    id = fz_track_id(project, project->selected_track);
    tr = fz_track(project, id);
    if (!tr)
        return -1;
    inst = fz_instrument(project, tr->instrument);
    if (!inst)
        return -1;
    region = fz_instrument_region_for_note(project, tr->instrument, note);
    if (!region)
        return -1;
    asset = fz_asset(project, region->asset);
    if (!asset || !asset->pcm)
        return -1;
    return fz_engine_note_on_pcm(engine, asset->pcm, asset->frames, asset->native_rate, region->root, note,
                                 region->tune_cents, velocity, tr->volume, tr->pan, region->one_shot, asset->cycle,
                                 asset->tag);
}
