/*+===================================================================
  File:      pascal_test.c

  Summary:   Pascal string binary-byte, signed-capacity, prefix and boundary tests.

  Classes:   None.

  Functions: ROMlib_carbon_pascal_tests

  Origin:    Existing repository source in packages/carbonlib/tests; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Pascal string binary-byte, signed-capacity, prefix and boundary tests. */
#include "../src/internal.h"
#undef NDEBUG
#include <assert.h>

static void* function(const char* name)
{
    const ROMlib_carbon_export* entry = ROMlib_carbon_find_export(name, 2);
    assert(entry && entry->reference_library && !strcmp(entry->reference_library, "StdCLib"));
    return entry->function;
}

static void comparisons_and_searches(void)
{
    INTEGER (*length)(const Byte*) = function("PLstrlen");
    INTEGER (*compare)(const Byte*, const Byte*) = function("PLstrcmp");
    INTEGER (*compare_n)(const Byte*, const Byte*, INTEGER) = function("PLstrncmp");
    const Byte* (*find_char)(const Byte*, INTEGER) = function("PLstrchr");
    const Byte* (*find_last)(const Byte*, INTEGER) = function("PLstrrchr");
    const Byte* (*find_any)(const Byte*, const Byte*) = function("PLstrpbrk");
    INTEGER (*span)(const Byte*, const Byte*) = function("PLstrspn");
    const Byte* (*find_string)(const Byte*, const Byte*) = function("PLstrstr");
    INTEGER (*position)(const Byte*, const Byte*) = function("PLpos");
    Byte first[256], second[256];
    for (unsigned a = 0; a < 256; ++a)
        for (unsigned b = 0; b < 256; ++b) {
            first[0] = second[0] = 1;
            first[1] = a;
            second[1] = b;
            assert(compare(first, second) == (int)a - (int)b);
            assert(compare_n(first, second, 1) == (int)a - (int)b);
            assert(compare_n(first, second, 0) == 0 && compare_n(first, second, -32768) == 0);
        }
    for (unsigned size = 0; size < 256; ++size) {
        first[0] = size;
        for (unsigned i = 1; i <= size; ++i)
            first[i] = (Byte)i;
        assert(length(first) == size);
        assert(compare(first, first) == 0);
        for (unsigned byte = 0; byte < 256; ++byte) {
            const Byte* expected = byte && byte <= size ? first + byte : NULL;
            assert(find_char(first, byte) == expected && find_last(first, byte) == expected);
            assert(find_char(first, byte | 0xff00) == expected);
        }
        second[0] = size ? size - 1 : 0;
        memcpy(second + 1, first + 1, second[0]);
        assert(compare(first, second) == !!size);
        assert(compare_n(first, second, second[0]) == 0);
        assert(compare_n(first, second, 32767) == !!size);
    }
    const Byte binary[] = {6, 0, 0xff, 0x80, 0, 0xff, 0x80};
    const Byte set[] = {2, 0, 0xff}, needle[] = {3, 0, 0xff, 0x80}, empty[] = {0};
    assert(find_char(binary, 0) == binary + 1 && find_last(binary, 0) == binary + 4);
    assert(find_any(binary, set) == binary + 1 && span(binary, set) == 2);
    assert(!find_any(binary, empty) && span(binary, empty) == 0);
    assert(find_string(binary, needle) == binary + 1 && position(needle, binary) == 1);
    assert(find_string(binary, binary) == binary + 1 && position(binary, binary) == 1);
    assert(!find_string(needle, binary) && position(binary, needle) == 0);
    assert(!find_string(binary, empty) && !position(empty, binary));
    assert(!find_string(empty, empty) && !find_any(empty, set));
}

static void copies_and_appends(void)
{
    Byte* (*copy)(Byte*, const Byte*) = function("PLstrcpy");
    Byte* (*copy_n)(Byte*, const Byte*, INTEGER) = function("PLstrncpy");
    Byte* (*append)(Byte*, const Byte*) = function("PLstrcat");
    Byte* (*append_n)(Byte*, const Byte*, INTEGER) = function("PLstrncat");
    Byte source[256], actual[258], expected[258];
    const unsigned destination_sizes[] = {0, 1, 3, 127, 254, 255};
    const INTEGER limits[] = {-32768, -1, 0, 1, 2, 7, 127, 254, 255, 256, 32767};
    for (unsigned size = 0; size < 256; ++size) {
        source[0] = size;
        for (unsigned i = 1; i <= size; ++i)
            source[i] = (Byte)(i * 13);
        memset(actual, 0xa5, sizeof actual);
        memset(expected, 0xa5, sizeof expected);
        memcpy(expected + 1, source, size + 1);
        assert(copy(actual + 1, source) == actual + 1 && !memcmp(actual, expected, sizeof actual));
        assert(copy(actual + 1, actual + 1) == actual + 1 && !memcmp(actual, expected, sizeof actual));
        for (unsigned d = 0; d < sizeof destination_sizes / sizeof *destination_sizes; ++d)
            for (unsigned n = 0; n < sizeof limits / sizeof *limits; ++n) {
                unsigned old = destination_sizes[d];
                INTEGER limit = limits[n];
                memset(expected, 0xa5, sizeof expected);
                expected[1] = old;
                memcpy(actual, expected, sizeof actual);
                if (limit > 0) {
                    unsigned cap = limit > 255 ? 255 : limit;
                    unsigned copied = size < cap ? size : cap;
                    memcpy(expected + 2, source + 1, copied);
                    memset(expected + 2 + copied, 0, cap - copied);
                    expected[1] = old >= cap && size >= cap ? old : copied;
                }
                assert(copy_n(actual + 1, source, limit) == actual + 1);
                assert(!memcmp(actual, expected, sizeof actual));
                memset(expected, 0xa5, sizeof expected);
                expected[1] = old;
                memcpy(actual, expected, sizeof actual);
                if (limit > 0) {
                    unsigned copied = size < limit ? size : limit;
                    if (copied > 255 - old)
                        copied = 255 - old;
                    memcpy(expected + old + 2, source + 1, copied);
                    expected[1] = old + copied;
                }
                assert(append_n(actual + 1, source, limit) == actual + 1);
                assert(!memcmp(actual, expected, sizeof actual));
                if (limit == 255) {
                    memset(actual, 0xa5, sizeof actual);
                    actual[1] = old;
                    assert(append(actual + 1, source) == actual + 1);
                    assert(!memcmp(actual, expected, sizeof actual));
                }
            }
    }
    Byte self[] = {3, 'a', 0, 'b', 0xa5, 0xa5, 0xa5, 0xa5};
    assert(append(self, self) == self && self[0] == 6);
    assert(!memcmp(self + 1, "a\0ba\0b", 6) && self[7] == 0xa5);
}

void ROMlib_carbon_pascal_tests(void)
{
    comparisons_and_searches();
    copies_and_appends();
}
