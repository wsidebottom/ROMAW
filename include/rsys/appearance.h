/*+===================================================================
  File:      appearance.h

  Summary:   Declares appearance support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_APPEARANCE_H_)
#define _APPEARANCE_H_

/*
 * Copyright 2000 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: appearance.h 63 2004-12-24 18:19:43Z ctm $
 */

/*
  NOTE: this is not an attempt to recreate Apple's appearance manager.
  this is just a hack to make it so that end-users can ask for a different
  set of WDEFs, CDEFs, etc. at run time
*/

typedef enum {
    appearance_sys7,
    appearance_win3,
} appearance_t;

extern void ROMlib_set_appearance(void);
extern void ROMlib_replace_resources(INTEGER master, INTEGER source);
extern void ROMlib_install_font_resources(INTEGER source);
extern boolean_t ROMlib_parse_appearance(const char* appearance_str);
extern appearance_t ROMlib_get_appearance(void);

extern int32 ROMlib_RegisterAppearanceClient(void);
extern int32 ROMlib_UnregisterAppearanceClient(void);

#endif
