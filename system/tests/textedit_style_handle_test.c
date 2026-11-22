/*+===================================================================
  File:      textedit_style_handle_test.c

  Summary:   A host pointer must never overwrite the mode/size after the guest Handle.

  Classes:   None.

  Functions: _gui_fatal, _warning, main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* A host pointer must never overwrite the mode/size after the guest Handle. */
#define NDEBUG 1
#include "../../textedit/teIMV.c"
#undef main
#undef NDEBUG
#include <assert.h>

__typeof__(ROMlib_offset) ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;

void* syn68k_guest_to_host(uint32 address)
{
    abort();
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    abort();
}

void _warning(int error, const char* kind, const char* file, int line, const char* function, const char* format, ...)
{
}

void _gui_fatal(const char* file, int line, const char* function, const char* format, ...)
{
    abort();
}

int main(void)
{
    unsigned char* memory = calloc(1, 65536);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    TEHandle te = (TEHandle)(memory + 0x1000);
    TEPtr record = (TEPtr)(memory + 0x2000);
    TEStyleHandle style = (TEStyleHandle)(memory + 0x3000);
    HIDDEN_RAW_SET(*te, RM((Ptr)record));
    TE_TX_SIZE_X(te) = CWC(-1);
    TE_TX_MODE_X(te) = CWC(0x1234);
    C_SetStylHandle(style, te);
    assert(TE_TX_MODE_X(te) == CWC(0x1234));
    assert(TE_TX_SIZE_X(te) == CWC(-1));
    assert(TE_GET_STYLE(te) == style);
    assert(TEP_GET_STYLE(record) == style);
    C_SetStylHandle(NULL, te);
    assert(!TE_GET_STYLE(te));
    assert(TE_TX_MODE_X(te) == CWC(0x1234) && TE_TX_SIZE_X(te) == CWC(-1));
    TE_TX_SIZE_X(te) = CWC(12);
    INTEGER saved = TE_TX_FONT_X(te);
    C_SetStylHandle(style, te);
    assert(TE_TX_FONT_X(te) == saved && !TE_GET_STYLE(te));
    free(memory);
    puts("PASS: styled TextEdit stores four guest bytes and preserves mode/size on x64");
    return 0;
}
