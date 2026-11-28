/*+===================================================================
  File:      menu.c

  Summary:   Implements menu support for Macintosh Menu Manager operations.

  Classes:   None.

  Functions: ROMlib_change_menu_item_property, ROMlib_get_menu_item_key_glyph, ROMlib_get_menu_item_modifiers,
             ROMlib_get_menu_item_command, ROMlib_get_menu_item_encoding, ROMlib_get_menu_item_refcon,
             ROMlib_get_menu_item_refcon2,
             ROMlib_get_menu_item_font, ROMlib_get_menu_item_icon, ROMlib_menu_bar_item, ROMlib_menu_hide_bar,
             ROMlib_get_submenu_parent_choosable, ROMlib_set_submenu_parent_choosable,
             ROMlib_set_menu_item_encoding, ROMlib_set_menu_item_font, ROMlib_set_menu_item_icon,
             ROMlib_menu_item_count,
             ROMlib_menu_item_enabled, ROMlib_set_menu_item_command,
             ROMlib_menu_item_property, ROMlib_menu_restore_bar, ROMlib_menu_show_bar, ROMlib_menu_set_bar_height,
             ROMlib_menu_validate_bar, ROMlib_menu_invalidate_sizes,
             ROMlib_menu_sync_item_removal, ROMlib_menu_sync_saved_insert,
             ROMlib_mbdfcall, ROMlib_mentosix, ROMlib_menu_event, ROMlib_menucall, ROMlib_menuhelper, ROMlib_mitemtop,
             ROMlib_prepare_quit_menu_key, ROMlib_set_menu_item_key_glyph, ROMlib_set_menu_item_modifiers,
             ROMlib_set_menu_item_enabled, ROMlib_set_menu_item_refcon, ROMlib_set_menu_item_refcon2,
             ROMlib_set_menu_items_enabled,
             ROMlib_shouldalarm

  Origin:    Existing repository source in executor/src/menus; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986-2000 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in MenuMgr.h (DO NOT DELETE THIS LINE) */

/*
 * TODO: use update regions if we don't have room to save and restore bitmaps
 */

#include "rsys/common.h"
#include "rsys/desk.h"
#include "rsys/options.h"
#include "rsys/prefpanel.h"
#include "QuickDraw.h"
#include "MenuMgr.h"
#include "WindowMgr.h"
#include "ResourceMgr.h"
#include "MemoryMgr.h"
#include "EventMgr.h"
#include "HelpMgr.h"
#include "FontMgr.h"
#include "OSUtil.h"
#include "ToolboxUtil.h"
#include "ToolboxEvent.h"
#include "OSEvent.h"
#include "DeskMgr.h"
#include "SysErr.h"

#include "rsys/menu.h"
#include "rsys/toolevent.h"
#include "rsys/cquick.h"
#include "rsys/quick.h"
#include "rsys/glue.h"
#include "rsys/mman.h"
#include "rsys/wind.h"
#include "rsys/layers.h"
#include "rsys/hook.h"
#include "rsys/resource.h"
#include "rsys/aboutbox.h"
#include "rsys/system_error.h"
#include "rsys/vdriver.h"
#include "rsys/dirtyrect.h"
#include "rsys/notmac.h"
#include "rsys/version.h"
#include "rsys/romaw.h"
#include "rsys/segment.h"

PUBLIC int ROMlib_sticky_menus_p = 0;
static boolean_t quit_menu_key_pending;
PRIVATE void change_menu_shortcuts(MenuHandle menu, INTEGER item, int shift);
static MenuHandle finder_file_menu;
static INTEGER finder_quit_item;
static RgnHandle hidden_menu_region;
static INTEGER hidden_menu_height;

static void menu_bar_strip(RgnHandle strip, INTEGER top, INTEGER bottom)
{
    const Rect bounds = GD_BOUNDS(MR(MainDevice));
    const int origin = CW(bounds.top), limit = CW(bounds.bottom);
    SetRectRgn(strip, CW(bounds.left), MIN(origin + top, limit), CW(bounds.right), MIN(origin + bottom, limit));
    SectRgn(strip, PORT_VIS_REGION(MR(wmgr_port)), strip);
}

PUBLIC void ROMlib_menu_restore_bar(boolean_t redraw)
{
    RgnHandle strip = hidden_menu_region;
    if (!strip)
        return;
    MBarHeight = CW(hidden_menu_height);
    hidden_menu_region = NULL;
    hidden_menu_height = 0;
    /* A patched draw can rebuild the desktop region while painting the bar. */
    if (redraw && CW(MBarHeight) > 0)
        DrawMenuBar();
    if (GrayRgn && wmgr_port) {
        DiffRgn(MR(GrayRgn), strip, MR(GrayRgn));
        CopyRgn(MR(GrayRgn), PORT_CLIP_REGION(MR(wmgr_port)));
        C_CalcVisBehind(ROMlib_window_first_all(), strip);
    }
    DisposeRgn(strip);
}

PUBLIC void ROMlib_menu_hide_bar(void)
{
    INTEGER height = CW(MBarHeight);
    if (hidden_menu_region || height <= 0 || !GrayRgn || !wmgr_port || !MainDevice)
        return;
    RgnHandle strip = NewRgn();
    if (!strip)
        return;
    menu_bar_strip(strip, 0, height);
    hidden_menu_height = height;
    hidden_menu_region = strip;
    MBarHeight = 0;
    UnionRgn(MR(GrayRgn), strip, MR(GrayRgn));
    CopyRgn(MR(GrayRgn), PORT_CLIP_REGION(MR(wmgr_port)));
    HIDDEN_GrafPtr port;
    GetPort(&port);
    C_CalcVisBehind(ROMlib_window_first_all(), strip);
    C_PaintBehind(ROMlib_window_first_all(), strip);
    SetPort(HIDDEN_NATIVE(port));
}

PUBLIC void ROMlib_menu_show_bar(void)
{
    ROMlib_menu_restore_bar(TRUE);
}

PUBLIC void ROMlib_menu_set_bar_height(INTEGER height)
{
    if (height < 0)
        return;
    if (hidden_menu_region) {
        hidden_menu_height = height;
        menu_bar_strip(hidden_menu_region, 0, height);
        return;
    }

    const INTEGER previous = CW(MBarHeight);
    if (height == previous)
        return;
    if (!GrayRgn || !wmgr_port || !MainDevice) {
        MBarHeight = CW(height);
        return;
    }
    RgnHandle strip = NewRgn();
    if (!strip)
        return;

    menu_bar_strip(strip, MIN(height, previous), MAX(height, previous));
    MBarHeight = CW(height);
    if (height > previous)
        DiffRgn(MR(GrayRgn), strip, MR(GrayRgn));
    else
        UnionRgn(MR(GrayRgn), strip, MR(GrayRgn));

    HIDDEN_GrafPtr port;
    GetPort(&port);
    if (height)
        DrawMenuBar();
    CopyRgn(MR(GrayRgn), PORT_CLIP_REGION(MR(wmgr_port)));
    C_CalcVisBehind(ROMlib_window_first_all(), strip);
    if (height < previous)
        C_PaintBehind(ROMlib_window_first_all(), strip);
    SetPort(HIDDEN_NATIVE(port));
    DisposeRgn(strip);
}

/* Finder is ROMAW's shell, so its File/Quit command exits the host through
   the classic ExitToShell cleanup (resource files, open files and disk cache). */
static void finder_quit_menu_setup(void)
{
    if (ROMlib_creator != TICK("MACS"))
        return;
    MenuHandle menu = NULL;
    if (!MenuList)
        return;
    unsigned count = Hx(MENULIST, muoff) / sizeof(muelem);
    for (unsigned index = 0; index < count; ++index) {
        MenuHandle candidate = PPR(HxX(MENULIST, mulist)[index].muhandle);
        unsigned char* title = HxX(candidate, menuData);
        if (title[0] == 4 && !memcmp(title + 1, "File", 4)) {
            menu = candidate;
            break;
        }
    }
    if (!menu)
        return;
    if (menu != finder_file_menu || finder_quit_item > CountMItems(menu)) {
        Str255 name;
        INTEGER count = CountMItems(menu);
        for (INTEGER item = 1; item <= count; ++item) {
            GetItem(menu, item, name);
            if (name[0] == 4 && !memcmp(name + 1, "Quit", 4))
                return;
        }
        AppendMenu(menu, (StringPtr) "\010-;Quit/Q");
        finder_file_menu = menu;
        finder_quit_item = CountMItems(menu);
    }
    EnableItem(menu, finder_quit_item);
}

static BOOLEAN finder_quit_selection(LONGINT selection)
{
    if (ROMlib_creator != TICK("MACS") || !finder_file_menu || (selection >> 16) != Hx(finder_file_menu, menuID) ||
        (selection & 0xffff) != finder_quit_item)
        return FALSE;
    HiliteMenu(0);
    ROMlib_exit = 1;
    if (ROMlib_post_quit_event())
        return TRUE;
    C_ExitToShell();
    return TRUE;
}

void ROMlib_prepare_quit_menu_key(void)
{
    quit_menu_key_pending = TRUE;
}

A1(PRIVATE, void, dirtymenusize, MenuHandle, mh)
{
    if (mh) {
        HxX(mh, menuWidth) = -1;
        HxX(mh, menuHeight) = -1;
    }
}

P0(PUBLIC pascal trap, void, InvalMenuBar)
{
    DrawMenuBar();
}

P0(PUBLIC pascal trap, void, DrawMenuBar)
{
    finder_quit_menu_setup();
    if (MBDFHndl) {
        MENU_BAR_GLOBAL_INVALID &= ~MENU_BAR_GLOBAL_INVALID_MASK;
        TheMenu = 0;
        MBDFCALL(mbDraw, 0, 0L);
    }
}

PUBLIC void ROMlib_menu_validate_bar(void)
{
    enum { validating = 0x08 };

    Byte* const menu_list_flags = SYN68K_TO_US(0xb21);
    if (MENU_BAR_GLOBAL_INVALID & validating)
        return;

    MENU_BAR_GLOBAL_INVALID |= validating;
    const boolean_t redraw = ((*menu_list_flags | MENU_BAR_GLOBAL_INVALID) & MENU_BAR_GLOBAL_INVALID_MASK) != 0;
    *menu_list_flags &= ~MENU_BAR_GLOBAL_INVALID_MASK;
    if (redraw)
        DrawMenuBar();
    MENU_BAR_GLOBAL_INVALID &= ~validating;
}

#define HIEROFFX (*(INTEGER*)((char*)STARH(MENULIST) + Hx(MENULIST, muoff) + sizeof(muelem) + 4))

#define HIEROFF CW(HIEROFFX)

#define MINMENULISTSIZE ((Size)sizeof(INTEGER) * 4 + sizeof(HIDDEN_Handle))

static void normalize_empty_menu_list(void)
{
    /* ResEdit clears its private menu list between DITL editor operations.
     The first lookup/deletion must see an empty hierarchy, just as insertion
     does, rather than walking backwards past an uninitialized end offset. */
    if (MenuList && GetHandleSize(MR(MenuList)) >= MINMENULISTSIZE && Hx(MENULIST, muoff) == 0 && HIEROFF == 0) {
        HxX(MENULIST, muright) = CWC(MENULEFT);
        HIEROFFX = CWC(sizeof(muelem));
    }
}

