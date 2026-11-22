/*+===================================================================
  File:      version_parse_test.c

  Summary:   Implements version parse test support for regression tests and test fixtures.

  Classes:   None.

  Functions: ROMlib_set_system_version, main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "rsys/common.h"
#include <assert.h>
#include "../../options/parseopt.c"
#undef main

uint32 system_version = 0x700;
char* program_name = "version-test";
static int set_calls;

void ROMlib_set_system_version(uint32 version)
{
    assert(version == system_version);
    ++set_calls;
}

int main(void)
{
    static const char* invalid[] = {"",
                                    "0",
                                    "0.1",
                                    "16",
                                    "7.16",
                                    "7.0.16",
                                    "7x",
                                    "7.0x",
                                    "7.0.1x",
                                    "7.",
                                    "7..1",
                                    "7.0.",
                                    ".7",
                                    "7.0.1.2",
                                    "7.0.1.",
                                    "7.0.1 extra",
                                    " 7",
                                    "7 ",
                                    "+7",
                                    "-7",
                                    "7.-1",
                                    "7.+1",
                                    "7. 1",
                                    "7.1\n",
                                    "4294967303.0.0",
                                    "7.4294967296",
                                    "7.0.4294967296",
                                    "9999999999999999999999999999999",
                                    "\xFF",
                                    "7.\x80"};
    uint32 version;
    char text[64], *long_text;
    int major, minor, patch, i;
    for (major = 1; major <= 15; ++major) {
        sprintf(text, "%d", major);
        assert(ROMlib_parse_version(text, &version) && version == (major << 8));
        for (minor = 0; minor <= 15; ++minor) {
            sprintf(text, "%d.%d", major, minor);
            assert(ROMlib_parse_version(text, &version));
            assert(version == CREATE_SYSTEM_VERSION(major, minor, 0));
            for (patch = 0; patch <= 15; ++patch) {
                sprintf(text, "%d.%d.%d", major, minor, patch);
                assert(ROMlib_parse_version(text, &version));
                assert(version == CREATE_SYSTEM_VERSION(major, minor, patch));
            }
        }
    }
    assert(ROMlib_parse_version("007.00.001", &version) && version == 0x701);
    for (i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
        version = 0xDEADBEEF;
        assert(!ROMlib_parse_version(invalid[i], &version));
        assert(version == 0xDEADBEEF);
    }
    version = 0xDEADBEEF;
    assert(!ROMlib_parse_version(NULL, &version) && version == 0xDEADBEEF);
    assert(!ROMlib_parse_version("7", NULL));
    long_text = malloc(1000002);
    assert(long_text);
    memset(long_text, '0', 1000000);
    long_text[1000000] = '7';
    long_text[1000001] = 0;
    assert(ROMlib_parse_version(long_text, &version) && version == 0x700);
    long_text[1000000] = 'x';
    assert(!ROMlib_parse_version(long_text, &version) && version == 0x700);
    free(long_text);
    assert(!parse_system_version("7.0junk"));
    assert(system_version == 0x700 && !set_calls);
    assert(parse_system_version("6.0.8"));
    assert(system_version == 0x608 && set_calls == 1);
    puts("PASS: all 4095 valid versions, malformed/overflowing inputs, million-byte input and unchanged error outputs");
    return 0;
}
