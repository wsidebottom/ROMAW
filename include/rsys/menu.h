/*+===================================================================
  File:      menu.h

  Summary:   Declares menu support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__MENU_H_)
#define _MENU_H_

/*
 * Copyright 1986, 1989, 1990 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: menu.h 63 2004-12-24 18:19:43Z ctm $
 */

#include "MenuMgr.h"
#include "EventMgr.h"
#include "ResourceMgr.h"
#include "rsys/mman.h"
#include "rsys/pstuff.h"

#define MI_ID_X(mi) (HxX(mi, menuID))
#define MI_WIDTH_X(mi) (HxX(mi, menuWidth))
#define MI_HEIGHT_X(mi) (HxX(mi, menuHeight))
#define MI_PROC_X(mi) (HxX(mi, menuProc))
#define MI_ENABLE_FLAGS_X(mi) (HxX(mi, enableFlags))
#define MI_DATA(mi) (HxX(mi, menuData))

#define MI_ID(mi) (CW(MI_ID_X(mi)))
#define MI_WIDTH(mi) (CW(MI_WIDTH_X(mi)))
#define MI_HEIGHT(mi) (CW(MI_HEIGHT_X(mi)))
#define MI_PROC(mi) (PPR(MI_PROC_X(mi)))
#define MI_ENABLE_FLAGS(mi) (CL(MI_ENABLE_FLAGS_X(mi)))

#define MI_TITLE(mi) (HxX(mi, menuTitle))

typedef struct PACKED {
    Byte micon;
    Byte mkeyeq;
    Byte mmarker;
    Byte mstyle;
    Byte mnextlen;
} mext, *mextp;

extern void C_mdef0(INTEGER, MenuHandle, Rect*, Point, INTEGER*);
extern int32 C_mbdf0(int16, int16, int16, int32);

#define SIZEOFMEXT 5
#define SIZEOFMINFO (sizeof(MenuInfo) - sizeof(Str255) + 1)

#if !defined(__STDC__)
extern void ROMlib_alarmoffmbar();
extern mextp ROMlib_mitemtop();
extern INTEGER ROMlib_mentosix();
extern LONGINT ROMlib_menuhelper();
#else  /* __STDC__ */
extern void ROMlib_alarmoffmbar(void);
extern mextp ROMlib_mitemtop(MenuHandle mh, INTEGER item, StringPtr* ssp);
extern INTEGER ROMlib_mentosix(INTEGER mid);
extern LONGINT ROMlib_menuhelper(MenuHandle mh, Rect* saver, LONGINT where, BOOLEAN ispopup, INTEGER ndisplayed);
#endif /* __STDC__ */

#define mbDraw 0
#define mbHit 1
#define mbCalc 2
#define mbInit 3
#define mbDispose 4
#define mbHilite 5
#define mbHeight 6
#define mbSave 7
#define mbRestore 8
#define mbRect 9
#define mbSaveAlt 10
#define mbResetAlt 11
#define mbMenuRgn 12
#define mbDrawMBARString 15

/* MenuMgrPriv.a: the menu bar contains a temporary banner. */
#define MENU_BAR_GLOBAL_INVALID (*(Byte*)SYN68K_TO_US(0xB20))
#define MENU_BAR_GLOBAL_INVALID_MASK 0x40

#define MLMAX 16

typedef struct PACKED {
    PACKED_MEMBER(MenuHandle, muhandle);
    INTEGER muleft;
} muelem;

typedef struct menu_elt {
    MenuHandle /* elt */ muhandle;
    /* for a hierarchical menu, the menu_left field is reserved */
    INTEGER /* menu_left */ muleft;
} menu_elt;

#define ML_MENU(ml, index) (((menu_elt*)&HxX(ml, data))[index])
#define ML_HMENU(ml, index)                                                                                        \
    (((menu_elt*)((char*)STARH(ml) + ML_LAST_MENU_OFFSET(ml) + sizeof(menu_list) + sizeof(INTEGER) /* lastHMenu */ \
                  + sizeof(PixMapHandle) /* menuTitleSave */))[index])