P0(PUBLIC pascal trap, void, ClearMenuBar)
{
    if (MenuList)
        SetHandleSize(MR(MenuList), MINMENULISTSIZE);
    else
        HIDDEN_RAW_SET(MenuList_H, RM(NewHandle(MINMENULISTSIZE)));
    HxX(MENULIST, muoff) = 0;               /* int 1 */
    HxX(MENULIST, muright) = CWC(MENULEFT); /* int 2 */

    (&STARH(MENULIST)->mufu)[3] = CWC(sizeof(muelem));                /* lastHMenu: int 6 */
    HIDDEN_RAW_SET(*(HIDDEN_Handle*)&(&STARH(MENULIST)->mufu)[1], 0); /* menuTitleSave: int 4,5 */
}

#define BLACK_RGB {CWC(0), CWC(0), CWC(0)}
#define WHITE_RGB {CWC(0xFFFF), CWC(0xFFFF), CWC(0xFFFF)}
#define RESERVED_RGB /* arbitrary */ WHITE_RGB
PRIVATE MCEntry default_menu_ctab[] = {
    {
        CWC(0),
        CWC(0),
        BLACK_RGB,
        WHITE_RGB,
        BLACK_RGB,
        WHITE_RGB,
        /* reserved */ CWC(0),
    },
    /* end marker */
    {
        CWC(-99),
        /* reserved */ CWC(0),
        RESERVED_RGB,
        RESERVED_RGB,
        RESERVED_RGB,
        RESERVED_RGB,
        /* reserved */ CWC(0),
    },
};
#undef BLACK_RGB
#undef WHITE_RGB
#undef RESERVED_RGB

PRIVATE void append_end_marker_if_necessary(MCTableHandle h)
{
    Size size;
    MCEntryPtr entries;
    int i, nelem;

    size = GetHandleSize((Handle)h);
    entries = STARH(h);
    nelem = size / sizeof *entries;
    for (i = 0; i < nelem && MCENTRY_ID_X(&entries[i]) != CWC(-99);)
        ++i;
    if (i == nelem) {
        size += sizeof *entries;
        SetHandleSize((Handle)h, size);
        if (MemError() == noErr) {
            entries = STARH(h);
            entries[nelem] = default_menu_ctab[NELEM(default_menu_ctab) - 1];
        }
    }
}

P0(PUBLIC pascal trap, void, InitMenus)
{
    quit_menu_key_pending = FALSE;
    finder_file_menu = NULL;
    finder_quit_item = 0;
    ROMlib_desk_menu_forget(NULL);
    change_menu_shortcuts(NULL, 0, 0);
    Handle default_mcinfo;

    fprintf(stderr, "trace: InitMenus entered\n");
    fflush(stderr);
    HIDDEN_RAW_SET(MenuHook_H, HIDDEN_RAW_SET(MBarHook_H, 0));
    fprintf(stderr, "trace: InitMenus ClearMenuBar begin\n");
    fflush(stderr);
    ClearMenuBar();
    fprintf(stderr, "trace: InitMenus ClearMenuBar complete\n");
    fflush(stderr);
    default_mcinfo = ROMlib_getrestid(TICK("mctb"), 0);
    fprintf(stderr, "trace: InitMenus ROMlib_getrestid complete handle=%p\n", (void*)default_mcinfo);
    fflush(stderr);
    if (!default_mcinfo) {
        /* try to load 'mctb' resource 0; otherwise use default
 built into `ROM' */

        HIDDEN_RAW_SET(MenuCInfo_H, (MCTableHandle)RM(NewHandle(sizeof default_menu_ctab)));
        BlockMove((Ptr)default_menu_ctab, (Ptr)STARH(MR(MenuCInfo)), sizeof default_menu_ctab);
        fprintf(stderr, "trace: InitMenus default mctb created\n");
        fflush(stderr);
    }
    else {
        int n_entries;

        n_entries = CW(*(uint16*)STARH(default_mcinfo));
        HIDDEN_RAW_SET(MenuCInfo_H, (MCTableHandle)RM(NewHandle(n_entries * sizeof(MCEntry))));
        BlockMove((Ptr)(&((uint16*)STARH(default_mcinfo))[1]), (Ptr)STARH(MR(MenuCInfo)), n_entries * sizeof(MCEntry));
        append_end_marker_if_necessary(MR(MenuCInfo));
        fprintf(stderr, "trace: InitMenus resource mctb copied entries=%d\n", n_entries);
        fflush(stderr);
    }

    fprintf(stderr, "trace: InitMenus InitProcMenu begin\n");
    fflush(stderr);
    InitProcMenu(0); /* sets mbResID */ /* int 3 */
    fprintf(stderr, "trace: InitMenus InitProcMenu complete MBDFHndl=%p\n", (void*)MBDFHndl);
    fflush(stderr);

    fprintf(stderr, "trace: InitMenus MBDFCALL mbHeight begin\n");
    fflush(stderr);
    MBDFCALL(mbHeight, 0, 0L);
    fprintf(stderr, "trace: InitMenus MBDFCALL mbHeight complete MBarHeight=%d\n", CW(MBarHeight));
    fflush(stderr);

    if (WWExist == EXIST_YES
#if defined(CYGWIN32) || defined(__MINGW32__)
        && FALSE
#endif
    ) {
        fprintf(stderr, "trace: InitMenus DrawMenuBar begin\n");
        fflush(stderr);
        DrawMenuBar();
        fprintf(stderr, "trace: InitMenus DrawMenuBar complete\n");
        fflush(stderr);
    }
    else {
        fprintf(stderr, "trace: InitMenus DrawMenuBar skipped, window manager not ready\n");
        fflush(stderr);
    }
}

P2(PUBLIC pascal trap, MenuHandle, NewMenu, INTEGER, mid, StringPtr, str)
{
    MenuHandle retval;
    Handle temph;

    if (!str)
        str = (StringPtr) "";
    retval = (MenuHandle)NewHandle((Size)SIZEOFMINFO + U(str[0]) + 1);
    HxX(retval, menuID) = CW(mid);
    HxX(retval, menuWidth) = HxX(retval, menuHeight) = 0;
    /* menuHeight calculated elsewhere */
    SetResLoad(TRUE);
    temph = RM(GetResource(TICK("MDEF"), textMenuProc));
    FIELD_ASSIGN_RAW(PP_RAW(HxX(retval, menuProc)), temph);
    HxX(retval, enableFlags) = -1;
    str255assign(HxX(retval, menuData), str);
    *((char*)STARH(retval) + SIZEOFMINFO + U(str[0])) = 0;
    return (retval);
}

P1(PUBLIC pascal trap, void, CalcMenuSize, MenuHandle, mh)
{
    Point dummy_pt;
    Rect rect;
    int16 i;

    if (mh) {
        i = -1;
        THEPORT_SAVE_EXCURSION(MR(wmgr_port), {
            PORT_TX_FACE_X(MR(wmgr_port)) = (Style)CB(0);
            PORT_TX_FONT_X(MR(wmgr_port)) = CWC(0);

            /* initialize the unused point to a known value */
            memset(&dummy_pt, 0xFF, sizeof dummy_pt);
            memset(&rect, 0, sizeof rect);

            MENUCALL(mSizeMsg, mh, &rect, dummy_pt, &i);
        });
    }
}

/*
 * NOTE: the nastiness below is because it is very hard to tell
 *	 whether or not someone has already converted a menuProc
 *	 from a resource id to a handle.
 */

P1(PUBLIC pascal trap, MenuHandle, GetMenu, int16, rid)
{
    MenuHandle retval;
    Handle mct_res_h;

    SetResLoad(TRUE);
    retval = (MenuHandle)GetResource(TICK("MENU"), rid);

    mct_res_h = ROMlib_getrestid(TICK("mctb"), rid);
    if (mct_res_h)
        LOCK_HANDLE_EXCURSION_1(mct_res_h, {
            mct_res_t* mct_res;
            /*
                                 MCEntry entry;
                                 int i;

                                 entry.mctReserved = 0;
                                 mct_res = (mct_res_t *) STARH (mct_res_h);
                                 for (i = 0; i < CW (mct_res->n_entries); i ++)
                                   {
                                     memcpy (&entry, &mct_res->entries[i], sizeof mct_res->entries[i]);
                                     SetMCEntries (1, &entry);
                                   }
                              */
            mct_res = (mct_res_t*)STARH(mct_res_h);
            SetMCEntries(CW(mct_res->n_entries), &mct_res->entries[0]);
        });

    if (retval) {
        Handle current_proc;

        current_proc = HxP(retval, menuProc);
        if (!HandleZone(current_proc) || !GetHandleSize(current_proc)) {
            Handle temph;

            MemErr = CWC(noErr);
            temph = GetResource(TICK("MDEF"), CW(*(int16*)&PP_RAW(HxX(retval, menuProc))));
            if (SIZEOFMINFO != 15)
                Munger((Handle)retval, (int32)6, (Ptr)0, (int32)0, (Ptr) "x", (int32)2);
            FIELD_ASSIGN_RAW(MI_PROC_X(retval), RM(temph));
            CalcMenuSize(retval);
        }
    }

    return retval;
}

typedef struct menu_property {
    uint32_t creator, tag, size, attributes;
    struct menu_property* next;
    unsigned char bytes[];
} menu_property;

typedef struct menu_shortcut {
    MenuHandle menu;
    INTEGER item, glyph, font;
    Byte modifiers, icon_type;
    ULONGINT icon;
    ULONGINT refcon, refcon2, command, encoding;
    boolean_t disabled;
    menu_property* properties;
    struct menu_shortcut* next;
} menu_shortcut;

PRIVATE menu_shortcut* menu_shortcuts;

PRIVATE menu_shortcut* find_menu_shortcut(MenuHandle menu, INTEGER item, boolean_t create)
{
    menu_shortcut* entry;
    for (entry = menu_shortcuts; entry; entry = entry->next)
        if (entry->menu == menu && entry->item == item)
            return entry;
    if (!create)
        return NULL;
    entry = calloc(1, sizeof *entry);
    if (entry) {
        entry->menu = menu;
        entry->item = item;
        entry->next = menu_shortcuts;
        menu_shortcuts = entry;
    }
    return entry;
}

PRIVATE void change_menu_shortcuts(MenuHandle menu, INTEGER item, int shift)
{
    menu_shortcut** link = &menu_shortcuts;
    while (*link) {
        menu_shortcut* entry = *link;
        if ((!menu || entry->menu == menu) && ((!item && !shift) || (shift < 0 && entry->item == item))) {
            *link = entry->next;
            while (entry->properties) {
                menu_property* property = entry->properties;
                entry->properties = property->next;
                free(property);
            }
            free(entry);
            continue;
        }
        if (entry->menu == menu && entry->item > item)
            entry->item += shift;
        link = &entry->next;
    }
}

static menu_shortcut* detach_menu_tail(MenuHandle menu, INTEGER after)
{
    menu_shortcut* tail = NULL;
    menu_shortcut** link = &menu_shortcuts;
    while (*link) {
        menu_shortcut* entry = *link;
        if (entry->menu == menu && entry->item > after) {
            *link = entry->next;
            entry->next = tail;
            tail = entry;
        }
        else
            link = &entry->next;
    }
    return tail;
}

