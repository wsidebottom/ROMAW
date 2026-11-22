/*+===================================================================
  File:      sdlgpu.h

  Summary:   Optional GPU presentation of the CPU-owned Macintosh framebuffer.

  Classes:   None.

  Functions: C interface declarations for GPU presentation and software overlays.

  Origin:    Executor SDL frontend.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/
#ifndef ROMAW_SDLGPU_H
#define ROMAW_SDLGPU_H

#include "SDL/SDL.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && !defined(ROMAW_NO_GPU)
int sdl_gpu_present(SDL_Surface* screen, const unsigned char* pixels, int stride, int width, int height, int bpp,
                    int scale, int left, int top, int right, int bottom);
int sdl_gpu_repaint(SDL_Surface* screen);
void sdl_gpu_reset(void);
#else
static inline int sdl_gpu_present(SDL_Surface* screen, const unsigned char* pixels, int stride, int width, int height,
                                  int bpp, int scale, int left, int top, int right, int bottom)
{
    return 0;
}

static inline int sdl_gpu_repaint(SDL_Surface* screen)
{
    return 0;
}

static inline void sdl_gpu_reset(void)
{
}
#endif

void ROMlib_sdl_software_presentation(int hold);

#ifdef __cplusplus
}
#endif
#endif
