/* SPDX-License-Identifier: GPL-3.0-only */
/* Minimal declarations for the wasm32 simulator build. */
#ifndef FZ_SIM_MATH_H
#define FZ_SIM_MATH_H

#define M_PI 3.14159265358979323846

double sin(double x);
double cos(double x);
double fabs(double x);
double floor(double x);
double pow(double x, double y);
double log2(double x);
double exp2(double x);
float tanf(float x);
long lround(double x);
long lrintf(float x);

#define isfinite(x) ((x) == (x) && (x) < 1e300 && (x) > -1e300)

#endif