PUBLIC void ROMlib_menu_sync_saved_insert(MenuHandle menu, INTEGER after, int inserted, int old_count,
                                         uint32_t old_flags)
{
    change_menu_shortcuts(menu, after, inserted);
    /* The saved manager already maintains the classic enableFlags word. */
    for (int index = MAX(after + 1, 32 - inserted); index < 32 && index <= old_count; ++index)
        ROMlib_set_menu_item_enabled(menu, index + inserted, (old_flags & (UINT32_C(1) << index)) != 0);
}

PUBLIC void ROMlib_menu_sync_item_removal(MenuHandle menu, INTEGER item, boolean_t first_extended_enabled)
{
    change_menu_shortcuts(menu, item, -1);
    if (item < 32)
        ROMlib_set_menu_item_enabled(menu, 31, first_extended_enabled);
}

PUBLIC int ROMlib_menu_item_count(MenuHandle menu)
{
    if (!menu || !STARH(menu))
        return -1;
    const Size extent = GetHandleSize((Handle)menu);
    if (extent < SIZEOFMINFO + 1)
        return -1;
    const unsigned char* bytes = (const unsigned char*)STARH(menu);
    size_t offset = SIZEOFMINFO + bytes[offsetof(MenuInfo, menuData)];
    int count = 0;
    while (offset < (size_t)extent) {
        const unsigned length = bytes[offset];
        if (!length)
            return count;
        if (++count > INT16_MAX)
            break;
        offset += length + SIZEOFMEXT;
    }
    return -1;
}

PUBLIC boolean_t ROMlib_menu_item_enabled(MenuHandle menu, INTEGER item)
{
    if (item < 0)
        return FALSE;
    if (item < 32)
        return (MI_ENABLE_FLAGS(menu) & (UINT32_C(1) << item)) != 0;
    const menu_shortcut* entry = find_menu_shortcut(menu, item, FALSE);
    return !entry || !entry->disabled;
}

PUBLIC boolean_t ROMlib_set_menu_item_enabled(MenuHandle menu, INTEGER item, boolean_t enabled)
{
    if (item < 0)
        return FALSE;
    if (item < 32) {
        const uint32_t mask = UINT32_C(1) << item;
        MI_ENABLE_FLAGS_X(menu) = CL(enabled ? MI_ENABLE_FLAGS(menu) | mask : MI_ENABLE_FLAGS(menu) & ~mask);
        return TRUE;
    }
    menu_shortcut* entry = find_menu_shortcut(menu, item, !enabled);
    if (entry)
        entry->disabled = !enabled;
    return entry || enabled;
}

PUBLIC void ROMlib_set_menu_items_enabled(MenuHandle menu, int count, boolean_t enabled)
{
    /* Reserve any new entries before changing the classic mask or existing items. */
    menu_shortcut* reserved = NULL;
    if (!enabled) {
        for (int item = 32; item <= count; ++item) {
            if (find_menu_shortcut(menu, item, FALSE))
                continue;
            menu_shortcut* entry = calloc(1, sizeof *entry);
            if (!entry) {
                while (reserved) {
                    entry = reserved;
                    reserved = entry->next;
                    free(entry);
                }
                return;
            }
            entry->menu = menu;
            entry->item = item;
            entry->next = reserved;
            reserved = entry;
        }
    }
    while (reserved) {
        menu_shortcut* entry = reserved;
        reserved = entry->next;
        entry->next = menu_shortcuts;
        menu_shortcuts = entry;
    }
    const uint32_t mask = count >= 31 ? UINT32_MAX - 1 : (UINT32_C(1) << (count + 1)) - 2;
    MI_ENABLE_FLAGS_X(menu) = CL(enabled ? MI_ENABLE_FLAGS(menu) | mask : MI_ENABLE_FLAGS(menu) & ~mask);
    for (menu_shortcut* entry = menu_shortcuts; entry; entry = entry->next)
        if (entry->menu == menu && entry->item >= 32 && entry->item <= count)
            entry->disabled = !enabled;
}

static menu_property** find_menu_property(MenuHandle menu, INTEGER item, uint32_t creator, uint32_t tag,
                                          boolean_t create)
{
    menu_shortcut* entry = find_menu_shortcut(menu, item, create);
    if (!entry)
        return NULL;
    menu_property** link = &entry->properties;
    while (*link && ((*link)->creator != creator || (*link)->tag != tag))
        link = &(*link)->next;
    return link;
}

PUBLIC int ROMlib_menu_item_property(MenuHandle menu, INTEGER item, uint32_t creator, uint32_t tag, uint32_t* size,
                                     uint32_t* attributes, const void** data)
{
    menu_property** link = find_menu_property(menu, item, creator, tag, FALSE);
    const menu_property* property = link ? *link : NULL;
    if (!property)
        return -5604; /* menuPropertyNotFoundErr */
    *size = property->size;
    *attributes = property->attributes;
    *data = property->bytes;
    return noErr;
}

PUBLIC int ROMlib_change_menu_item_property(MenuHandle menu, INTEGER item, uint32_t creator, uint32_t tag,
                                            enum ROMlib_menu_property_change operation, uint32_t first, uint32_t second,
                                            const void* data)
{
    menu_property** link = find_menu_property(menu, item, creator, tag, operation == ROMLIB_MENU_PROPERTY_SET);
    if (!link)
        return operation == ROMLIB_MENU_PROPERTY_SET ? memFullErr : -5604;
    menu_property* previous = *link;
    if (operation == ROMLIB_MENU_PROPERTY_SET) {
        if (first > INT32_MAX - sizeof(menu_property))
            return memFullErr;
        menu_property* replacement = malloc(sizeof *replacement + first);
        if (!replacement)
            return memFullErr;
        *replacement =
            (menu_property){creator, tag, first, previous ? previous->attributes : 0, previous ? previous->next : NULL};
        if (first)
            memcpy(replacement->bytes, data, first);
        *link = replacement;
        free(previous);
    }
    else if (!previous)
        return -5604;
    else if (operation == ROMLIB_MENU_PROPERTY_REMOVE) {
        *link = previous->next;
        free(previous);
    }
    else
        previous->attributes = (previous->attributes | first) & ~second;
    return noErr;
}

/* The same native override record is used by the Carbon menu adapters. */
PUBLIC LONGINT ROMlib_get_submenu_parent_choosable(MenuHandle menu, INTEGER item, BOOLEAN* choosable)
{
    if (!choosable)
        return paramErr;
    *choosable = FALSE;
    if (!menu || item < 1 || item > ROMlib_menu_item_count(menu))
        return paramErr;
    uint32 size, attributes;
    const void* data;
    const int status = ROMlib_menu_item_property(menu, item, TICK("macs"), TICK("iatm"), &size, &attributes, &data);
    if (status == -5604) /* menuPropertyNotFoundErr: the default is not choosable. */
        return noErr;
    if (status || size != 8)
        return status ? status : paramErr;
    uint32 values[2];
    memcpy(values, data, sizeof values);
    *choosable = (CL(values[0]) & CL(values[1]) & 4) != 0;
    return noErr;
}

PUBLIC LONGINT ROMlib_set_submenu_parent_choosable(MenuHandle menu, INTEGER item, BOOLEAN choosable)
{
    if (!menu || item < 1 || item > ROMlib_menu_item_count(menu))
        return paramErr;
    uint32 values[2] = {0, 0}, size, attributes;
    const void* data;
    const int status = ROMlib_menu_item_property(menu, item, TICK("macs"), TICK("iatm"), &size, &attributes, &data);
    if (status != -5604) {
        if (status || size != sizeof values)
            return status ? status : paramErr;
        memcpy(values, data, sizeof values);
    }
    values[0] = CL(CL(values[0]) | 4);
    values[1] = CL(choosable ? CL(values[1]) | 4 : CL(values[1]) & ~4U);
    return ROMlib_change_menu_item_property(menu, item, TICK("macs"), TICK("iatm"), ROMLIB_MENU_PROPERTY_SET,
                                            sizeof values, 0, values);
}

PUBLIC LONGINT ROMlib_set_menu_item_modifiers(MenuHandle menu, INTEGER item, Byte modifiers)
{
    if (!ROMlib_mitemtop(menu, item, NULL) || (modifiers & ~15))
        return paramErr;
    menu_shortcut* entry = find_menu_shortcut(menu, item, TRUE);
    if (!entry)
        return memFullErr;
    entry->modifiers = modifiers;
    return noErr;
}

PUBLIC LONGINT ROMlib_get_menu_item_modifiers(MenuHandle menu, INTEGER item, Byte* modifiers)
{
    if (!modifiers || !ROMlib_mitemtop(menu, item, NULL))
        return paramErr;
    menu_shortcut* entry = find_menu_shortcut(menu, item, FALSE);
    *modifiers = entry ? entry->modifiers : 0;
    return noErr;
}

PUBLIC LONGINT ROMlib_set_menu_item_key_glyph(MenuHandle menu, INTEGER item, INTEGER glyph)
{
    if (!ROMlib_mitemtop(menu, item, NULL) || glyph < 0 || glyph > 255)
        return paramErr;
    menu_shortcut* entry = find_menu_shortcut(menu, item, TRUE);
    if (!entry)
        return memFullErr;
    entry->glyph = glyph;
    return noErr;
}

PUBLIC LONGINT ROMlib_get_menu_item_key_glyph(MenuHandle menu, INTEGER item, INTEGER* glyph)
{
    if (!glyph || !ROMlib_mitemtop(menu, item, NULL))
        return paramErr;
    menu_shortcut* entry = find_menu_shortcut(menu, item, FALSE);
    *glyph = CW(entry ? entry->glyph : 0);
    return noErr;
}

static OSErr set_menu_item_refcon(MenuHandle menu, INTEGER item, ULONGINT refcon, boolean_t second)
{
    if (!ROMlib_mitemtop(menu, item, NULL))
        return paramErr;
    menu_shortcut* entry = find_menu_shortcut(menu, item, TRUE);
    if (!entry)
        return memFullErr;
    if (second)
        entry->refcon2 = refcon;
    else
        entry->refcon = refcon;
    return noErr;
}

static OSErr get_menu_item_refcon(MenuHandle menu, INTEGER item, ULONGINT* refcon, boolean_t second)
{
    if (!refcon || !ROMlib_mitemtop(menu, item, NULL))
        return paramErr;
    menu_shortcut* entry = find_menu_shortcut(menu, item, FALSE);
    const ULONGINT value = CL(entry ? (second ? entry->refcon2 : entry->refcon) : 0);
    memcpy(refcon, &value, sizeof value);
    return noErr;
}

PUBLIC OSErr ROMlib_set_menu_item_refcon(MenuHandle menu, INTEGER item, ULONGINT refcon)
{
    return set_menu_item_refcon(menu, item, refcon, FALSE);
}

PUBLIC OSErr ROMlib_get_menu_item_refcon(MenuHandle menu, INTEGER item, ULONGINT* refcon)
{
    return get_menu_item_refcon(menu, item, refcon, FALSE);
}

PUBLIC OSErr ROMlib_set_menu_item_refcon2(MenuHandle menu, INTEGER item, ULONGINT refcon)
{
    return set_menu_item_refcon(menu, item, refcon, TRUE);
}

PUBLIC OSErr ROMlib_get_menu_item_refcon2(MenuHandle menu, INTEGER item, ULONGINT* refcon)
{
    return get_menu_item_refcon(menu, item, refcon, TRUE);
}

