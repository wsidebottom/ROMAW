/*+===================================================================
  File:      preferences_winres.h

  Summary:   Standalone resource-editor definitions for the Windows preferences layouts.

  Classes:   None.

  Functions: No functions; Windows resource style and language constants only.

  Origin:    Windows SDK resource constants used by ROMAW's preferences resources.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

#ifndef ROMAW_PREFERENCES_WINRES_H
#define ROMAW_PREFERENCES_WINRES_H

/* The standalone Visual Studio editor has no project SDK include paths.
   Builds use the SDK so additional styles saved by the designer remain available. */
#ifndef APSTUDIO_INVOKED
#include <windows.h>
#include <commctrl.h>
#else
#define IDOK 1
#define IDCANCEL 2
#define IDC_STATIC -1
#define LANG_ENGLISH 0x09
#define SUBLANG_ENGLISH_US 0x01
#define DS_SETFONT 0x0040
#define DS_MODALFRAME 0x0080
#define DS_CONTROL 0x0400
#define WS_POPUP 0x80000000
#define WS_CHILD 0x40000000
#define WS_VISIBLE 0x10000000
#define WS_DISABLED 0x08000000
#define WS_CAPTION 0x00c00000
#define WS_BORDER 0x00800000
#define WS_VSCROLL 0x00200000
#define WS_HSCROLL 0x00100000
#define WS_SYSMENU 0x00080000
#define WS_GROUP 0x00020000
#define WS_TABSTOP 0x00010000
#define WS_EX_CONTROLPARENT 0x00010000
#define ES_AUTOHSCROLL 0x0080
#define CBS_DROPDOWN 0x0002
#define CBS_DROPDOWNLIST 0x0003
#define ES_NUMBER 0x2000
#define LBS_NOTIFY 0x0001
#define LBS_NOINTEGRALHEIGHT 0x0100
#define WS_EX_CLIENTEDGE 0x00000200
#define TBS_AUTOTICKS 0x0001

#endif /* APSTUDIO_INVOKED */

#endif
