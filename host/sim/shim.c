/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 the fm1-fz-sampler contributors */
/* libc subset for the wasm32 panel. The host tests use the system libc. */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

void *memset(void *dst, int c, size_t n)
{
    unsigned char *p = dst;
    size_t i;
    for (i = 0; i < n; i++)
        p[i] = (unsigned char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    size_t i;
    for (i = 0; i < n; i++)
        d[i] = s[i];
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    size_t i;
    if (d == s || n == 0)
        return dst;
    if (d < s) {
        for (i = 0; i < n; i++)
            d[i] = s[i];
    } else {
        for (i = n; i > 0; i--)
            d[i - 1] = s[i - 1];
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a;
    const unsigned char *y = b;
    size_t i;
    for (i = 0; i < n; i++) {
        if (x[i] != y[i])
            return x[i] < y[i] ? -1 : 1;
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    if (!s)
        return 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    if (!a || !b)
        return a == b ? 0 : a ? 1 : -1;
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static void put_u(char *dst, size_t cap, size_t *used, unsigned long v)
{
    char tmp[16];
    int n = 0;
    int i;
    if (v == 0)
        tmp[n++] = '0';
    while (v && n < 16) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    for (i = n - 1; i >= 0; i--) {
        if (*used + 1 < cap)
            dst[*used] = tmp[i];
        (*used)++;
    }
}

int snprintf(char *dst, size_t cap, const char *fmt, ...)
{
    va_list ap;
    size_t used = 0;
    if (!dst || cap == 0)
        return 0;
    va_start(ap, fmt);
    while (fmt && *fmt) {
        if (*fmt != '%') {
            if (used + 1 < cap)
                dst[used] = *fmt;
            used++;
            fmt++;
            continue;
        }
        fmt++;
        if (*fmt == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "";
            while (*s) {
                if (used + 1 < cap)
                    dst[used] = *s;
                used++;
                s++;
            }
        } else if (*fmt == 'd') {
            int v = va_arg(ap, int);
            if (v < 0) {
                if (used + 1 < cap)
                    dst[used] = '-';
                used++;
                put_u(dst, cap, &used, (unsigned long)(-(long)v));
            } else {
                put_u(dst, cap, &used, (unsigned long)v);
            }
        } else if (*fmt == 'u') {
            put_u(dst, cap, &used, va_arg(ap, unsigned));
        } else if (*fmt == '%') {
            if (used + 1 < cap)
                dst[used] = '%';
            used++;
        }
        if (*fmt)
            fmt++;
    }
    va_end(ap);
    if (used < cap)
        dst[used] = 0;
    else
        dst[cap - 1] = 0;
    return (int)used;
}

double floor(double x)
{
    union {
        double d;
        uint64_t u;
    } bits;
    int exp;
    uint64_t frac;
    if (x == 0.0 || x != x)
        return x;
    bits.d = x;
    exp = (int)((bits.u >> 52) & 0x7ff) - 1023;
    if (exp < 0)
        return x < 0 ? -1.0 : 0.0;
    if (exp >= 52)
        return x;
    frac = bits.u & ((1ULL << 52) - 1ULL);
    frac &= ~((1ULL << (52 - exp)) - 1ULL);
    bits.u = (bits.u & ~((1ULL << 52) - 1ULL)) | frac;
    if (x < 0 && bits.d != x)
        bits.d -= 1.0;
    return bits.d;
}

double fabs(double x)
{
    return x < 0 ? -x : x;
}

static double exp_taylor(double y)
{
    double e = 1.0;
    double t = 1.0;
    int i;
    for (i = 1; i < 20; i++) {
        t *= y / (double)i;
        e += t;
    }
    return e;
}

static double exp2_small(double x)
{
    int n;
    double f, e;
    union {
        double d;
        uint64_t u;
    } bits;
    int biased;
    if (x > 1020.0)
        return 1e300;
    if (x < -1020.0)
        return 0.0;
    n = (int)floor(x);
    f = x - (double)n;
    e = exp_taylor(f * 0.6931471805599453);
    bits.d = e;
    biased = (int)((bits.u >> 52) & 0x7ff) + n;
    if (biased <= 0)
        return 0.0;
    if (biased >= 2047)
        return 1e300;
    bits.u = (bits.u & ~(((uint64_t)0x7ff) << 52)) | ((uint64_t)biased << 52);
    return bits.d;
}

double exp2(double x)
{
    return exp2_small(x);
}

double log2(double x)
{
    int n = 0;
    double y, z, num, den;
    if (!(x > 0.0))
        return -1e300;
    while (x >= 2.0) {
        x *= 0.5;
        n++;
    }
    while (x < 1.0) {
        x *= 2.0;
        n--;
    }
    y = (x - 1.0) / (x + 1.0);
    z = y * y;
    num = y * (1.0 + z * (1.0 / 3.0 + z * (1.0 / 5.0 + z * (1.0 / 7.0 + z / 9.0))));
    den = 0.6931471805599453;
    return (double)n + 2.0 * num / den;
}

double pow(double a, double b)
{
    if (b == 0.0)
        return 1.0;
    if (a == 2.0)
        return exp2_small(b);
    if (!(a > 0.0))
        return 0.0;
    return exp2_small(b * log2(a));
}

static double wrap_pi(double x)
{
    const double tau = 6.283185307179586;
    double turns = floor(x / tau + 0.5);
    x -= turns * tau;
    return x;
}

double sin(double x)
{
    double y, y2, s;
    int i;
    x = wrap_pi(x);
    y = x;
    y2 = y * y;
    s = y;
    for (i = 1; i < 10; i++) {
        y *= -y2 / (double)((2 * i) * (2 * i + 1));
        s += y;
    }
    return s;
}

double cos(double x)
{
    return sin(x + 1.5707963267948966);
}

float tanf(float x)
{
    double c = cos((double)x);
    if (c > -1e-12 && c < 1e-12)
        return x < 0 ? -1e6f : 1e6f;
    return (float)(sin((double)x) / c);
}

long lround(double x)
{
    return (long)floor(x + (x < 0 ? -0.5 : 0.5));
}

long lrintf(float x)
{
    return lround((double)x);
}

void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
    unsigned char *bytes = base;
    unsigned char tmp[64];
    size_t i, j;
    if (!base || !cmp || size == 0 || size > sizeof tmp)
        return;
    for (i = 1; i < n; i++) {
        memcpy(tmp, bytes + i * size, size);
        j = i;
        while (j > 0 && cmp(bytes + (j - 1) * size, tmp) > 0) {
            memcpy(bytes + j * size, bytes + (j - 1) * size, size);
            j--;
        }
        memcpy(bytes + j * size, tmp, size);
    }
}
