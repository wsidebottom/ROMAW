/*+===================================================================
  File:      sdl-sound.h

  Summary:   Declares sdl sound support for host-platform configuration and integration.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/config/sound/sdl; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_SDL_SOUND_H_)
#define _SDL_SOUND_H_

#define SOUND_SDL

#include "rsys/sounddriver.h"

extern boolean_t sound_sdl_init(sound_driver_t* s);
extern void ROMlib_set_sdl_audio_driver_name(const char* str);
extern int ROMlib_sdl_audio_test_mode(void);

#endif /* !_SDL_SOUND_H_ */
