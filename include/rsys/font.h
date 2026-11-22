/*+===================================================================
  File:      font.h

  Summary:   Declares font support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/*
 * Copyright 1990 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: font.h 63 2004-12-24 18:19:43Z ctm $
 */

typedef struct PACKED {
    INTEGER size;
    INTEGER style;
    INTEGER fontresid;
} fatabentry;

typedef struct PACKED {
    unsigned short style;
    INTEGER table[1]; /* actually more */
} widentry_t;

typedef WidthTable* WPtr;
MAKE_HIDDEN(WPtr);
typedef HIDDEN_WPtr* WHandle;

typedef FamRec* FPtr;
MAKE_HIDDEN(FPtr);
typedef HIDDEN_FPtr* FHandle;

typedef WHandle* WHandlePtr;
MAKE_HIDDEN(WHandlePtr);
typedef HIDDEN_WHandlePtr* WHandleHandle;

#define FONTRESID(font, size) (((font) << 7) | (size))

#define WIDTHPTR ((WPtr)MR(WidthPtr))
#define WIDTHTABHANDLE ((WHandle)MR(WidthTabHandle))
#define WIDTHLISTHAND ((WHandleHandle)MR(WidthListHand))

extern Fixed font_width_expand(Fixed width, Fixed extra, Fixed hOutputInverse);

extern void ROMlib_shutdown_font_manager(void);
extern void ROMlib_forget_font_resource(Handle resource);
