/*+===================================================================
  File:      icon_dispose_test.c

  Summary:   A freed/short suite must not expose adjacent heap objects as icon handles.

  Classes:   None.

  Functions: DisposHandle, GetHandleSize, HGetState, HSetState, _warning, hlock_return_orig_state, main,
             syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* A freed/short suite must not expose adjacent heap objects as icon handles. */
#define NDEBUG 1
#include "../../ui/icon.c"
#undef main
#undef NDEBUG
#include <assert.h>

__typeof__(ROMlib_offset) ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;
static Handle suite, victim, owned, resource;
static Size suite_size;
static unsigned disposed, locks, restored;

void _warning(int error, const char* kind, const char* file, int line, const char* function, const char* format, ...)
{
}

void* syn68k_guest_to_host(uint32 address)
{
    return (void*)(uintptr_t)(ROMlib_offset + address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return address - ROMlib_offset;
}

Size GetHandleSize(Handle handle)
{
    assert(handle == suite);
    return suite_size;
}

SignedByte hlock_return_orig_state(Handle handle)
{
    assert(handle == suite);
    ++locks;
    return 0x40;
}

void HSetState(Handle handle, SignedByte state)
{
    assert(handle == suite && state == 0x40);
    ++restored;
}

SignedByte HGetState(Handle handle)
{
    assert(handle == victim || handle == owned || handle == resource);
    return handle == resource ? RSRCBIT : 0;
}

void DisposHandle(Handle handle)
{
    assert(handle != victim && handle != resource);
    assert(handle == suite || handle == owned);
    ++disposed;
}

int main(void)
{
    unsigned char* memory = calloc(1, 65536);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    suite = (Handle)(memory + 0x1000);
    victim = (Handle)(memory + 0x1100);
    owned = (Handle)(memory + 0x1200);
    resource = (Handle)(memory + 0x1300);
    HIDDEN_RAW_SET(*suite, RM((Ptr)(memory + 0x2000)));
    Handle* icons = (Handle*)STARH(suite);
    /* MATM's stale suite pointed into reused memory next to GWorld bookkeeping.
     The old cleanup loop interpreted its PixMapHandle as the sixth icon. */
    icons[5] = victim;
    for (suite_size = 0; suite_size < sizeof(cotton_suite_layout_t); ++suite_size) {
        assert(C_DisposeIconSuite(suite, TRUE) == paramErr);
        assert(C_DisposeIconSuite(suite, FALSE) == paramErr);
        assert(!disposed && !locks && !restored);
    }
    assert(C_DisposeIconSuite(NULL, TRUE) == paramErr);
    HIDDEN_RAW_SET(*suite, 0);
    assert(C_DisposeIconSuite(suite, TRUE) == paramErr);
    HIDDEN_RAW_SET(*suite, RM((Ptr)(memory + 0x2000)));
    /* A reused handle can have enough storage but belong to a different kind
     of object. A size check alone must not grant ownership of its contents. */
    suite_size = sizeof(cotton_suite_layout_t) + 32;
    assert(C_DisposeIconSuite(suite, TRUE) == paramErr && !disposed);
    memset(icons, 0, sizeof(cotton_suite_layout_t));
    icons[0] = owned;
    icons[1] = resource;
    suite_size = sizeof(cotton_suite_layout_t);
    ((cotton_suite_layout_t*)icons)->signature = CLC(ICON_SUITE_SIGNATURE);
    assert(C_DisposeIconSuite(suite, TRUE) == noErr);
    assert(disposed == 2 && locks == 1 && restored == 1);
    assert(C_DisposeIconSuite(suite, TRUE) == paramErr && disposed == 2);
    disposed = locks = restored = 0;
    ((cotton_suite_layout_t*)icons)->signature = CLC(ICON_SUITE_SIGNATURE);
    assert(C_DisposeIconSuite(suite, FALSE) == noErr);
    assert(disposed == 1 && !locks && !restored);
    free(memory);
    puts("Icon disposal: stale/short suites preserve adjacent handles; valid ownership retained");
    return 0;
}
