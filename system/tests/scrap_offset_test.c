/*+===================================================================
  File:      scrap_offset_test.c

  Summary:   Test GetScrap's guest-zero output without touching the host clipboard.

  Classes:   None.

  Functions: CToPascalCall, Create, DisposHandle, FSClose, FSOpen, FSReadAll, FSWriteAll, GetHandleSize, GetScrapX,
             HLock, HUnlock, PtrToXHand, PutScrapX, ReallocHandle, SetEOF, SetFPos, SetHandleSize, _NewHandle_flags,
             main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Test GetScrap's guest-zero output without touching the host clipboard. */
#define NDEBUG 1
#include "rsys/common.h"
#include "../../services/scrap.c"
#undef main
#undef NDEBUG
#include <assert.h>
void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];

intptr_t CToPascalCall(void* proc, unsigned long magic, ...)
{
    abort();
}

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

static LONGINT host_result;
static unsigned char* memory;
static Size handle_size;
static boolean_t allocation_failure;
static boolean_t query_mode, resize_failure;
static Size query_size;
static const char* query_text;
static unsigned query_disposals;
static int publications;
static char last_payload[32];
static LONGINT last_length;
static ResType last_type;

void PutScrapX(ResType type, LONGINT length, char* data, int count)
{
    ++publications;
    assert(length <= sizeof last_payload);
    if (length)
        memcpy(last_payload, data, length);
    last_length = length;
    last_type = type;
}

Size GetHandleSize(Handle h)
{
    if (h == (Handle)(memory + 0x1010))
        return query_size;
    return handle_size;
}

int GetScrapX(ResType type, char** h)
{
    assert(h);
    if (query_mode && host_result >= 0) {
        Handle destination = (Handle)h;
        assert(destination == (Handle)(memory + 0x1010));
        HIDDEN_RAW_SET(*destination, RM((Ptr)(memory + 0x4000)));
        query_size = host_result;
        memcpy(memory + 0x4000, query_text, host_result);
    }
    return host_result;
}

OSErr FSOpen(StringPtr name, INTEGER vref, INTEGER* ref)
{
    abort();
}

OSErr SetFPos(INTEGER ref, INTEGER mode, LONGINT offset)
{
    abort();
}

OSErr Create(StringPtr name, INTEGER vref, OSType creator, OSType type)
{
    abort();
}

OSErr FSReadAll(INTEGER ref, LONGINT* count, Ptr p)
{
    abort();
}

OSErr FSWriteAll(INTEGER ref, LONGINT* count, Ptr p)
{
    abort();
}

OSErr FSClose(INTEGER ref)
{
    abort();
}

OSErr SetEOF(INTEGER ref, LONGINT length)
{
    abort();
}

Handle _NewHandle_flags(Size size, boolean_t clear, boolean_t sys)
{
    if (allocation_failure) {
        MemErr = CWC(memFullErr);
        return NULL;
    }
    Handle h = (Handle)(memory + (query_mode ? 0x1010 : 0x1000));
    HIDDEN_RAW_SET(*h, RM((Ptr)(memory + 0x2000)));
    if (query_mode)
        query_size = size;
    else
        handle_size = size;
    MemErr = noErr;
    return h;
}

void DisposHandle(Handle h)
{
    assert(query_mode && h == (Handle)(memory + 0x1010));
    ++query_disposals;
}

void ReallocHandle(Handle h, Size size)
{
    abort();
}

void SetHandleSize(Handle h, Size size)
{
    if (allocation_failure || resize_failure)
        MemErr = CWC(memFullErr);
    else {
        assert(size >= 0 && size < 4096);
        handle_size = size;
        MemErr = noErr;
    }
}

void HLock(Handle h)
{
}

void HUnlock(Handle h)
{
}

OSErrRET PtrToXHand(Ptr src, Handle h, LONGINT size)
{
    assert(size == 4 && !memcmp(src, "test", 4));
    return noErr;
}

