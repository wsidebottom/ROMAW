/*+===================================================================
  File:      notmac.h

  Summary:   Declares notmac support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__NOTMAC__)
/*
 * Copyright 1990 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: notmac.h 63 2004-12-24 18:19:43Z ctm $
 */

#if defined(CYGWIN32)
extern char ROMlib_start_drive;
#endif

extern char* ROMlib_appname;

#if !defined(USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES)

#include "rsys/commonevt.h"

extern char ROMlib_startdir[];
extern char ROMlib_userdir[];

extern char* ROMlib_SystemFolder;
extern char* ROMlib_PublicDirectoryMap;
extern char* ROMlib_PrivateDirectoryMap;
extern char* ROMlib_DefaultFolder;

#if !defined(__STDC__)
extern BOOLEAN ROMlib_shouldalarm();
#else  /* __STDC__ */
extern BOOLEAN ROMlib_shouldalarm(void);
#endif /* __STDC__ */

#define __NOTMAC__
/* DO NOT DELETE THIS LINE */
#if !defined(__STDC__)
extern void initX();
extern void putimageX();
extern void warppointerX();
extern void showcursorX();
extern void setcursorX();
extern void querypointerX();
extern BOOLEAN checkwindoweventX();
extern void autorepeatonX();
extern LONGINT lookupkeysymX();
extern void bellX();
extern void PutScrapX();
extern LONGINT GetScrapX();
#else  /* __STDC__ */
extern void initX(LONGINT argc, char** argv, INTEGER* rowbytep, INTEGER* bottomp, INTEGER* rightp, char** addrp);
extern void putimageX(LONGINT top, LONGINT left, LONGINT bottom, LONGINT right);
extern void warppointerX(INTEGER dh, INTEGER dv);
extern void showcursorX(LONGINT show);
extern void setcursorX(INTEGER* data, INTEGER* mask, LONGINT hotx, LONGINT hoty);
extern void querypointerX(LONGINT* xp, LONGINT* yp, LONGINT* modp);
extern BOOLEAN checkwindoweventX(char** eventp, commonevent* commonp);
extern void autorepeatonX(void);
extern LONGINT lookupkeysymX(char* p);
extern void bellX(void);
extern void PutScrapX(LONGINT type, LONGINT length, char* p, int scrap_cnt);
extern LONGINT GetScrapX(LONGINT type, char** h);
#endif /* __STDC__ */

#endif

#endif /* __NOTMAC__ */