#define ML_LAST_MENU_OFFSET_X(ml) (HxX(ml, last_menu_offset))
#define ML_LAST_RIGHT_X(ml) (HxX(ml, last_right))
#define ML_RES_ID_X(ml) (HxX(ml, mb_res_id))
#define ML_LAST_HMENU_OFFSET_X(ml) (*(INTEGER*)((char*)STARH(ml) + ML_LAST_MENU_OFFSET(ml) + sizeof(menu_elt)))
#define ML_MENU_TITLE_SAVE_X(ml)                                                                       \
    HIDDEN_RAW(*(HIDDEN_PixMapHandle*)((char*)STARH(ml) + ML_LAST_MENU_OFFSET(ml) + sizeof(menu_elt) + \
                                       sizeof(INTEGER) /* lastHMenu */))

#define ML_LAST_MENU_OFFSET(ml) (CW(ML_LAST_MENU_OFFSET_X(ml)))
#define ML_LAST_RIGHT(ml) (CW(ML_LAST_RIGHT_X(ml)))
#define ML_RES_ID(ml) (CW(ML_RES_ID_X(ml)))
#define ML_LAST_HMENU_OFFSET(ml) (CW(ML_LAST_HMENU_OFFSET_X(ml)))
#define ML_MENU_TITLE_SAVE(ml) (MR(ML_MENU_TITLE_SAVE_X(ml)))

typedef struct PACKED menu_list {
    INTEGER last_menu_offset;
    INTEGER last_right;
    INTEGER mb_res_id;
    /* other stuff... */
    char data;
} menu_list;

typedef menu_list* menu_list_ptr;
MAKE_HIDDEN(menu_list_ptr);
typedef HIDDEN_menu_list_ptr* menu_list_handle;

typedef struct PACKED {
    INTEGER muoff;
    INTEGER muright;
    INTEGER mufu;
    muelem mulist[MLMAX]; /* WILL NEED Cx() */
} menulist;

typedef menulist* menulistp;
MAKE_HIDDEN(menulistp);
typedef HIDDEN_menulistp* mlhandle;

/* Menu Color Entry accessors */
#define MCENTRY_RGB1(entry) ((entry)->mctRGB1)
#define MCENTRY_RGB2(entry) ((entry)->mctRGB2)
#define MCENTRY_RGB3(entry) ((entry)->mctRGB3)
#define MCENTRY_RGB4(entry) ((entry)->mctRGB4)

#define MCENTRY_ID_X(entry) ((entry)->mctID)
#define MCENTRY_ITEM_X(entry) ((entry)->mctItem)
#define MCENTRY_RESERVED_X(entry) ((entry)->mctReserved)

#define MCENTRY_ID(entry) (CW(MCENTRY_ID_X(entry)))
#define MCENTRY_ITEM(entry) (CW(MCENTRY_ITEM_X(entry)))

#define MENULIST ((mlhandle)MR(MenuList))

#define MENULEFT 10

#define DRAWMENUBAR 0
#define CLEARMENUBAR (-1)

typedef struct PACKED {                     /* from MPW Private.a */
    INTEGER lastMBSave;                     /* offset to top most menu saved */
    PACKED_MEMBER(Handle, mbCustomStorage); /* for custom jobs (i.e. we don't use) */
    Rect mbItemRect;                        /* currently chosen menu */
    Byte mbMenuDelay;                       /* MenuDelay from param ram */
    Byte mbMenuDrag;                        /* MenuDrag from param ram */
    INTEGER mbUglyScroll;                   /* HMenu flag having to do with scrolling?? */
    INTEGER mbIconState;                    /* ??? NMgr icon state */
} mbdfheader;

typedef mbdfheader* mbdfheaderptr;
MAKE_HIDDEN(mbdfheaderptr);
typedef HIDDEN_mbdfheaderptr* mbdfheaderhand;

typedef struct PACKED {
    Rect mbRectSave;                       /* where it is on screen */
    PACKED_MEMBER(Handle, mbBitsSave);     /* where the bits are */
    INTEGER mbMenuDir;                     /* what direction the menu was placed */
    INTEGER mbMLOffset;                    /* 6 byte offset into MenuList */
    PACKED_MEMBER(MenuHandle, mbMLHandle); /* the handle from MenuList */
    INTEGER mbTopScroll;                   /* copy of TopMenuItem */
    INTEGER mbBotScroll;                   /* copy of AtMenuBottom */
    LONGINT mbReserved;                    /* i dunno */
} mbdfentry;

