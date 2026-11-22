/*+===================================================================
  File:      help_menu_handle_test.c

  Summary:   A four-byte Help Manager output must not overwrite the saved guest frame.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* A four-byte Help Manager output must not overwrite the saved guest frame. */
#include "rsys/common.h"
#undef warning_unimplemented
#define warning_unimplemented(...)
#include "../../ui/balloon.c"
#undef main
#include <assert.h>

int main(void)
{
    struct PACKED {
        uint16 before;
        HIDDEN_MenuHandle menu;
        uint32 saved_frame;
        uint32 return_address;
    } guest_frame;

    _Static_assert(sizeof(HIDDEN_MenuHandle) == 4, "guest handles are four bytes");
    memset(&guest_frame, 0xa5, sizeof guest_frame);
    assert(C_HMGetHelpMenuHandle(&guest_frame.menu) == noErr);
    assert(PP_RAW(guest_frame.menu) == 0);
    assert(guest_frame.before == 0xa5a5);
    assert(guest_frame.saved_frame == 0xa5a5a5a5);
    assert(guest_frame.return_address == 0xa5a5a5a5);
    puts("Help menu handle: four-byte output and adjacent guest frame preserved");
    return 0;
}