int main(void)
{
    memory = calloc(1, 65536);
    Handle h;
    LONGINT off;
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    h = (Handle)(memory + 0x1000);
    host_result = 17;
    memset(memory, 0xa5, 8);
    assert(C_GetScrap(h, TICK("TEXT"), NULL) == 17);
    assert(!memory[0] && !memory[1] && !memory[2] && !memory[3]);
    assert(memory[4] == 0xa5);
    off = CLC(-1);
    assert(C_GetScrap(h, TICK("TEXT"), &off) == 17 && off == 0);
    syn68k_address_mask = 0x00FFFFFF;
    assert(C_GetScrap(h, TICK("TEXT"), NULL) == 17);
    HIDDEN_RAW_SET(ScrapHandle_H, RM(h));
    HIDDEN_RAW_SET(*h, RM((Ptr)(memory + 0x2000)));
    ScrapState = CWC(1);
    ScrapSize = CLC(0);
    host_result = -1;
    off = CLC(123);
    assert(C_GetScrap(h, TICK("TEXT"), &off) == noTypeErr && off == CLC(123));
    {
        LONGINT fields[2] = {CLC(TICK("TEXT")), CLC(4)};
        memcpy(memory + 0x2000, fields, sizeof fields);
        memcpy(memory + 0x2008, "test", 4);
        ScrapSize = CLC(12);
        handle_size = 12;
        assert(C_GetScrap(h, TICK("TEXT"), NULL) == 4);
        assert(!memory[0] && !memory[1] && !memory[2] && memory[3] == 8);
        assert(memory[4] == 0xa5);
        assert(C_GetScrap(h, TICK("TEXT"), &off) == 4 && off == CLC(8));
    }
    syn68k_address_mask = 0xffffffff;
    HIDDEN_RAW_SET(ScrapHandle_H, NULL);
    ScrapState = CWC(-1);
    allocation_failure = TRUE;
    assert(C_ZeroScrap() == memFullErr && !ScrapHandle);
    allocation_failure = FALSE;
    assert(C_ZeroScrap() == noErr);
    assert(Cx(ScrapState) == 1 && Cx(ScrapSize) == 0);
    assert(MR(ScrapName)[0] == 0);
    assert(C_LoadScrap() == noErr && C_UnloadScrap() == noErr);

    /* The following byte is not payload and must not become scrap padding. */
    const struct {
        char payload[3];
        unsigned char guard;
    } odd = {{'a', 'b', 'c'}, 0xa5};

    assert(C_PutScrap(3, TICK("TEST"), (Ptr)odd.payload) == noErr);
    assert(handle_size == 12 && Cx(ScrapSize) == 12);
    assert(last_type == TICK("TEST") && last_length == 3);
    assert(!memcmp(last_payload, "abc", 3) && memory[0x200b] == 0);
    assert(C_UnloadScrap() == noErr && Cx(ScrapState) == 1);
    assert(C_LoadScrap() == noErr && Cx(ScrapSize) == 12);
    ScrapState = 0; /* A caller requesting the old disk state still uses memory. */
    assert(C_UnloadScrap() == noErr && Cx(ScrapState) == 1);
    ScrapState = 0;
    assert(C_LoadScrap() == noErr && Cx(ScrapState) == 1);
    int old_publications = publications;
    allocation_failure = TRUE;
    assert(C_PutScrap(4, TICK("TEXT"), (Ptr) "test") == memFullErr);
    assert(Cx(ScrapSize) == 12 && publications == old_publications);
    allocation_failure = FALSE;
    assert(C_PutScrap(-1, TICK("TEXT"), (Ptr) "test") == paramErr);
    assert(C_PutScrap(1, TICK("TEXT"), NULL) == paramErr);
    assert(C_PutScrap(INT32_MAX, TICK("TEXT"), (Ptr) "test") == paramErr);
    assert(C_PutScrap(3, TICK("MORE"), (Ptr)(memory + 0x2008)) == noErr);
    assert(publications == old_publications + 1);
    assert(last_length == 3 && !memcmp(last_payload, "abc", 3));
    assert(C_ZeroScrap() == noErr && Cx(ScrapSize) == 0);
    assert(handle_size == 0 && Cx(ScrapState) == 1);
    puts("Fileless scrap load/unload, odd lengths, aliases and allocation failures passed");
    query_mode = TRUE;
    host_result = 10;
    query_text = "Calculator";
    off = CLC(-1);
    old_publications = publications;
    assert(C_GetScrap(NULL, TICK("TEXT"), &off) == 10 && CL(off) == 8);
    assert(Cx(ScrapSize) == 18 && !memcmp(memory + 0x2008, query_text, 10));
    assert(!memcmp(memory + 0x2000, "TEXT\0\0\0\12", 8));
    INTEGER count = ScrapCount;
    assert(C_GetScrap(NULL, TICK("TEXT"), &off) == 10 && ScrapCount == count);
    assert(Cx(ScrapSize) == 18 && publications == old_publications);
    host_result = 5;
    query_text = "style";
    assert(C_GetScrap(NULL, TICK("styl"), &off) == 5 && CL(off) == 26);
    assert(Cx(ScrapSize) == 32 && memory[0x201f] == 0);
    host_result = 14;
    query_text = "Calculator new";
    assert(C_GetScrap(NULL, TICK("TEXT"), &off) == 14 && CL(off) == 8);
    assert(Cx(ScrapSize) == 36 && !memcmp(memory + 0x201e, "style", 5));
    host_result = 3;
    query_text = "abc";
    assert(C_GetScrap(NULL, TICK("TEXT"), &off) == 3 && CL(off) == 8);
    assert(Cx(ScrapSize) == 26 && !memcmp(memory + 0x2014, "style", 5));
    assert(memory[0x200b] == 0);
    unsigned char snapshot[36];
    memcpy(snapshot, memory + 0x2000, sizeof snapshot);
    count = ScrapCount;
    host_result = 32;
    query_text = "01234567890123456789012345678901";
    resize_failure = TRUE;
    off = CLC(-1);
    assert(C_GetScrap(NULL, TICK("TEST"), &off) == memFullErr && CL(off) == -1);
    assert(Cx(ScrapSize) == 26 && ScrapCount == count);
    assert(!memcmp(snapshot, memory + 0x2000, sizeof snapshot));
    resize_failure = FALSE;
    assert(query_disposals == 6 && publications == old_publications);
    puts("Host scrap queries: valid offsets, preserved formats, replacements and allocation failure passed");
    free(memory);
    puts("GetScrap guest-zero and explicit offsets passed; host clipboard untouched");
    return 0;
}