#define NMBDFENTRIES 5
#define MBDFSTRUCTBYTES ((NMBDFENTRIES + 1) * sizeof(mbdfentry))

#define MBRIGHTDIR 0
#define MBLEFTDIR 1

#if !defined(MBSaveLoc_H)
extern HIDDEN_Handle MBSaveLoc_H;
extern HIDDEN_Handle MBDFHndl_H;
#endif

#define MBSaveLoc (HIDDEN_RAW(MBSaveLoc_H))
#define MBDFHndl (HIDDEN_RAW(MBDFHndl_H))

#define MBSAVELOC ((mbdfheaderhand)MR(MBSaveLoc))

#define SLOP 13

typedef pascal void (*menuprocp)(INTEGER mess, MenuHandle themenu, Rect* menrect, Point hit, INTEGER* which);
typedef pascal LONGINT (*mbdfprocp)(INTEGER variant, INTEGER msg, INTEGER param1, LONGINT param2);

extern void ROMlib_menucall(INTEGER mess, MenuHandle themenu, Rect* menrect, Point hit, INTEGER* which);
extern intptr_t ROMlib_mbdfcall(INTEGER msg, INTEGER param1, intptr_t param2);

#define MENUCALL ROMlib_menucall
#define MBDFCALL ROMlib_mbdfcall

#define MBDFDECL() INTEGER mbdfstate

#define MBDFBEGIN() (mbdfstate = HGetState(MR(MBDFHndl)), HSetState(MR(MBDFHndl), mbdfstate | LOCKBIT))

#define MBDFEND() HSetState(MR(MBDFHndl), mbdfstate)

#define NOTHITINMBAR 0
#define NOTHIT (-1)

