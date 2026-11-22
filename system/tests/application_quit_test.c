/*+===================================================================
  File:      application_quit_test.c

  Summary:   Host close requests queue guest quit, retaining the Command-Q fallback.

  Classes:   None.

  Functions: CToPascalCall, C_AECreateAppleEvent, C_AECreateDesc, C_AEDisposeDesc, C_AEGetEventHandler, C_AESend,
             C_GetCurrentProcess, C_PostHighLevelEvent, ROMlib_PPostEvent, ROMlib_prepare_quit_menu_key, main,
             msecs_elapsed, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Host close requests queue guest quit, retaining the Command-Q fallback. */
#define NDEBUG 1
#include "../../events/toolevent.c"
#undef main
#undef NDEBUG
#include <assert.h>

#if __SIZEOF_POINTER__ == 8
uint64 ROMlib_offset;
#else
uint32 ROMlib_offset;
#endif
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;
void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];
static unsigned keys, sends, disposals, stage, fail_stage;
static Boolean installed;
LONGINT ROMlib_creator;
static unsigned menu_requests;
int ROMlib_clock;
struct timeval ROMlib_start_time;
LONGINT ROMlib_GMTcorrect;

void ROMlib_prepare_quit_menu_key(void)
{
    ++menu_requests;
}

unsigned long msecs_elapsed(void)
{
    return 123;
}

void* syn68k_guest_to_host(uint32 address)
{
    return (void*)(ROMlib_offset + address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return address - ROMlib_offset;
}

intptr_t CToPascalCall(void* proc, unsigned long magic, ...)
{
    abort();
}

OSErrRET ROMlib_PPostEvent(INTEGER code, LONGINT message, HIDDEN_EvQElPtr* element, LONGINT when, Point where,
                           INTEGER modifiers)
{
    const INTEGER codes[] = {keyDown, keyDown, keyUp, keyUp};
    const LONGINT messages[] = {MKV_CLOVER << 8, (MKV_q << 8) | 'q', (MKV_q << 8) | 'q', MKV_CLOVER << 8};
    assert(keys < 4 && code == codes[keys] && message == messages[keys]);
    assert((modifiers & cmdKey) == (keys ? cmdKey : 0));
    ++keys;
    return noErr;
}

OSErr C_AEGetEventHandler(AEEventClass cls, AEEventID id, HIDDEN_ProcPtr* handler, int32* refcon, Boolean system)
{
    assert(cls == kCoreEventClass && id == kAEQuitApplication && !system);
    return installed ? noErr : errAEHandlerNotFound;
}

static OSErr next_stage(void)
{
    return ++stage == fail_stage ? memFullErr : noErr;
}

OSErr C_GetCurrentProcess(ProcessSerialNumber* psn)
{
    psn->highLongOfPSN = 0;
    psn->lowLongOfPSN = CLC(kCurrentProcess);
    return next_stage();
}

OSErr C_AECreateDesc(DescType type, Ptr data, Size size, AEDesc* desc)
{
    ProcessSerialNumber* psn = (ProcessSerialNumber*)data;
    assert(type == typeProcessSerialNumber && size == sizeof *psn);
    assert(!psn->highLongOfPSN && psn->lowLongOfPSN == CLC(kCurrentProcess));
    return next_stage();
}

OSErr C_AECreateAppleEvent(AEEventClass cls, AEEventID id, AEAddressDesc* target, int16 return_id, int32 transaction,
                           AppleEvent* event)
{
    assert(cls == kCoreEventClass && id == kAEQuitApplication);
    assert(return_id == -1 && !transaction);
    return next_stage();
}

OSErr C_AESend(AppleEvent* event, AppleEvent* reply, AESendMode mode, AESendPriority priority, int32 timeout,
               IdleProcPtr idle, EventFilterProcPtr filter)
{
    assert(!reply && mode == kAENoReply && !idle && !filter);
    ++sends;
    return next_stage();
}

OSErr C_AEDisposeDesc(AEDesc* desc)
{
    ++disposals;
    return noErr;
}

OSErr C_PostHighLevelEvent(EventRecord* event, Ptr receiver, int32 refcon, Ptr payload, int32 size, int32 options)
{
    const unsigned char expected[] = {'a', 'e', 'v', 't', 0, 1, 0, 1, ';', ';', ';', ';'};
    assert(size == sizeof expected && !memcmp(payload, expected, size));
    assert(CW(event->what) == kHighLevelEvent);
    assert(CL(event->message) == kCoreEventClass);
    assert(!memcmp(&event->where, "quit", 4));
    assert(!receiver && refcon == -1 && options == -1);
    ++sends;
    return fail_stage ? memFullErr : noErr;
}

int main(void)
{
    unsigned char* memory = calloc(1, 65536);
    AE_info_t* info;
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    ROMlib_send_quit();
    assert(keys == 4 && !sends && !disposals && menu_requests == 1);
    info = (AE_info_t*)(memory + 0x4000);
    PP_ASSIGN(AE_info_H, info);
    PP_ASSIGN(info->appl_zone_tables, (AE_zone_tables_h)(memory + 0x5000));
    keys = 0;
    ROMlib_send_quit();
    assert(keys == 4 && !sends && !disposals && menu_requests == 2);
    installed = TRUE;
    keys = 0;
    ROMlib_send_quit();
    assert(!keys && sends == 1 && disposals == 2 && stage == 4 && menu_requests == 2);
    for (fail_stage = 1; fail_stage <= 4; ++fail_stage) {
        stage = keys = sends = disposals = 0;
        ROMlib_send_quit();
        assert(stage == fail_stage && keys == 4 && disposals == 2);
        assert(sends == (fail_stage == 4));
        assert(menu_requests == 2 + fail_stage);
    }
    /* Finder accepts the core quit event through its own HLE dispatcher. */
    fail_stage = stage = keys = sends = disposals = 0;
    installed = FALSE;
    ROMlib_creator = TICK("MACS");
    ROMlib_send_quit();
    assert(!keys && sends == 1 && !disposals && !stage);
    fail_stage = 1;
    sends = 0;
    ROMlib_send_quit();
    assert(keys == 4 && sends == 1 && !disposals);
    free(memory);
    puts("Guest quit: AppleEvent dispatch, keyboard fallback and failure cleanup passed");
    return 0;
}
