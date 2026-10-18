#ifndef FM_TEST_H
#define FM_TEST_H
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
#define NEAR(a, b, t) CHECK(fabs((double)(a) - (double)(b)) <= (double)(t))
#endif
