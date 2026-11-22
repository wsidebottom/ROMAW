/*+===================================================================
  File:      sdl_focus_test.c

  Summary:   Focus/visibility recovery must repaint even without a guest drawing call.

  Classes:   None.

  Functions: CToPascalCall, C_ZeroScrap, main, sdl_hold_presentation, sendresumeevent, sendsuspendevent,
             syn68k_guest_to_host, vdriver_update_screen, we_lost_clipboard

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Focus/visibility recovery must repaint even without a guest drawing call. */
#define NDEBUG 1
#include "rsys/common.h"
#include "../../frontend-sdl/sdlevents.c"
#undef main
#undef NDEBUG
#include <assert.h>

#include "guest_test_memory.h"

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 0xe0000000, syn68k_host_alias_size = 0x10000000;
void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];
INTEGER ROMlib_mods;
int32 ROMlib_options;
int ROMlib_desk_host_active;
size_info_t size_info;
static boolean_t held;

void sdl_hold_presentation(int hold)
{
    held = hold;
}

void* syn68k_guest_to_host(uint32 address)
{
    return fixture_guest_to_host(address);
}

intptr_t CToPascalCall(void* procedure, unsigned long convention, ...)
{
    abort();
}

int vdriver_width = 800, vdriver_height = 600;
static int lost, suspended, resumed, converted, cleared, repainted;

boolean_t we_lost_clipboard(void)
{
    return lost;
}

void sendsuspendevent(void)
{
    ++suspended;
}

void sendresumeevent(boolean_t convert)
{
    ++resumed;
    converted = convert;
}

LONGINT
C_ZeroScrap(void)
{
    ++cleared;
    return noErr;
}

int vdriver_update_screen(int top, int left, int bottom, int right, boolean_t cursor)
{
    assert(!top && !left && bottom == 600 && right == 800 && !cursor);
    ++repainted;
    return 0;
}

int main(void)
{
    SDL_ActiveEvent event = {SDL_ACTIVEEVENT, 0, SDL_APPINPUTFOCUS};
    size_info.size_flags = SZacceptSuspendResumeEvents;
    held_mouse_buttons = SDL_BUTTON(SDL_BUTTON_LEFT);
    right_button_keymod = optionKey;
    keymod = shiftKey;
    sdl_handle_activation(&event);
    assert(suspended == 1 && repainted == 1 && !resumed && held);
    assert(!held_mouse_buttons && !right_button_keymod);
    assert(ROMlib_mods == (shiftKey | btnState));
    event.gain = 1;
    sdl_handle_activation(&event);
    assert(resumed == 1 && !converted && !cleared && repainted == 2 && held);
    lost = 1;
    sdl_handle_activation(&event);
    assert(resumed == 2 && converted && cleared == 1 && repainted == 3 && held);
    event.state = SDL_APPACTIVE;
    sdl_handle_activation(&event);
    assert(resumed == 2 && repainted == 4 && held);
    event.state = SDL_APPMOUSEFOCUS;
    sdl_handle_activation(&event);
    assert(resumed == 2 && repainted == 4 && held);
    event.state = SDL_APPACTIVE;
    event.gain = 0;
    sdl_handle_activation(&event);
    assert(suspended == 1 && repainted == 4 && held);
    /* Only removing a resume event and returning to the event loop releases
     presentation. Merely receiving host focus must preserve the saved frame. */
    sdl_finish_resume();
    assert(held);
    sdl_resume_event_delivered();
    sdl_finish_resume();
    assert(!held && repainted == 5);
    sdl_finish_resume();
    assert(repainted == 5);
    event.state = SDL_APPINPUTFOCUS;
    event.gain = 0;
    sdl_handle_activation(&event);
    sdl_present_for_input();
    assert(held); /* Background input cannot release the saved frame. */
    event.gain = 1;
    sdl_handle_activation(&event);
    sdl_present_for_input();
    assert(!held); /* Modal loops need not accept the queued osEvt. */
    int input_repaints = repainted;
    sdl_present_for_input();
    assert(repainted == input_repaints); /* Ordinary input needs no full repaint. */
    event.state = SDL_APPINPUTFOCUS;
    event.gain = 0;
    sdl_handle_activation(&event);
    sdl_resume_event_delivered();
    sdl_finish_resume();
    assert(held); /* Old queued resume events cannot unfreeze an inactive app. */
    event.gain = 1;
    size_info.size_flags = 0;
    sdl_handle_activation(&event);
    assert(!held);
    event.gain = 0;
    sdl_handle_activation(&event);
    assert(held);
    event.gain = 1;
    size_info.size_flags = SZacceptSuspendResumeEvents;
    ROMlib_options = ROMLIB_NOSUSPEND_BIT;
    sdl_handle_activation(&event);
    assert(!held);
    puts("SDL focus/restore repaint and clipboard resume routing passed");
    return 0;
}
