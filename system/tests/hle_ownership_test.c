/*+===================================================================
  File:      hle_ownership_test.c

  Summary:   Peeking must not dispose a high-level event that is still queued.

  Classes:   None.

  Functions: C_GetCurrentProcess, C_GetPortNameFromProcessSerialNumber, DisposPtr, MemError, _NewPtr_flags, main,
             syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Peeking must not dispose a high-level event that is still queued. */
#define NDEBUG 1
#include "rsys/common.h"
static unsigned warnings;
#undef warning_unexpected
#define warning_unexpected(...) (++warnings)
#include "../../events/hle.c"
#undef main
#undef NDEBUG
#include <assert.h>

uint32 ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;
static void* allocated[64];
static unsigned live;

OSErr C_GetCurrentProcess(ProcessSerialNumber* serial)
{
    serial->highLongOfPSN = 0;
    serial->lowLongOfPSN = CLC(4);
    return noErr;
}

OSErr C_GetPortNameFromProcessSerialNumber(PPCPortPtr port, ProcessSerialNumber* serial)
{
    assert(serial->lowLongOfPSN == CLC(4));
    memset(port, 0, sizeof *port);
    memcpy(port->name, "\004Test", 5);
    port->portKindsSelector = CWC(ppcByString);
    memcpy(port->u.portTypeStr, "\010TESTep01", 9);
    return noErr;
}

void* syn68k_guest_to_host(uint32 address)
{
    return (void*)(ROMlib_offset + (address & syn68k_address_mask));
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return (address - ROMlib_offset) & syn68k_address_mask;
}

Ptr _NewPtr_flags(Size size, boolean_t sys, boolean_t clear)
{
    Ptr p = calloc(1, size ? size : 1);
    unsigned i;
    assert(p);
    for (i = 0; i < NELEM(allocated) && allocated[i]; ++i)
        ;
    assert(i < NELEM(allocated));
    allocated[i] = p;
    ++live;
    return p;
}

OSErr MemError(void)
{
    return noErr;
}

void DisposPtr(Ptr p)
{
    unsigned i;
    for (i = 0; i < NELEM(allocated) && allocated[i] != p; ++i)
        ;
    assert(p && i < NELEM(allocated));
    allocated[i] = NULL;
    --live;
    free(p);
}

static void accept(void)
{
    char data[4];
    int32 length = CLC(sizeof data), refcon;

    struct {
        TargetID sender;
        unsigned char guard[4];
    } result;

    memset(&result, 0xa5, sizeof result);
    assert(C_AcceptHighLevelEvent(&result.sender, &refcon, data, &length) == noErr);
    assert(result.sender.location.locationKindSelector == CWC(ppcNoLocation));
    assert(!memcmp(result.sender.name.name, "\004Test", 5));
    assert(!memcmp(&result.sender.name, &result.sender.recvrName, sizeof(PPCPortRec)));
    for (unsigned i = 0; i < sizeof result.guard; ++i)
        assert(result.guard[i] == 0xa5);
    assert(refcon == CLC(42) && length == CLC(sizeof data));
    assert(!memcmp(data, "TEST", sizeof data));
}

int main(void)
{
    /* These numeric values are part of the classic Event Manager ABI. */
    assert(bufferIsSmall == -607 && noOutstandingHLE == -608);
    EventRecord sent = {0}, received;
    HighLevelEventMsgPtr first, second;
    int32 refcon, length = 0;
    unsigned i;
    sent.what = CWC(kHighLevelEvent);
    hle_init();
    assert(C_PostHighLevelEvent(&sent, NULL, 42, "TEST", 4, 0) == noErr);
    assert(live == 3);
    for (i = 0; i < 5; ++i) {
        hle_reset();
        assert(hle_get_event(&received, FALSE));
        assert(received.what == CWC(kHighLevelEvent));
        accept();
        assert(live == 3 && hle_q && !current_hle_owned);
    }
    hle_reset();
    assert(live == 3);
    assert(hle_get_event(&received, TRUE));
    assert(live == 3 && !hle_q && current_hle_owned);
    accept();
    first = ROMlib_hle_exchange_current(NULL);
    assert(first && !current_hle_msg);
    hle_reset();
    assert(live == 3);
    assert(C_AcceptHighLevelEvent(NULL, &refcon, NULL, &length) == noOutstandingHLE);
    assert(C_PostHighLevelEvent(&sent, NULL, 42, "TEST", 4, 0) == noErr);
    assert(hle_get_event(&received, TRUE));
    assert(live == 6);
    second = ROMlib_hle_exchange_current(first);
    assert(second && second != first);
    accept();
    hle_reset();
    assert(live == 3);
    assert(!ROMlib_hle_exchange_current((HighLevelEventMsgPtr)1));
    assert(!ROMlib_hle_exchange_current(second));
    accept();
    hle_reset();
    hle_reset();
    assert(!live && !current_hle_msg);
    assert(!hle_get_event(&received, FALSE));
    assert(C_AcceptHighLevelEvent(NULL, &refcon, NULL, &length) == noOutstandingHLE);
    assert(!warnings);
    puts("High-level event repeated peeks, acceptance, consume and disposal passed");
    return 0;
}