PUBLIC LONGINT ROMlib_get_menu_item_command(MenuHandle menu, INTEGER item, ULONGINT* command)
{
    if (!command || item < 1 || item > ROMlib_menu_item_count(menu))
        return paramErr;
    const menu_shortcut* entry = find_menu_shortcut(menu, item, FALSE);
    const ULONGINT stored = CL(entry ? entry->command : 0);
    memcpy(command, &stored, sizeof stored);
    return noErr;
}

PUBLIC MenuHandle ROMlib_menu_bar_item(unsigned index)
{
    const mlhandle list = MENULIST;
    if (!list || !STARH(list))
        return NULL;
    const Size extent = GetHandleSize((Handle)list);
    const Size header = offsetof(menulist, mulist);
    if (extent < header)
        return NULL;
    const Size bytes = Hx(list, muoff);
    if (bytes < 0 || bytes > extent - header || index >= (unsigned)bytes / sizeof(muelem))
        return NULL;
    return PPR(HxX(list, mulist)[index].muhandle);
}

PUBLIC OSErr ROMlib_set_menu_item_command(MenuHandle menu, INTEGER item, ULONGINT command)
{
    if (item < 1 || item > ROMlib_menu_item_count(menu))
        return paramErr;
    menu_shortcut* entry = find_menu_shortcut(menu, item, command != 0);
    if (entry)
        entry->command = command;
    return entry || !command ? noErr : memFullErr;
}

PUBLIC OSErr ROMlib_get_menu_item_encoding(MenuHandle menu, INTEGER item, ULONGINT* encoding)
{
    if (!encoding || item < 1 || item > ROMlib_menu_item_count(menu))
        return paramErr;
    const menu_shortcut* entry = find_menu_shortcut(menu, item, FALSE);
    const ULONGINT stored = CL(entry ? entry->encoding : 0);
    memcpy(encoding, &stored, sizeof stored);
    return noErr;
}

PUBLIC OSErr ROMlib_set_menu_item_encoding(MenuHandle menu, INTEGER item, ULONGINT encoding)
{
    if (item < 1 || item > ROMlib_menu_item_count(menu))
        return paramErr;
    menu_shortcut* entry = find_menu_shortcut(menu, item, encoding != 0);
    if (entry)
        entry->encoding = encoding;
    return entry || !encoding ? noErr : memFullErr;
}

OSErr ROMlib_get_menu_item_font(MenuHandle menu, INTEGER item, INTEGER* font)
{
    if (!font || item < 1 || item > ROMlib_menu_item_count(menu))
        return paramErr;
    const menu_shortcut* entry = find_menu_shortcut(menu, item, FALSE);
    const INTEGER encoded = CW(entry ? entry->font : 0);
    memcpy(font, &encoded, sizeof encoded);
    return noErr;
}

OSErr ROMlib_set_menu_item_font(MenuHandle menu, INTEGER item, INTEGER font)
{
    if (item < 1 || item > ROMlib_menu_item_count(menu))
        return paramErr;
    menu_shortcut* entry = find_menu_shortcut(menu, item, font != 0);
    if (!entry && font)
        return memFullErr;
    if (entry)
        entry->font = font;
    HxX(menu, menuWidth) = CWC(-1);
    HxX(menu, menuHeight) = CWC(-1);
    return noErr;
}

OSErr ROMlib_get_menu_item_icon(MenuHandle menu, INTEGER item, Byte* type, ULONGINT* icon)
{
    if (!type || !icon || item < 1 || item > ROMlib_menu_item_count(menu))
        return paramErr;
    const menu_shortcut* entry = find_menu_shortcut(menu, item, FALSE);
    *type = entry ? entry->icon_type : 0;
    const ULONGINT encoded = CL(entry ? entry->icon : 0);
    memcpy(icon, &encoded, sizeof encoded);
    return noErr;
}

OSErr ROMlib_set_menu_item_icon(MenuHandle menu, INTEGER item, Byte type, Handle icon)
{
    if (item < 1 || item > ROMlib_menu_item_count(menu))
        return paramErr;
    if (type > 5)
        return -4; /* IconRef and newer image representations need a supporting MenuDispatch patch. */
    const ULONGINT address = type && icon ? US_TO_SYN68K(icon) : 0;
    menu_shortcut* entry = find_menu_shortcut(menu, item, address != 0);
    if (!entry && address)
        return memFullErr;
    if (entry) {
        entry->icon = address;
        entry->icon_type = address ? type : 0;
    }
    HxX(menu, menuWidth) = CWC(-1);
    HxX(menu, menuHeight) = CWC(-1);
    return noErr;
}

P1(PUBLIC pascal trap, void, DisposeMenu, MenuHandle, mh)
{
    if (mh)
        change_menu_shortcuts(mh, 0, 0);
    if (mh == finder_file_menu) {
        finder_file_menu = NULL;
        finder_quit_item = 0;
    }
    ROMlib_desk_menu_forget(mh);
    if (mh) {
        ReleaseResource((Handle)mh);
        if (Cx(ResErr) == resNotFound)
            DisposHandle((Handle)mh);
    }
}

typedef struct {     /* NOTE: These structures are strictly local */
    MenuHandle menh; /*	 There's no reason to swap the bytes */
    INTEGER menitem; /*	 in these fields */
    INTEGER menoff;  /* although *menh will still need to be swapped */
} endinfo;

A2(PRIVATE, void, toend, MenuHandle, mh, endinfo*, eip)
{
    char* menuop;

    eip->menh = mh;
    eip->menitem = 0;

    menuop = (char*)STARH(mh) + SIZEOFMINFO + U(Hx(mh, menuData[0]));
    while (*menuop != 0) {
        ++eip->menitem;
        menuop += U(*menuop) + SIZEOFMEXT;
    }
    eip->menoff = menuop - (char*)STARH(mh);
}

A7(PRIVATE, void, app, StringPtr, str, char, icon, char, marker, char, style, char, keyequiv, char, disflag, endinfo*,
   eip)
{
    char *ip, *ep, *menuop;
    Size newsize;
    SignedByte state;

    eip->menitem++;
    ROMlib_set_menu_item_enabled(eip->menh, eip->menitem, !disflag);
    newsize = eip->menoff + SIZEOFMEXT + 1 + U(str[0]);
    SetHandleSize((Handle)eip->menh, newsize);
    /*
   * The following lines were put in because Virex 4.0 calls AddResMenu with
   * a locked handle that happens to have a locked block past it, hence it
   * can't grow.  It's unclear what we should do about this sort of thing
   * in general, but this hack will suffice for now.
   */
    if (MemError() != noErr) {
        state = HGetState((Handle)eip->menh);
        HUnlock((Handle)eip->menh);
        SetHandleSize((Handle)eip->menh, newsize);
        HSetState((Handle)eip->menh, state);
    }
    menuop = (char*)STARH(eip->menh) + eip->menoff;
    ip = (char*)str;
    ep = ip + U(str[0]) + 1;
    while (ip != ep)
        *menuop++ = *ip++;
    *menuop++ = icon;
    *menuop++ = keyequiv;
    *menuop++ = marker;
    *menuop++ = style;
    *menuop = 0;
    eip->menoff = menuop - (char*)STARH(eip->menh);
}

P2(PUBLIC pascal trap, void, AppendMenu, MenuHandle, mh, StringPtr, str)
{
    Str255 tempstr;
    char *ip, *op;
    char c, icon = 0, marker = 0, keyequiv = 0, disflag = 0;
    int style = 0;
    int i;
    endinfo endinf;

    if (mh) {
        toend(mh, &endinf);
        ip = (char*)str + 1;
        op = (char*)tempstr + 1;
        tempstr[0] = 0;
        i = U(str[0]);
        while (i > 0) {
            switch (i--, c = *ip++) {
            case '^':
                icon = *ip++ - '0';
                i--;
                break;
            case '!':
                marker = *ip++;
                i--;
                break;
            case '<':
                switch (i--, c = *ip++) {
                case 'B':
                    style |= (int)bold;
                    break;
                case 'I':
                    style |= (int)italic;
                    break;
                case 'U':
                    style |= (int)underline;
                    break;
                case 'O':
                    style |= (int)outline;
                    break;
                case 'S':
                    style |= (int)shadow;
                    break;
                default:
                    *op++ = '<';
                    *op++ = c;
                    tempstr[0] += 2;
                }
                break;
            case '/':
                keyequiv = *ip++;
                i--;
                break;
            case '(':
                disflag = TRUE;
                break;
            case ';':
            case '\n':
            case '\r':
                if (tempstr[0])
                    app(tempstr, icon, marker, style, keyequiv, disflag, &endinf);
                op = (char*)tempstr + 1;
                tempstr[0] = icon = marker = style = keyequiv = disflag = 0;
                break;
            default:
                *op++ = c;
                tempstr[0]++;
            }
        }
        if (tempstr[0])
            app(tempstr, icon, marker, style, keyequiv, disflag, &endinf);
        dirtymenusize(mh);
    }
}

A2(PRIVATE, void, handleinsert, Handle, h, StringPtr, strp)
{
    LONGINT n;
    StringPtr sp;
    BOOLEAN done;

    n = GetHandleSize(h);
    sp = (StringPtr)STARH(h);
    done = FALSE;
    while (n > 0 && !done) {
        switch (RelString(sp, strp, TRUE, TRUE)) {
        case 1:
            done = TRUE;
            break;
        case 0:
            return;
            break;
        default:
            n -= sp[0] + 1;
            sp += sp[0] + 1;
            break;
        }
    }
    Munger(h, sp - (StringPtr)STARH(h), (Ptr)0, 0, (Ptr)strp, strp[0] + 1);
}

P2(PUBLIC pascal trap, void, AddResMenu, MenuHandle, mh, ResType, restype)
{
    if (mh) {
        int nres, n;
        Handle h;
        INTEGER i;
        ResType t;
        Str255 str;
        endinfo endinf;
        Handle temph;
        StringPtr sp;

        SetResLoad(FALSE);
        toend(mh, &endinf);
        temph = NewHandle((Size)0);
        do {
            nres = CountResources(restype);
            for (n = 1; n <= nres; n++) {
                h = GetIndResource(restype, n);
                GetResInfo(h, &i, &t, str);
                if (str[0] && str[1] != '.' && str[1] != '%')
                    handleinsert(temph, str);
            }
        } while (restype == TICK("FONT") && (restype = TICK("FOND")));

        /* Add an "About ROMAW..." menu to the Apple menu when they
     * ask for desk accessories.
     */

        if (restype == TICK("DRVR") && about_box_menu_name_pstr[0]) {
            if (endinf.menitem > 0) {
                GetItem(mh, endinf.menitem, str);
                if (!str[0] || str[1] != '-')
                    app((StringPtr)"\001-", 0, 0, 0, 0, TRUE, &endinf);
            }
            app(about_box_menu_name_pstr, 0, 0, 0, 0, FALSE, &endinf);
        }
        if (restype == TICK("DRVR") && !(ROMlib_options & ROMLIB_NOPREFS_BIT)) {
            app((StringPtr)ROMAW_PREFS_MENU_NAME, 0, 0, 0, 0, FALSE, &endinf);
            app((StringPtr)"\001-", 0, 0, 0, 0, TRUE, &endinf);
        }

        if (restype == TICK("DRVR"))
            ROMlib_desk_folder_names(temph);

        n = GetHandleSize(temph);
        sp = (StringPtr)STARH(temph);
        HLock(temph);
        while (n > 0) {
            app(sp, 0, 0, 0, 0, FALSE, &endinf);
            n -= sp[0] + 1;
            sp += sp[0] + 1;
        }
        HUnlock(temph);
        DisposHandle(temph);
        SetResLoad(TRUE); /* IMI-353 says to do this. */
        dirtymenusize(mh);
        if (restype == TICK("DRVR"))
            ROMlib_desk_menu_setup(mh);
    }
    /* FIXME - IMI-353 says to call SetResLoad(TRUE), but we only do
   * this "if (mh)".
   */
}

