/*+===================================================================
  File:      finder_code.h

  Summary:   Service boundary for C translations of the four Finder code resources.

  Classes:   C has no classes.

  Functions: Declarations for finder_cdef128, finder_wdef128, finder_gest128,
             finder_hcsl0_install and finder_hcsl0_query.

  Origin:    ROMAW Finder resource translation interface.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

#ifndef ROMAW_FINDER_CODE_H
#define ROMAW_FINDER_CODE_H

#include <stdint.h>

/* Addresses and handles are guest addresses, never host pointers. Reads/writes
 * use Macintosh byte order. The caller supplies actual Memory Manager,
 * QuickDraw and Gestalt services; these translations do not own a guest VM.
 * Service errors and invalid guest accesses belong to that implementation.
 */
typedef struct {
    int16_t top, left, bottom, right;
} FinderCodeRect;

typedef struct {
    uint8_t (*read8)(void* context, uint32_t address);
    uint32_t (*read32)(void* context, uint32_t address);
    void (*write32)(void* context, uint32_t address, uint32_t value);
    void (*lock)(void* context, uint32_t handle);
    void (*unlock)(void* context, uint32_t handle);
    /* System-heap, clear allocation, matching _NewHandleSysClear (A522). */
    int16_t (*new_handle)(void* context, uint32_t size, uint32_t* handle);
    int32_t (*handle_size)(void* context, uint32_t handle);
    uint16_t (*rom85)(void* context);
    void (*pen_size)(void* context, int16_t width, int16_t height);
    void (*frame_rect)(void* context, const FinderCodeRect* rect);
    void (*pen_normal)(void* context);
    void (*erase_rect)(void* context, const FinderCodeRect* rect);
    void (*paint_rect)(void* context, const FinderCodeRect* rect);
    void (*fill_rect)(void* context, const FinderCodeRect* rect, const uint8_t pattern[8]);
    void (*get_fore_color)(void* context, uint16_t rgb[3]);
    void (*pm_fore_color)(void* context, int16_t index);
    void (*rgb_fore_color)(void* context, const uint16_t rgb[3]);
    void (*rect_region)(void* context, uint32_t region, const FinderCodeRect* rect);
    /* Optional service failure check; bounds a malformed guest table search. */
    int (*failed)(void* context);
} FinderCodeServices;

typedef struct {
    uint32_t handle;
    uint32_t entry_address;
} FinderGest128;

typedef struct {
    uint32_t response;
} FinderHcsl0;

typedef int16_t (*FinderHcslRegister)(void* context, uint32_t selector, FinderHcsl0* handler);

int32_t finder_cdef128(const FinderCodeServices* services, void* context, int16_t variant, uint32_t control,
                       int16_t message, uint32_t parameter);
int32_t finder_wdef128(int16_t variant, uint32_t window, int16_t message, uint32_t parameter);
int16_t finder_gest128(const FinderCodeServices* services, void* context, FinderGest128* state, uint32_t selector,
                       uint32_t response_address);
void finder_hcsl0_install(FinderHcsl0* state, uint32_t response, FinderHcslRegister register_handler, void* context);
int16_t finder_hcsl0_query(const FinderCodeServices* services, void* context, const FinderHcsl0* state,
                           uint32_t response_address);

#endif
