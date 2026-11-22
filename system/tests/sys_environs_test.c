/*+===================================================================
  File:      sys_environs_test.c

  Summary:   Verify SysEnvirons uses Gestalt machine IDs and classic numbering.

  Classes:   None.

  Functions: Gestalt, main

  Origin:    Regression fixture for Executor's SysEnvirons implementation.

  Copyright and Legal notices: See the repository license.
===================================================================+*/

#define NDEBUG 1
#include "../../services/osutil.c"
#undef main
#undef NDEBUG
#include <assert.h>

uint64 ROMlib_offset;
static OSErr machine_error;
static LONGINT machine_id;
static unsigned queries;

OSErrRET Gestalt(OSType selector, LONGINT* response)
{
    assert(selector == gestaltMachineType);
    ++queries;
    *response = CL(machine_id);
    return machine_error;
}

int main(void)
{
    unsigned char memory[65536] = {0};
    ROMlib_offset = (uintptr_t)memory;
    SysVersion = CWC(0x0900);
    BootDrive = CWC(-3);

    struct {
        uint32 before;
        SysEnvRec record;
        uint32 after;
    } guarded;

    const LONGINT machines[] = {3, 6, 53, 75, 100, 510, 32767};
    for (unsigned i = 0; i < NELEM(machines); ++i) {
        memset(&guarded, 0xa5, sizeof guarded);
        machine_id = machines[i];
        assert(SysEnvirons(2, &guarded.record) == noErr);
        assert(CW(guarded.record.machineType) == machine_id - 2);
        assert(CW(guarded.record.environsVersion) == 2);
        assert(CW(guarded.record.systemVersion) == 0x0900);
        assert(CW(guarded.record.sysVRefNum) == -3);
        assert(guarded.before == 0xa5a5a5a5 && guarded.after == 0xa5a5a5a5);
    }
    machine_error = gestaltUndefSelectorErr;
    assert(SysEnvirons(1, &guarded.record) == noErr);
    assert(CW(guarded.record.machineType) == envMachUnknown);
    assert(SysEnvirons(3, &guarded.record) == envVersTooBig);
    SysEnvRec previous = guarded.record;
    unsigned calls = queries;
    assert(SysEnvirons(0, &guarded.record) == envBadVers);
    assert(SysEnvirons(-1, &guarded.record) == envBadVers);
    assert(!memcmp(&previous, &guarded.record, sizeof previous) && queries == calls);
    puts("PASS: SysEnvirons Gestalt machine overrides, numbering and errors");
    return 0;
}
