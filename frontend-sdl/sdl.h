/*+===================================================================
  File:      sdl.h

  Summary:   define `SDL' the Simple DirectMedia Layer front-end

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/config/front-ends/sdl; original attribution and provenance
             comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* define `SDL' the Simple DirectMedia Layer front-end */
#if !defined(SDL)

#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>

/* Define the front end */
#define SDL

/* We need this for the syn68k_addr_t definition */
#include <syn68k_public.h>

extern syn68k_addr_t handle_sdl_events(syn68k_addr_t interrupt_addr, void* unused);

extern boolean_t ROMlib_fullscreen_p;
extern boolean_t ROMlib_fixed_window_p;
extern void sdl_apply_pending_resize(void);
extern void ROMlib_sdl_queue_window_position(void);
extern void ROMlib_sdl_apply_window_position(void);
extern void ROMlib_sdl_save_window_position(void);
extern void sdl_queue_window_size(int width, int height);
extern void ROMlib_sdl_select_application(const unsigned char* name, unsigned long type, unsigned long creator);

#define SDL_MAX_SCREEN_WIDTH 4096
#define SDL_MAX_SCREEN_HEIGHT 4096
extern SDL_cond* ROMlib_shouldbeawake_cond;
extern SDL_mutex* ROMlib_shouldbeawake_mutex;

#endif /* !SDL */
