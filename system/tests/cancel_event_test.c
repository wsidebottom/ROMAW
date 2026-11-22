/*+===================================================================
  File:      cancel_event_test.c

  Summary:   Cancel polling removes only matching keyboard events from the OS queue.

  Classes:   None.

  Functions: Dequeue, main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Cancel polling removes only matching keyboard events from the OS queue. */
#define NDEBUG 1
#include "../../events/osevent.c"
#undef main
#undef NDEBUG
#include <assert.h>
#include "guest_test_memory.h"

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 0xe0000000, syn68k_host_alias_size = 0x10000000;

void* syn68k_guest_to_host(uint32 p)
{
    return fixture_guest_to_host(p);
}

uint32 syn68k_host_to_guest(ptr_sized_uint p)
{
    return fixture_host_to_guest(p);
}

CPUState cpu_state;
virtual_int_state_t _virtual_interrupts_blocked;

OSErrRET Dequeue(QElemPtr element, QHdrPtr header)
{
    QElemPtr previous = NULL;
    for (QElemPtr p = PPR(header->qHead); p; p = PPR(p->evQElem.qLink)) {
        if (p == element) {
            if (previous)
                PP_RAW(previous->evQElem.qLink) = PP_RAW(p->evQElem.qLink);
            else
                PP_RAW(header->qHead) = PP_RAW(p->evQElem.qLink);
            if (PPR(header->qTail) == p)
                PP_ASSIGN(header->qTail, previous);
            return noErr;
        }
        previous = p;
    }
    return -1;
}

int main(void)
{
    EventRecord event = {0};
    event.what = CWC(keyDown);
    event.message = CLC(0x351b);
    assert(ROMlib_user_cancel_event(&event));
    event.message = CLC('.');
    assert(!ROMlib_user_cancel_event(&event));
    event.modifiers = CWC(cmdKey);
    assert(ROMlib_user_cancel_event(&event));
    event.what = CWC(keyUp);
    assert(!ROMlib_user_cancel_event(&event));
    assert(!ROMlib_user_cancel_event(NULL));
    unsigned char* memory = calloc(1, 65536);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    EvQEl *first = (EvQEl*)(memory + 4096), *cancel = first + 1, *last = cancel + 1;
    PP_ASSIGN(first->qLink, (QElemPtr)cancel);
    PP_ASSIGN(cancel->qLink, (QElemPtr)last);
    first->evtQWhat = cancel->evtQWhat = last->evtQWhat = CWC(keyDown);
    first->evtQMessage = CLC('x');
    cancel->evtQMessage = CLC(0x351b);
    last->evtQMessage = CLC('y');
    PP_ASSIGN(EventQueue.qHead, (QElemPtr)first);
    PP_ASSIGN(EventQueue.qTail, (QElemPtr)last);
    nevent = 3;
    cpu_state.sr = 0x2300;
    assert(ROMlib_next_user_cancel_event(&event) == TRUE);
    assert(CL(event.message) == 0x351b && nevent == 2);
    assert(PPR(EventQueue.qHead) == (QElemPtr)first && PPR(first->qLink) == (QElemPtr)last);
    assert(PPR(EventQueue.qTail) == (QElemPtr)last && !PP_RAW(last->qLink));
    assert(cpu_state.sr == 0x2300 && !_virtual_interrupts_blocked);
    assert(ROMlib_next_user_cancel_event(&event) == FALSE && nevent == 2);
    assert(CL(event.message) == 0x351b);
    assert(ROMlib_next_user_cancel_event(NULL) == FALSE);
    last->evtQMessage = CLC(0x351b);
    assert(ROMlib_next_user_cancel_event(NULL) == TRUE && nevent == 1);
    assert(PPR(EventQueue.qTail) == (QElemPtr)first && !PP_RAW(first->qLink));
    free(memory);
    puts("Cancel events: classification, selective removal, preserved keys and interrupt state passed");
    return 0;
}