A3(PUBLIC, mextp, ROMlib_mitemtop, MenuHandle, mh, /* INTERNAL */
   INTEGER, item, StringPtr*, stashstringp)
{
    mextp retval;
    StringPtr stashstring;

    if (!mh)
        /*-->*/ return 0;
    retval = (mextp)((char*)STARH(mh) + SIZEOFMINFO + U(Hx(mh, menuData[0])));
    if (*(char*)retval != 0) {
        stashstring = (StringPtr)retval;
        retval = (mextp)((char*)retval + U(*(char*)retval) + 1);

        while (retval->mnextlen && item > 1) {
            stashstring = (StringPtr) & (retval->mnextlen);
            retval = (mextp)((char*)retval + SIZEOFMEXT + U(retval->mnextlen));
            item--;
        }
        if (stashstringp)
            *stashstringp = stashstring;
        if (item != 1)
            retval = 0;
    }
    else
        retval = 0;
    return retval;
}

P2(PUBLIC pascal trap, void, DelMenuItem, MenuHandle, mh, /* IMIV-56 */
   INTEGER, item)
{
    if (mh) {
        ULONGINT unchangedmask;
        StringPtr stashstring;

        if (ROMlib_mitemtop(mh, item, &stashstring)) {
            const boolean_t item32_enabled = ROMlib_menu_item_enabled(mh, 32);
            if (Munger((Handle)mh, (char*)stashstring - (char*)STARH(mh), (Ptr)0, SIZEOFMEXT + U(stashstring[0]),
                       (Ptr) "", 0) < 0)
                return;
            if (item < 32) {
                unchangedmask = (UINT32_C(1) << item) - 1;
                HxX(mh, enableFlags) =
                    CL(((Hx(mh, enableFlags) >> 1) & ~unchangedmask) | (Hx(mh, enableFlags) & unchangedmask));
            }
            ROMlib_menu_sync_item_removal(mh, item, item32_enabled);
        }
        dirtymenusize(mh);
    }
}

A4(PRIVATE, void, xInsertResMenu, MenuHandle, mh, StringPtr, str, ResType, restype, INTEGER, after)
{
    Size hsize;
    Handle h;
    int soff;
    char *sp, *dp, *ep;
    int omen;
    mextp mmm;
    endinfo endinf;

    if (mh) {
        after = MAX(after, 0);
        toend(mh, &endinf);
        omen = endinf.menitem;
        mmm = 0;
        if (after > 0 && !(mmm = ROMlib_mitemtop(mh, after, (StringPtr*)0)))
            if (str)
                AppendMenu(mh, str);
            else
                AddResMenu(mh, restype);
        else {
            const uint32_t old_flags = Hx(mh, enableFlags);
            if (mmm)
                soff = (char*)&(mmm->mnextlen) - (char*)STARH(mh);
            else
                soff = SIZEOFMINFO + U(Hx(mh, menuData[0]));
            hsize = GetHandleSize((Handle)mh) - soff;
            gui_assert(hsize >= 0);
            h = NewHandle(hsize);
            sp = (char*)STARH(mh) + soff;
            dp = (char*)STARH(h);
            ep = sp + hsize;
            while (sp != ep)
                *dp++ = *sp++;
            *((char*)STARH(mh) + soff) = 0;
            SetHandleSize((Handle)mh, (Size)soff + 1);
            menu_shortcut* tail = detach_menu_tail(mh, after);
            if (str)
                AppendMenu(mh, str);
            else
                AddResMenu(mh, restype);
            SetHandleSize((Handle)mh, GetHandleSize((Handle)mh) + hsize - 1);
            sp = (char*)STARH(h);
            toend(mh, &endinf);
            dp = (char*)STARH(mh) + endinf.menoff;
            const int inserted = endinf.menitem - after;
            while (tail) {
                menu_shortcut* entry = tail;
                tail = entry->next;
                entry->item += inserted;
                entry->next = menu_shortcuts;
                menu_shortcuts = entry;
            }
            ep = sp + hsize;
            while (sp != ep)
                *dp++ = *sp++;
            for (int index = after + 1; inserted > 0 && index < 32 && index <= omen; ++index)
                ROMlib_set_menu_item_enabled(mh, index + inserted, (old_flags & (UINT32_C(1) << index)) != 0);
            dirtymenusize(mh);
            DisposHandle(h);
        }
    }
}

P3(PUBLIC pascal trap, void, InsertResMenu, MenuHandle, mh, ResType, restype, INTEGER, after)
{
    if (mh)
        xInsertResMenu(mh, (StringPtr)0, restype, after);
}

P3(PUBLIC pascal trap, void, InsMenuItem, MenuHandle, mh, StringPtr, str, INTEGER, after) /* IMIV-55 */
{
    if (mh)
        xInsertResMenu(mh, str, (ResType)0, after);
}

#define FIRSTHIER ((muelem*)((char*)STARH(MENULIST) + Hx(MENULIST, muoff)) + 2)

P2(PUBLIC pascal trap, void, InsertMenu, MenuHandle, mh, INTEGER, before)
{
    muelem *mp, *mpend, *bindex, newmuelem;
    INTEGER mid1, mid2;
    LONGINT binoff;

    if (mh) {
        normalize_empty_menu_list();
        mid1 = Hx(mh, menuID);
        if (before == -1) {
            mpend = HxX(MENULIST, mulist) + HIEROFF / sizeof(muelem);
            for (mp = FIRSTHIER; mp != mpend && (CW(STARH(PPR(mp->muhandle))->menuID) != mid1); mp++)
                ;
            if (mp != mpend) /* already there */
                /*-->*/ return;
            SetHandleSize(MR(MenuList), (Size)HIEROFF + 2 * sizeof(muelem));
            mpend = HxX(MENULIST, mulist) + HIEROFF / sizeof(muelem);
            PP_RAW(mpend->muhandle) = RM(mh);
            mpend->muleft = 0;
        }
        else {
            bindex = 0;
            mpend = HxX(MENULIST, mulist) + Hx(MENULIST, muoff) / sizeof(muelem);
            for (mp = HxX(MENULIST, mulist); mp != mpend; mp++) {
                if ((mid2 = CW(STARH(PPR(mp->muhandle))->menuID)) == mid1)
                    /*-->*/ return;
                if (mid2 == before)
                    bindex = mp;
            }
            PP_RAW(newmuelem.muhandle) = RM(mh);
            if (bindex == 0) {
                newmuelem.muleft = HxX(MENULIST, muright);
                Munger(MR(MenuList), Hx(MENULIST, muoff) + sizeof(muelem), (Ptr)0, (LONGINT)0, (Ptr)&newmuelem,
                       (LONGINT)sizeof(newmuelem));
                binoff = Hx(MENULIST, muoff);
            }
            else {
                newmuelem.muleft = bindex->muleft;
                binoff = (char*)bindex - (char*)STARH(MR(MenuList));
                Munger(MR(MenuList), binoff, (Ptr)0, (LONGINT)0, (Ptr)&newmuelem, (LONGINT)sizeof(newmuelem));
            }
            HxX(MENULIST, muoff) = CW(Hx(MENULIST, muoff) + sizeof(muelem));
            MBDFCALL(mbCalc, 0, binoff);
        }
        HIEROFFX = CW(HIEROFF + sizeof(muelem));
    }
}

P1(PUBLIC pascal trap, void, DeleteMenu, int16, mid)
{
    muelem *mp, *mpend;
    int32 deleteloc;

    normalize_empty_menu_list();
    menu_delete_entries(mid);

    mpend = HxX(MENULIST, mulist) + Hx(MENULIST, muoff) / sizeof(muelem);
    for (mp = HxX(MENULIST, mulist); mp != mpend && CW(STARH(PPR(mp->muhandle))->menuID) != mid; mp++)
        ;
    if (mp != mpend) {
        deleteloc = (LONGINT)((char*)mp - (char*)STARH(MR(MenuList)));
        Munger(MR(MenuList), deleteloc, (Ptr)0, (int32)sizeof(muelem), (Ptr) "", (int32)0);
        HxX(MENULIST, muoff) = CW(Hx(MENULIST, muoff) - sizeof(muelem));
        MBDFCALL(mbCalc, 0, deleteloc);
    }
    else {
        mpend = HxX(MENULIST, mulist) + HIEROFF / sizeof(muelem);
        for (mp = FIRSTHIER; mp != mpend && (CW(STARH(PPR(mp->muhandle))->menuID) != mid); mp++)
            ;
        if (mp == mpend)
            /*-->*/ return; /* not there */
        Munger(MR(MenuList), (int32)((char*)mp - (char*)STARH(MR(MenuList))), (Ptr)0, (int32)sizeof(muelem), (Ptr) "",
               (int32)0);
    }
    HIEROFFX = CW(HIEROFF - sizeof(muelem));
}

typedef mbartype* mbarptr;
MAKE_HIDDEN(mbarptr);
typedef HIDDEN_mbarptr* mbarhandle;

P1(PUBLIC pascal trap, Handle, GetNewMBar, INTEGER, mbarid)
{
    mbarhandle mb;
    mlhandle saveml;
    INTEGER *ip, *ep;
    MenuHandle mh;
    Handle retval;

    fprintf(stderr, "trace: GetNewMBar id=%d CurMap=%d\n", mbarid, Cx(CurMap));
    fflush(stderr);
    mb = (mbarhandle)GetResource(TICK("MBAR"), mbarid);
    fprintf(stderr, "trace: GetNewMBar resource id=%d handle=%p\n", mbarid, (void*)mb);
    fflush(stderr);
    if (!mb)
        retval = 0;
    else {
        if (!HIDDEN_RAW((*mb)))
            LoadResource((Handle)mb);

        LOCK_HANDLE_EXCURSION_1(mb, {
            ip = HxX(mb, mrid);
            ep = ip + Hx(mb, nmen);
            saveml = MENULIST;

            HIDDEN_RAW_SET(MenuList_H, 0);
            ClearMenuBar();
            retval = MR(MenuList);

            HxX(MENULIST, mufu) = HxX(saveml, mufu); /* int 3 */

            while (ip != ep) {
                mh = GetMenu(Cx(*ip));
                InsertMenu(mh, 0);
                ip++;
            }
        });
        HIDDEN_RAW_SET(MenuList_H, RM((Handle)saveml));
    }
    return retval;
}

P0(PUBLIC pascal trap, Handle, GetMenuBar)
{
    Handle retval = MR(MenuList);
    HandToHand(&retval);
    return retval;
}

P1(PUBLIC pascal trap, void, SetMenuBar, Handle, ml)
{
    Handle temph;

    DisposHandle(MR(MenuList));
    temph = ml;
    HandToHand(&temph);
    HIDDEN_RAW_SET(MenuList_H, RM(temph));
}

enum { nonhier = 0, hier = 1 };

