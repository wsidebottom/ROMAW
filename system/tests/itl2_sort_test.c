/*+===================================================================
  File:      itl2_sort_test.c

  Summary:   Execute the actual comparison driver with observable register callbacks.

  Classes:   None.

  Functions: CToPascalCall, C_CurResFile, C_GetResource, C_GetScript, C_LoadResource, C_ResError, C_UseResFile,
             GetHandleSize, HGetState, HLock, HSetState, ROMlib_getrestid, ROMlib_setreserr, _NewHandle_flags, _warning,
             main, syn68k_guest_to_host, syn68k_host_to_guest, syn68k_memory_span

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Execute the actual comparison driver with observable register callbacks. */
static void sorting_callback(unsigned address);
#define CALL_EMULATOR(address) sorting_callback(address)
#define NDEBUG 1
#include "../../text/iu.c"
#undef main
#undef NDEBUG
#include <assert.h>

CPUState cpu_state;
uint64 ROMlib_offset;
uint16* syn68k_callback_address_space;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;
void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];
const char NULL_STRING[] = "";
static unsigned char* memory;
static Handle resource;
static unsigned mode, calls[5], warnings, loads;
static Size resource_size = 256;
static SignedByte resource_state = 0x40;

enum {
    reverse_projection = 1,
    fold_projection = 2,
    hash_fetch = 4,
    reverse_vernier = 8,
    negate_exit = 16,
    ignore_dash = 32,
    double_fetch = 64,
    invalid_fetch = 128,
    scratch_init = 256,
    stall = 512,
    weak_magnitude = 1024,
    text_order = 2048
};

void* syn68k_guest_to_host(uint32 address)
{
    assert(address < 65536);
    return memory + address;
}

uint32 syn68k_host_to_guest(ptr_sized_uint p)
{
    assert(p >= (uintptr_t)memory && p < (uintptr_t)memory + 65536);
    return p - (uintptr_t)memory;
}

int syn68k_memory_span(syn68k_addr_t address, uint32 length)
{
    return (uint64)address + length <= 65536;
}

void _warning(int error, const char* kind, const char* file, int line, const char* function, const char* message, ...)
{
    ++warnings;
}

intptr_t CToPascalCall(void* p, unsigned long convention, ...)
{
    abort();
}

void C_LoadResource(Handle volatile h)
{
    assert(h == resource);
    ++loads;
    HIDDEN_RAW_SET(*h, RM((Ptr)(memory + 0x2000)));
}

Size GetHandleSize(Handle h)
{
    assert(h == resource);
    return resource_size;
}

SignedByte HGetState(Handle h)
{
    assert(h == resource);
    return resource_state;
}

void HLock(Handle h)
{
    assert(h == resource);
    resource_state |= 0x80;
}

void HSetState(Handle h, SignedByte state)
{
    assert(h == resource);
    resource_state = state;
}

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
    return NULL;
}

Handle _NewHandle_flags(Size size, boolean_t system, boolean_t clear)
{
    abort();
}

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

LONGINT
C_GetScript(INTEGER script, INTEGER selector)
{
    return 0;
}

Handle C_GetResource(ResType type, INTEGER id)
{
    if (type == TICK("itl2"))
        return resource;
    assert(type == TICK("itlm"));
    return NULL;
}

static void sorting_callback(unsigned address)
{
    unsigned hook = (address - 0x2000 - 64) / 16;
    assert(hook < 5 && address == 0x2000 + 64 + hook * 16);
    assert(EM_A4 == 0x2000 && EM_A6 == 0xf000 - 22 && EM_A7 == 0xf000 - 88);
    assert(resource_state == (SignedByte)0xc0);
    romaw_itl2_frame* f = (romaw_itl2_frame*)(memory + EM_A6 - 66);
    assert(CL(f->resource) == 0x1000 && CL(f->a_text) == 0x4000 && CL(f->b_text) == 0x5000);
    assert(CL(f->old_a6) == 0x1234000e);
    romaw_itl2_string* s = NULL;
    if (hook == 1 || hook == 3) {
        assert(EM_A2 == EM_A6 - 16 || EM_A2 == EM_A6 - 32);
        s = (romaw_itl2_string*)(memory + EM_A2);
    }
    ++calls[hook];
    if (hook == 0 && (mode & scratch_init))
        f->scratch[0] = 0x5a;
    if (hook == 0 && (mode & weak_magnitude))
        f->weak_magnitude = 1;
    if (hook == 1) {
        if ((mode & hash_fetch) && (EM_D4 & 65535) == 0x2300)
            EM_D4 = 0x5a00;
        if ((mode & double_fetch) && (EM_D4 & 65535) == 0x4000 && !(EM_D5 & 255)) {
            EM_D4 = 0xa000 | memory[EM_A3++];
            s->remaining = CW((uint16)(CW(s->remaining) - 1));
        }
        if (mode & invalid_fetch)
            EM_A3 = 0xdeadbeef;
    }
    if (hook == 2) {
        assert(EM_A2 == EM_A6 - 11 || EM_A2 == EM_A6 - 27);
        if (mode & reverse_vernier)
            EM_D4 = 255 - (EM_D4 & 255);
        cpu_state.ccnz = (mode & reverse_vernier) != 0;
    }
    if (hook == 3) {
        cpu_state.ccnz = 0;
        if (mode & reverse_projection) {
            EM_D4 ^= 0xff00;
            cpu_state.ccnz = 1;
        }
        if (mode & fold_projection) {
            unsigned c = (EM_D4 >> 8) & 255;
            EM_D4 = (c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c) * 256;
            cpu_state.ccnz = 1;
        }
        if (mode & double_fetch)
            cpu_state.ccnz = 1;
        if ((mode & ignore_dash) && (EM_D4 & 65535) == 0x2d00) {
            s->ignored = 1;
            EM_D4 = 0;
            cpu_state.ccnz = 1;
        }
        if (mode & stall)
            s->no_fetch = 1;
    }
    if (hook == 4) {
        if (mode & scratch_init)
            assert(f->scratch[0] == 0x5a);
        if (mode & negate_exit)
            f->result = CW((int16)-CW(f->result));
    }
    EM_D0 = 0xaaaaaaaa;
    EM_D1 = 0xbbbbbbbb;
    EM_A0 = 0xcccccccc;
    EM_A1 = 0xdddddddd;
    cpu_state.ccx = 0;
}

