/*+===================================================================
  File:      system_version_request_test.c

  Summary:   Check bounded classic OS requirement messages and version parsing.

  Classes:   None.

  Functions: main.

  Origin:    Original ROMAW malformed-input and startup-message fixtures.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

#include "rsys/system_version_request.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const struct {
        const char* text;
        uint32_t expected;
    } cases[] = {
        {"This program requires Mac OS 8.6 or later.", 0x860},
        {"Requires System 7.5.3.", 0x753},
        {"This application REQUIRES SYSTEM SOFTWARE VERSION 9.0.4", 0x904},
        {"Application can only run on Mac OS 9.2.2.", 0x922},
        {"This app requires Mac OS version 8.6", 0x860},
        {"Requires System 7", 0x700},
        {"Requires System 10.0", 0},
        {"Requires Mac OS X 10.4", 0},
        {"Requires System 9.3", 0},
        {"Requires Mac OS 8.60", 0},
        {"Requires Mac OS 8.6.4.1", 0},
        {"Requires Mac OS 8.6beta", 0},
        {"Requires Mac OS ", 0},
        {"Mac OS 8.6 is supported.", 0},
        {"Requires 8.6 MB of memory.", 0},
        {"Version 8.6 requires 16 MB.", 0},
        {"Mac OS version 8.6 is too new.", 0},
    };

    for (unsigned i = 0; i < sizeof cases / sizeof *cases; ++i)
        assert(ROMlib_required_version_message(cases[i].text, strlen(cases[i].text)) == cases[i].expected);
    const char bounded[] = {'R', 'e', 'q', 'u', 'i', 'r', 'e', 's', ' ', 'S',
                            'y', 's', 't', 'e', 'm', ' ', '8', '.', '6'};
    for (size_t length = 0; length < sizeof bounded - 1; ++length)
        assert(ROMlib_required_version_message(bounded, length) == (length == 17 || length == 18 ? 0x800 : 0));
    assert(ROMlib_required_version_message(bounded, sizeof bounded) == 0x860);
    size_t consumed = 0;
    assert(ROMlib_required_version_parse("9.0.1", 5, &consumed) == 0x901 && consumed == 5);
    puts("System version requirements: explicit requests, bounds and malformed versions passed");
    return 0;
}
