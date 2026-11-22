/*+===================================================================
  File:      paramline_test.c

  Summary:   Implements paramline test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int fail_after = -1, live;

static void* test_malloc(size_t n)
{
    void* p;
    if (fail_after == 0)
        return NULL;
    if (fail_after > 0)
        --fail_after;
    p = malloc(n);
    if (p)
        ++live;
    return p;
}

static void test_free(void* p)
{
    if (p)
        --live;
    free(p);
}

#define malloc test_malloc
#define free test_free
#include "../../options/paramline.c"
#undef malloc
#undef free

static void release(int argc, char** argv)
{
    while (argc > 0)
        test_free(argv[--argc]);
    test_free(argv);
    assert(live == 0);
}

int main(void)
{
    int argc, i;
    char** argv;
    char* large = malloc(1000001);
    const char* expected[] = {"romaw", "", "", "ab cd", "a'b", "C:\\Apps\\Book", "open ended"};
    paramline_to_argcv("romaw '' \"\" a\"b c\"d \"a'b\" C:\\Apps\\Book 'open ended", &argc, &argv);
    assert(argc == 7 && !argv[argc]);
    for (i = 0; i < argc; ++i)
        assert(!strcmp(argv[i], expected[i]));
    release(argc, argv);
    paramline_to_argcv(" \t\r\n ", &argc, &argv);
    assert(argc == 0 && !argv[0]);
    release(argc, argv);
    paramline_to_argcv("\xFF\x80", &argc, &argv);
    assert(argc == 1 && !strcmp(argv[0], "\xFF\x80"));
    release(argc, argv);
    assert(large);
    memset(large, 'a', 1000000);
    large[1000000] = 0;
    paramline_to_argcv(large, &argc, &argv);
    assert(argc == 1 && strlen(argv[0]) == 1000000);
    release(argc, argv);
    free(large);
    /* Fail the vector and each successive argument allocation. */
    for (i = 0; i < 4; ++i) {
        fail_after = i;
        argc = 123;
        argv = (char**)1;
        paramline_to_argcv("one '' three", &argc, &argv);
        assert(argc == -1 && !argv && live == 0);
    }
    fail_after = -1;
    {
        char* arguments[] = {"romaw", "", "Coloring Book 3.2", "\xFF"};
        char* joined = ROMlib_argv_to_string(4, arguments);
        assert(joined && !strcmp(joined, "romaw  Coloring Book 3.2 \xFF"));
        test_free(joined);
        joined = ROMlib_argv_to_string(0, NULL);
        assert(joined && !joined[0]);
        test_free(joined);
        fail_after = 0;
        assert(!ROMlib_argv_to_string(4, arguments) && !live);
        fail_after = -1;
    }
    {
        char** arguments = malloc(100000 * sizeof *arguments);
        char* joined;
        assert(arguments);
        for (i = 0; i < 100000; ++i)
            arguments[i] = "abc";
        joined = ROMlib_argv_to_string(100000, arguments);
        assert(joined && strlen(joined) == 399999);
        assert(!memcmp(joined, "abc abc", 7));
        test_free(joined);
        free(arguments);
        assert(!live);
    }
    puts("PASS: argument quoting, long inputs, high bytes and allocation failures");
    return 0;
}
