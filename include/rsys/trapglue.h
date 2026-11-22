/*+===================================================================
  File:      trapglue.h

  Summary:   Declares trapglue support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__TRAPGLUE__)
#define __TRAPGLUE__

typedef struct {
    void* wheretogo;
    ULONGINT magic;
} ptocblock_t;

typedef struct {
    ptocblock_t ptoc;
    void* orig;
} toolstuff_t;

typedef struct {
    void* orig;
    void* func;
} osstuff_t;

#define TOOLBIT (0x0800)
#define NTOOLENTRIES (0x400)
#define NOSENTRIES (0x100)

extern void* tooltraptable[NTOOLENTRIES];
extern void* ostraptable[NOSENTRIES];

extern syn68k_addr_t PascalToCCall(syn68k_addr_t ignoreme, ptocblock_t* infop);
/* Use a full width encoding for argument lists computed at runtime. */
extern intptr_t ROMlib_CToPascalCall64(void*, uint64, ...);

extern unsigned short mostrecenttrap;

#endif
