/*+===================================================================
  File:      mactcp_driver_guest_probe.c

  Summary:   Verify guest .IPP resource initialization and native fallback precedence.

  Classes:   None.

  Functions: main.

  Origin:    ROMAW original Device Manager regression fixture.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include <Devices.h>
#include <Files.h>
#include <Memory.h>
#include <Resources.h>
#include <stdio.h>

static short report;
static int checks, failures;

static void check(int ok, const char* name)
{
    char line[160];
    long length = sprintf(line, "%s %s\n", ok ? "PASS" : "FAIL", name);
    FSWrite(report, &length, line);
    ++checks;
    failures += !ok;
}

/* GetDCtlEntry is an SDK library routine; inspect its documented low-memory
   unit table directly because the standalone Retro68 runtime does not ship it. */
static void* driver_entry(short reference)
{
    void** table = *(void***)0x11c;
    return table && reference < 0 ? table[-reference - 1] : 0;
}

int main(void)
{
    Str255 path = "\pD:Source:ROMAW:build:mactcp:driver-results.txt";
    HCreate(0, 0, path, 'MTPr', 'TEXT');
    if (HOpen(0, 0, path, fsRdWrPerm, &report))
        return 1;
    SetEOF(report, 0);
    short driver = 0;
    Handle resource = Get1Resource('DRVR', 22);
    check(resource != 0, "original .IPP driver fixture is present");
    check(OpenDriver("\p.IPP", &driver) == -23003, "guest initializer error is preserved");
    if (resource) {
        RemoveResource(resource);
        check(!ResError(), "detach application driver fixture");
        DisposeHandle(resource);
    }
    check(!OpenDriver("\p.IPP", &driver), "native fallback opens after resource removal");
    check(driver_entry(driver) != 0, "native fallback has a Device Control Entry");
    check(!CloseDriver(driver), "native fallback closes");
    check(!OpenDriver("\p.IPP", &driver), "native fallback reopens");
    check(!CloseDriver(driver), "reopened fallback closes");
    char line[80];
    long length = sprintf(line, "failures=%d checks=%d\n", failures, checks);
    FSWrite(report, &length, line);
    FSClose(report);
    return failures != 0;
}