#define image_bits(bits)                                                                                 \
    (((((0x##bits##UL) >> (7 * 4)) << 7) & (1 << 7)) | ((((0x##bits##UL) >> (6 * 4)) << 6) & (1 << 6)) | \
     ((((0x##bits##UL) >> (5 * 4)) << 5) & (1 << 5)) | ((((0x##bits##UL) >> (4 * 4)) << 4) & (1 << 4)) | \
     ((((0x##bits##UL) >> (3 * 4)) << 3) & (1 << 3)) | ((((0x##bits##UL) >> (2 * 4)) << 2) & (1 << 2)) | \
     ((((0x##bits##UL) >> (1 * 4)) << 1) & (1 << 1)) | ((((0x##bits##UL) >> (0 * 4)) << 0) & (1 << 0)))

typedef enum { HILITE, RESTORE } highstate;

/* menu item icon code */

#define ICON_PAD 4 /* space on either side */

typedef struct con_info {
    /* true if this is a color icon */
    int color_icon_p;
    int suite_p;
    int borrowed_type;

    /* pointer to the icon data */
    Handle icon;

    /* for convient access */
    int width, height;
} icon_info_t;

typedef struct PACKED mct_res {
    int16 n_entries;
    MCEntry entries[1];
} mct_res_t;

typedef struct PACKED {
    INTEGER nmen;
    INTEGER mrid[1];
} mbartype;

typedef struct PACKED {
    muelem* startp;
    muelem* endp;
} startendpairs[2];

typedef struct PACKED {
    int32 lasttick;
    int16 count;

    struct PACKED tableentry {
        int16 top;
        StringPtr name;
        mextp options;
    } entry[1];
} table, *tablePtr, **tableHandle;

extern int ROMlib_sticky_menus_p;
extern void ROMlib_prepare_quit_menu_key(void);
extern int ROMlib_AppleChar;

void cleanup_icon_info(icon_info_t* info);
int get_icon_info(MenuHandle menu, StringPtr name, mextp item_info, icon_info_t* info, int need_icon_p);

void menu_bar_color(RGBColor* bar_color);
void menu_title_color(int16 id, RGBColor* title_color);
void menu_bk_color(int16 id, RGBColor* bk_color);
void menu_item_colors(int16 id, int16 item, RGBColor* bk_color, RGBColor* name_color, RGBColor* mark_color,
                      RGBColor* command_color);

void menu_delete_entries(int16 menu_id);

/* Host presentation enters the standard MDEF item painter with a literal
   shortcut string, retaining Ctrl/F-key captions from its command model. */
void ROMlib_draw_host_menu_item(MenuHandle menu, Rect* bounds, StringPtr shortcut, int icon_width, int selected,
                                int separator, int draw, int* width, int* height);

extern LONGINT ROMlib_set_menu_item_modifiers(MenuHandle menu, INTEGER item, Byte modifiers);
extern LONGINT ROMlib_get_menu_item_modifiers(MenuHandle menu, INTEGER item, Byte* modifiers);
extern LONGINT ROMlib_set_menu_item_key_glyph(MenuHandle menu, INTEGER item, INTEGER glyph);
extern LONGINT ROMlib_get_menu_item_key_glyph(MenuHandle menu, INTEGER item, INTEGER* glyph);
extern OSErr ROMlib_set_menu_item_refcon(MenuHandle menu, INTEGER item, ULONGINT refcon);
extern OSErr ROMlib_get_menu_item_refcon(MenuHandle menu, INTEGER item, ULONGINT* refcon);
extern OSErr ROMlib_set_menu_item_refcon2(MenuHandle menu, INTEGER item, ULONGINT refcon);
extern OSErr ROMlib_get_menu_item_refcon2(MenuHandle menu, INTEGER item, ULONGINT* refcon);
extern LONGINT ROMlib_get_menu_item_command(MenuHandle menu, INTEGER item, ULONGINT* command);
extern OSErr ROMlib_set_menu_item_command(MenuHandle menu, INTEGER item, ULONGINT command);
extern OSErr ROMlib_get_menu_item_encoding(MenuHandle menu, INTEGER item, ULONGINT* encoding);
extern OSErr ROMlib_set_menu_item_encoding(MenuHandle menu, INTEGER item, ULONGINT encoding);
OSErr ROMlib_get_menu_item_font(MenuHandle menu, INTEGER item, INTEGER* font);
OSErr ROMlib_set_menu_item_font(MenuHandle menu, INTEGER item, INTEGER font);
OSErr ROMlib_get_menu_item_icon(MenuHandle menu, INTEGER item, Byte* type, ULONGINT* icon);
OSErr ROMlib_set_menu_item_icon(MenuHandle menu, INTEGER item, Byte type, Handle icon);
extern MenuHandle ROMlib_menu_bar_item(unsigned index);
extern LONGINT ROMlib_menu_event(const EventRecord* event);
void ROMlib_menu_hide_bar(void);
void ROMlib_menu_show_bar(void);
void ROMlib_menu_set_bar_height(INTEGER height);
void ROMlib_menu_validate_bar(void);
void ROMlib_menu_invalidate_sizes(void);
void ROMlib_menu_restore_bar(boolean_t redraw);
int ROMlib_menu_item_count(MenuHandle menu);
void ROMlib_menu_sync_saved_insert(MenuHandle menu, INTEGER after, int inserted, int old_count, uint32_t old_flags);
void ROMlib_menu_sync_item_removal(MenuHandle menu, INTEGER item, boolean_t first_extended_enabled);
LONGINT ROMlib_get_submenu_parent_choosable(MenuHandle menu, INTEGER item, BOOLEAN* choosable);
LONGINT ROMlib_set_submenu_parent_choosable(MenuHandle menu, INTEGER item, BOOLEAN choosable);
boolean_t ROMlib_menu_item_enabled(MenuHandle menu, INTEGER item);
boolean_t ROMlib_set_menu_item_enabled(MenuHandle menu, INTEGER item, boolean_t enabled);
void ROMlib_set_menu_items_enabled(MenuHandle menu, int count, boolean_t enabled);

enum ROMlib_menu_property_change {
    ROMLIB_MENU_PROPERTY_SET,
    ROMLIB_MENU_PROPERTY_REMOVE,
    ROMLIB_MENU_PROPERTY_ATTRIBUTES
};

int ROMlib_menu_item_property(MenuHandle menu, INTEGER item, uint32_t creator, uint32_t tag, uint32_t* size,
                              uint32_t* attributes, const void** data);
int ROMlib_change_menu_item_property(MenuHandle menu, INTEGER item, uint32_t creator, uint32_t tag,
                                     enum ROMlib_menu_property_change operation, uint32_t first, uint32_t second,
                                     const void* data);

#endif /* !_MENU_H_ */