A1(PRIVATE, void, initpairs, startendpairs, pairs)
{
    normalize_empty_menu_list();
    pairs[(int)nonhier].startp = HxX(MENULIST, mulist);
    pairs[(int)nonhier].endp = HxX(MENULIST, mulist) + Hx(MENULIST, muoff) / sizeof(muelem);
    pairs[(int)hier].startp = FIRSTHIER;
    pairs[(int)hier].endp = HxX(MENULIST, mulist) + HIEROFF / sizeof(muelem);
}

PUBLIC void ROMlib_menu_invalidate_sizes(void)
{
    if (!MenuList || (uintptr_t)MenuList == UINT32_MAX || !STARH(MENULIST))
        return;

    startendpairs pairs;
    initpairs(pairs);
    for (unsigned list = nonhier; list <= hier; ++list)
        for (muelem* element = pairs[list].startp; element < pairs[list].endp; ++element)
            dirtymenusize(PPR(element->muhandle));
}

static boolean_t menu_id_exists_p(int id)
{
    muelem* mp;
    startendpairs mps;

    initpairs(mps);
    for (mp = mps[nonhier].startp; mp != mps[nonhier].endp; mp++) {
        if (CW(STARH(PPR(mp->muhandle))->menuID) == id)
            return TRUE;
    }
    return FALSE;
}

A1(PUBLIC, INTEGER, ROMlib_mentosix, INTEGER, menuid)
{
    muelem *mp, *mpend;
    startendpairs mps;
    INTEGER i;

    initpairs(mps);
    for (i = (int)nonhier; i <= (int)hier; i++) {
        for (mp = mps[i].startp, mpend = mps[i].endp; mp < mpend && CW(STARH(PPR(mp->muhandle))->menuID) != menuid;
             mp++)
            ;
        if (mp < mpend)
            /*-->*/ return (char*)mp - (char*)STARH(MR(MenuList));
    }
    return -1;
}

A2(PRIVATE, BOOLEAN, mtoggle, INTEGER, mid, highstate, h)
{
    LONGINT l;

    l = ROMlib_mentosix(mid);
    if (l != -1) {
        l |= (LONGINT)h << 16;
        MBDFCALL(mbHilite, 0, l);
        /*-->*/ return TRUE;
    }
    return FALSE;
}

P1(PUBLIC pascal trap, void, HiliteMenu, INTEGER, mid)
{
    if (MENU_BAR_GLOBAL_INVALID & MENU_BAR_GLOBAL_INVALID_MASK)
        DrawMenuBar();
    if (mid != CW(TheMenu)) {
        if (TheMenu)
            mtoggle(CW(TheMenu), RESTORE);
        if (!menu_id_exists_p(mid))
            mid = 0;
        if (mid) {
            if (mid == Hx(HxP(MENULIST, mulist[0].muhandle), menuID))
                ROMlib_alarmoffmbar();
            mtoggle(mid, HILITE);
        }
    }
    TheMenu = CW(mid);
}

typedef pascal void (*menuhookp)(void);

typedef pascal LONGINT (*mbarhookp)(Rect* rp);

#define CALLMENUHOOK(fp) ROMlib_CALLMENUHOOK((menuhookp)(fp))

A1(static inline, void, ROMlib_CALLMENUHOOK, menuhookp, fp)
{
    ROMlib_hook(menu_menuhooknumber);
    CALL_EMULATOR((syn68k_addr_t)US_TO_SYN68K((uintptr_t)fp));
}

#define CALLMBARHOOK(arg, fp) ROMlib_CALLMBARHOOK(arg, (mbarhookp)(fp))

A2(static inline, LONGINT, ROMlib_CALLMBARHOOK, Rect*, rp, mbarhookp, fp)
{
    ROMlib_hook(menu_mbarhooknumber);

    PUSHADDR((LONGINT)US_TO_SYN68K((uintptr_t)rp));
    CALL_EMULATOR((syn68k_addr_t)US_TO_SYN68K((uintptr_t)fp));
    return EM_D0;
}

A1(PRIVATE, INTEGER, wheretowhich, LONGINT, offset)
{
    mbdfentry *p, *ep;

    ep = (mbdfentry*)STARH(MR(MBSaveLoc));
    for (p = (mbdfentry*)((char*)STARH(MR(MBSaveLoc)) + Hx(MBSAVELOC, lastMBSave));
         p != ep && Cx(p->mbMLOffset) != offset; p--)
        ;
    return p - ep;
}

A1(PRIVATE, void, shadowrect, Rect*, rp)
{
    rp->top = CW(CW(rp->top) - 1);
    rp->left = CW(CW(rp->left) - 1);
    rp->bottom = CW(CW(rp->bottom) + 2);
    rp->right = CW(CW(rp->right) + 2);
}

A3(PRIVATE, void, restoren, INTEGER, ntodrop, RgnHandle, restoredrgn, Rect*, rp)
{
    mbdfentry* p;
    RgnHandle tmprgn;

    if (restoredrgn) {
        p = (mbdfentry*)((char*)STARH(MR(MBSaveLoc)) + Hx(MBSAVELOC, lastMBSave));
        RectRgn(restoredrgn, &p->mbRectSave);
        shadowrect(&HxX(restoredrgn, rgnBBox));
    }
#if !defined(LETGCCWAIL)
    else
        p = (mbdfentry*)-1;
#endif /* LETGCCWAIL */
    MBDFCALL(mbRestore, 0, 0L);

    while (--ntodrop > 0) {
        if (restoredrgn) {
            --p;
            tmprgn = NewRgn();
            RectRgn(tmprgn, &p->mbRectSave);
            shadowrect(&HxX(tmprgn, rgnBBox));
            UnionRgn(restoredrgn, tmprgn, restoredrgn);
            DisposeRgn(tmprgn);
        }
        MBDFCALL(mbRestore, 0, 0L);
    }
    if (restoredrgn && rp)
        *rp = p[-1].mbRectSave;
}

A2(PRIVATE, MenuHandle, menunumtomh, INTEGER, mid, INTEGER*, sixp)
{
    muelem *mp, *mpend;

    normalize_empty_menu_list();
    mpend = HxX(MENULIST, mulist) + HIEROFF / sizeof(muelem);
    for (mp = FIRSTHIER; mp != mpend && CW(STARH(PPR(mp->muhandle))->menuID) != mid; mp++)
        ;
    if (mp != mpend) {
        *sixp = (char*)mp - (char*)STARH(MR(MenuList));
        /*-->*/ return PPR(mp->muhandle);
    }
    return 0;
}

A3(PRIVATE, MenuHandle, itemishierarchical, MenuHandle, mh, INTEGER, item, INTEGER*, sixp)
{
    mextp mep;

    if ((mep = ROMlib_mitemtop(mh, item, (StringPtr*)0)) && Cx(mep->mkeyeq) == 0x1B)
        /*-->*/ return menunumtomh(Cx(mep->mmarker), sixp);
    return 0;
}

#define HIERRECTBIT (1L << 16)

