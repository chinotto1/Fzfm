/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
int test_heap(void);
int test_workflows(void);
int test_system(void);
int test_midi(void);
int test_core(void);
int test_capture(void);
int test_audio(void);
int test_storage(void);
int test_edit(void);
int test_usb(void);
int test_sequence(void);
int test_ui(void);
int test_arp(void);
int main(void)
{
    struct
    {
        const char *name;
        int (*run)(void);
    } tests[] = {{"core", test_core},        {"system", test_system}, {"midi", test_midi},
                 {"capture", test_capture},  {"audio", test_audio},   {"storage", test_storage},
                 {"edit", test_edit},        {"usb", test_usb},       {"sequence", test_sequence},
                 {"arpeggiator", test_arp},  {"ui", test_ui},         {"workflows", test_workflows},
                 {"bounded heap", test_heap}};
    unsigned failed = 0;
    for (unsigned i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
        int result = tests[i].run();
        printf("%s: %s\n", tests[i].name, result ? "FAIL" : "PASS");
        if (result)
            failed++;
    }
    printf("%zu suites, %u failed\n", sizeof(tests) / sizeof(tests[0]), failed);
    return failed ? 1 : 0;
}
