/*+===================================================================
  File:      version_resources_test.c

  Summary:   Verify version updates tolerate removed, unloaded and failed-growth System resources.

  Classes:   None.

  Functions: C_Get1Resource, C_LoadResource, CToPascalCall, HUnlock, SetHandleSize, main,
             syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Regression fixture for editable System resource overrides.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

#include "../../runtime/version.c"
#undef main
#include <assert.h>

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xFFFFFFFFUL;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;
void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];
static Handle resource;
static int failed_growth, writes;

void* syn68k_guest_to_host(uint32 address)
{
    return (void*)(uintptr_t)(ROMlib_offset + address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return address - ROMlib_offset;
}

intptr_t CToPascalCall(void* proc, unsigned long magic, ...)
{
    abort();
}

Handle C_Get1Resource(ResType type, INTEGER id)
{
    assert(CurMap == SysMap);
    return resource;
}

void C_LoadResource(Handle volatile h)
{
    assert(h == resource);
}

void HUnlock(Handle h)
{
    assert(h == resource);
}

void SetHandleSize(Handle h, Size size)
{
    assert(h == resource && size > 0 && size < 256);
    ++writes;
    MemErr = failed_growth ? CWC(memFullErr) : noErr;
}

int main(void)
{
    unsigned char memory[16384] = {0};
    ROMlib_offset = (uintptr_t)memory;
    SysMap = CWC(2);
    CurMap = CWC(7);
    resource = NULL;
    update_version_resource(TICK("STR "), 0, "Xhello", 6);
    assert(CurMap == CWC(7) && !writes);
    resource = (Handle)(memory + 4096);
    update_version_resource(TICK("STR "), 0, "Xhello", 6);
    assert(!writes);
    *(uint32*)resource = CLC(8192);
    memset(memory + 8192, 0xcc, 256);
    failed_growth = 1;
    update_version_resource(TICK("STR "), 0, "Xhello", 6);
    assert(writes == 1 && memory[8192] == 0xcc);
    failed_growth = 0;
    update_version_resource(TICK("STR "), 0, "Xhello", 6);
    assert(writes == 2 && memory[8192] == 5 && !memcmp(memory + 8193, "hello", 5));
    assert(memory[8198] == 0xcc && CurMap == CWC(7));
    char vers[18] = {0};
    update_version_resource(TICK("vers"), 1, vers, sizeof vers);
    assert(memory[8192 + 12] == 5 && memory[8192 + 18] == 0xcc);
    puts("Version resources: removed/unloaded handles, failed growth, Pascal lengths and map restoration passed");
    return 0;
}