static int compare(const char* a, const char* b, unsigned flags, int identity)
{
    M68kReg saved[16];
    mode = flags;
    memcpy(memory + 0x4000, a, strlen(a));
    memcpy(memory + 0x5000, b, strlen(b));
    memset(calls, 0, sizeof calls);
    for (unsigned i = 0; i < 16; ++i)
        cpu_state.regs[i].ul.n = 0x12340000 + i;
    EM_A7 = 0xf000;
    memcpy(saved, cpu_state.regs, sizeof saved);
    cpu_state.ccnz = 17;
    cpu_state.ccn = 0;
    cpu_state.ccc = 1;
    cpu_state.ccv = 0;
    cpu_state.ccx = 4;
    int result;
    if (flags & text_order)
        result = C_IUTextOrder((Ptr)(memory + 0x4000), (Ptr)(memory + 0x5000), strlen(a), strlen(b), 0, 0, 0, 0);
    else
        result = identity
                     ? C_IUMagIDPString((Ptr)(memory + 0x4000), (Ptr)(memory + 0x5000), strlen(a), strlen(b), resource)
                     : C_IUMagPString((Ptr)(memory + 0x4000), (Ptr)(memory + 0x5000), strlen(a), strlen(b), resource);
    assert(!memcmp(saved, cpu_state.regs, sizeof saved));
    assert(cpu_state.ccnz == 17 && !cpu_state.ccn && cpu_state.ccc == 1 && !cpu_state.ccv && cpu_state.ccx == 4);
    assert(resource_state == 0x40);
    return result;
}

int main(void)
{
    memory = calloc(1, 65536);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_callback_address_space = (uint16*)(memory + 0x3000);
    resource = (Handle)(memory + 0x1000);
    HIDDEN_RAW_SET(*resource, RM((Ptr)(memory + 0x2000)));
    const unsigned fields[] = {0, 2, 4, 6, 10};
    for (unsigned i = 0; i < 5; ++i)
        memory[0x2000 + fields[i] + 1] = 64 + i * 16;
    assert(compare("a", "b", reverse_projection | scratch_init, 0) > 0);
    assert(calls[0] == 1 && calls[1] == 2 && calls[3] == 2 && calls[4] == 1);
    assert(compare("a", "b", reverse_projection | text_order, 0) > 0);
    assert(compare("a", "b", reverse_projection | negate_exit, 0) < 0);
    assert(!compare("A", "a", fold_projection, 1));
    assert(compare("A", "a", 0, 0) < 0 && calls[2] == 2);
    assert(compare("A", "a", reverse_vernier, 0) > 0);
    assert(compare("#", "A", hash_fetch, 0) > 0);
    assert(compare("@a", "Za", double_fetch, 0) > 0);
    assert(!compare("a-b", "Ab", ignore_dash, 1));
    assert(!compare("a--", "A", ignore_dash, 1));
    assert(!compare("\xae", "AE", 0, 1));
    assert(compare("\xae", "AE", 0, 0) > 0);
    assert(!compare("\xa7", "ss", 0, 1));
    assert(!compare("\xde", "fi", 0, 1));
    assert(!compare("abc", "abc", 0, 0) && !calls[0]);
    assert(compare("", "x", 0, 0) < 0 && !calls[0]);
    assert(compare("a", "b", weak_magnitude, 1) < 0);
    HIDDEN_RAW_SET(*resource, NULL);
    assert(compare("a", "b", reverse_projection, 0) > 0 && loads == 1);
    assert(!warnings);
    assert(compare("@", "Z", double_fetch, 0) < 0 && warnings == 1);
    assert(compare("a", "b", invalid_fetch, 0) < 0 && warnings == 2);
    assert(compare("A", "a", fold_projection | stall, 0) < 0 && warnings == 3);
    memory[0x2001] = 65;
    assert(compare("a", "b", 0, 0) < 0 && warnings == 4 && !calls[0]);
    free(memory);
    puts("PASS: custom itl2 hooks, frame ABI, expansions, ignored text, register restoration and fallbacks");
    return 0;
}
