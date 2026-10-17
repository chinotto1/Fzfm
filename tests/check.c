/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#include "check.h"

#include <stdio.h>

int g_failed;
int g_passed;

void expect_true(int cond, const char *msg, const char *file, int line)
{
    if (cond) {
        g_passed++;
        return;
    }
    fprintf(stderr, "FAIL %s:%d %s\n", file, line, msg);
    g_failed++;
}