int32 ROMlib_menuhelper(MenuHandle mh, Rect* saverp, int32 oldwhere, BOOLEAN ispopup, int16 nmenusdisplayed)
{
    mbdfentry *oldentry, *newentry;
    Rect r, r2;
    Point dummy_pt;
    Point tempp;
    INTEGER mid, item, olditem, tempi, oldtopmenuitem;
    INTEGER firstitem;
    MenuHandle newmh;
    int i;
    LONGINT pointaslong;
    LONGINT where, templ;
    RgnHandle saveclip, restoredrgn;
    BOOLEAN changedmenus;
    INTEGER oldwhichmenuhit, whichmenuhit;
    HIDDEN_GrafPtr saveport;
    EventRecord ev;
    Point pt;
    LONGINT myd0;
    boolean_t seen_up_already, done;

    GetPort(&saveport);
    SetPort(MR(wmgr_port));

    olditem = -1;
    item = 0;
    if (!ispopup)
        firstitem = 0;
    else
        firstitem = -1;
    changedmenus = FALSE;
    r = *saverp;
    memset(&dummy_pt, 0xFF, sizeof dummy_pt);
    oldwhichmenuhit = whichmenuhit = wheretowhich(oldwhere);
    restoredrgn = NewRgn();

    seen_up_already = FALSE;
    done = FALSE;
    goto enter;
    while (!done) {
        GetMouse(&pt);
        pt.h = CW(pt.h);
        pt.v = CW(pt.v);
        pointaslong = ((int32)pt.v << 16) | (unsigned short)pt.h;
        where = MBDFCALL(mbHit, 0, pointaslong);
        if (MenuHook)
            CALLMENUHOOK(MR(MenuHook));
        if (where == oldwhere) {
            if (mh) {
                PORT_TX_FACE_X(MR(wmgr_port)) = (Style)CB(0);
                PORT_TX_FONT_X(MR(wmgr_port)) = CWC(0);
                item = CW(item);
                MENUCALL(mChooseMsg, mh, &r, pt, &item);
                item = CW(item);
                if (item != olditem || changedmenus) {
                    if (firstitem == -1)
                        firstitem = item;
                    changedmenus = FALSE;
                    if ((i = nmenusdisplayed - whichmenuhit)) {
                        restoren(i, restoredrgn, &r);
                        nmenusdisplayed -= i;
                        saveclip = PP_RAW(PORT_CLIP_REGION_X(MR(wmgr_port)));

                        FIELD_ASSIGN_RAW(PORT_CLIP_REGION_X(MR(wmgr_port)), RM(NewRgn()));
                        RectRgn(PORT_CLIP_REGION(MR(wmgr_port)), &r);

                        if (item == 0) {  /* may have been 'cause of */
                            pt.v = 32767; /* scrolling, in which case */
                            pt.h = 32767; /* we don't want to scroll */
                                          /* a messed clip region */
                        }
                        item = olditem;
                        PORT_TX_FACE_X(MR(wmgr_port)) = (Style)CB(0);
                        PORT_TX_FONT_X(MR(wmgr_port)) = CWC(0);
                        item = CW(item);
                        MENUCALL(mChooseMsg, mh, &r, pt, &item);
                        item = CW(item);
                        DisposeRgn(PORT_CLIP_REGION(MR(wmgr_port)));
                        FIELD_ASSIGN_RAW(PORT_CLIP_REGION_X(MR(wmgr_port)), saveclip);
                    }
                    if ((newmh = itemishierarchical(mh, item, &tempi))) {
                        int16 saveatmenubottom;

                        r2 = *(Rect*)MBDFCALL(mbRect, 0, tempi | HIERRECTBIT);
                        MBDFCALL(mbSave, tempi, (intptr_t)&r2);
                        if (MBarHook) {
                            myd0 = CALLMBARHOOK(&r2, MR(MBarHook));
                            if (myd0 != 0)
                                goto out;
                        }
                        ((mbdfentry*)STARH(MR(MBSaveLoc)))[wheretowhich(tempi)].mbReserved = CLC(0);
                        oldtopmenuitem = TopMenuItem;
                        saveatmenubottom = AtMenuBottom;
                        TopMenuItem = r2.top;
                        PORT_TX_FACE_X(MR(wmgr_port)) = (Style)CB(0);
                        PORT_TX_FONT_X(MR(wmgr_port)) = CWC(0);
                        saveclip = PP_RAW(PORT_CLIP_REGION_X(thePort));
                        FIELD_ASSIGN_RAW(PORT_CLIP_REGION_X(thePort), RM(NewRgn()));
                        RectRgn(PORT_CLIP_REGION(thePort), &r2);
                        MENUCALL(mDrawMsg, newmh, &r2, dummy_pt, (INTEGER*)0);
                        DisposeRgn(PORT_CLIP_REGION(thePort));
                        FIELD_ASSIGN_RAW(PORT_CLIP_REGION_X(thePort), saveclip);
                        nmenusdisplayed++;
                        MBDFCALL(mbSaveAlt, 0, tempi);
                        TopMenuItem = oldtopmenuitem;
                        AtMenuBottom = saveatmenubottom;
                    }
                    olditem = item;
                }
            }
        }
        else {
            if (pt.v < Cx(MBarHeight) && !ispopup) {
                if (nmenusdisplayed) {
                    restoren(nmenusdisplayed, (RgnHandle)0, 0);
                    nmenusdisplayed = 0;
                }
                whichmenuhit = 0;
                if (where == NOTHITINMBAR) {
                    mh = NULL;
                    HiliteMenu(0);
                }
                else {
                    mh = PPR(((muelem*)((char*)STARH(MR(MenuList)) + where))->muhandle);
                    HiliteMenu(Hx(mh, menuID));
                    r = *(Rect*)MBDFCALL(mbRect, 0, where);
                    MBDFCALL(mbSave, where, (intptr_t)&r);
                    if (MBarHook) {
                        myd0 = CALLMBARHOOK(&r, MR(MBarHook));
                        if (myd0 != 0)
                            goto out;
                    }
                    olditem = item = 0;
                    TopMenuItem = MBarHeight;
                    PORT_TX_FACE_X(MR(wmgr_port)) = (Style)CB(0);
                    PORT_TX_FONT_X(MR(wmgr_port)) = CWC(0);
                    saveclip = PP_RAW(PORT_CLIP_REGION_X(thePort)); /* ick */
                    FIELD_ASSIGN_RAW(PORT_CLIP_REGION_X(thePort), RM(NewRgn()));
                    RectRgn(PORT_CLIP_REGION(thePort), &r);
                    MENUCALL(mDrawMsg, mh, &r, dummy_pt, (INTEGER*)0);
                    DisposeRgn(PORT_CLIP_REGION(thePort));
                    FIELD_ASSIGN_RAW(PORT_CLIP_REGION_X(thePort), saveclip);
                    nmenusdisplayed++;
                    whichmenuhit = wheretowhich(where);
                }
            }
            else if (where == NOTHIT || (ispopup && pt.v < Cx(MBarHeight))) {
                if ((i = nmenusdisplayed - whichmenuhit)) {
                    restoren(i, (RgnHandle)0, 0);
                    nmenusdisplayed -= i;
                }
                if (mh) {
                    MR(wmgr_port)->txFace = (Style)0;
                    MR(wmgr_port)->txFont = 0;
                    item = CW(item);
                    MENUCALL(mChooseMsg, mh, &r, pt, &item);
                    item = CW(item);
                }
                else
                    item = 0;
                where = oldwhere;
                olditem = 0;
            }
            else {
                whichmenuhit = wheretowhich(where);
                newentry = (mbdfentry*)STARH(MR(MBSaveLoc)) + whichmenuhit;
                oldentry = (mbdfentry*)STARH(MR(MBSaveLoc)) + oldwhichmenuhit;
                oldentry->mbReserved = CL((ULONGINT)item);
                olditem = item = CL(newentry->mbReserved);
                changedmenus = TRUE;
                mh = PPR(((muelem*)((char*)STARH(MR(MenuList)) + where))->muhandle);
                templ = where;
                if (where > Hx(MENULIST, muoff))
                    templ |= HIERRECTBIT;
                r = *(Rect*)MBDFCALL(mbRect, 0, templ);
                whichmenuhit = wheretowhich(where);
                MBDFCALL(mbSaveAlt, 0, oldwhere);
                MBDFCALL(mbResetAlt, 0, where);
            }
            oldwhere = where;
            oldwhichmenuhit = whichmenuhit;
        }
        /* we're done if we get our first mouse-up while item is non-zero,
 or when we get our first mouse-down while item is non-zero
 or when we get our second mouse-up, no matter what */

    enter:
        if (!ROMlib_sticky_menus_p)
            done = !StillDown();
        else {
            if (OSEventAvail(mUpMask, &ev)) {
                if (seen_up_already || (item != firstitem && firstitem != -1))
                    done = TRUE;
                else {
                    GetOSEvent(mUpMask, &ev);
                    seen_up_already = TRUE;
                }
            }
            if (!done && OSEventAvail(mDownMask, &ev)) {
                GetOSEvent(mDownMask, &ev);
                done = item != 0;
            }
        }
    }
    while (!GetOSEvent(mUpMask, &ev))
        ;
out:
    if (mh) {
        if (item) {
            mid = Hx(mh, menuID);
            tempi = item;
            tempp.v = 0;
            tempp.h = 0;
            PORT_TX_FACE_X(MR(wmgr_port)) = (Style)CB(0);
            PORT_TX_FONT_X(MR(wmgr_port)) = CWC(0);
            tempi = CW(tempi);
            MENUCALL(mChooseMsg, mh, &r, tempp, &tempi);
            tempi = CW(tempi);
            const INTEGER flash_count = Cx(MenuFlash);
            if (flash_count > 0) {
                for (i = 0; i < flash_count; i++) {
                    dirty_rect_update_screen();
                    Delay(3L, (LONGINT*)0);
                    PORT_TX_FACE_X(MR(wmgr_port)) = (Style)CB(0);
                    PORT_TX_FONT_X(MR(wmgr_port)) = CWC(0);
                    tempi = CW(tempi);
                    MENUCALL(mChooseMsg, mh, &r, pt, &tempi);
                    tempi = CW(tempi);
                    dirty_rect_update_screen();
                    Delay(3L, (LONGINT*)0);
                    PORT_TX_FACE_X(MR(wmgr_port)) = (Style)CB(0);
                    PORT_TX_FONT_X(MR(wmgr_port)) = CWC(0);
                    tempi = CW(tempi);
                    MENUCALL(mChooseMsg, mh, &r, tempp, &tempi);
                    tempi = CW(tempi);
                }
                dirty_rect_update_screen();
                Delay(3L, (LONGINT*)0);
            }
        }
        else
            mid = 0;
        if (nmenusdisplayed)
            restoren(nmenusdisplayed, (RgnHandle)0, 0);
    }
    else {
        mid = 0;
        item = 0;
    }
    if (!mid)
        HiliteMenu(0);
    SetPort(HIDDEN_NATIVE(saveport)); /* Does SystemMenu() expect the WMgrPort also? */

    /* Illustrator 5.5 behavior suggests that hits on hierarchical menus
   * for non leaf-node menus are handled differently.  [PR #1683].
   * Returning the "correct" mid/item for a non-leaf-node menu causes
   * Illustrator to crash when a routine of theirs that looks up some
   * value fails to find a match and doesn't fill in a value via
   * reference.  This is a workaround, but it's known to be incorrect.
   * On a real Mac, choosing a non-leaf node menu for a font name in
   * the Character window switches to that font.  This patch, as well
   * as one trying to use newmh's menu id, causes such a selection to
   * be ignored.
   */
    newmh = itemishierarchical(mh, item, &tempi);
    BOOLEAN parent_choosable = FALSE;
    if (newmh)
        ROMlib_get_submenu_parent_choosable(mh, item, &parent_choosable);
    if (newmh && !parent_choosable) {
        mid = 0;
        item = 0;
    }

    if (ROMlib_desk_menu_select(((LONGINT)mid << 16) | (unsigned short)item))
        return 0;
    if (MBarEnable && mid != kHMHelpMenuID) {
        SystemMenu(((LONGINT)mid << 16) | (unsigned short)item);
        return 0;
    }
    else
        return ((LONGINT)mid << 16) | (unsigned short)item;
}

P1(PUBLIC pascal trap, LONGINT, MenuSelect, Point, p)
{
    if (MENU_BAR_GLOBAL_INVALID & MENU_BAR_GLOBAL_INVALID_MASK)
        DrawMenuBar();
    Rect spooeyr;
    LONGINT retval;

    TopMenuItem = MBarHeight;
    retval = ROMlib_menuhelper((MenuHandle)0, &spooeyr, 0, FALSE, 0);
    if (finder_quit_selection(retval))
        return 0;
    return retval;
}

P1(PUBLIC pascal trap, void, FlashMenuBar, INTEGER, mid)
{
    LONGINT l;

    if (mid == 0 || (l = ROMlib_mentosix(mid)) == -1)
        l = 0;
    else if (mid == Cx(TheMenu)) {
        l |= (LONGINT)RESTORE << 16;
        TheMenu = 0;
    }
    else {
        l |= (LONGINT)HILITE << 16;
        TheMenu = CW(mid);
    }
    MBDFCALL(mbHilite, 0, l);
}

A2(PRIVATE, BOOLEAN, findroot, INTEGER, menuid, INTEGER*, root_unswp)
{
    INTEGER loopcount, i, maxi;

    enum { noparent, hierparent, nonhierparent } partype;

    startendpairs mps;
    muelem *mp, *mpend;
    MenuHandle mh;
    unsigned char* p;
    mextp mxp;
    INTEGER mitem;

    initpairs(mps);
    maxi = mps[(int)hier].endp - mps[(int)nonhier].startp; /* upper bound */
    for (partype = hierparent, loopcount = 0; partype == hierparent && loopcount < maxi; loopcount++) {
        for (i = (int)nonhier; i <= (int)hier; i++) {
            for (mp = mps[i].startp, mpend = mps[i].endp; mp != mpend; mp++) {
                mh = PPR(mp->muhandle);
                p = (unsigned char*)STARH(mh) + SIZEOFMINFO + *(char*)(HxX(mh, menuData));
                mitem = 1;
                while (*p != 0) {
                    mxp = (mextp)(p + *p + 1);
                    if (mxp->mkeyeq == 0x1B && mxp->mmarker == menuid) {
                        menuid = Hx(mh, menuID);
                        partype = i == (int)hier ? hierparent : nonhierparent;
                        goto out;
                    }
                    mitem++;
                    p += *p + SIZEOFMEXT;
                }
            }
        }
    out:;
    }
    if (loopcount == maxi)
        gui_assert(0); /* should be SysError */
    if (partype == nonhierparent) {
        *root_unswp = menuid;
        /*-->*/ return TRUE;
    }
    return FALSE;
}

