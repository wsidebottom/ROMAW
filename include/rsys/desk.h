/*+===================================================================
  File:      desk.h

  Summary:   Declares desk support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_RSYS_DESK_H_)
#define _RSYS_DESK_H_
#include "EventMgr.h"
#include "FileMgr.h"
#include "MenuMgr.h"

/*
 * Copyright 1995 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: desk.h 63 2004-12-24 18:19:43Z ctm $
 */

enum { DESK_ACC_MIN = 12, DESK_ACC_MAX = 31, ROMLIB_CHOOSER_REFNUM = -32768 };

extern int16 ROMlib_afp_classic_chooser(void);
extern void ROMlib_afp_classic_close(void);
extern void ROMlib_afp_classic_event(EventRecord* event);
extern void ROMlib_afp_classic_idle(void);
extern BOOLEAN ROMlib_afp_classic_edit(INTEGER command);
extern void ROMlib_desk_folder_names(Handle names);
extern void ROMlib_desk_menu_setup(MenuHandle menu);
extern void ROMlib_desk_menu_forget(MenuHandle menu);
extern BOOLEAN ROMlib_desk_menu_select(LONGINT selection);
extern Handle ROMlib_desk_menu_icon(MenuHandle menu, StringPtr name);
extern BOOLEAN ROMlib_open_desk_file(StringPtr name, INTEGER* result);
extern OSErr ROMlib_run_desk_file(INTEGER refnum, StringPtr title);
extern BOOLEAN ROMlib_desk_file_p(void);
extern int ROMlib_desk_resource_selected, ROMlib_desk_resource_id;
extern OSErr ROMlib_launch_desk_accessory(const FSSpec* file, StringPtr name);
extern int ROMlib_desk_host_active, ROMlib_desk_host_close;
#if defined(SDL)
extern void ROMlib_sdl_startup_begin(void);
extern void ROMlib_sdl_startup_ready(void);
#endif

#endif
