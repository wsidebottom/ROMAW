/*+===================================================================
  File:      afp_guest_probe.c

  Summary:   Verify the real AFP volume mount and File Manager path in a 68K guest.

  Classes:   None.

  Functions: main.

  Origin:    ROMAW interoperability fixture.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include <Files.h>
#include <Gestalt.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static short report;
static unsigned checks, failures;

static void check(int passed, const char* message)
{
    char line[160];
    long size = sprintf(line, "%s %s\n", passed ? "PASS" : "FAIL", message);
    FSWrite(report, &size, line);
    ++checks;
    failures += !passed;
}

static short find_macintosh(void)
{
    short volume = 0;
    for (short index = 1; index < 128; ++index) {
        ParamBlockRec info = {0};
        Str255 name;
        info.volumeParam.ioVolIndex = index;
        info.volumeParam.ioNamePtr = name;
        if (PBGetVInfoSync(&info))
            break;
        long kind;
        uint32_t selector = 0x45560000 | (uint16_t)info.volumeParam.ioVRefNum;
        if (name[0] == 9 && !memcmp(name + 1, "Macintosh", 9) && !Gestalt(selector, &kind) && kind == 2)
            volume = info.volumeParam.ioVRefNum;
    }
    return volume;
}

#ifndef AFP_SHARED_CONSUMER
static short volume_mount(ParamBlockRec* block)
{
    register void* a0 __asm__("a0") = block;
    register long d0 __asm__("d0") = 0x41;
    __asm__ volatile(".word 0xa260" : "+d"(d0), "+a"(a0) : : "a1", "d1", "cc", "memory");
    return d0;
}

#endif

int main(void)
{
#ifndef AFP_SHARED_CONSUMER
    unsigned char record[48] = {
        [1] = 48,   [2] = 'a',  [3] = 'f',  [4] = 'p',  [5] = 'm',  [7] = 0x80, [17] = 30,  [25] = 1,
        [29] = 40,  [30] = 9,   [31] = 'M', [32] = 'a', [33] = 'c', [34] = 'i', [35] = 'n', [36] = 't',
        [37] = 'o', [38] = 's', [39] = 'h', [41] = 1,   [42] = 6,   [43] = 1,   [44] = 10,  [47] = 114,
    };
#endif
    Str255 path = "\pD:Source:ROMAW:build:mactcp:afp-guest-mount-results.txt";
    HCreate(0, 0, path, 'MTPr', 'TEXT');
    if (HOpen(0, 0, path, fsRdWrPerm, &report))
        return 1;
    SetEOF(report, 0);

    short volume = 0;
    short error;
#ifdef AFP_SHARED_CONSUMER
    volume = find_macintosh();
    error = volume ? 0 : -35;
    check(!error, "independent ROMAW process discovered the Chooser mount");
#else
    short existing = find_macintosh();
    ParamBlockRec mount = {0};
    mount.ioParam.ioBuffer = (Ptr)record;
    error = volume_mount(&mount);
    volume = mount.ioParam.ioVRefNum;
    char mount_result[100];
    sprintf(mount_result, "PBVolumeMount registers the AFP volume (error %d, reference %d)", error, volume);
    check(!error && volume < 0, mount_result);
#endif
    if (!error) {
        Str255 name;
        ParamBlockRec info = {0};
        info.volumeParam.ioVRefNum = volume;
        info.volumeParam.ioNamePtr = name;
        check(!PBGetVInfoSync(&info) && name[0] == 9 && !memcmp(name + 1, "Macintosh", 9) &&
                  info.volumeParam.ioVRefNum == volume,
              "File Manager reports the mounted Macintosh volume");

        CInfoPBRec entry = {0};
        entry.dirInfo.ioVRefNum = volume;
        entry.dirInfo.ioDrDirID = 2;
        entry.dirInfo.ioFDirIndex = -1;
        entry.dirInfo.ioNamePtr = name;
        check(!PBGetCatInfoSync(&entry) && (entry.dirInfo.ioFlAttrib & 16) && entry.dirInfo.ioDrDirID == 2,
              "lookup by directory ID returns AFP volume root");
        unsigned read_file = 0;
        for (short index = 1; index <= 64; ++index) {
            memset(&entry, 0, sizeof entry);
            entry.hFileInfo.ioVRefNum = volume;
            entry.hFileInfo.ioDirID = 2;
            entry.hFileInfo.ioFDirIndex = index;
            entry.hFileInfo.ioNamePtr = name;
            if (PBGetCatInfoSync(&entry))
                break;
            if ((entry.hFileInfo.ioFlAttrib & 16) || !entry.hFileInfo.ioFlLgLen)
                continue;
            short fork;
            if (!HOpen(volume, 2, name, fsRdPerm, &fork)) {
                char byte;
                long size = 1;
                read_file = !FSRead(fork, &size, &byte) && size == 1;
                check(!FSClose(fork), "close the AFP fork");
            }
            break;
        }
        check(read_file, "browse the AFP catalog and read a real file byte");
#ifndef AFP_SHARED_CONSUMER
        if (existing != volume)
            check(!UnmountVol(0, volume), "unmount the AFP volume");
#endif
    }
    char footer[80];
    long bytes = sprintf(footer, "failures=%u checks=%u\n", failures, checks);
    FSWrite(report, &bytes, footer);
    FSClose(report);
    return failures != 0;
}
