/*+===================================================================
  File:      intl_compare_test.c

  Summary:   Long-date output must initialize strings and retain all 64 date bits.

  Classes:   None.

  Functions: BlockMove, CToPascalCall, C_CurResFile, C_GetResource, C_GetScript, C_LoadResource, C_ResError,
             C_UseResFile, GetHandleSize, HGetState, HLock, HSetState, NumToString, ROMlib_getrestid, ROMlib_setreserr,
             _NewHandle_flags, _warning, main, syn68k_guest_to_host, syn68k_host_to_guest, syn68k_memory_span

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Long-date output must initialize strings and retain all 64 date bits. */
#define NDEBUG 1
#define CALL_EMULATOR(address) abort()
#include "../../text/iu.c"
#undef main
#undef NDEBUG
#include <assert.h>
const char NULL_STRING[] = "";

void _warning(int err, const char* kind, const char* file, int line, const char* fn, const char* fmt, ...)
{
    abort();
}

uint64 ROMlib_offset;
CPUState cpu_state;
uint16* syn68k_callback_address_space;

void C_LoadResource(Handle volatile h)
{
    abort();
}

SignedByte HGetState(Handle h)
{
    abort();
}

void HLock(Handle h)
{
    abort();
}

void HSetState(Handle h, SignedByte state)
{
    abort();
}

int syn68k_memory_span(syn68k_addr_t address, uint32 length)
{
    abort();
}

uint32 syn68k_address_mask = 0xFFFFFFFFUL;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;
void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];
static void* pointers[32];
static unsigned pointer_count;

intptr_t CToPascalCall(void* proc, unsigned long magic, ...)
{
    abort();
}

void* syn68k_guest_to_host(uint32 address)
{
    assert(address && address <= pointer_count);
    return pointers[address - 1];
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    for (unsigned i = 0; i < pointer_count; ++i)
        if (pointers[i] == (void*)address)
            return i + 1;
    pointers[pointer_count++] = (void*)address;
    return pointer_count;
}

static HIDDEN_Ptr table_handle;
static unsigned char table[128];
static Size table_size = sizeof table;
static int table_enabled;
static Handle map_handle;
static INTEGER resource_error;

INTEGER
C_ResError(void)
{
    return resource_error;
}

INTEGER
ROMlib_setreserr(INTEGER error)
{
    return resource_error = error;
}

Handle C_GetResource(ResType type, INTEGER id)
{
    if (type == TICK("itlm")) {
        resource_error = map_handle ? noErr : resNotFound;
        return map_handle;
    }
    assert(type == TICK("itl2") || type == TICK("itl4"));
    assert(!id);
    return table_enabled ? &table_handle : NULL;
}

LONGINT
C_GetScript(INTEGER script, INTEGER verb)
{
    return 0;
}

Size GetHandleSize(Handle h)
{
    return table_size;
}

static Handle locale[2];

INTEGER
C_CurResFile(void)
{
    return 0;
}

void C_UseResFile(INTEGER ref)
{
}

Handle ROMlib_getrestid(ResType type, INTEGER id)
{
    return id < 2 ? locale[id] : NULL;
}

Handle _NewHandle_flags(Size size, boolean_t sys, boolean_t clear)
{
    abort();
}

void NumToString(LONGINT n, StringPtr p)
{
    char text[32];
    int length = snprintf(text, sizeof text, "%ld", (long)n);
    p[0] = length;
    memcpy(p + 1, text, length);
}

void BlockMove(Ptr source, Ptr destination, Size size)
{
    memmove(destination, source, size);
}

