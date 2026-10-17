/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
#ifndef FZ_TEST_CHECK_H
#define FZ_TEST_CHECK_H

void expect_true(int cond, const char *msg, const char *file, int line);

#define EXPECT(cond) expect_true((cond), #cond, __FILE__, __LINE__)

void test_product(void);
void test_sim(void);

#endif
