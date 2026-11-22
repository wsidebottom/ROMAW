/*+===================================================================
  File:      get_map_test.c

  Summary:   Exercise the real resource-chain lookup and private GetMap implementation.

  Classes:   None.

  Functions: main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Exercise the real resource-chain lookup and private GetMap implementation. */
#define NDEBUG 1
#include "../../resources/resSetcur.c"
#include "../../resources/resOpen.c"
#undef NDEBUG
#undef main
#include <assert.h>

CPUState cpu_state;
uint32 ROMlib_offset, syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;

void* syn68k_guest_to_host(uint32 address)
{
    return (void*)(ROMlib_offset + address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return address - ROMlib_offset;
}

int main(void)
{
    static unsigned char low_memory[0x2000];
    ROMlib_offset = (uint32)low_memory;
    resmap records[3] = {0};
    HIDDEN_resmapptr masters[3];
    for (unsigned i = 0; i < 3; ++i) {
        HIDDEN_RAW_SET(masters[i], RM(&records[i]));
        records[i].resfn = CW(i == 2 ? -12 : i + 7);
        PP_RAW(records[i].nextmap) = i < 2 ? (Handle)RM(&masters[i + 1]) : 0;
    }
    TopMapHndl = (Handle)RM(&masters[0]);
    SysMap = CWC(8);
    CurMap = CWC(7);
    ResErr = CWC(ioErr);
    ResErrProc = (ProcPtr)CLC(0x12345678); /* Must never be called. */
    for (unsigned i = 0; i < 3; ++i) {
        assert(C_GetMap(CW(records[i].resfn)) == (Handle)&masters[i]);
        assert(ResErr == CWC(ioErr));
    }
    assert(C_GetMap(0) == (Handle)&masters[1]);
    assert(CurMap == CWC(7));
    ResErr = 0;
    assert(C_GetMap(8) == (Handle)&masters[1] && ResErr == 0);
    assert(C_GetMap(99) == NULL && ResErr == CWC(resFNotFound));
    assert(C_GetMap(7) == (Handle)&masters[0]);
    assert(ResErr == CWC(resFNotFound));
    /* Return the actual map, with changes observable by the next lookup. */
    HxX((resmaphand)C_GetMap(7), resfatr) = CWC(mapChanged);
    assert(records[0].resfatr == CWC(mapChanged));
    /* A map removed from the chain is no longer an open resource file. */
    PP_RAW(records[0].nextmap) = (Handle)RM(&masters[2]);
    assert(!C_GetMap(8) && !C_GetMap(0));
    assert(C_GetMap(-12) == (Handle)&masters[2]);
    TopMapHndl = 0;
    ResErr = 0;
    assert(!C_GetMap(0) && ResErr == CWC(resFNotFound));
    uint8 attributes[3] = {0xa5, 0x5a, 0xc3};
    for (unsigned i = 0; i < 65536; ++i) {
        records[0].resfatr = CW(i);
        ResErr = CWC(ioErr);
        ROMlib_get_override_attributes((Handle)&masters[0], &attributes[1]);
        assert(attributes[1] == (i & 0x1e));
        assert(attributes[0] == 0xa5 && attributes[2] == 0xc3);
        assert(ResErr == CWC(ioErr) && records[0].resfatr == CW((INTEGER)i));
    }
    attributes[1] = 0x5a;
    ROMlib_get_override_attributes(NULL, &attributes[1]);
    assert(ResErr == CWC(nilHandleErr) && attributes[1] == 0x5a);
    puts("GetMap/override attributes: chain, refs, errors, masks and state passed");
    return 0;
}
