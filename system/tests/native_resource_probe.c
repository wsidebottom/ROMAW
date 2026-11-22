/*+===================================================================
  File:      native_resource_probe.c

  Summary:   Verify the C-backed INTL 1 hook, caller state, relocation and date services on Windows.

  Classes:   None.

  Functions: main

  Origin:    ROMAW native resource regression fixture using the original RTS hook ABI.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

#include <Quickdraw.h>
#include <Resources.h>
#include <Memory.h>
#include <Files.h>
#include <Multiverse.h>
#include <stdio.h>
#include <string.h>

static short report;
static int failures, checks;

static void check(int passed, const char* name)
{
    char line[160];
    long length = sprintf(line, "%s %s\n", passed ? "PASS" : "FAIL", name);
    FSWrite(report, &length, line);
    ++checks;
    if (!passed)
        ++failures;
}

static void call_localization(Ptr entry)
{
    unsigned short flags;
    unsigned long result_a0;
    __asm__ volatile("movea.l #0x12345678,%%a0\n\t"
                     "move.w #0x15,%%ccr\n\t"
                     "jsr (%[entry])\n\t"
                     "move.w %%sr,%[flags]\n\t"
                     "move.l %%a0,%[result_a0]"
                     : [flags] "=&d"(flags), [result_a0] "=&a"(result_a0)
                     : [entry] "a"(entry)
                     : "a0", "cc", "memory");
    check(result_a0 == 0x12345678, "localization preserves A0 and balances the stack");
    check((flags & 0x1f) == 0x15, "localization preserves condition codes including X");
}

static int contains(const unsigned char* string, const char* needle)
{
    size_t length = strlen(needle);
    for (unsigned i = 1; i + length <= (unsigned)string[0] + 1; ++i)
        if (!memcmp(string + i, needle, length))
            return 1;
    return 0;
}

int main(void)
{
    Str255 path = "\pD:Source:ROMAW:build:native-resources:guest-results.txt";
    HCreate(0, 0, path, 'NRPr', 'TEXT');
    if (HOpen(0, 0, path, fsRdWrPerm, &report))
        return 1;
    SetEOF(report, 0);
    InitGraf(&qd.thePort);
    InitFonts();
    Handle locale = GetResource('INTL', 1);
    check(locale != 0, "INTL 1 loads through Resource Manager");
    if (locale) {
        check(GetHandleSize(locale) == 344, "date record retains offset 330 and contains only a native veneer");
        HLock(locale);
        check((unsigned char)(*locale)[0] == 6 && !memcmp(*locale + 1, "Sunday", 6),
              "international name data retained");
        call_localization(*locale + 330);
        Handle copy = NewHandle(344);
        check(copy != 0, "relocated localization resource allocated");
        if (copy) {
            HLock(copy);
            memcpy(*copy, *locale, 344);
            call_localization(*copy + 330);
            DisposeHandle(copy);
        }
        HUnlock(locale);
        EmptyHandle(locale);
        LoadResource(locale);
        check(*locale != 0 && GetHandleSize(locale) == 344, "resource reload restores the native hook");
        if (*locale) {
            HLock(locale);
            call_localization(*locale + 330);
            HUnlock(locale);
        }
        Str255 date;
        IUDateString(0, 1, date);
        check(contains(date, "January") && contains(date, "1904"), "long-date formatting remains functional");
    }
    char line[80];
    long length = sprintf(line, "failures=%d checks=%d\n", failures, checks);
    FSWrite(report, &length, line);
    FSClose(report);
    return failures != 0;
}
