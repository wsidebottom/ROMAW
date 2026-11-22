/*+===================================================================
  File:      display.h

  Summary:   Minimal classic 68K Display Manager 2 ABI. See README.md for references. These are stack-Pascal traps, not
             CFM/Carbon imports. ifndef VGA_DISPLAY_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in tools/lc475-vga-startup; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Minimal classic 68K Display Manager 2 ABI. See README.md for references.
   These are stack-Pascal traps, not CFM/Carbon imports. */
#ifndef VGA_DISPLAY_H
#define VGA_DISPLAY_H

#include <Quickdraw.h>

typedef struct {
    unsigned short mode;
    unsigned long data;
    unsigned short page;
    Ptr base;
    unsigned long reserved;
} VGASwitch;

typedef struct {
    unsigned long previous, mode, width, height;
    Fixed refresh;
    unsigned short maxDepth;
    unsigned long flags, reserved;
} VGAResolution;

typedef struct {
    unsigned long mode, reserved, format, data, flags;
} VGATiming;

typedef struct {
    long base;
    short rowBytes;
    Rect bounds;
    short version, packType;
    long packSize, hRes, vRes;
    short pixelType, pixelSize, cmpCount, cmpSize;
    long planeBytes;
} VGAParameters;

typedef struct {
    VGASwitch* switchInfo;
    VGAParameters* parameters;
    unsigned long flags, reserved1, reserved2;
} VGADepth;

typedef struct {
    unsigned long count;
    VGADepth* depths;
    unsigned long flags, reserved1, reserved2;
} VGADepths;

typedef struct {
    unsigned long flags;
    VGASwitch* switchInfo;
    VGAResolution* resolution;
    VGATiming* timing;
    VGADepths* depths;
    unsigned long version;
    StringPtr name;
    void* displayInfo;
} VGAMode;

typedef pascal void (*VGAIterator)(void*, unsigned long, VGAMode*);

pascal OSErr VGAGetDisplayID(GDHandle device, unsigned long* id, Boolean fallback) M68K_INLINE(0x303C, 0x051F, 0xABEB);
pascal OSErr VGANewList(unsigned long id, unsigned long flags, unsigned long reserved, unsigned long* count,
                        void** list) M68K_INLINE(0x303C, 0x0A36, 0xABEB);
pascal OSErr VGAGetMode(void* list, unsigned long index, unsigned long reserved, VGAIterator iterator, void* context)
    M68K_INLINE(0x303C, 0x0A37, 0xABEB);
pascal OSErr VGADisposeList(void* list) M68K_INLINE(0x303C, 0x022C, 0xABEB);
pascal OSErr VGASetMode(GDHandle device, unsigned long mode, unsigned long* depth, unsigned long reserved, Handle state)
    M68K_INLINE(0x303C, 0x0A11, 0xABEB);
pascal OSErr VGASavePreferences(unsigned long reserved1, unsigned long flags, unsigned long reserved2)
    M68K_INLINE(0x303C, 0x0632, 0xABEB);
pascal OSErr VGAReadMode(GDHandle device, VGASwitch* mode) M68K_INLINE(0x303C, 0x043E, 0xABEB);

_Static_assert(sizeof(VGASwitch) == 16, "68K switch ABI");
_Static_assert(sizeof(VGAResolution) == 30, "68K resolution ABI");
_Static_assert(sizeof(VGAParameters) == 42, "68K video parameters ABI");
_Static_assert(sizeof(VGAMode) == 32, "68K mode list ABI");

#endif
