/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "types.h"

const char *fz_status_str(FzStatus status)
{
    switch (status) {
    case FZ_OK:
        return "OK";
    case FZ_ERR_ARGS:
        return "BAD ARGUMENT";
    case FZ_ERR_FULL:
        return "CAPACITY FULL";
    case FZ_ERR_TRACK_CAPACITY:
        return "TRACK CAPACITY FULL";
    case FZ_ERR_PATTERN_CAPACITY:
        return "PATTERN CAPACITY FULL";
    case FZ_ERR_REGION_CAPACITY:
        return "REGION CAPACITY FULL";
    case FZ_ERR_ASSET_CAPACITY:
        return "ASSET CAPACITY FULL";
    case FZ_ERR_SAMPLE_STORAGE:
        return "SAMPLE STORAGE FULL";
    case FZ_ERR_BAD_ID:
        return "BAD HANDLE";
    case FZ_ERR_BUSY:
        return "ASSET IN USE";
    case FZ_ERR_RANGE:
        return "OUT OF RANGE";
    case FZ_ERR_FORMAT:
        return "BAD FORMAT";
    case FZ_ERR_STATE:
        return "BAD STATE";
    }
    return "UNKNOWN";
}