PRIVATE LONGINT menu_key_with_modifiers(CHAR thec, int modifiers)
{
    muelem *mp, *mpend;
    startendpairs mps;
    unsigned char* p;
    MenuHandle mh;
    int mitem;
    mextp mxp;
    LONGINT retval;
    Byte c;
    INTEGER i, menuid;
    boolean_t host_quit = modifiers < 0 && quit_menu_key_pending;
    LONGINT quit_item = 0;

    if (modifiers < 0)
        quit_menu_key_pending = FALSE;

    if (thec >= 0x1B && thec <= 0x1F)
        /*-->*/ return 0;
    c = thec;
    if (c >= 'a' && c <= 'z')
        c = 'A' + c - 'a';

    initpairs(mps);
    for (i = (int)nonhier; i <= (int)hier; i++) {
        for (mpend = mps[i].startp - 1, mp = mps[i].endp - 1; mp != mpend; mp--) {
            mh = PPR(mp->muhandle);
            p = (unsigned char*)STARH(mh) + SIZEOFMINFO + *(unsigned char*)(HxX(mh, menuData));
            mitem = 1;
            while (*p != 0) {
                mxp = (mextp)(p + U(*p) + 1);
                /* Early applications such as MacWrite 2.2 have a Quit item
                 without Command-Q. Resolve only a host-generated request;
                 ordinary keyboard shortcuts retain their guest behavior. */
                if (host_quit && c == 'Q' && i == (int)nonhier && Hx(mh, menuID) > 0 && mitem < 32 &&
                    (Hx(mh, enableFlags) & 1) && (Hx(mh, enableFlags) & ((LONGINT)1 << mitem)) && !mxp->mkeyeq &&
                    p[0] == 4 && (p[1] == 'Q' || p[1] == 'q') && (p[2] == 'U' || p[2] == 'u') &&
                    (p[3] == 'I' || p[3] == 'i') && (p[4] == 'T' || p[4] == 't'))
                    quit_item = ((LONGINT)Hx(mh, menuID) << 16) | mitem;
                menu_shortcut* shortcut = find_menu_shortcut(mh, mitem, FALSE);
                if (mxp->mkeyeq == c && ROMlib_menu_item_enabled(mh, 0) && ROMlib_menu_item_enabled(mh, mitem) &&
                    (host_quit || (modifiers < 0 ? !shortcut || !(shortcut->modifiers & 8)
                                                 : modifiers == (shortcut ? shortcut->modifiers : 0)))) {
                    if (i == (int)nonhier)
                        menuid = Hx(mh, menuID);
                    else if (!findroot(Hx(mh, menuID), &menuid))
                        /*-->*/ return 0L;
                    retval = ((LONGINT)Hx(mh, menuID) << 16) | (unsigned short)mitem;
                    FlashMenuBar(menuid);
                    if (finder_quit_selection(retval))
                        return 0;
                    if (Hx(mh, menuID) < 0 && Hx(mh, menuID) != kHMHelpMenuID) {
                        SystemMenu(retval);
                        retval = 0;
                    }
                    /*-->*/ return retval;
                }
                mitem++;
                p += U(*p) + SIZEOFMEXT;
            }
        }
    }
    if (quit_item)
        FlashMenuBar(quit_item >> 16);
    return quit_item;
}

P1(PUBLIC pascal trap, LONGINT, MenuKey, CHAR, thec)
{
    return menu_key_with_modifiers(thec, -1);
}

PUBLIC LONGINT ROMlib_menu_event(const EventRecord* event)
{
    if (!event || (CW(event->what) != keyDown && CW(event->what) != autoKey))
        return 0;
    unsigned keys = CW(event->modifiers);
    int modifiers = ((keys & shiftKey) ? 1 : 0) | ((keys & optionKey) ? 2 : 0) |
                    ((keys & (ControlKey | rightControlKey)) ? 4 : 0) | ((keys & cmdKey) ? 0 : 8);
    return menu_key_with_modifiers(CL(event->message) & charCodeMask, modifiers);
}

P3(PUBLIC pascal trap, void, SetItem, MenuHandle, mh, INTEGER, item, StringPtr, str)
{
    StringPtr stashstring;

    if (!str || !ROMlib_mitemtop(mh, item, &stashstring))
        return;
    const Size new_size = (unsigned char)str[0] + 1;
    const Size old_size = (unsigned char)stashstring[0] + 1;
    if (new_size == old_size)
        memmove(stashstring, str, new_size);
    else {
        const LONGINT offset = (char*)stashstring - (char*)STARH(mh);
        Str255 saved;
        memcpy(saved, str, new_size);
        if (Munger((Handle)mh, offset, NULL, old_size, (Ptr)saved, new_size) < 0) {
            return;
        }
    }
    dirtymenusize(mh);
}

P3(PUBLIC pascal trap, void, GetItem, MenuHandle, mh, INTEGER, item, StringPtr, str)
{
    StringPtr stashstring;

    if (ROMlib_mitemtop(mh, item, &stashstring))
        str255assign(str, stashstring);
    else
        str[0] = 0;
}

P2(PUBLIC pascal trap, void, DisableItem, MenuHandle, mh, INTEGER, item)
{
    if (mh)
        ROMlib_set_menu_item_enabled(mh, item, FALSE);
}

P2(PUBLIC pascal trap, void, EnableItem, MenuHandle, mh, INTEGER, item)
{
    if (mh)
        ROMlib_set_menu_item_enabled(mh, item, TRUE);
}

P3(PUBLIC pascal trap, void, CheckItem, MenuHandle, mh, INTEGER, item, BOOLEAN, cflag)
{
    if (mh) {
        mextp mep;

        if ((mep = ROMlib_mitemtop(mh, item, (StringPtr*)0)))
            mep->mmarker = cflag ? checkMark : 0;
    }
}

P3(PUBLIC pascal trap, void, SetItemMark, MenuHandle, mh, INTEGER, item, CHAR, mark)
{
    mextp mep;

    if ((mep = ROMlib_mitemtop(mh, item, (StringPtr*)0)))
        mep->mmarker = mark;
}

P3(PUBLIC pascal trap, void, GetItemMark, MenuHandle, mh, INTEGER, item, INTEGER*, markp)
{
    mextp mep;

    if ((mep = ROMlib_mitemtop(mh, item, (StringPtr*)0)))
        *markp = CW((INTEGER)(unsigned char)mep->mmarker);
}

P3(PUBLIC pascal trap, void, SetItemIcon, MenuHandle, mh, INTEGER, item, INTEGER, icon)
{
    mextp mep;

    if ((mep = ROMlib_mitemtop(mh, item, (StringPtr*)0))) {
        mep->micon = icon;
        dirtymenusize(mh);
    }
}

P3(PUBLIC pascal trap, void, GetItemIcon, MenuHandle, mh, INTEGER, item, INTEGER*, iconp)
{
    mextp mep;

    if ((mep = ROMlib_mitemtop(mh, item, (StringPtr*)0)))
        *iconp = CW((INTEGER)(unsigned char)Cx(mep->micon));
}

P3(PUBLIC pascal trap, void, SetItemStyle, MenuHandle, mh, INTEGER, item, INTEGER, style)
{
    mextp mep;

    if ((mep = ROMlib_mitemtop(mh, item, (StringPtr*)0))) {
        mep->mstyle = style;
        dirtymenusize(mh);
    }
}

P3(PUBLIC pascal trap, void, GetItemStyle, MenuHandle, mh, INTEGER, item, INTEGER*, stylep)
{
    mextp mep;

    if ((mep = ROMlib_mitemtop(mh, item, (StringPtr*)0)))
        *stylep = CW((INTEGER)(unsigned char)Cx(mep->mstyle));
}

P1(PUBLIC pascal trap, INTEGER, CountMItems, MenuHandle, mh)
{
    endinfo endinf;

    if (mh) {
        toend(mh, &endinf);
        /*-->*/ return endinf.menitem;
    }
    else
        return 0;
}

P1(PUBLIC pascal trap, MenuHandle, GetMHandle, INTEGER, mid)
{
    MenuHandle retval;

    normalize_empty_menu_list();
    if (!MenuList)
        retval = 0;
    else {
        muelem *mp, *mpend;

        mpend = HxX(MENULIST, mulist) + HIEROFF / sizeof(muelem);
        for (mp = FIRSTHIER; mp != mpend && CW(STARH(PPR(mp->muhandle))->menuID) != mid; mp++)
            ;
        if (mp != mpend)
            retval = PPR(mp->muhandle);
        else {
            mpend = HxX(MENULIST, mulist) + Hx(MENULIST, muoff) / sizeof(muelem);
            for (mp = HxX(MENULIST, mulist); mp != mpend && CW(STARH(PPR(mp->muhandle))->menuID) != mid; mp++)
                ;
            if (mp != mpend)
                retval = PPR(mp->muhandle);
            else
                return (0);
        }
    }
    return retval;
}

P1(PUBLIC pascal trap, void, SetMenuFlash, INTEGER, i)
{
    MenuFlash = CW(i);
}

A0(PUBLIC, BOOLEAN, ROMlib_shouldalarm)
{
    return MENULIST && Hx(HxP(MENULIST, mulist[0].muhandle), menuData[0]) == 1;
}

void ROMlib_menucall(INTEGER mess, MenuHandle themenu, Rect* menrect, Point hit, INTEGER* which)
{
    Handle defproc;
    menuprocp mp;

    defproc = HxP(themenu, menuProc);

    if (defproc) {
        if (HIDDEN_RAW(*(defproc)) == NULL)
            LoadResource(defproc);

        mp = (menuprocp)STARH(defproc);

        if (mp == P_mdef0) {
            C_mdef0(mess, themenu, menrect, hit, which);
        }
        else {
            ROMlib_hook(menu_mdefnumber);
            LOCK_HANDLE_EXCURSION_1(
                defproc, { CToPascalCall((menuprocp)STARH(defproc), CTOP_mdef0, mess, themenu, menrect, hit, which); });
        }
    }
}

intptr_t ROMlib_mbdfcall(INTEGER msg, INTEGER param1, intptr_t native_param2)
{
    Handle defproc;
    intptr_t retval;
    int32 param2 = (int32)native_param2;
    mbdfprocp mp;
    mlhandle menulist;
    int mbdf_flags;

    defproc = MR(MBDFHndl);
    menulist = MENULIST;
    if (menulist && STARH(menulist))
        mbdf_flags = Hx(menulist, mufu) & 7;
    else {
        fprintf(stderr, "trace: ROMlib_mbdfcall missing MenuList handle=%p msg=%d\n", (void*)menulist, msg);
        fflush(stderr);
        mbdf_flags = 0;
    }

    switch (msg) {
    case mbSave:
    case mbMenuRgn:
        param2 = US_TO_SYN68K(native_param2);
        break;
    default:
        break;
    }

    if (HIDDEN_RAW(*(defproc)) == NULL)
        LoadResource(defproc);

    mp = (mbdfprocp)STARH(defproc);
    fprintf(stderr, "trace: ROMlib_mbdfcall msg=%d defproc=%p data=%p P_mbdf0=%p C_mbdf0=%p\n", msg, (void*)defproc,
            (void*)mp, (void*)P_mbdf0, (void*)C_mbdf0);
    fflush(stderr);

    if (mp == P_mbdf0 || mbdf_flags == 0
#if defined(CYGWIN32) || defined(__MINGW32__)
        || TRUE
#endif
    ) {
        fprintf(stderr, "trace: ROMlib_mbdfcall native begin\n");
        fflush(stderr);
        retval = C_mbdf0(mbdf_flags, msg, param1, param2);
        fprintf(stderr, "trace: ROMlib_mbdfcall native complete retval=%ld\n", (long)retval);
        fflush(stderr);
    }
    else {
        fprintf(stderr, "trace: ROMlib_mbdfcall emulated begin\n");
        fflush(stderr);
        ROMlib_hook(menu_mbdfnumber);
        LOCK_HANDLE_EXCURSION_1(
            defproc, { retval = CToPascalCall(STARH(defproc), CTOP_mbdf0, mbdf_flags, msg, param1, param2); });
        fprintf(stderr, "trace: ROMlib_mbdfcall emulated complete retval=%ld\n", (long)retval);
        fflush(stderr);
    }

    switch (msg) {
    case mbRect:
    case mbMenuRgn:
        retval = (intptr_t)SYN68K_TO_US((uint32)retval);
        break;
    default:
        break;
    }

    return retval;
}
