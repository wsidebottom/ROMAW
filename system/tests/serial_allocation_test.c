/*+===================================================================
  File:      serial_allocation_test.c

  Summary:   Implements serial allocation test support for regression tests and test fixtures.

  Classes:   None.

  Functions: Dequeue, Enqueue, MemError, main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "rsys/common.h"
#include "MemoryMgr.h"
#include "CommTool.h"
#include "OSUtil.h"
#include "rsys/mman.h"
#include "rsys/blockinterrupts.h"
#include <assert.h>

static int fail_after = -1, live, installed;
static Size last_handle_size;
static OSErr memory_error;
static CRMRecPtr installed_record;
#include "guest_test_memory.h"

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xFFFFFFFFUL;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 0xe0000000, syn68k_host_alias_size = 0x10000000;

void* syn68k_guest_to_host(uint32 address)
{
    return fixture_guest_to_host(address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return fixture_host_to_guest(address);
}

static void* test_alloc(Size n)
{
    void* p;
    if (fail_after == 0) {
        memory_error = memFullErr;
        return NULL;
    }
    if (fail_after > 0)
        --fail_after;
    p = malloc(n);
    assert(p);
    ++live;
    return p;
}

static void test_dispose_ptr(Ptr p)
{
    assert(p && live > 0);
    --live;
    free(p);
    memory_error = noErr;
}

static Handle test_handle(Size size)
{
    Handle h;
    Ptr bytes;
    /* Count one failure point for each Macintosh handle allocation. */
    h = test_alloc(sizeof *h + size);
    if (!h)
        return NULL;
    bytes = (Ptr)(h + 1);
    HIDDEN_RAW_SET(*h, RM(bytes));
    last_handle_size = size;
    return h;
}

OSErr MemError(void)
{
    return memory_error;
}

void Enqueue(QElemPtr record, QHdrPtr queue)
{
    ++installed;
    installed_record = (CRMRecPtr)record;
    if (PP_RAW(queue->qTail))
        PP_ASSIGN(((CRMRecPtr)PPR(queue->qTail))->qLink, record);
    else
        PP_ASSIGN(queue->qHead, record);
    PP_ASSIGN(queue->qTail, record);
}

OSErrRET Dequeue(QElemPtr record, QHdrPtr queue)
{
    /* Rollback removes the first serial record; tests start with an empty queue. */
    assert(PPR(queue->qHead) == record);
    PP_RAW(queue->qHead) = PP_RAW(((CRMRecPtr)record)->qLink);
    if (!PP_RAW(queue->qHead))
        PP_RAW(queue->qTail) = 0;
    --installed;
    return noErr;
}

#undef NewPtrSys
#define NewPtrSys(n) ((Ptr)test_alloc(n))
#undef NewHandle
#define NewHandle(n) test_handle(n)
#undef DisposPtr
#define DisposPtr(p) test_dispose_ptr((Ptr)(p))
#undef DisposHandle
#define DisposHandle(h) test_dispose_ptr((Ptr)(h))
#undef block_virtual_ints
#define block_virtual_ints() ((virtual_int_state_t)0)
#undef restore_virtual_ints
#define restore_virtual_ints(n) ((void)(n))
#undef ZONE_SAVE_EXCURSION
#define ZONE_SAVE_EXCURSION(zone, body) \
    do {                                \
        body                            \
    } while (0)
#undef warning_unexpected
#define warning_unexpected(...) ((void)0)
#include "../../text/string.c"
#include "../../devices/commtool.c"
#undef main

static void dispose_record(CRMRecPtr qp)
{
    CRMSerialPtr p = MR((CRMSerialPtr)(uintptr_t)qp->crmAttributes);
    DisposHandle(PPR(p->inputDriverName));
    DisposHandle(PPR(p->outputDriverName));
    DisposHandle(PPR(p->name));
    DisposPtr((Ptr)p);
    DisposPtr((Ptr)qp);
    memset(&commtool_head, 0, sizeof commtool_head);
}

int main(void)
{
    int i;
    char name[301];
    StringHandle h;
    CRMSerialPtr p;
    CRMRecPtr created;
    memset(name, 'x', 300);
    name[300] = 0;
    h = stringhandle_from_c_string(name);
    assert(h && last_handle_size == 256);
    assert(STARH(h)[0] == 255 && !memcmp(STARH(h) + 1, name, 255));
    DisposHandle((Handle)h);
    h = stringhandle_from_c_string("");
    assert(h && last_handle_size == 1 && !STARH(h)[0]);
    DisposHandle((Handle)h);
    for (i = 0; i < 5; ++i) {
        fail_after = i;
        assert(serial_insert(".AIn", ".AOut", "COM1", &created) == memFullErr);
        assert(!created && !live && !installed && !PP_RAW(commtool_head.qHead) && !PP_RAW(commtool_head.qTail));
    }
    fail_after = -1;
    assert(serial_insert(".AIn", ".AOut", "COM1", &created) == noErr);
    assert(installed == 1 && live == 5);
    p = MR((CRMSerialPtr)(uintptr_t)installed_record->crmAttributes);
    assert(STARH(PPR(p->name))[0] == 4);
    assert(!memcmp(STARH(PPR(p->name)) + 1, "COM1", 4));
    assert(CL(installed_record->crmDeviceID) == 1);
    dispose_record(installed_record);
    assert(!live);
    installed = 0;
    /* Fail every allocation across both ports, then retry. No queue entry or
   * successful-initialization flag may survive a failed attempt. */
    for (i = 0; i < 10; ++i) {
        fail_after = i;
        assert(InitCRM() == memFullErr);
        assert(!live && !installed && !PP_RAW(commtool_head.qHead) && !PP_RAW(commtool_head.qTail));
    }
    fail_after = -1;
    assert(InitCRM() == noErr && installed == 2 && live == 10);
    assert(InitCRM() == noErr && installed == 2 && live == 10);
    {
        CRMRecPtr first = (CRMRecPtr)PPR(commtool_head.qHead);
        CRMRecPtr second = (CRMRecPtr)PPR(first->qLink);
        assert(second && CL(first->crmDeviceID) == 1 && CL(second->crmDeviceID) == 2);
        dispose_serial_record(first);
        dispose_serial_record(second);
        assert(!live);
    }
    puts("PASS: atomic serial initialization and retry at all ten allocation points");
    puts("PASS: Pascal string handles and all five serial allocation failure points");
    return 0;
}