int main(void)
{
    syn68k_host_alias_count = 1;
    syn68k_host_alias_size = 0xffffffff;
    assert(C_IUMagPString((Ptr) "Alpha", (Ptr) "Beta", 5, 4, NULL) < 0);
    assert(C_IUMagPString((Ptr) "Beta", (Ptr) "Alpha", 4, 5, NULL) > 0);
    assert(!C_IUMagIDPString((Ptr) "Alpha", (Ptr) "ALPHA", 5, 5, NULL));
    assert(C_IUMagIDPString((Ptr) "Alpha", (Ptr) "Beta", 5, 4, NULL) == 1);
    assert(C_IUMagIDPString((Ptr) "Beta", (Ptr) "Alpha", 4, 5, NULL) == 1);
    assert(!C_IUMagIDPString((Ptr) "e", (Ptr) "\x8e", 1, 1, NULL));
    assert(!C_IUMagPString(NULL, NULL, 0, -1, NULL));
    assert(C_IUMagPString((Ptr) "a", NULL, 1, 0, NULL) > 0);
    assert(C_IUTextOrder((Ptr) "a", (Ptr) "b", 1, 1, -1, 0, 0, 0) < 0);
    assert(C_IUScriptOrder(-2, 0) == 0);
    assert(C_IULangOrder(0, 1) < 0);
    resource_error = ioErr;
    assert(C_IUScriptOrder(-3, 0) > 0);
    assert(resource_error == ioErr);
    assert(C_IULangOrder(-2, 0) == 0);
    {
        struct {
            unsigned before;
            HIDDEN_Handle handle;
            unsigned after;
        } output = {0xa5a5a5a5, {0}, 0xa5a5a5a5};

        LONGINT offset, length;
        table_enabled = 1;
        HIDDEN_RAW_SET(table_handle, RM((Ptr)table));
        for (int selector = 0; selector < 5; ++selector) {
            const unsigned offsets[] = {22, 24, 36, 32, 40};
            const unsigned lengths[] = {54, 56, 60, 58, 62};
            memset(table, 0, sizeof table);
            table[offsets[selector] + (selector < 2 ? 1 : 3)] = 80;
            table[lengths[selector] + 1] = 16;
            C_IUGetItlTable(0, selector, &output.handle, &offset, &length);
            assert(HIDDEN_NATIVE(output.handle) == &table_handle);
            assert(CL(offset) == 80 && CL(length) == 16);
            assert(output.before == 0xa5a5a5a5 && output.after == 0xa5a5a5a5);
            table_size = 90;
            C_IUGetItlTable(0, selector, &output.handle, &offset, &length);
            assert(!HIDDEN_RAW(output.handle) && !offset && !length);
            table_size = sizeof table;
        }
        C_IUGetItlTable(0, 5, &output.handle, &offset, &length);
        assert(!HIDDEN_RAW(output.handle) && !offset && !length);
        table_enabled = 0;
    }
    {
        const unsigned char mapping[] = {0, 1,  0, 0, 0, 2, 0, 0, 0, 30, 0, 0, 0, 14, 0, 0,  0, 44, 0, 0, 0, 22,
                                         0, 0,  0, 0, 0, 0, 0, 0, 0, 64, 0, 0, 0, 2,  0, 0,  0, 4,  0, 1, 0, 11,
                                         0, 11, 0, 0, 0, 4, 0, 4, 0, 0,  0, 0, 0, 0,  0, 11, 0, 1,  0, 2, 0, 0};
        memcpy(table, mapping, sizeof mapping);
        table_size = sizeof mapping;
        map_handle = &table_handle;
        assert(C_IULangOrder(4, 0) < 0);
        assert(C_IULangOrder(-3, 0) < 0);
        assert(!C_IULangOrder(-2, 0));
        /* Script order takes precedence over text and language order. */
        assert(C_IUTextOrder((Ptr) "a", (Ptr) "z", 1, 1, 1, 0, 4, 0) > 0);
        assert(C_IUTextOrder((Ptr) "z", (Ptr) "a", 1, 1, 0, 0, 4, 0) < 0);
        assert(C_IUTextOrder((Ptr) "a", (Ptr) "z", 1, 1, 0, 0, 4, 4) < 0);
        assert(resource_error == ioErr);
        table_size = 32; /* Truncated tables use the bounded Roman fallback. */
        assert(C_IULangOrder(4, 0) > 0 && resource_error == ioErr);
        table_size = -1;
        assert(C_IULangOrder(4, 0) > 0 && resource_error == ioErr);
    }
    puts("PASS: Roman comparisons, itlm ordering, table outputs and preserved errors");
    return 0;
}
