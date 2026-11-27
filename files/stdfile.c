/*+===================================================================
  File:      stdfile.c

  Summary:   Implements classic file dialogs with masked icons, resizable lists and host-picker fallback.

  Classes:   None.

  Functions: C_ROMlib_filebox, C_ROMlib_stdffilt, C_ROMlib_stdftrack, ROMlib_host_fsspec, ROMlib_init_stdfile,
             ROMlib_choose_catalog, adjustdrivebutton, futzwithdosdisks, keyarrow, linuxfloppy_open, spfcommon

  Origin:    Existing repository source in executor/src/files; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/*
 * NOTE: currently we only do an automount when crossing a filesystem.
 *	 This is might be bad:  currently we're seeing an error when
 *	 we try to open "/tmp" on roland, because 4 has already been
 *	 specified as "/Net" from iclone.  I'm really not convinced
 *	 that the proper solution is to do an automount, though.
 */

/* Copyright 1987 - 1997 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in StdFilePkg.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"

#if defined(LINUX)
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/cdrom.h>
#endif

#include "DialogMgr.h"
#include "AliasMgr.h"
#include "Iconutil.h"
#include "FileMgr.h"
#include "EventMgr.h"
#include "StdFilePkg.h"
#include "ControlMgr.h"
#include "MenuMgr.h"
#include "ResourceMgr.h"
#include "ToolboxEvent.h"
#include "ToolboxUtil.h"
#include "MemoryMgr.h"
#include "OSUtil.h"
#include "ScriptMgr.h"

#include "rsys/cquick.h"
#include "rsys/ctl.h"
#include "rsys/wind.h"
#include "rsys/itm.h"
#if defined(SDL)
#include "windowprefs.h"
#include "sdlevents.h"
#endif
#include "rsys/hfs.h"
#include "rsys/file.h"
#include "rsys/stdfile.h"
#include "rsys/arrowkeys.h"
#include "rsys/glue.h"
#include "rsys/resource.h"
#include "rsys/pstuff.h"
#include "rsys/hfs.h"
#include "rsys/time.h"
#include "rsys/flags.h"
#include "rsys/tempalloc.h"
#include "rsys/hook.h"
#include "rsys/toolevent.h"
#include "rsys/string.h"
#include "rsys/dcache.h"
#include "rsys/menu.h"
#include "rsys/diskimage.h"
#include "rsys/user_profile.h"
#include "rsys/hostdialog.h"
#include "rsys/filetypemap.h"
#include "rsys/ecf.h"
#include "rsys/fileicon.h"
#include "rsys/application_registry.h"
#include "../finder-metadata/include/finder_metadata/icons.h"

#include "rsys/romaw.h"

#include "rsys/osevent.h"

PUBLIC int nodrivesearch_p = FALSE;

#if defined(CYGWIN32)
#include "dosdisk.h"
#include "aspi.h"
#include "rsys/checkpoint.h"
#endif

#include "rsys/print.h"
#include "rsys/system_error.h"

typedef union {
    SFReply* oreplyp;
    StandardFileReply* nreplyp;
} reply_u;

typedef union {
    ProcPtr oflfilef;
    FileFilterYDProcPtr cflfilef;
} file_filter_u;

typedef union {
    ProcPtr ofilterp;
    ModalFilterYDProcPtr cfilterp;
} filter_u;

typedef union {
    ProcPtr odh;
    DlgHookYDProcPtr cdh;
} dialog_hook_u;

struct flinfostr {
    Size floffs;
    INTEGER flicns;
    INTEGER volume;
    uint32 type, creator;
    LONGINT resource_size;
    INTEGER flags;
    boolean_t icon_loaded;
    Handle icon_suite;
};

typedef struct {
    sf_flavor_t flavor;
    ROMlib_catalog_choice choice;
    reply_u flreplyp;
    ControlHandle flsh;
    INTEGER flnmfil;
    INTEGER flnmlin;
    INTEGER fllinht;
    INTEGER flascent;
    INTEGER flsel;
    Rect flrect;
    Rect flcurdirrect;
    Handle flinfo;
    Handle flstrs;
    file_filter_u flfilef;
    INTEGER flnumt;
    OSType* fltl;
    ControlHandle flch;
    INTEGER flgraynondirs;
    Str255 flcurdirname;
    filter_u magicfp;
    INTEGER fl_cancel_item;
    UNIV Ptr mydata;
    boolean_t desktop;
    boolean_t desktop_files;
    INTEGER selected_volume;
    INTEGER minimum_width, minimum_height;
    INTEGER list_item;
    Handle volume_suite;
    INTEGER icon_volume;
    Handle folder_suite, desktop_suite;
} fltype;

static void select_file_desktop(fltype* state);

A1(PRIVATE, INTEGER, movealert, INTEGER, id)
{
    Handle h;
    INTEGER dh, dv;
    Rect* rp;

    h = GetResource(TICK("ALRT"), id);
    if (!STARH(h))
        LoadResource(h);
    rp = (Rect*)STARH(h);
    dh = CW(rp->right) - CW(rp->left);
    dv = CW(rp->bottom) - CW(rp->top);
    rp->left = CWC(150);
    rp->top = CWC(30);
    rp->right = CW(150 + dh);
    rp->bottom = CW(30 + dv);
    return (Alert(id, (ProcPtr)0));
}

static int custom_volume_icon(INTEGER volume, FinderIcon* icon)
{
    FSSpec spec = {0};
    spec.vRefNum = CW(volume);
    spec.parID = CLC(2);
    memcpy(spec.name,
           "\x05"
           "Icon\r",
           6);
    INTEGER saved = C_CurResFile();
    BOOLEAN loading = ResLoad;
    C_SetResLoad(TRUE);
    INTEGER file = C_FSpOpenResFile(&spec, fsRdPerm);
    int found = FALSE;
    if (file != -1) {
        static const size_t offsets[] = {offsetof(FinderIcon, mono),   offsetof(FinderIcon, small),
                                         offsetof(FinderIcon, color4), offsetof(FinderIcon, color8),
                                         offsetof(FinderIcon, small4), offsetof(FinderIcon, small8)};
        memset(icon, 0, sizeof *icon);
        for (unsigned i = 0; i < 6; ++i) {
            Handle resource = C_Get1Resource(finder_icon_types[i], -16455);
            if (resource && GetHandleSize(resource) >= finder_icon_sizes[i]) {
                memcpy((char*)icon + offsets[i], STARH(resource), finder_icon_sizes[i]);
                icon->present |= 1u << i;
            }
        }
        found = !!(icon->present & 3); /* A monochrome mask is required. */
        C_CloseResFile(file);
    }
    C_UseResFile(saved);
    C_SetResLoad(loading);
    if (found)
        return TRUE;

    memcpy(spec.name,
           "\x10"
           ".VolumeIcon.icns",
           17);
    INTEGER reference;
    if (C_FSpOpenDF(&spec, fsRdPerm, &reference) == noErr) {
        LONGINT length;
        INTEGER ref = CW(reference);
        if (GetEOF(ref, &length) == noErr && CL(length) >= 8 && CL(length) <= 32 * 1024 * 1024) {
            unsigned char* bytes = malloc(CL(length));
            if (bytes) {
                LONGINT count = length;
                if (FSRead(ref, &count, (Ptr)bytes) == noErr && count == length)
                    found = finder_parse_icns_icon(bytes, CL(length), icon);
                free(bytes);
            }
        }
        FSClose(ref);
    }
    return found;
}

static Handle suite_from_finder_icon(const FinderIcon* icon)
{
    Handle encoded = NULL;
    if (C_NewIconSuite(&encoded) != noErr)
        return NULL;
    Handle suite = MR(encoded);
    const unsigned char* data[] = {icon->mono, icon->small, icon->color4, icon->color8, icon->small4, icon->small8};
    for (unsigned i = 0; i < 6; ++i)
        if (icon->present & (1u << i)) {
            Handle copy = NewHandle(finder_icon_sizes[i]);
            if (!copy)
                continue;
            memcpy(STARH(copy), data[i], finder_icon_sizes[i]);
            if (C_AddIconToSuite(copy, suite, finder_icon_types[i]) != noErr)
                DisposHandle(copy);
        }
    return suite;
}

static Handle volume_icon_suite(INTEGER volume)
{
    FinderIcon icon;
    HVCB* vcb = ROMlib_vcbbyvrn(volume);
    if (!vcb)
        return NULL;
    if (!custom_volume_icon(volume, &icon)) {
        unsigned kind = ROMlib_image_volume_kind(vcb);
        if (!kind)
            kind = vcb->vcbCTRef ? FINDER_VOLUME_IMAGE : romaw_host_volume_kind(((VCBExtra*)vcb)->unixname);
        if (vcb->vcbVN[0] == 5 && !memcmp(vcb->vcbVN + 1, "ROMAW", 5))
            kind = FINDER_VOLUME_ROMAW;
        if (!finder_volume_icon(kind, &icon))
            return NULL;
    }
    return suite_from_finder_icon(&icon);
}

static boolean_t draw_volume_icon(fltype* state, INTEGER volume)
{
    if (state->icon_volume != volume) {
        if (state->volume_suite)
            C_DisposeIconSuite(state->volume_suite, TRUE);
        state->volume_suite = volume_icon_suite(volume);
        state->icon_volume = volume;
    }
    if (!state->volume_suite)
        return FALSE;
    Rect bounds;
    bounds.top = PORT_PEN_LOC(thePort).v;
    bounds.left = PORT_PEN_LOC(thePort).h;
    bounds.bottom = CW(CW(bounds.top) + 16);
    bounds.right = CW(CW(bounds.left) + 16);
    return C_PlotIconSuite(&bounds, atNone, ttNone, state->volume_suite) == noErr;
}

static Handle file_row_icon(struct flinfostr* entry, StringPtr name)
{
    if (entry->icon_loaded)
        return entry->icon_suite;
    entry->icon_loaded = TRUE;
    FSSpec spec = {0}, application;
    if (name[0] >= sizeof spec.name)
        return NULL;
    spec.vRefNum = CW(-CW(SFSaveDisk));
    spec.parID = CurDirStore;
    memcpy(spec.name, name, name[0] + 1);
    FinderIcon icon;
    boolean_t custom = !!(entry->flags & 0x0400); /* kHasCustomIcon */
    int found = entry->resource_size && ROMlib_read_file_icon(&spec, entry->creator, entry->type, custom, &icon);
    if (!found && custom && entry->resource_size)
        found = ROMlib_read_file_icon(&spec, entry->creator, entry->type, FALSE, &icon);
    if (!found && entry->type != TICK("APPL") &&
        ROMlib_application_spec_cached(entry->creator, entry->type, &application) == noErr) {
        FInfo info;
        if (C_FSpGetFInfo(&application, &info) == noErr)
            found = ROMlib_read_file_icon(&application, CL(info.fdCreator), entry->type, FALSE, &icon);
    }
    if (!found && ((name[0] >= 4 && !strncasecmp((char*)name + name[0] - 3, ".ico", 4)) ||
                   (name[0] >= 5 && !strncasecmp((char*)name + name[0] - 4, ".icns", 5))))
        found = ROMlib_read_icon_data(&spec, &icon, NULL);
    if (!found)
        found = finder_type_icon(entry->type, &icon);
    if (found)
        entry->icon_suite = suite_from_finder_icon(&icon);
    return entry->icon_suite;
}

static void dispose_file_row_icons(fltype* state)
{
    SignedByte saved = HGetState(state->flinfo);
    HLock(state->flinfo);
    struct flinfostr* entries = (struct flinfostr*)STARH(state->flinfo);
    for (int i = 0; i < state->flnmfil; ++i)
        if (entries[i].icon_suite)
            C_DisposeIconSuite(entries[i].icon_suite, TRUE);
    HSetState(state->flinfo, saved);
}

A1(PRIVATE, void, drawminiicon, INTEGER, icon)
{
    static const INTEGER resources[] = {-4000, -3999, -3998, -3997, -3996, -3995};
    Handle encoded = NULL;
    Rect r;

    if ((unsigned)icon >= NELEM(resources))
        return;
    Handle suite;
    if (icon == MICONOFOLDER) {
        FinderIcon artwork;
        finder_location_icon(FALSE, &artwork);
        suite = suite_from_finder_icon(&artwork);
    }
    else {
        if (C_GetIconSuite(&encoded, resources[icon], svAllAvailableData) != noErr)
            return;
        suite = MR(encoded);
    }
    if (!suite)
        return;
    r.top = PORT_PEN_LOC(thePort).v;
    r.left = PORT_PEN_LOC(thePort).h;
    r.bottom = CW(CW(r.top) + 16);
    r.right = CW(CW(r.left) + 16);
    /* SICN strips contain only image bits and discard the artwork's mask. */
    C_PlotIconSuite(&r, atNone, ttNone, suite);
    C_DisposeIconSuite(suite, icon == MICONOFOLDER);
}

/*
 * NOTE:  the Rect pointed to below has a normal top, left and right,
 *	  BUT the bottom is the amount to add to the top for drawing
 *	  text, not the real bottom.
 */

static Rect file_icon_bounds(const Rect* row)
{
    Rect bounds = {row->top, CW(CW(row->left) + 2), CW(CW(row->top) + 16), CW(CW(row->left) + 18)};
    return bounds;
}

static void drawinboxwithicon(StringPtr str, Rect* rp, INTEGER icon, fltype* volume_state, INTEGER volume,
                              Handle row_suite)
{
    Rect r;
    INTEGER width, strlen, strwidths[255], lengthavail, *widp;
    static char* ellipsis = "\003...";
    Rect icon_bounds = file_icon_bounds(rp);

    /*
   * Note:  the ellipsis code is a bit shabby, not truncating at a character
   *	      boundary, nor leaving space on either side, but for now, it's
   *	      ok.
   */

    MoveTo(CW(rp->left) + 2, CW(rp->top));
    if (row_suite && C_PlotIconSuite(&icon_bounds, atNone, ttNone, row_suite) == noErr) {
        /* The suite's mask preserves the existing background. */
    }
    else if (volume_state && (icon == -1 || icon == -2)) {
        Handle* suite = icon == -2 ? &volume_state->desktop_suite : &volume_state->folder_suite;
        if (!*suite) {
            FinderIcon artwork;
            finder_location_icon(icon == -2, &artwork);
            *suite = suite_from_finder_icon(&artwork);
        }
        Rect bounds = file_icon_bounds(rp);
        if (*suite)
            C_PlotIconSuite(&bounds, atNone, ttNone, *suite);
    }
    else if (!volume_state || !draw_volume_icon(volume_state, volume))
        drawminiicon(icon);
    MoveTo(CW(rp->left) + 2 + 16 + 3, CW(rp->top) + CW(rp->bottom) - 1); /* see note above */
    r.left = rp->left;
    r.right = rp->right;
    r.top = rp->top;
    r.bottom = CWC(32766);
    RgnHandle saved_clip = NewRgn(), text_clip = NewRgn();
    GetClip(saved_clip);
    RectRgn(text_clip, &r);
    SectRgn(saved_clip, text_clip, text_clip);
    SetClip(text_clip);
    strlen = *str;
    MeasureText(strlen, (Ptr)(str + 1), (Ptr)strwidths);
    lengthavail = CW(rp->right) - (CW(rp->left) + 2 + 16 + 3);
    if (CW(strwidths[strlen]) > lengthavail) {
        width = StringWidth((StringPtr)ellipsis);
        /* 4 might be the space on the right of the ellipsis. */
        /* TODO: figure out exactly what the number is. */
        lengthavail -= (width + 4);
        widp = strwidths;
        while (CW(*++widp) < lengthavail)
            DrawChar(*++str);
        DrawString((StringPtr)ellipsis);
    }
    else
        DrawString(str);
    SetClip(saved_clip);
    DisposeRgn(text_clip);
    DisposeRgn(saved_clip);
}

A2(PRIVATE, void, safeflflip, fltype*, f, INTEGER, sel)
{
    Rect r;
    INTEGER fltop = GetCtlValue(f->flsh);

    if (sel >= fltop && sel < fltop + f->flnmlin) {
        r.left = f->flrect.left;
        r.right = f->flrect.right;
        r.top = CW(CW(f->flrect.top) + (sel - fltop) * f->fllinht);
        r.bottom = CW(CW(r.top) + f->fllinht);
        if (EmptyRgn(PPR(((WindowPeek)thePort)->updateRgn))) /* stuff to draw? */
            InvertRect(&r);                                  /* no: we can flip */
        else
            InvalRect(&r); /* yes: flip later */
    }
}

#define GRAYBIT (1 << 5)
#define ICONMASK (~GRAYBIT)

A3(PRIVATE, void, flupdate, fltype*, f, INTEGER, st, INTEGER, n)
{
    INTEGER i;
    struct flinfostr* ip;
    INTEGER sel = f->flsel;
    INTEGER fltop;
    Rect r;

    fltop = GetCtlValue(f->flsh);
    r.top = CW(CW(f->flrect.top) + (st - fltop) * f->fllinht);
    r.bottom = CW(f->flascent);
    r.left = f->flrect.left;
    r.right = f->flrect.right;

    RgnHandle saved_clip = NewRgn(), list_clip = NewRgn();
    GetClip(saved_clip);
    RectRgn(list_clip, &f->flrect);
    SectRgn(saved_clip, list_clip, list_clip);
    SetClip(list_clip);

    /* Loading a volume's icon resources may compact the guest heap. */
    SignedByte info_state = HGetState(f->flinfo);
    HLock(f->flinfo);
    HLock((Handle)f->flstrs);
    ip = ((struct flinfostr*)STARH((Handle)f->flinfo)) + st;
    for (i = st; i < st + n && i < fltop + f->flnmlin && i < f->flnmfil; i++) {
        Handle suite = NULL;
        if (!ip->volume && (ip->flicns & ICONMASK) != MICONCFOLDER) {
            StringPtr name = (StringPtr)STARH(f->flstrs) + ip->floffs;
            suite = file_row_icon(ip, name);
        }
        drawinboxwithicon((StringPtr)(((char*)STARH((Handle)f->flstrs)) + ip->floffs), &r, ip->flicns & ICONMASK,
                          ip->volume ? f : NULL, ip->volume, suite);
        if (ip->flicns & GRAYBIT) {
            r.bottom = CW(CW(r.top) + f->fllinht);
            PenMode(notPatBic);
            PenPat(gray);
            PaintRect(&r);
            PenPat(black);
            PenMode(patCopy);
            r.bottom = CW(f->flascent);
        }
        ip++;
        r.top = CW(CW(r.top) + (f->fllinht));
    }
    HUnlock((Handle)f->flstrs);
    HSetState(f->flinfo, info_state);
    if (sel >= st && sel < st + n)
        safeflflip(f, sel);
    SetClip(saved_clip);
    DisposeRgn(list_clip);
    DisposeRgn(saved_clip);
}

A3(PRIVATE, void, flscroll, fltype*, f, INTEGER, from, INTEGER, to)
{
    if (from != to) {
        /* Redraw the cached rows, including the fractional row at the bottom.
           Moving bitmap strips can leave parts of masked icons behind. */
        EraseRect(&f->flrect);
        flupdate(f, to, f->flnmlin);
    }
}

#define CTLFL(sh) WINDFL(PPR(STARH((sh))->contrlOwner))

/*
 * this hack is necessary because Excel 4 can bring up a dialog on top of
 * stdfile at a time when the refcon has been set to 'stdf'.
 */

static LONGINT emergency_save_ref_con;

PRIVATE fltype* WINDFL(void* dp)
{
    fltype* retval;

    uint32 refcon = CL(((WindowPeek)dp)->refCon);
    if (refcon == TICK("stdf"))
        refcon = emergency_save_ref_con;
    retval = (fltype*)SYN68K_TO_US(refcon);
    return retval;
}

static void update_file_scrollbar(fltype* state)
{
    int maximum = MAX(0, state->flnmfil - state->flnmlin);
    C_SetCtlMax(state->flsh, maximum);
    C_HiliteControl(state->flsh, maximum ? 0 : 255);
}

P2(PUBLIC, pascal void, ROMlib_stdftrack, ControlHandle, sh, INTEGER, part)
{
    const uint32 min_between_scroll_msecs = 100;
    static uint32 last_scroll_msecs;
    uint32 current_msecs;
    int16 from, pg;

    current_msecs = msecs_elapsed();

    if (current_msecs - min_between_scroll_msecs < last_scroll_msecs)
        return;
    else
        last_scroll_msecs = current_msecs;

    from = GetCtlValue(sh);
    pg = CTLFL(sh)->flnmlin - 1;
    switch (part) {
    case inUpButton:
        SetCtlValue(sh, from - 1);
        break;
    case inDownButton:
        SetCtlValue(sh, from + 1);
        break;
    case inPageUp:
        SetCtlValue(sh, from - pg);
        break;
    case inPageDown:
        SetCtlValue(sh, from + pg);
        break;
    }
    flscroll(CTLFL(sh), from, GetCtlValue(sh));
}

PRIVATE INTEGER cachedvrn = 32767;
PRIVATE INTEGER savesel = -1;
PRIVATE LONGINT oldticks = -1000;
PRIVATE LONGINT lastkeydowntime = 0;
PRIVATE Str255 prefix = {0};
PRIVATE Handle holdstr;

PUBLIC void ROMlib_init_stdfile(void)
{
    cachedvrn = 32767;
    savesel = -1;
    oldticks = -1000;
    lastkeydowntime = 0;
    prefix[0] = 0;
    holdstr = 0;
}

A2(PRIVATE, StringPtr, getdiskname, BOOLEAN*, ejectablep, boolean_t*, writablep)
{
    static BOOLEAN ejectable;
    static boolean_t writable;
    static Str255 retval;
    ParamBlockRec pbr;

    if (SFSaveDisk != cachedvrn) {
        OSErr err;

        FIELD_ASSIGN_RAW(pbr.volumeParam.ioNamePtr, RM(&retval[0]));
        pbr.volumeParam.ioVolIndex = 0;
        pbr.volumeParam.ioVRefNum = CW(-CW(SFSaveDisk));
        err = PBGetVInfo(&pbr, FALSE);
        cachedvrn = SFSaveDisk;
        if (err == noErr) {
            ejectable = !(pbr.volumeParam.ioVAtrb & CWC(VNONEJECTABLEBIT));
            writable = !(pbr.volumeParam.ioVAtrb & CWC(VHARDLOCKBIT | VSOFTLOCKBIT));
            if (writable && pbr.volumeParam.ioVFrBlk == 0)
                writable = FALSE;
        }
        else {
            warning_unexpected("surprising PBGetVInfo retval = %d\n", err);
            ejectable = FALSE;
            writable = TRUE;
        }
    }
    if (ejectablep)
        *ejectablep = ejectable;
    if (writablep)
        *writablep = writable;
    return retval;
}

A1(PRIVATE, void, drawjobberattop, DialogPeek, dp)
{
    INTEGER icon;
    Rect* rp;
    fltype* flp;
    INTEGER savebottom;
    BOOLEAN ejectable;
    PenState ps;

    GetPenState(&ps);
    PenNormal();
    if (CL(CurDirStore) == 2) {
#if 1
        /* TODO: ask cliff about a better way to do this */
        /* unused = */ getdiskname(&ejectable, NULL);
        icon = ejectable ? MICONFLOPPY : MICONDISK;
#else  /* 0 */
        icon = MICONDISK;
#endif /* 0 */
    }
    else
        icon = MICONOFOLDER;
    flp = WINDFL(dp);
    rp = &flp->flcurdirrect;
    savebottom = rp->bottom;
    rp->bottom = CW(flp->flascent);
    rp->left = CW(CW(rp->left) + (2));
    drawinboxwithicon(flp->flcurdirname, rp,
                      flp->desktop           ? -2
                      : CL(CurDirStore) == 2 ? icon
                                             : -1,
                      flp, -CW(SFSaveDisk), NULL);
    rp->left = CW(CW(rp->left) - (2));
    rp->bottom = savebottom;
    FrameRect(rp);
    MoveTo(CW(rp->left) + 1, CW(rp->bottom));
    LineTo(CW(rp->right), CW(rp->bottom));
    LineTo(CW(rp->right), CW(rp->top) + 1);
    SetPenState(&ps);
}

A1(PRIVATE, LONGINT, getdirid, StringPtr, fname)
{
    CInfoPBRec hpb;
    LONGINT retval;
    OSErr err;

    FIELD_ASSIGN_RAW(hpb.dirInfo.ioCompletion, 0);
    FIELD_ASSIGN_RAW(hpb.dirInfo.ioNamePtr, RM(fname));
    hpb.dirInfo.ioVRefNum = CW(-CW(SFSaveDisk));
    hpb.dirInfo.ioFDirIndex = CWC(0);
    hpb.dirInfo.ioDrDirID = CurDirStore;
    err = PBGetCatInfo(&hpb, FALSE);
    if (err == noErr)
        retval = CL(hpb.dirInfo.ioDrDirID);
    else {
        warning_unexpected("PBGetCatInfo return value err = %d\n", err);
        retval = 2; /* known good id ... an error might be better here */
    }
    return retval;
}

#define MAXPREFIX 64

PRIVATE void set_type_and_name(fltype* f, OSType type, StringPtr name)
{
    switch (f->flavor) {
    case original_sf:
        f->flreplyp.oreplyp->fType = CL(type);
        str31assign(f->flreplyp.oreplyp->fName, name);
        break;
    case new_sf:
    case new_custom_sf:
        f->flreplyp.nreplyp->sfType = CL(type);
        str31assign(f->flreplyp.nreplyp->sfFile.name, name);
        f->flreplyp.nreplyp->sfIsFolder = !!type;
        break;
    default:
        warning_unexpected("flavor = %d", f->flavor);
        break;
    }
}

A2(PRIVATE, void, settype, fltype*, f, INTEGER, newsel)
{
    StringPtr ip;

    ip = (StringPtr)((char*)STARH((Handle)f->flstrs)) + ((struct flinfostr*)STARH((Handle)f->flinfo))[newsel].floffs;
    struct flinfostr* entry = (struct flinfostr*)STARH(f->flinfo) + newsel;
    f->selected_volume = entry->volume;
    if (entry->volume)
        set_type_and_name(f, 2, (StringPtr) "");
    else if (entry->flicns == MICONCFOLDER)
        set_type_and_name(f, getdirid(ip), (StringPtr) "");
    else
        set_type_and_name(f, 0, ip);
}

A2(PRIVATE, INTEGER, flwhich, fltype*, f, Point, p)
{
    INTEGER retval;
    INTEGER bump, from;

    if (!PtInRect(p, &f->flrect)) {
        bump = 0;
        if (p.v < CW(f->flrect.top))
            bump = -1;
        else if (p.v >= CW(f->flrect.bottom))
            bump = 1;
        if (bump) {
            from = GetCtlValue(f->flsh);
            SetCtlValue(f->flsh, from + bump);
            flscroll(f, from, GetCtlValue(f->flsh));
        }
        /*-->*/ return (-1);
    }
    retval = (p.v - CW(f->flrect.top)) / f->fllinht + GetCtlValue(f->flsh);
    if (retval >= f->flnmfil || ((struct flinfostr*)STARH((Handle)f->flinfo))[retval].flicns & GRAYBIT)
        /*-->*/ retval = -1;
    return (retval);
}

A3(PRIVATE, void, flmouse, fltype*, f, Point, p, ControlHandle, ch)
{
    INTEGER newsel;
    EventRecord evt;

    evt.where = p;
    do {
        GlobalToLocal(&evt.where);
        p.h = CW(evt.where.h);
        p.v = CW(evt.where.v);
        if ((newsel = flwhich(f, p)) != f->flsel) {
            if (f->flsel != -1) {
                safeflflip(f, f->flsel);
                if (newsel == -1) {
                    if (!f->flgraynondirs)
                        HiliteControl(ch, 255);
                    set_type_and_name(f, 0, (StringPtr) "");
                }
            }
            if (newsel != -1) {
                safeflflip(f, newsel);
                if (f->flsel == -1)
                    HiliteControl(ch, 0);
                settype(f, newsel);
            }
            f->flsel = newsel;
        }
    } while (!GetNextEvent(mUpMask, &evt));
}

typedef pascal BOOLEAN (*filtp)(DialogPtr dp, EventRecord* evp, INTEGER* ith);

#define CALLFILTERPROC(dp, evt, ith, fp) ROMlib_CALLFILTERPROC((dp), (evt), (ith), (filtp)(fp))

A4(static inline, BOOLEAN, ROMlib_CALLFILTERPROC, DialogPtr, dp, EventRecord*, evtp, INTEGER, *ith, filtp, fp)
{
    BOOLEAN retval;
    LONGINT save_ref_con;

    save_ref_con = GetWRefCon(dp);
    SetWRefCon(dp, TICK("stdf"));
    ROMlib_hook(stdfile_filtnumber);
    HOOKSAVEREGS();
    retval = CToPascalCall(fp, CTOP_SectRect, dp, evtp, ith);
    HOOKRESTOREREGS();
    SetWRefCon(dp, save_ref_con);
    return retval;
}

typedef pascal BOOLEAN (*custom_filtp)(DialogPtr dp, EventRecord* evp, INTEGER* ith, UNIV Ptr data);

#define CALL_NEW_FILTER_PROC(dp, evt, ith, data, fp) \
    ROMlib_CALL_NEW_FILTER_PROC((dp), (evt), (ith), (data), (custom_filtp)(fp))

PRIVATE BOOLEAN ROMlib_CALL_NEW_FILTER_PROC(DialogPtr dp, EventRecord* evtp, INTEGER* ith, UNIV Ptr data,
                                            custom_filtp fp)
{
    BOOLEAN retval;
    LONGINT save_ref_con;

    save_ref_con = GetWRefCon(dp);
    /* Iconographer's background update handler treats the front window's
     refCon as an application object pointer. An empty refCon tells it that
     the picker has no such object. */
    boolean_t iconographer_background_update =
        ROMlib_creator == TICK("Mngl") && evtp->what == CWC(updateEvt) && CL(evtp->message) != US_TO_SYN68K(dp);
    SetWRefCon(dp, iconographer_background_update ? 0 : TICK("stdf"));
    ROMlib_hook(stdfile_filtnumber);
    HOOKSAVEREGS();
    retval = CToPascalCall(fp, CTOP_unused_stdfile, dp, evtp, ith, data);
    HOOKRESTOREREGS();
    SetWRefCon(dp, save_ref_con);
    return retval;
}

A1(PRIVATE, void, getcurname, fltype*, f)
{
    int w;
    CInfoPBRec hpb;
    Rect* r;
    OSErr err;

    FIELD_ASSIGN_RAW(hpb.dirInfo.ioCompletion, 0);
    FIELD_ASSIGN_RAW(hpb.dirInfo.ioNamePtr, RM(&f->flcurdirname[0]));
    f->flcurdirname[0] = 0;
    hpb.dirInfo.ioVRefNum = CW(-CW(SFSaveDisk));
    hpb.dirInfo.ioFDirIndex = CWC(-1);
    hpb.dirInfo.ioDrDirID = CurDirStore;
    err = PBGetCatInfo(&hpb, FALSE);
    if (err != noErr) {
        warning_unexpected("PBGetCatInfo err = %d\n", err);
        str255_from_c_string(f->flcurdirname, "?error?");
    }
    if (f->desktop)
        str255_from_c_string(f->flcurdirname, "Desktop");
    r = &f->flrect;
    w = StringWidth(f->flcurdirname) + 2 + 16 + 3 + 2 + 2 + 4;
#if 1
    if (w > CW(r->right) - CW(r->left) + 17) {
        f->flcurdirrect.left = r->left;
        f->flcurdirrect.right = CW(CW(r->right) + 17);
    }
    else {
        f->flcurdirrect.left = CW((CW(r->left) + CW(r->right) + 17 - w) / 2 - 2);
        f->flcurdirrect.right = CW(CW(f->flcurdirrect.left) + w);
    }
#else  /* 1 */
    f->flcurdirrect.left = (CW(r->left) + CW(r->right) + 17 - w) / 2 - 2;
    f->flcurdirrect.right = CW(CW(f->flcurdirrect.left) + w);
#endif /* 1 */
}

A1(PRIVATE, void, flfinit, fltype*, fp)
{
    dispose_file_row_icons(fp);
    if (fp->volume_suite)
        C_DisposeIconSuite(fp->volume_suite, TRUE);
    if (fp->folder_suite)
        C_DisposeIconSuite(fp->folder_suite, TRUE);
    if (fp->desktop_suite)
        C_DisposeIconSuite(fp->desktop_suite, TRUE);
    DisposeControl(fp->flsh);
    DisposHandle((Handle)fp->flinfo);
    DisposHandle((Handle)fp->flstrs);
}

A3(PRIVATE, void, flinsert, fltype*, f, StringPtr, p, INTEGER, micon)
{
    struct flinfostr finfo = {0};

    finfo.floffs = GetHandleSize((Handle)f->flstrs);
    finfo.flicns = micon;
    finfo.volume = 0;
    PtrAndHand((Ptr)p, (Handle)f->flstrs, (LONGINT)U(p[0]) + 1);
    PtrAndHand((Ptr)&finfo, (Handle)f->flinfo, (LONGINT)sizeof(struct flinfostr));
    ++f->flnmfil;
}

A3(PRIVATE, int, typeinarray, OSType, ft, INTEGER, numt, OSType*, tl)
{
    OSType* ostp = tl;

    while (numt--)
        if (ft == *ostp++)
            /*-->*/ return (TRUE);
    return (FALSE);
}

/*
 * NOTE: stdfcmp is called from qsort, so we can't smash d2.
 */

/* ip1 and ip2 would really be "void *" in ANSI C */

A2(PRIVATE, LONGINT, stdfcmp, char*, ip1, char*, ip2)
{
    struct flinfostr *fp1, *fp2;
    LONGINT retval;

    fp1 = (struct flinfostr*)ip1;
    fp2 = (struct flinfostr*)ip2;
    retval = ROMlib_strcmp((StringPtr)(((char*)STARH((Handle)holdstr)) + fp1->floffs),
                           (StringPtr)(((char*)STARH((Handle)holdstr)) + fp2->floffs));
    return retval;
}

typedef pascal BOOLEAN (*filefiltp)(ParmBlkPtr pbp);

#define CALLFILEFILT(pbp, fp) ROMlib_CALLFILEFILT((pbp), (filefiltp)(fp))

A2(static inline, BOOLEAN, ROMlib_CALLFILEFILT, ParmBlkPtr, pbp, filefiltp, fp)
{
    BOOLEAN retval;

    ROMlib_hook(stdfile_filefiltnumber);
    HOOKSAVEREGS();
    retval = CToPascalCall(fp, CTOP_SystemEvent, pbp);
    HOOKRESTOREREGS();
    return retval;
}

typedef pascal BOOLEAN (*custom_file_filtp)(ParmBlkPtr pbp, UNIV Ptr data);

#define CALL_CUSTOM_FILE_FILT(pbp, data, fp) ROMlib_CALL_CUSTOM_FILE_FILT((pbp), (data), (custom_file_filtp)(fp))

PRIVATE BOOLEAN ROMlib_CALL_CUSTOM_FILE_FILT(ParmBlkPtr pbp, UNIV Ptr data, custom_file_filtp fp)
{
    BOOLEAN retval;

    ROMlib_hook(stdfile_filefiltnumber);
    HOOKSAVEREGS();
    retval = CToPascalCall(fp, CTOP_GetAuxCtl, pbp, data);
    HOOKRESTOREREGS();
    return retval;
}

PRIVATE boolean_t passes_filter(fltype* f, CInfoPBRec* cinfop, INTEGER numt)
{
    boolean_t retval;

    if (cinfop->hFileInfo.ioFlFndrInfo.fdFlags & CWC(0x4000))
        return FALSE;

    switch (f->flavor) {
    case original_sf:
        /* NOTE: the code for original_sf was changed after we ran into
 some trouble with Accordance.  It's not clear whether or not
 the new code here should also be used for new_sf.  */
        retval = (!f->flfilef.oflfilef || (numt == 0 && (cinfop->hFileInfo.ioFlAttrib & ATTRIB_ISADIR)) ||
                  !CALLFILEFILT((ParmBlkPtr)cinfop, f->flfilef.oflfilef));
        break;
    case new_sf:
        retval = (!f->flfilef.oflfilef || (cinfop->hFileInfo.ioFlAttrib & ATTRIB_ISADIR) ||
                  !CALLFILEFILT((ParmBlkPtr)cinfop, f->flfilef.oflfilef));
        break;
    case new_custom_sf:
        retval = (!f->flfilef.cflfilef || !CALL_CUSTOM_FILE_FILT((ParmBlkPtr)cinfop, f->mydata, f->flfilef.cflfilef));
        break;
    default:
        warning_unexpected("flavor = %d", f->flavor);
        retval = TRUE;
        break;
    }
    return retval;
}

A1(PRIVATE, void, flfill, fltype*, f)
{
    CInfoPBRec pb;
    OSErr err;
    Str255 s;
    int micon;
    CursHandle watchh;
    INTEGER errcount;
    INTEGER dirindex;

    SetCursor(STARH((watchh = GetCursor(watchCursor))));
    if (f->desktop)
        for (HVCB* volume = (HVCB*)PPR(VCBQHdr.qHead); volume; volume = (HVCB*)PPR(volume->qLink)) {
            flinsert(f, volume->vcbVN, MICONDISK);
            struct flinfostr* entry = (struct flinfostr*)STARH(f->flinfo);
            entry[f->flnmfil - 1].volume = CW(volume->vcbVRefNum);
        }

    FIELD_ASSIGN_RAW(pb.hFileInfo.ioNamePtr, RM(&s[0]));
    pb.hFileInfo.ioVRefNum = CW(-CW(SFSaveDisk));
    err = noErr;
    errcount = 0;
    for (dirindex = 1; (!f->desktop || f->desktop_files) && err != fnfErr && errcount != 3; dirindex++) {
        pb.hFileInfo.ioFDirIndex = CW(dirindex);
        pb.hFileInfo.ioDirID = CurDirStore;
        err = PBGetCatInfo(&pb, FALSE);
        if (err) {
            if (err != fnfErr) {
                warning_unexpected("PBGetCatInfo err = %d\n", err);
                ++errcount;
                /* register int d7 = err; */
            }
        }
        else {
            errcount = 0;
            if ((f->choice == romlib_choose_folder || f->choice == romlib_create_folder) &&
                !(pb.hFileInfo.ioFlAttrib & ATTRIB_ISADIR))
                continue;
            if (f->flnumt <= 0 || (pb.hFileInfo.ioFlAttrib & ATTRIB_ISADIR) ||
                typeinarray(pb.hFileInfo.ioFlFndrInfo.fdType, f->flnumt, f->fltl))
                if (passes_filter(f, &pb, f->flnumt)) {
                    if (pb.hFileInfo.ioFlAttrib & ATTRIB_ISADIR)
                        micon = MICONCFOLDER;
                    else if (pb.hFileInfo.ioFlFndrInfo.fdType == TICKX("APPL"))
                        micon = MICONAPP | f->flgraynondirs;
                    else
                        micon = MICONLETTER | f->flgraynondirs;
                    flinsert(f, PPR(pb.hFileInfo.ioNamePtr), micon);
                    if (!(pb.hFileInfo.ioFlAttrib & ATTRIB_ISADIR)) {
                        struct flinfostr* entry = (struct flinfostr*)STARH(f->flinfo) + f->flnmfil - 1;
                        entry->type = CL(pb.hFileInfo.ioFlFndrInfo.fdType);
                        entry->creator = CL(pb.hFileInfo.ioFlFndrInfo.fdCreator);
                        entry->flags = CW(pb.hFileInfo.ioFlFndrInfo.fdFlags);
                        entry->resource_size = CL(pb.hFileInfo.ioFlRLgLen);
                    }
                }
        }
    }
    update_file_scrollbar(f);
    SetCtlValue(f->flsh, 0);

    if (f->flnmfil > 0) {
        holdstr = f->flstrs;
        qsort(((struct flinfostr*)STARH((Handle)f->flinfo)), f->flnmfil, sizeof(struct flinfostr), (void*)stdfcmp);
        if (!(((struct flinfostr*)STARH((Handle)f->flinfo))[0].flicns & GRAYBIT) && !f->flgraynondirs) {
            f->flsel = 0;
            settype(f, 0);
            HiliteControl(f->flch, 0);
        }
    }
    else if (!f->flgraynondirs)
        HiliteControl(f->flch, 255);
    /* i'm not sure this is right, but there is no obvious way to
     save/restore the cursor */
    SetCursor(&arrowX);
}

/*
 * ROMlib_filebox should be renamed to reflect the fact that it also draws the
 * dotted line (whopee!)
 */

static void draw_file_grow_box(DialogPeek dialog);

static void draw_file_default_button(DialogPeek dialog)
{
    HIDDEN_Handle control;
    INTEGER kind;
    Rect bounds;
    PenState pen;

    GetDItem((DialogPtr)dialog, CW(dialog->aDefItem), &kind, &control, &bounds);
    if ((CW(kind) & 0x7f) != (ctrlItem + btnCtrl) || !HIDDEN_NATIVE(control) ||
        CW(bounds.left) >= CW(dialog->window.port.portRect.right))
        return;

    GetPenState(&pen);
    PenNormal();
    PenSize(3, 3);
    InsetRect(&bounds, -4, -4);
    ROMlib_frame_default_button(&bounds);
    SetPenState(&pen);
}

P2(PUBLIC, pascal void, ROMlib_filebox, DialogPeek, dp, INTEGER, which)
{
    HIDDEN_Handle h;
    Rect r, r2;
    INTEGER i;
    int width, strwidth, offset;
    StringPtr diskname;
    HIDDEN_Handle ejhand;
    BOOLEAN ejectable;
    PenState ps;

    GetPenState(&ps);
    PenNormal();

    HIDDEN_RAW_SET(h, 0);
    GetDItem((DialogPtr)dp, which, &i, &h, &r);
    /*    PPR (h) = CL(PPR (h)); we don't really use h */
    switch (which) {
    case getNmList:
    case putNmList:
        if (HIDDEN_NATIVE(h))
            FrameRect(&r);
        flupdate(WINDFL(dp), GetCtlValue(WINDFL(dp)->flsh), WINDFL(dp)->flnmlin);
        break;
    case getDotted:
        FillRect(&r, gray);
        break;
    case getDiskName:
        /*  case putDiskName:	getDiskName and putDiskName are the same */
        EraseRect(&r);
        width = CW(r.right) - CW(r.left);
        diskname = getdiskname(&ejectable, NULL);
        GetDItem((DialogPtr)dp, putEject, &i, &ejhand, &r2);

        HiliteControl((ControlHandle)HIDDEN_NATIVE(ejhand), 0);
        strwidth = StringWidth(diskname) + 2 + 16 + 3;
        offset = (width - strwidth) / 2;
        if (offset < 3)
            r.left = CW(CW(r.left) + (3));
        else
            r.left = CW(CW(r.left) + (offset));
        r.bottom = CW(WINDFL(dp)->flascent);
        drawinboxwithicon(diskname, &r, ejectable ? MICONFLOPPY : MICONDISK, WINDFL(dp), -CW(SFSaveDisk), NULL);
        break;
    }
    SetPenState(&ps);
    draw_file_default_button(dp);
    if (WINDFL(dp)->minimum_width)
        draw_file_grow_box(dp);
}

A2(PRIVATE, void, realcd, DialogPeek, dp, LONGINT, dir)
{
    fltype* fp;

    CurDirStore = CL(dir);
    fp = WINDFL(dp);
    dispose_file_row_icons(fp);
    SetHandleSize((Handle)fp->flinfo, (Size)0);
    SetHandleSize((Handle)fp->flstrs, (Size)0);
    fp->flnmfil = 0;
    fp->flsel = -1;
    fp->selected_volume = 0;
    set_type_and_name(fp, 0, (StringPtr) "");
    flfill(fp);
    fp->flcurdirrect.right = CW(CW(fp->flcurdirrect.right) + 1);
    fp->flcurdirrect.bottom = CW(CW(fp->flcurdirrect.bottom) + 1);
    EraseRect(&fp->flcurdirrect);
    getcurname(fp);
    fp->flcurdirrect.bottom = CW(CW(fp->flcurdirrect.bottom) - 1);
    /* don't need to do right; getcurname does */
    drawjobberattop(dp);
    C_ROMlib_filebox(dp, getDiskName);
    EraseRect(&fp->flrect);
    if (fp->flgraynondirs)
        C_ROMlib_filebox(dp, putNmList);
    else
        C_ROMlib_filebox(dp, getNmList);
}

A1(PRIVATE, LONGINT, getparent, LONGINT, dirid)
{
    OSErr err;
    CInfoPBRec cb;
    LONGINT retval;

    FIELD_ASSIGN_RAW(cb.dirInfo.ioCompletion, 0);
    FIELD_ASSIGN_RAW(cb.dirInfo.ioNamePtr, 0);
    cb.dirInfo.ioVRefNum = CW(-CW(SFSaveDisk));
    cb.dirInfo.ioFDirIndex = CWC(-1);
    cb.dirInfo.ioDrDirID = CL(dirid);
    err = PBGetCatInfo(&cb, FALSE);
    if (err == noErr)
        retval = CL(cb.dirInfo.ioDrParID);
    else {
        warning_unexpected("PBGetCatInfo return = %d\n", err);
        retval = 2;
    }
    return retval;
}

static BOOLEAN moveuponedir(fltype* state)
{
    LONGINT parent;

    parent = getparent(CL(CurDirStore));
    if (parent != CL(CurDirStore) && parent != 1) {
        CurDirStore = CL(parent);
    }
    else
        select_file_desktop(state);
    return TRUE;
}

BOOLEAN
keyarrow(fltype* fl, INTEGER incr) /* -1: up, 1: down */
{
    INTEGER nsel, oldval, newval;
    struct flinfostr* flp;
    /*
   * If there's no selection we start at the top or bottom, depending on
   * whether we're going down or p
   */
    if (fl->flsel == -1)
        nsel = incr > 0 ? 0 : fl->flnmfil - 1;
    else
        nsel = fl->flsel + incr;

    /*
   * If we land on a grayed out file, advance until non-grayed
   */

    flp = (struct flinfostr*)STARH(fl->flinfo);
    for (; nsel >= 0 && nsel < fl->flnmfil && (flp[nsel].flicns & GRAYBIT); nsel += incr)
        ;

    /*
   * If we don't find a non-grayed out entry, we leave
   */
    if (nsel < 0 || nsel >= fl->flnmfil)
        /*-->*/ return FALSE;

    /*
   * Figure out what should be visible and scroll there if necessary
   */

    newval = oldval = GetCtlValue(fl->flsh);
    if (nsel < oldval)
        newval = nsel;
    else if (nsel >= oldval + fl->flnmlin)
        newval = nsel - fl->flnmlin + 1;
    if (newval != oldval) {
        SetCtlValue(fl->flsh, newval);
        flscroll(fl, oldval, newval);
    }

    /*
   * Get rid of the previously selected rectangle
   */
    if (fl->flsel != -1)
        safeflflip(fl, fl->flsel);
    /*
   * Flip the new rectangle, set up the type information and make assignment
   */
    safeflflip(fl, nsel);
    settype(fl, nsel);
    fl->flsel = nsel;
    return TRUE;
}

PRIVATE boolean_t folder_selected_p(fltype* fl)
{
    boolean_t retval;

    switch (fl->flavor) {
    case original_sf:
        retval = !!fl->flreplyp.oreplyp->fType;
        break;
    case new_sf:
    case new_custom_sf:
        retval = fl->flreplyp.nreplyp->sfIsFolder;
        break;
    default:
        warning_unexpected("flavor = %d", fl->flavor);
        retval = FALSE;
        break;
    }

    return retval;
}

PRIVATE INTEGER call_magicfp(fltype* fl, DialogPeek dp, EventRecord* evt, INTEGER* ith)
{
    INTEGER retval;

    switch (fl->flavor) {
    case original_sf:
    case new_sf:
        retval = fl->magicfp.ofilterp ? CALLFILTERPROC((DialogPtr)dp, evt, ith, fl->magicfp.ofilterp) : FALSE;
        break;
    case new_custom_sf:
        retval = fl->magicfp.cfilterp ? CALL_NEW_FILTER_PROC((DialogPtr)dp, evt, ith, fl->mydata, fl->magicfp.cfilterp)
                                      : FALSE;
        break;
    default:
        warning_unexpected("flavor = %d", fl->flavor);
        retval = FALSE;
    }

    return retval;
}

#define keydownbit 0x1000

static Rect file_grow_box(const Rect* port)
{
    Rect box = *port;
    box.left = CW(CW(box.right) - 15);
    box.top = CW(CW(box.bottom) - 15);
    return box;
}

static void draw_file_grow_box(DialogPeek dialog)
{
    PenState pen;
    Rect box = file_grow_box(&dialog->window.port.portRect);
    GetPenState(&pen);
    PenNormal();
    EraseRect(&box);
    FrameRect(&box);
    InsetRect(&box, 3, 3);
    FrameRect(&box);
    OffsetRect(&box, 2, 2);
    FrameRect(&box);
    SetPenState(&pen);
}

/* Keep the file list elastic, the action buttons at the bottom right, and
   the filename/prompt below the list. Coordinates are Macintosh byte order. */
static Rect resized_file_item(Rect bounds, const Rect* list, int item, int list_item, int cancel_item, int kind, int dx,
                              int dy)
{
    if (item == list_item) {
        bounds.right = CW(CW(bounds.right) + dx);
        bounds.bottom = CW(CW(bounds.bottom) + dy);
        return bounds;
    }
    if (item == getOpen || item == cancel_item || item == getDiskName || item == getEject || item == getDrive ||
        (list_item == sfItemFileListUser && item == sfItemNewFolderUser) || CW(bounds.left) >= CW(list->right)) {
        bounds.left = CW(CW(bounds.left) + dx);
        bounds.right = CW(CW(bounds.right) + dx);
    }
    else if (!((kind & 0x7f) >= ctrlItem && (kind & 0x7f) < statText) &&
             CW(bounds.right) - CW(bounds.left) > (CW(list->right) - CW(list->left)) / 2)
        bounds.right = CW(CW(bounds.right) + dx);
    if (item == getOpen || item == cancel_item || CW(bounds.top) >= CW(list->bottom)) {
        bounds.top = CW(CW(bounds.top) + dy);
        bounds.bottom = CW(CW(bounds.bottom) + dy);
    }
    return bounds;
}

/* Keep the title and content reachable after a guest screen-size change. */
static void fit_file_dialog(const Rect* desktop, int minimum_width, int minimum_height, int* width, int* height,
                            int* left, int* top)
{
    int available_width = CW(desktop->right) - CW(desktop->left);
    int available_height = CW(desktop->bottom) - CW(desktop->top);
    *width = MAX(minimum_width, MIN(*width, available_width));
    *height = MAX(minimum_height, MIN(*height, available_height));
    *left = MAX(CW(desktop->left), MIN(*left, CW(desktop->right) - *width));
    *top = MAX(CW(desktop->top), MIN(*top, CW(desktop->bottom) - *height));
}

static void resize_file_dialog(DialogPeek dialog, fltype* state, int width, int height)
{
    DialogPtr dp = (DialogPtr)dialog;
    int dx = width - CW(dp->portRect.right) + CW(dp->portRect.left);
    int dy = height - CW(dp->portRect.bottom) + CW(dp->portRect.top);
    INTEGER kind;
    HIDDEN_Handle handle;
    Rect list;
    GetDItem(dp, state->list_item, &kind, &handle, &list);
    Handle items = PPR(dialog->items);
    int count = CW(*(INTEGER*)STARH(items)) + 1;
    for (int item = 1; item <= count; ++item) {
        Rect bounds;
        GetDItem(dp, item, &kind, &handle, &bounds);
        kind = CW(kind);
        if (CW(bounds.left) >= CW(dp->portRect.right) || CW(bounds.top) >= CW(dp->portRect.bottom))
            continue; /* Hidden compatibility items stay outside the window. */
        bounds = resized_file_item(bounds, &list, item, state->list_item, state->fl_cancel_item, kind, dx, dy);
        if ((kind & 0x7f) >= ctrlItem && (kind & 0x7f) < statText && HIDDEN_NATIVE(handle)) {
            ControlHandle control = (ControlHandle)HIDDEN_NATIVE(handle);
            MoveControl(control, CW(bounds.left), CW(bounds.top));
            SizeControl(control, CW(bounds.right) - CW(bounds.left), CW(bounds.bottom) - CW(bounds.top));
        }
        SetDItem(dp, item, kind, HIDDEN_NATIVE(handle), &bounds);
    }
    state->flrect.right = CW(CW(state->flrect.right) + dx);
    state->flrect.bottom = CW(CW(state->flrect.bottom) + dy);
    int lines = (CW(state->flrect.bottom) - CW(state->flrect.top)) / state->fllinht;
    state->flnmlin = lines > 0 ? lines : 1;
    MoveControl(state->flsh, CW(state->flrect.right), CW(state->flrect.top) - 1);
    SizeControl(state->flsh, 16, CW(state->flrect.bottom) - CW(state->flrect.top) + 2);
    int maximum = state->flnmfil - state->flnmlin;
    if (maximum < 0)
        maximum = 0;
    int top = GetCtlValue(state->flsh);
    if (state->flsel >= 0) {
        if (state->flsel < top)
            top = state->flsel;
        else if (state->flsel >= top + state->flnmlin)
            top = state->flsel - state->flnmlin + 1;
    }
    update_file_scrollbar(state);
    SetCtlValue(state->flsh, top > maximum ? maximum : top);
    SizeWindow((WindowPtr)dp, width, height, TRUE);
    getcurname(state);
    THEPORT_SAVE_EXCURSION((GrafPtr)dp, {
        EraseRect(&dp->portRect);
        InvalRect(&dp->portRect);
    });
}

P3(PUBLIC, pascal INTEGER, ROMlib_stdffilt, DialogPeek, dp, EventRecord*, evt, INTEGER*, ith) /* handle disk insert */
{
    LONGINT ticks;
    INTEGER i, from;
    HIDDEN_ControlHandle h;
    Rect r;
    Point p;
    INTEGER t;
    fltype* fl;
    INTEGER opentoken;
    struct flinfostr *flp, *flep;
    INTEGER nsel, fltop;
    INTEGER part;
    INTEGER retval, retval2;

    fl = WINDFL(dp);
    opentoken = getOpen; /* getOpen and putSave are both 1 */
    retval = FALSE;
    switch (CW(evt->what)) {
    case keyDown:
        *ith = CW((CL(evt->message) & 0xFF) + keydownbit);
        switch (CL(evt->message) & 0xFF) {
        case NUMPAD_ENTER:
        case '\r': {
            HIDDEN_Handle item = {0};
            INTEGER kind = 0;
            ControlHandle button = fl->flch;
            if (!fl->choice) {
                GetDItem((DialogPtr)dp, CW(dp->aDefItem), &kind, &item, &r);
                if ((CW(kind) & 0x7f) == (ctrlItem + btnCtrl))
                    button = (ControlHandle)HIDDEN_NATIVE(item);
                else
                    button = NULL;
            }
            if (button && CTL_VIS(button) && U(CTL_HILITE(button)) != 255) {
                prefix[0] = 0;
                oldticks = -1000;
                *ith = CW(opentoken);
                retval = -1;
                HiliteControl(button, inButton);
                Delay((LONGINT)5, (LONGINT*)0);
                HiliteControl(button, 0);
            }
            break;
        }
        case ASCIIUPARROW:
            if (CW(evt->modifiers) & cmdKey)
                *ith = CWC(getDiskName); /* the same as putDiskName */
            else
                keyarrow(fl, -1);
            retval = -1;
            break;
        case ASCIIDOWNARROW:
            if ((CW(evt->modifiers) & cmdKey) && fl->flsel != -1 &&
                (((struct flinfostr*)STARH((Handle)fl->flinfo)) + fl->flsel)->flicns == MICONCFOLDER) {
                prefix[0] = 0;
                oldticks = -1000;
                *ith = CWC(fl->choice ? FAKEOPENDIR : opentoken);
            }
            else
                keyarrow(fl, 1);
            retval = -1;
            break;
        case '\t':
            *ith = CWC(putDrive); /* PutDrive is 6 which is also getDrive. */
            retval = -1;
            break;
        case 27:
            if (fl->choice) {
                *ith = CW(fl->fl_cancel_item);
                retval = -1;
                break;
            }
            /* FALL THROUGH */
        case '.':
            if ((CL(evt->message) & 0xff) == '.' && (evt->modifiers & CWC(cmdKey))) {
                *ith = CW(fl->fl_cancel_item);
                retval = -1;
                break;
            }
        default:
            /*
       * The Cx(dp->editField) check was made to get HFS_XFer to work.  There
       * may be a better place to put it, but not enough tests have been done
       * to say where.
       */
            if (fl->flnmfil && !fl->flgraynondirs && dp->editField == -1) {
                flep = ((struct flinfostr*)STARH((Handle)fl->flinfo)) + fl->flnmfil - 1;
                if (prefix[0] == MAXPREFIX || CL(evt->when) > lastkeydowntime + CL(DoubleTime)) {
                    flp = ((struct flinfostr*)STARH((Handle)fl->flinfo));
                    prefix[0] = 0;
                    oldticks = -1000;
                }
                else
                    flp = ((struct flinfostr*)STARH((Handle)fl->flinfo)) + ((fl->flsel) == -1 ? 0 : fl->flsel);
                lastkeydowntime = CL(evt->when);
                prefix[++prefix[0]] = CL(evt->message) & 0xff;
                while (flp < flep && RelString((StringPtr)(((char*)STARH((Handle)fl->flstrs)) + flp->floffs), prefix,
                                               FALSE, TRUE) < 0)
                    ++flp;
                nsel = flp - ((struct flinfostr*)STARH((Handle)fl->flinfo));
                if (nsel != fl->flsel) {
                    safeflflip(fl, fl->flsel);
                    fltop = GetCtlValue(fl->flsh);
                    if (nsel < fltop || nsel >= fltop + fl->flnmlin) {
                        SetCtlValue(fl->flsh, nsel - fl->flnmlin / 2);
                        flscroll(fl, fltop, GetCtlValue(fl->flsh));
                    }
                    safeflflip(fl, fl->flsel = nsel);
                    settype(fl, nsel);
                }
                retval = -1;
            }
            break;
        }
        break;
    case mouseDown: {
        Point start = {CW(evt->where.v), CW(evt->where.h)};
        HIDDEN_WindowPtr window;
        if (FindWindow(start, &window) == inDrag && HIDDEN_NATIVE(window) == (WindowPtr)dp) {
            Rect limits = screenBitsX.bounds;
            limits.top = CW(CW(MBarHeight) + 4);
            DragWindow((WindowPtr)dp, start, &limits);
            *ith = CWC(100);
            return TRUE;
        }
    }
        p = evt->where;
        GlobalToLocal(&p);
        p.h = CW(p.h);
        p.v = CW(p.v);
        r = file_grow_box(&((DialogPtr)dp)->portRect);
        if (fl->minimum_width && PtInRect(p, &r)) {
            Rect limits;
            int maximum_width = CW(screenBitsX.bounds.right) + CW(PORT_BOUNDS((WindowPtr)dp).left) - 8;
            int maximum_height = CW(screenBitsX.bounds.bottom) + CW(PORT_BOUNDS((WindowPtr)dp).top) - 8;
            if (maximum_width < fl->minimum_width)
                maximum_width = fl->minimum_width;
            if (maximum_height < fl->minimum_height)
                maximum_height = fl->minimum_height;
            SetRect(&limits, fl->minimum_width, fl->minimum_height, maximum_width, maximum_height);
            Point start = {CW(evt->where.v), CW(evt->where.h)};
            LONGINT size = GrowWindow((WindowPtr)dp, start, &limits);
            if (size)
                resize_file_dialog(dp, fl, LoWord(size), HiWord(size));
            *ith = CWC(100);
            return TRUE;
        }
        else if (PtInRect(p, &fl->flrect)) {
            GetDItem((DialogPtr)dp, getOpen, &i, (HIDDEN_Handle*)&h, &r);

            flmouse(fl, evt->where, HIDDEN_NATIVE(h));
            ticks = TickCount();
            if (fl->flsel != -1 && savesel == fl->flsel && (ticks < oldticks + CL(DoubleTime))) {
                prefix[0] = 0;
                *ith = CWC(fl->choice && fl->choice != romlib_choose_volume && folder_selected_p(fl)
                               ? FAKEOPENDIR
                               : opentoken);
                oldticks = -1000;
                retval = -1;
            }
            else
                oldticks = ticks;
            savesel = fl->flsel;
        }
        else if ((t = TestControl(fl->flsh, p))) {
            if (t == inThumb) {
                from = GetCtlValue(fl->flsh);
                TrackControl(fl->flsh, p, (ProcPtr)0);
                flscroll(fl, from, GetCtlValue(fl->flsh));
            }
            else
                TrackControl(fl->flsh, p, (ProcPtr)P_ROMlib_stdftrack);
            *ith = CWC(100);
            retval = TRUE;
        }
        else if (PtInRect(p, &fl->flcurdirrect)) {
            *ith = CWC(FAKECURDIR);
            retval = -1;
        }
        else {
            GetDItem((DialogPtr)dp, getOpen, &i, (HIDDEN_Handle*)&h, &r);

            if ((part = TestControl(HIDDEN_NATIVE(h), p)) && TrackControl(HIDDEN_NATIVE(h), p, (ProcPtr)0)) {
                prefix[0] = 0;
                oldticks = -1000;
                *ith = CWC(opentoken);
                retval = -1;
            }
        }
        break;
    case nullEvent:
        *ith = CWC(100);
        retval = -1;
        break;
    case updateEvt:
        if ((DialogPeek)MR(evt->message) == dp) {
            drawjobberattop(dp);
            draw_file_default_button(dp);
            if (fl->minimum_width)
                draw_file_grow_box(dp);
        }
        *ith = CWC(100);
        break;
    }
    retval2 = call_magicfp(fl, dp, evt, ith);

    if (!fl->choice && *ith == CWC(getOpen) && folder_selected_p(fl)) /* 1 is getOpen and putSave */
        *ith = CWC(FAKEOPENDIR);

    return retval ? retval : retval2;
}

static int file_row_height(const FontInfo* info)
{
    return MAX(16, CW(info->ascent) + CW(info->descent) + CW(info->leading));
}

A3(PRIVATE, void, flinit, fltype*, f, Rect*, r, ControlHandle, sh)
{
    FontInfo fi;
    THz savezone;

    GetFontInfo(&fi);

    f->flsh = sh;
    f->flrect = *r;
    f->fllinht = file_row_height(&fi);

    f->flcurdirrect.top = CW(CW(r->top) - f->fllinht - 5);
    f->flcurdirrect.bottom = CW(CW(f->flcurdirrect.top) + f->fllinht);
    getcurname(f);

    f->flascent = CW(fi.ascent);
    f->flnmlin = (CW(r->bottom) - CW(r->top)) / f->fllinht;
    f->flnmfil = 0;
    f->flsel = -1;
    savezone = TheZone;
    HIDDEN_RAW_SET(TheZone_H, SysZone);
    f->flinfo = NewHandle((Size)0);
    f->flstrs = NewHandle((Size)0);
    HIDDEN_RAW_SET(TheZone_H, savezone);
}

A3(PRIVATE, void, stdfflip, Rect*, rp, INTEGER, n, INTEGER, height)
{
    INTEGER savetop;

    savetop = rp->top;
    rp->top = CW(CW(rp->top) + (n * height + 1));
    rp->bottom = CW(CW(rp->top) + height - 2);
    InvertRect(rp);
    rp->top = savetop;
}

static void select_file_desktop(fltype* state)
{
    INTEGER volume;
    LONGINT directory;
    state->desktop_files = state->choice != romlib_choose_volume &&
                           C_FindFolder(kOnSystemDisk, kDesktopFolderType, FALSE, &volume, &directory) == noErr;
    if (state->desktop_files) {
        SFSaveDisk = CW(-CW(volume));
        CurDirStore = directory;
    }
    state->desktop = TRUE;
}

A1(PRIVATE, BOOLEAN, trackdirs, DialogPeek, dp)
{
    WRAPPER_PIXMAP_FOR_COPY(wrapper);
    PixMapHandle save_bits;
    Rect therect, fillinrect;

    struct link {
        Str255 name;
        LONGINT dirid;
        struct link* next;
        INTEGER vrefnum;
    } first, *next;

    int count, i;
    CInfoPBRec hpb;
    int max_width;
    fltype* fl;
    EventRecord evt;
    LONGINT id;
    int sel, newsel, firstsel;
    BOOLEAN done;
    ALLOCABEGIN
    BOOLEAN ejectable;
    boolean_t seen_up_already;
    TEMP_ALLOC_DECL(temp_save_bits);

    THEPORT_SAVE_EXCURSION(MR(wmgr_port), {
        int str_width;

        next = &first;
        FIELD_ASSIGN_RAW(hpb.dirInfo.ioCompletion, 0);
        FIELD_ASSIGN_RAW(hpb.dirInfo.ioNamePtr, RM(&next->name[0]));
        next->name[0] = 0;
        hpb.dirInfo.ioVRefNum = CW(-CW(SFSaveDisk));
        hpb.dirInfo.ioFDirIndex = CWC(-1);
        hpb.dirInfo.ioDrDirID = CurDirStore;
        max_width = 0;
        count = 0;
        done = FALSE;

        do {
            OSErr err;

            err = PBGetCatInfo(&hpb, FALSE);
            if (err != noErr) {
                warning_unexpected("PBGetCatInfo returns err = %d\n", err);
                done = TRUE;
            }
            id = next->dirid = CL(hpb.dirInfo.ioDrDirID);
            next->vrefnum = CW(hpb.dirInfo.ioVRefNum);
            next->next = (struct link*)ALLOCA(sizeof(struct link));
            gui_assert(next->next);
            str_width = StringWidth(next->name);
            if (str_width > max_width)
                max_width = str_width;
            next = next->next; /* make Steve Jobs happy */
            hpb.dirInfo.ioDrDirID = hpb.dirInfo.ioDrParID;
            FIELD_ASSIGN_RAW(hpb.dirInfo.ioNamePtr, RM(&next->name[0]));
            next->name[0] = 0;
            count++;
            if (id == 2)
                done = TRUE;
        } while (!done);
        fl = WINDFL(dp);
        str255_from_c_string(next->name, "Desktop");
        next->dirid = 0;
        next->vrefnum = 0;
        next->next = NULL;
        int desktop_width = StringWidth(next->name);
        if (desktop_width > max_width)
            max_width = desktop_width;
        ++count;
        therect.top = CW(CW(fl->flcurdirrect.top) - CW(PORT_BOUNDS(dp).top));
        therect.left = CW(CW(fl->flcurdirrect.left) - CW(PORT_BOUNDS(dp).left));
        therect.bottom = CW(CW(therect.top) + count * fl->fllinht + 1);
        therect.right = CW(CW(therect.left) + 2 + 16 + 3 + max_width + 4 + 2 + 3);
        ClipRect(&therect);

        {
            Rect* bounds;
            PixMapHandle port_pixmap;
            INTEGER save_bpp_x;
            int row_bytes;
            void* save_bits_mem;

            save_bits = NewPixMap();
            port_pixmap = CPORT_PIXMAP(thePort);

            bounds = &PIXMAP_BOUNDS(save_bits);
            bounds->top = CWC(0);
            bounds->left = CWC(0);
            bounds->bottom = CW(RECT_HEIGHT(&therect));
            bounds->right = CW(RECT_WIDTH(&therect));
            PIXMAP_PIXEL_SIZE_X(save_bits) = save_bpp_x = PIXMAP_PIXEL_SIZE_X(port_pixmap);
            ROMlib_copy_ctab(PIXMAP_TABLE(port_pixmap), PIXMAP_TABLE(save_bits));
            row_bytes = ((CW(bounds->right) * CW(save_bpp_x) + 31) / 32) * 4;
            PIXMAP_SET_ROWBYTES_X(save_bits, CW(row_bytes));

            /* Allocate potentially large temporary pixmap space. */
            TEMP_ALLOC_ALLOCATE(save_bits_mem, temp_save_bits, CW(bounds->bottom) * row_bytes);
            PP_ASSIGN(PIXMAP_BASEADDR_X(save_bits), save_bits_mem);
            WRAPPER_SET_PIXMAP_X(wrapper, RM(save_bits));

            CopyBits(PORT_BITS_FOR_COPY(thePort), wrapper, &therect, bounds, srcCopy, NULL);
        }

        EraseRect(&therect);

        /* loop through, displaying stuff */
        /* highlite the appropriate box, etc. */

        fillinrect.top = therect.top;
        fillinrect.left = CW(CW(therect.left) + 2);
        fillinrect.bottom = CW(fl->flascent);
        fillinrect.right = therect.right;

        for (i = count, next = &first; --i >= 0; next = next->next) {

            /* TODO: ask cliff about a better way to do this */
            /* unused = */
            getdiskname(&ejectable, NULL);
            drawinboxwithicon(next->name, &fillinrect,
                              !next->vrefnum     ? -2
                              : next->dirid == 2 ? MICONDISK
                                                 : -1,
                              fl, next->vrefnum, NULL);
            fillinrect.top = CW(CW(fillinrect.top) + (fl->fllinht));
        }

        therect.right = CW(CW(therect.right) - (1));
        therect.bottom = CW(CW(therect.bottom) - (1));
        FrameRect(&therect);
        MoveTo(CW(therect.left) + 1, CW(therect.bottom));
        LineTo(CW(therect.right), CW(therect.bottom));
        LineTo(CW(therect.right), CW(therect.top) + 1);
        therect.right = CW(CW(therect.right) + (1));

        sel = 0;
        fillinrect.top = therect.top;
        fillinrect.left = CW(CW(fillinrect.left) - 1);
        fillinrect.right = CW(CW(fillinrect.right) - 2);
        stdfflip(&fillinrect, sel, fl->fllinht);
        done = FALSE;
        seen_up_already = FALSE;
        firstsel = -1;
        while (!done) {
            evt.where.h = CW(evt.where.h);
            evt.where.v = CW(evt.where.v);
            if (PtInRect(evt.where, &therect))
                newsel = (evt.where.v - CW(therect.top)) / fl->fllinht;
            else
                newsel = -1;
            if (newsel != sel) {
                if (firstsel == -1)
                    firstsel = newsel;
                if (sel != -1)
                    stdfflip(&fillinrect, sel, fl->fllinht);
                if (newsel != -1)
                    stdfflip(&fillinrect, newsel, fl->fllinht);
                sel = newsel;
            }
            if (!ROMlib_sticky_menus_p)
                done = GetNextEvent(mUpMask, &evt);
            else {
                if (OSEventAvail(mUpMask, &evt)) {
                    if (seen_up_already || (sel != firstsel && firstsel != -1))
                        done = TRUE;
                    else {
                        GetOSEvent(mUpMask, &evt);
                        seen_up_already = TRUE;
                    }
                }
                if (!done && OSEventAvail(mDownMask, &evt)) {
                    GetOSEvent(mDownMask, &evt);
                    done = sel != -1;
                }
            }
        }

        therect.bottom = CW(CW(therect.bottom) + 1);

        /* restore the rect and clean up after ourselves */
        CopyBits(wrapper, PORT_BITS_FOR_COPY(thePort), &PIXMAP_BOUNDS(save_bits), &therect, srcCopy, NULL);
        DisposPixMap(save_bits);
    });
    if (sel != -1) {
        for (i = 0, next = &first; i != sel; ++i, next = next->next)
            ;
        if (!next->vrefnum)
            select_file_desktop(WINDFL(dp));
        else {
            WINDFL(dp)->desktop = FALSE;
            CurDirStore = CL(next->dirid);
            SFSaveDisk = CW(-next->vrefnum);
        }
        return TRUE;
    }
    ALLOCAEND

    TEMP_ALLOC_FREE(temp_save_bits);

    return FALSE;
}

PRIVATE void makeworking(fltype* f)
{

    switch (f->flavor) {
    case original_sf: {
        WDPBRec wdpb;
        OSErr err;

        wdpb.ioVRefNum = CW(-CW(SFSaveDisk));
        wdpb.ioWDDirID = CurDirStore;
        wdpb.ioWDProcID = TICKX("STDF");
        FIELD_ASSIGN_RAW(wdpb.ioNamePtr, 0);
        err = PBOpenWD(&wdpb, FALSE);
        if (err != noErr)
            warning_unexpected("PBOpenWD returns %d\n", err);

        f->flreplyp.oreplyp->vRefNum = wdpb.ioVRefNum;
    } break;
    case new_sf:
    case new_custom_sf:
        f->flreplyp.nreplyp->sfFile.vRefNum = CW(-CW(SFSaveDisk));
        f->flreplyp.nreplyp->sfFile.parID = CurDirStore;
        break;
    default:
        warning_unexpected("flavor = %d", f->flavor);
        break;
    }
}

A1(PRIVATE, BOOLEAN, ejected, HParmBlkPtr, pb)
{
    return pb->volumeParam.ioVDrvInfo == 0;
}

/*
 * returns true if the filesystem that pb refers to is part of a single
 * tree, as in UNIX.  Under Windows, filesystems are distinct (i.e. C:, D:,
 * etc.)
 */

PRIVATE boolean_t single_tree_fs_p(HParmBlkPtr pb)
{
#if defined(CYGWIN32)
    return FALSE;
#else
    HVCB* vcbp;

    vcbp = ROMlib_vcbbyvrn(CW(pb->volumeParam.ioVRefNum));
    return vcbp && !vcbp->vcbCTRef;
#endif
}

#if defined(LINUX)
PUBLIC int linuxfloppy_open(int disk, LONGINT* bsizep, drive_flags_t* flagsp, const char* dname)
{
    int retval;
    struct cdrom_subchnl sub_info;
    boolean_t force_read_only;

    force_read_only = FALSE;
    *flagsp = 0;
#define FLOPPY_PREFIX "/dev/fd"
    if (strncmp(dname, FLOPPY_PREFIX, sizeof(FLOPPY_PREFIX) - 1) == 0)
        *flagsp |= DRIVE_FLAGS_FLOPPY;

    if (!force_read_only)
        retval = Uopen(dname, O_RDWR, 0);
#if !defined(LETGCCWAIL)
    else
        retval = noErr;
#endif
    if (force_read_only || retval < 0) {
        retval = Uopen(dname, O_RDONLY, 0);
        if (retval >= 0)
            *flagsp |= DRIVE_FLAGS_LOCKED;
    }

    memset(&sub_info, 0, sizeof sub_info);
    sub_info.cdsc_format = CDROM_MSF;
    if (retval >= 0 && ioctl(retval, CDROMSUBCHNL, &sub_info) != -1) {
        switch (sub_info.cdsc_audiostatus) {
        case CDROM_AUDIO_PLAY:
        case CDROM_AUDIO_PAUSED:
        case CDROM_AUDIO_COMPLETED:
        case CDROM_AUDIO_ERROR:
            close(retval);
            return -1;
        }
    }

    *bsizep = 512;
    if (retval >= 0) {
        /* *ejectablep = TRUE; DRIVE_FLAGS_FIXED not set */
    }

    return retval;
}

PRIVATE int linuxfloppy_close(int disk)
{
    return close(disk);
}
#endif

typedef struct {
    const char* dname;
    DrvQExtra* dqp;
    BOOLEAN loaded;
    int fd;
} dosdriveinfo_t;

#define IGNORED (-1) /* doesn't really matter */

#if defined(CYGWIN32)

#define N_DRIVES (32)

PRIVATE boolean_t drive_loaded[N_DRIVES] = {0};

PRIVATE char* drive_name_of(int i)
{
    static char retval[] = "A:";

    retval[0] = 'A' + i;
    return retval;
}

enum { NUM_FLOPPIES = 2, NON_FLOPPY_BIT = 0x80 };

PRIVATE int fd_of(int i)
{
    int retval;

#if defined(CYGWIN32)
    retval = i;
#else
    if (i < NUM_FLOPPIES)
        retval = i;
    else
        retval = i - NUM_FLOPPIES + NON_FLOPPY_BIT;
#endif
    return retval;
}

#define DRIVE_NAME_OF(x) drive_name_of(x)
#define FD_OF(x) fd_of(x)
#define DRIVE_LOADED(x) drive_loaded[x]

#else

#define DRIVE_NAME_OF(x) drives[x].dname

#define FD_OF(x) drives[x].fd

#define DRIVE_LOADED(x) drives[x].loaded

#endif

PUBLIC void futzwithdosdisks(void)
{
#if defined(LINUX) || defined(CYGWIN32)
    int i, fd;
    LONGINT mess;
    LONGINT blocksize;
    drive_flags_t flags;
#if defined(CYGWIN32)
/* #warning "We're cheating on DOS drive specs: ejectable, bsize, maxsize, writable" */
#define OPEN_ROUTINE dosdisk_open
#define CLOSE_ROUTINE dosdisk_close
#define EXTRA_CLOSE_PARAM , FALSE
#define MARKER DOSFDBIT
#define EXTRA_PARAM
#define ROMLIB_MACDRIVES ROMlib_macdrives
#elif defined(LINUX)
    static dosdriveinfo_t drives[] = {
        {"/dev/fd0", (DrvQExtra*)0, FALSE, IGNORED},
        {"/dev/cdrom", (DrvQExtra*)0, FALSE, IGNORED},
#if 0
	{ "/dev/fd1", (DrvQExtra *) 0, FALSE, IGNORED },
#endif
    };
#define N_DRIVES NELEM(drives)
#define OPEN_ROUTINE linuxfloppy_open
#define CLOSE_ROUTINE linuxfloppy_close
#define EXTRA_CLOSE_PARAM
#define MARKER 0
#define EXTRA_PARAM , (DRIVE_NAME_OF(i))
#define ROMLIB_MACDRIVES (~0)
#endif

    /* Since we're scanning for new disks, let's be paranoid and
     * flush all cached disk information.
     */
    dcache_invalidate_all(TRUE);

    if (!nodrivesearch_p) {
        for (i = 0; i < N_DRIVES; ++i) {
            if (/* DRIVE_LOADED(i) */ ROMLIB_MACDRIVES & (1 << i)) {
#if defined(CYGWIN32)
                checkpoint_macdrive(checkpointp, begin, 1 << i);
#endif
                if (((fd = OPEN_ROUTINE(FD_OF(i), &blocksize, &flags EXTRA_PARAM)) >= 0) ||
                    (flags & DRIVE_FLAGS_FLOPPY)) {
                    try_to_mount_disk(DRIVE_NAME_OF(i), fd | MARKER, &mess, blocksize, 16 * PHYSBSIZE, flags, 0);
                    mess = CL(mess);
                    if (mess) {
                        if (mess >> 16 == 0) {
                            DRIVE_LOADED(i) = TRUE;
                            PPostEvent(diskEvt, mess, (HIDDEN_EvQElPtr*)0);
                            /* TODO: we probably should post if mess returns an
		     error, but I think we get confused if we do */
                        }
                        else {
                            if (!(flags & DRIVE_FLAGS_FLOPPY))
                                CLOSE_ROUTINE(fd EXTRA_CLOSE_PARAM);
                        }
                    }
                    else {
                        if (!(flags & DRIVE_FLAGS_FLOPPY))
                            CLOSE_ROUTINE(fd EXTRA_CLOSE_PARAM);
                    }
                }
#if defined(CYGWIN32)
                checkpoint_macdrive(checkpointp, end, 1 << i);
#endif
            }
        }
    }
#endif
}

/* Applications may leave a working-directory reference in SFSaveDisk, or a
   directory ID belonging to a previously selected volume in CurDirStore. */
static void resolve_file_directory(INTEGER* volume, LONGINT* directory)
{
    if (ISWDNUM(*volume)) {
        WDPBRec working = {0};
        working.ioVRefNum = CW(*volume);
        if (PBGetWDInfo(&working, FALSE) == noErr) {
            *volume = CW(working.ioWDVRefNum);
            *directory = CL(working.ioWDDirID);
        }
    }

    CInfoPBRec catalog = {0};
    catalog.dirInfo.ioVRefNum = CW(*volume);
    catalog.dirInfo.ioFDirIndex = CWC(-1);
    catalog.dirInfo.ioDrDirID = CL(*directory);
    OSErr err = PBGetCatInfo(&catalog, FALSE);
    if (err == dirNFErr || err == fnfErr || *directory == 1)
        *directory = 2;
}

static void resolve_file_starting_directory(void)
{
    INTEGER volume = -CW(SFSaveDisk);
    LONGINT directory = CL(CurDirStore);
    resolve_file_directory(&volume, &directory);
    SFSaveDisk = CW(-volume);
    CurDirStore = CL(directory);
}

A2(PRIVATE, void, bumpsavedisk, DialogPtr, dp, BOOLEAN, always)
{
    INTEGER current;
    HParamBlockRec pb;
    INTEGER vref;
    OSErr err;
    BOOLEAN is_single_tree_fs, seenus;

#if defined(CYGWIN32)
    /*    static BOOLEAN beenhere = FALSE; */

    if (ROMlib_drive_check /* || !beenhere */) {
        futzwithdosdisks();
        /*      beenhere = TRUE; */
    }
#endif
    pb.volumeParam.ioVRefNum = CW(-CW(SFSaveDisk));
    FIELD_ASSIGN_RAW(pb.volumeParam.ioNamePtr, 0);
    pb.volumeParam.ioVolIndex = 0;
    err = PBHGetVInfo(&pb, FALSE);
    if (err != noErr)
        warning_unexpected("PBHGetVInfo returns %d", err);
    else if (!SFSaveDisk || ISWDNUM(-CW(SFSaveDisk)))
        SFSaveDisk = CW(-CW(pb.volumeParam.ioVRefNum));
    if (always || pb.ioParam.ioResult != noErr || ejected(&pb)) {
        current = pb.volumeParam.ioVRefNum;
        is_single_tree_fs = single_tree_fs_p(&pb);
        pb.volumeParam.ioVolIndex = 0;
        seenus = FALSE;
        vref = 0;
        do {
            pb.volumeParam.ioVolIndex = CW(CW(pb.volumeParam.ioVolIndex) + 1);
            err = PBHGetVInfo(&pb, FALSE);
            if (err != noErr && !seenus)
                warning_unexpected("PBHGetVInfo = %d\n", err);
            else {
                if (pb.volumeParam.ioVRefNum == current)
                    seenus = TRUE;
                else if (!ejected(&pb) && (!is_single_tree_fs || !single_tree_fs_p(&pb))) {
                    if (!vref || seenus)
                        vref = CW(pb.volumeParam.ioVRefNum);
                    if (seenus)
                        /*-->*/ break;
                }
            }
        } while (err == noErr);
        if (vref) {
            SFSaveDisk = CW(-vref);
            CurDirStore = CLC(2);
        }
    }
}

/*
   * NOTE: we no longer swap out the refcon when making the sfHookFirstCall (-1)
   *       or the sfHookLastCall (-2).  This fixes the bug that had Photoshop 3.0
   *	 dying the second time that SFCustomGetFile was called.  This fix
   *	 was determined by trial and error.
   *
   * I have just verified that the fix is incorrect.  It works because we
   * bypass some startup code that Photoshop 3.0 uses to set up its type list.
   * This means that the type list won't work properly.  However if we don't
   * swap in 'stdf' then we wind up making calls to InsMenuItem that add
   * entries to the "Format" menu which somehow results in a crash eventually.
   * If we have ROMlib_CALLDHOOK temporarily disable InsMenuItem then PS3.0
   * will work even w/ 'stdf' in the refcon.  It will also work if we rig
   * CountMItems to always return 0 inside ROMlib_CALLDHOOK.  So something
   * appears to go wrong if we call the startup code and actually add (and
   * notice that we add) items the Format menu.
   */

PRIVATE INTEGER ROMlib_CALLDHOOK(fltype* fl, INTEGER ihit, DialogPtr dp, dialog_hook_u dhu)
{
    INTEGER retval;
    LONGINT save_ref_con;

    ROMlib_hook(stdfile_dialhooknumber);
    HOOKSAVEREGS();

    save_ref_con = GetWRefCon(dp);
    emergency_save_ref_con = save_ref_con;
    SetWRefCon(dp, TICK("stdf"));

    switch (fl->flavor) {
    case original_sf:
    case new_sf:
        retval = CToPascalCall(dhu.odh, CTOP_Alert, ihit, dp);
        break;
    case new_custom_sf:
        retval = CToPascalCall(dhu.cdh, CTOP_unused_stdfile_2, ihit, dp, fl->mydata);
        break;
    default:
        warning_unexpected("flavor = %d", fl->flavor);
        retval = 0;
        break;
    }
    HOOKRESTOREREGS();
    SetWRefCon(dp, save_ref_con);
    return retval;
}

A4(PRIVATE, void, transformsfpdialog, DialogPtr, dp, Point*, offset, Rect*, scrollrect, BOOLEAN, getting)
{
    INTEGER numitems, windheight, i, j, extrasizeneeded;
    HIDDEN_Handle h;
    Rect r;
    TEPtr tep;

    if (getting) {
        extrasizeneeded = 20;
    }
    else {
        extrasizeneeded = 110;
        SetRect(scrollrect, 16, 24, 231, 106);
        tep = STARH(PPR(((DialogPeek)dp)->textH));
        tep->destRect.top = CW(CW(tep->destRect.top) + (extrasizeneeded));
        tep->destRect.bottom = CW(CW(tep->destRect.bottom) + (extrasizeneeded));
        tep->viewRect.top = CW(CW(tep->viewRect.top) + (extrasizeneeded));
        tep->viewRect.bottom = CW(CW(tep->viewRect.bottom) + (extrasizeneeded));
    }
    numitems = CW(*(INTEGER*)STARH((PPR(((DialogPeek)dp)->items)))) + 1;
    for (j = 1; j <= numitems; j++) {
        GetDItem(dp, j, &i, &h, &r);
        i = CW(i);

        if (!getting || CW(r.bottom) > CW(scrollrect->top)) {
            r.top = CW(CW(r.top) + (extrasizeneeded));
            r.bottom = CW(CW(r.bottom) + (extrasizeneeded));
            if (i <= 7 && i >= 4) /* It's a control */
                MoveControl((ControlHandle)HIDDEN_NATIVE(h), CW(r.left), CW(r.top));
            SetDItem(dp, j, i, HIDDEN_NATIVE(h), &r);
        }
    }
    windheight = CW(dp->portRect.bottom) - CW(dp->portRect.top) + extrasizeneeded;
    SizeWindow((WindowPtr)dp, CW(dp->portRect.right) - CW(dp->portRect.left), windheight, FALSE);
    if (getting) {
        scrollrect->top = CW(CW(scrollrect->top) + (extrasizeneeded));
        scrollrect->bottom = CW(CW(scrollrect->bottom) + (extrasizeneeded));
    }
    InvalRect(&dp->portRect);
    offset->v -= extrasizeneeded / 2;
}

void adjustdrivebutton(DialogPtr dp)
{
    HIDDEN_Handle item;
    INTEGER kind;
    Rect bounds;
    GetDItem(dp, putDrive, &kind, &item, &bounds);
    SetCTitle((ControlHandle)HIDDEN_NATIVE(item), (StringPtr) "\x07"
                                                              "Desktop");
    HiliteControl((ControlHandle)HIDDEN_NATIVE(item), 0);
}

A3(PRIVATE, OSType, gettypeX, StringPtr, name, INTEGER, vref, LONGINT, dirid)
{
    OSType retval;
    OSErr err;
    HParamBlockRec pbr;

    FIELD_ASSIGN_RAW(pbr.fileParam.ioNamePtr, RM(name));
    pbr.fileParam.ioVRefNum = CW(vref);
    pbr.fileParam.ioFVersNum = 0;
    pbr.fileParam.ioFDirIndex = CWC(0);
    pbr.fileParam.ioDirID = CL(dirid);
    err = PBHGetFInfo(&pbr, FALSE);
    if (err == noErr)
        retval = pbr.fileParam.ioFlFndrInfo.fdType;
    else
        retval = 0;
    return retval;
}

PRIVATE OSErr unixcore(StringPtr namep, INTEGER* vrefnump, LONGINT* diridp)
{
    INTEGER vrefnum;
    HVCB* vcbp;
    char* newname;
    INTEGER namelen;
    OSErr err;
    ParamBlockRec pb;
    char *pathname, *filename, *endname;
    VCBExtra* vcbp2;
    struct stat sbuf;
    LONGINT templ;
    char* tempcp;
    ParamBlockRec pbr;

    vrefnum = *vrefnump;

    FIELD_ASSIGN_RAW(pbr.ioParam.ioNamePtr, CLC(0));
    pbr.ioParam.ioVRefNum = CW(vrefnum);
    vcbp = ROMlib_breakoutioname(&pbr, &templ, &tempcp, (BOOLEAN*)0, TRUE);
    free(tempcp);

    if (vcbp && !vcbp->vcbCTRef) {
        FIELD_ASSIGN_RAW(pb.ioParam.ioNamePtr, CLC(0));
        pb.ioParam.ioVRefNum = pbr.ioParam.ioVRefNum;
        err = ROMlib_nami(&pb, *diridp, NoIndex, &pathname, &filename, &endname, FALSE, &vcbp2, &sbuf);
        if (err == noErr) {
            VCBExtra* vcbextrap;

            namelen = endname - pathname - 1;
            newname = alloca(namelen + 1 + namep[0] + 1);
            strncpy(newname, pathname, namelen);
            newname[namelen] = '/';
            strncpy(newname + namelen + 1, (char*)namep + 1, namep[0]);
            newname[namelen + 1 + namep[0]] = 0;
            ROMlib_automount(newname);
            vcbextrap = ROMlib_vcbbyunixname(newname);
            if (vcbextrap) {
                *vrefnump = CW(vcbextrap->vcb.vcbVRefNum);
                if (*diridp == vcbextrap->u.ufs.ino)
                    *diridp = 2;
            }
            else
                err = nsvErr;
            free(pathname);
        }
    }
    else
        err = nsvErr;
    return err;
}

P1(PUBLIC pascal trap, OSErr, unixmount, CInfoPBRec*, cbp)
{
    OSErr err;

    if (!cbp) {
        dofloppymount();
        err = noErr;
    }
    else {
        INTEGER vrefnum;
        LONGINT dirid;

        vrefnum = CW(cbp->hFileInfo.ioVRefNum);
        dirid = CL(cbp->hFileInfo.ioDirID);
        err = unixcore(PPR(cbp->hFileInfo.ioNamePtr), &vrefnum, &dirid);
        if (err == noErr) {
            cbp->hFileInfo.ioVRefNum = CW(vrefnum);
            cbp->hFileInfo.ioDirID = CW(dirid);
        }
    }
    return err;
}

PRIVATE void unixcd(fltype* f)
{
    StringPtr name;
    INTEGER vrefnum;
    LONGINT dirid;

    name =
        (StringPtr)(((char*)STARH((Handle)f->flstrs)) + ((struct flinfostr*)STARH((Handle)f->flinfo))[f->flsel].floffs);
    vrefnum = -CW(SFSaveDisk);
    dirid = CL(CurDirStore);
    if (unixcore(name, &vrefnum, &dirid) == noErr) {
        SFSaveDisk = CW(-vrefnum);
        CurDirStore = CL(dirid);
    }
}

PRIVATE void get_starting_point(Point* pp)
{
    INTEGER screen_width, screen_height;
    Rect main_gd_rect;

    main_gd_rect = PIXMAP_BOUNDS(GD_PMAP(MR(MainDevice)));
    screen_width = CW(main_gd_rect.right);
    screen_height = CW(main_gd_rect.bottom);
    pp->h = (screen_width - STANDARD_WIDTH) / 2;
    pp->v = (screen_height - STANDARD_HEIGHT) / 2;
}

PRIVATE ControlHandle create_new_folder_button(DialogPtr dp)
{
    ControlHandle retval;
    Handle dial_test;

    dial_test = GetResource(TICK("DLOG"), -6044);
    if (!dial_test)
        retval = NULL;
    else {
        INTEGER i;
        HIDDEN_Handle h;
        Rect r;

        GetDItem(dp, sfItemNewFolderUser, &i, &h, &r);
        retval = NewControl((WindowPtr)dp, &r, (StringPtr) "\012New Folder", TRUE, 0, 0, 0, 0, 0L);
        SetDItem(dp, sfItemNewFolderUser, ctrlItem | btnCtrl, (Handle)retval, &r);
    }
    return retval;
}

PRIVATE void destroy_new_folder_button(DialogPtr dp, ControlHandle ch){
    /* May not need to do anything since CloseDialog should clean up all
       outstanding controls */
}

#define SF_NAME(fp) ((fp)->flavor == original_sf ? (fp)->flreplyp.oreplyp->fName : (fp)->flreplyp.nreplyp->sfFile.name)

#define SF_GOOD_XP(fp) ((fp)->flavor == original_sf ? &(fp)->flreplyp.oreplyp->good : &(fp)->flreplyp.nreplyp->sfGood)

#define SF_FTYPE_XP(fp) ((fp)->flavor == original_sf ? &(fp)->flreplyp.oreplyp->fType : &(fp)->flreplyp.nreplyp->sfType)

#define SF_VREFNUM_X(fp) \
    ((fp)->flavor == original_sf ? (fp)->flreplyp.oreplyp->vRefNum : (fp)->flreplyp.nreplyp->sfFile.vRefNum)

#define SF_VREFNUM(fp) (CW(SF_VREFNUM_X(fp)))

#define SF_DIRID_X(fp) ((fp)->flavor == original_sf ? 0 : (fp)->flreplyp.nreplyp->sfFile.parID)

#define SF_DIRID(fp) (CL(SF_DIRID_X(fp)))

static void read_reply_name(fltype* state, Handle item)
{
    Str255 name;
    GetIText(item, name);
    str31assign(SF_NAME(state), name);
}

PRIVATE void getditext(DialogPtr dp, INTEGER item, StringPtr text)
{
    INTEGER i;
    HIDDEN_Handle h;
    Rect r;

    HIDDEN_RAW_SET(h, 0);
    GetDItem(dp, item, &i, &h, &r);
    if (!HIDDEN_NATIVE(h))
        text[0] = 0;
    else {

        GetIText(HIDDEN_NATIVE(h), text);
    }
}

PRIVATE void report_new_folder_failure(OSErr err)
{
    char* message;
    Str255 str;

    switch (err) {
    default:
        message = alloca(256);
        sprintf(message, "Error #%d occurred; you may not be able to create a new folder.", err);
        break;
    case dupFNErr:
        message = "That name is already taken; please use another name.";
        break;
    case dirFulErr:
        message = "This directory is full; you may not be able to create a new folder.";
        break;
    case dskFulErr:
        message = "This disk is full; you may not be able to create a new folder.";
        break;
    case permErr:
    case wrPermErr:
        message = "Filesystem permissions prevent the creation of a new folder.";
        break;
    case vLckdErr:
        message = "This volume is locked; you can not create a new folder.";
        break;
    }
    str255_from_c_string(str, message);
    ParamText(str, 0, 0, 0);
    NoteAlert(GENERIC_COMPLAINT_ID, (ProcPtr)0);
}

PRIVATE boolean_t new_folder_from_dp(DialogPtr dp, fltype* f)
{
    Str255 str;
    HParamBlockRec hpb = {0};
    OSErr err;
    boolean_t retval;

    getditext(dp, 3, str);
    hpb.ioParam.ioVRefNum = CW(-CW(SFSaveDisk));
    hpb.fileParam.ioDirID = CurDirStore;
    if (str[0] > 31)
        str[0] = 31;
    FIELD_ASSIGN_RAW(hpb.ioParam.ioNamePtr, RM(&str[0]));
    err = PBDirCreate(&hpb, FALSE);
    if (err != noErr)
        report_new_folder_failure(err);
    else {
        *SF_FTYPE_XP(f) = hpb.fileParam.ioDirID;
        retval = TRUE;
    }
    retval = err == noErr;
    return retval;
}

PRIVATE boolean_t do_new_folder(fltype* f)
{
    boolean_t retval;
    GrafPtr gp;
    DialogPtr dp;

    retval = FALSE;
    gp = thePort;
    dp = f->choice ? ROMlib_get_new_dialog(-6044, NULL, (WindowPtr)-1, TRUE)
                   : GetNewDialog(-6044, NULL, (WindowPtr)-1);
    if (dp) {
        boolean_t done;
        INTEGER ihit;

        C_SetDialogDefaultItem(dp, 1);
        C_SetDialogCancelItem(dp, 2);
        SelIText(dp, 3, 0, 32767);
        ShowWindow(dp);
        SelectWindow(dp);
        done = FALSE;
        while (!done) {
            /* TODO: consider a filter that limits the length
           of the string to 31 letters */
            if (f->choice)
                ROMlib_run_internal_modal_dialog(dp, NULL, &ihit);
            else
                ModalDialog(NULL, &ihit);
            switch (ihit) {
            default:
                break;
            case CWC(1):
                done = new_folder_from_dp(dp, f);
                if (done)
                    retval = TRUE;
                break;
            case CWC(2):
                done = TRUE;
                break;
            }
        }
        if (f->choice)
            C_DisposDialog(dp);
        else
            CloseDialog(dp);
        SetPort(gp);
    }
    return retval;
}

/* Resolve native paths through the same host-volume directory cache used by
   catalog enumeration. A save result may name a nonexistent file, never a
   nonexistent parent. Selection itself neither creates nor truncates files. */
OSErr ROMlib_host_fsspec(char* path, FSSpec* spec)
{
    char *slash, *base, *parent;
    size_t length;
    LONGINT directory;
    VCBExtra* volume;
    struct stat info;
    Str255 name;
    OSErr error;
#if defined(CYGWIN32)
    for (char* p = path; *p; ++p)
        if (*p == '\\')
            *p = '/';
#endif
    slash = strrchr(path, '/');
    if (!slash || !slash[1])
        return bdNamErr;
    base = slash + 1;
    length = strlen(base);
    /* Decode escaped host bytes before enforcing the Macintosh name bound. */
    if (length > 255)
        return bdNamErr;
    memcpy(name + 1, base, length);
    length -= ROMlib_UNIX7_to_Mac((char*)name + 1, length);
    if (!length || length > sizeof spec->name - 1 || memchr(name + 1, ':', length) || memchr(name + 1, 0, length))
        return bdNamErr;
    name[0] = length;
    /* Verify round trip through the File Manager's host-name encoding. */
    parent = ROMlib_newunixfrommac((char*)name + 1, name[0]);
    if (!parent)
        return memFullErr;
    error = strcmp(parent, base) == 0 ? noErr : bdNamErr;
    free(parent);
    if (error)
        return error;
    parent = strdup(path);
    if (!parent)
        return memFullErr;
    length = slash - path;
    if (!length || (length == 2 && path[1] == ':'))
        ++length; /* Preserve / and D:/ roots. */
    parent[length] = 0;
    if (Ustat(parent, &info) < 0 || !S_ISDIR(info.st_mode)) {
        free(parent);
        return dirNFErr;
    }
    ROMlib_automount(parent);
    volume = (VCBExtra*)ROMlib_vcbbybiggestunixname(parent);
    if (!volume || volume->vcb.vcbCTRef) {
        free(parent);
        return nsvErr;
    }
    directory = ST_INO(info);
    if (directory == volume->u.ufs.ino)
        directory = 2;
    else
        ROMlib_dbm_store(volume, parent, &directory, TRUE);
    free(parent);
    return C_FSMakeFSSpec(CW(volume->vcb.vcbVRefNum), directory, name, spec);
}

static OSErr spec_from_path(char* path, FSSpec* spec, CInfoPBRec* catalog)
{
    memset(catalog, 0, sizeof *catalog);
    OSErr error = ROMlib_host_fsspec(path, spec);
    if (error != noErr && error != fnfErr)
        return error;
    FIELD_ASSIGN_RAW(catalog->hFileInfo.ioNamePtr, RM((StringPtr)spec->name));
    catalog->hFileInfo.ioVRefNum = spec->vRefNum;
    catalog->hFileInfo.ioDirID = spec->parID;
    if (error == noErr) {
        error = PBGetCatInfo(catalog, FALSE);
        if (!error && (catalog->hFileInfo.ioFlAttrib & ATTRIB_ISADIR))
            error = bdNamErr;
    }
    return error;
}

static boolean_t host_catalog_allowed(fltype* state, CInfoPBRec* catalog)
{
    return (state->flnumt <= 0 || typeinarray(catalog->hFileInfo.ioFlFndrInfo.fdType, state->flnumt, state->fltl)) &&
           passes_filter(state, catalog, state->flnumt);
}

/* Keep the classic dialog and its custom controls alive when switching to
   the host panel. Cancellation returns to those controls without changing
   the reply, working directory, or typed filename. */
static boolean_t use_host_file_dialog(fltype* state, getorput_t operation, StringPtr prompt)
{
    FSSpec current = {0}, spec;
    INTEGER saved_disk = SFSaveDisk;
    LONGINT saved_directory = CurDirStore;
    CInfoPBRec catalog;
    char title[256], *directory = NULL, *path = NULL;
    current.vRefNum = CW(-CW(SFSaveDisk));
    current.parID = CurDirStore;
    if (ROMlib_filename_from_fsspec(&directory, &current) != noErr) {
        free(directory);
        directory = NULL;
    }
    if (prompt && prompt[0]) {
        memcpy(title, prompt + 1, prompt[0]);
        title[prompt[0]] = 0;
    }
    else
        strcpy(title, operation == put ? "Save" : "Open");
    char* name = operation == put ? ROMlib_newunixfrommac((char*)SF_NAME(state) + 1, SF_NAME(state)[0]) : strdup("");
    boolean_t accepted = FALSE;
    if (!name)
        goto cleanup;
    int result;
    result = host_file_dialog(operation == put, title, directory, name, &path);
    if (result < 0)
        host_dialog_error("The host file picker could not open or return a "
                          "path supported by ROMAW.");
    if (result <= 0)
        goto cleanup;
    OSErr error = spec_from_path(path, &spec, &catalog);
    if ((error != noErr && !(operation == put && error == fnfErr)) ||
        (operation == get && !host_catalog_allowed(state, &catalog))) {
        host_dialog_error("This file cannot be used by this Macintosh "
                          "application. Choose a supported file and name.");
        goto cleanup;
    }
    SFSaveDisk = CW(-CW(spec.vRefNum));
    CurDirStore = spec.parID;
    str31assign(SF_NAME(state), spec.name);
    *SF_FTYPE_XP(state) = catalog.hFileInfo.ioFlFndrInfo.fdType;
    if (state->flavor != original_sf) {
        state->flreplyp.nreplyp->sfReplacing = operation == put && error == noErr;
        state->flreplyp.nreplyp->sfScript = CWC(smRoman);
    }
    *SF_GOOD_XP(state) = TRUE;
    accepted = TRUE;
cleanup:
#if defined(SDL)
    sdl_preferences_input_reset();
#endif
    if (!accepted) {
        SFSaveDisk = saved_disk;
        CurDirStore = saved_directory;
    }
    free(name);
    free(directory);
    free(path);
    return accepted;
}

static size_t application_document_types(uint32_t* types, size_t capacity)
{
    if (CW(CurApRefNum) <= 0)
        return 0;
    OSErr saved_error = ResErr;
    INTEGER saved_file = C_CurResFile();
    BOOLEAN saved_load = ResLoad;
    size_t count = 0;
    C_UseResFile(CW(CurApRefNum));
    if (C_ResError() == noErr) {
        C_SetResLoad(TRUE);
        int refs = MIN(C_Count1Resources(TICK("FREF")), 1024);
        for (int i = 1; i <= refs && count < capacity; ++i) {
            Handle ref = C_Get1IndResource(TICK("FREF"), i);
            if (!ref || !STARH(ref))
                continue;
            Size size = GetHandleSize(ref);
            const unsigned char* bytes = (unsigned char*)STARH(ref);
            if (size < 7 || bytes[6] > size - 7)
                continue;
            uint32_t type = (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | bytes[2] << 8 | bytes[3];
            if (!type || type == 0x4150504c)
                continue;
            size_t n;
            for (n = 0; n < count && types[n] != type; ++n)
                ;
            if (n == count)
                types[count++] = type;
        }
    }
    C_SetResLoad(saved_load);
    C_UseResFile(saved_file);
    ResErr = saved_error;
    return count;
}

static void add_save_extension_hint(DialogPtr dp, int filename_item, StringPtr name)
{
    char extensions[232], text[256];
    uint32_t types[256];
    size_t type_count = application_document_types(types, NELEM(types));
    if (!type_count && name && name[0]) {
        OSType type = CL(gettypeX(name, -CW(SFSaveDisk), CL(CurDirStore)));
        if (type)
            types[type_count++] = type;
    }
    if (filetype_map_extensions_path(types, type_count, ROMlib_creator, extensions, sizeof extensions))
        snprintf(text, sizeof text, "DOS extensions: %s", extensions);
    else
        strcpy(text, "DOS extension: none mapped");
    INTEGER kind;
    HIDDEN_Handle handle;
    Rect filename;
    GetDItem(dp, filename_item, &kind, &handle, &filename);
    int width = MAX(1, CW(filename.right) - CW(filename.left));
    Str255 label;
    str255_from_c_string(label, text);
    FontInfo font;
    GetFontInfo(&font);
    int line_height = CW(font.ascent) + CW(font.descent) + CW(font.leading);
    int height = MAX(1, (StringWidth(label) + width - 1) / width) * MAX(line_height, 16) + 6;
    size_t length = strlen(text);
    Handle item = NewHandleClear(2 + sizeof(itmstr) + ((length + 1) & ~1));
    if (!item)
        return;
    itmp entry = (itmp)((INTEGER*)STARH(item) + 1);
    entry->itmr = filename;
    entry->itmr.top = CW(CW(filename.bottom) + 4);
    entry->itmr.bottom = CW(CW(filename.bottom) + height);
    entry->itmtype = statText | itemDisable;
    entry->itmlen = length;
    memcpy(ITEM_DATA(entry), text, length);

    Handle items = PPR(((DialogPeek)dp)->items);
    int count = CW(*(INTEGER*)STARH(items)) + 1;
    for (int n = 1; n <= count; ++n) {
        Rect bounds;
        GetDItem(dp, n, &kind, &handle, &bounds);
        kind = CW(kind);
        if (CW(bounds.top) < CW(filename.bottom) || CW(bounds.top) >= CW(dp->portRect.bottom))
            continue;
        OffsetRect(&bounds, 0, height);
        if ((kind & 0x7f) >= ctrlItem && (kind & 0x7f) < statText && HIDDEN_NATIVE(handle))
            MoveControl((ControlHandle)HIDDEN_NATIVE(handle), CW(bounds.left), CW(bounds.top));
        SetDItem(dp, n, kind, HIDDEN_NATIVE(handle), &bounds);
    }
    AppendDITL(dp, item, overlayDITL);
    DisposHandle(item);
    SizeWindow((WindowPtr)dp, CW(dp->portRect.right) - CW(dp->portRect.left),
               CW(dp->portRect.bottom) - CW(dp->portRect.top) + height, FALSE);
}

static OSErr accept_catalog_choice(fltype* state)
{
    StandardFileReply* reply = state->flreplyp.nreplyp;
    if (state->flsel < 0)
        return fnfErr;
    const struct flinfostr* entry = (struct flinfostr*)STARH(state->flinfo) + state->flsel;
    const boolean_t folder = entry->flicns == MICONCFOLDER;
    const boolean_t volume = entry->volume != 0;
    const OSType type = entry->type;
    const INTEGER vref = volume ? entry->volume : -CW(SFSaveDisk);
    const LONGINT directory = volume ? 2 : folder ? CL(reply->sfType) : CL(CurDirStore);
    Str255 name = {0};
    if (!folder && !volume)
        str255assign(name, (StringPtr)STARH(state->flstrs) + entry->floffs);
    const OSErr status = C_FSMakeFSSpec(vref, directory, name, &reply->sfFile);
    if (!status) {
        reply->sfType = folder || volume ? 0 : CL(type);
        reply->sfIsFolder = folder || volume;
        reply->sfIsVolume = volume;
        reply->sfGood = TRUE;
    }
    return status;
}

static OSErr new_catalog_folder(fltype* state)
{
    StandardFileReply* reply = state->flreplyp.nreplyp;
    Str255 name = {0};
    const OSErr status = C_FSMakeFSSpec(-CW(SFSaveDisk), CL(reply->sfType), name, &reply->sfFile);
    if (!status) {
        reply->sfType = 0;
        reply->sfIsFolder = TRUE;
        reply->sfIsVolume = FALSE;
        reply->sfGood = TRUE;
    }
    return status;
}

PUBLIC OSErr spfcommon(Point p, StringPtr prompt, StringPtr name, dialog_hook_u dh, reply_u rep, INTEGER dig,
                       filter_u fp, file_filter_u filef, INTEGER numt, SFTypeList tl, getorput_t getorput,
                       sf_flavor_t flavor, Ptr activeList, ActivateYDProcPtr activateproc, UNIV Ptr yourdatap,
                       ROMlib_catalog_choice choice)
{
    OSErr status = paramErr;
    TRAPBEGIN();
    if (!rep.oreplyp)
        goto finished;
    memset(rep.oreplyp, 0, flavor == original_sf ? sizeof(SFReply) : sizeof(StandardFileReply));
    {
        HIDDEN_Handle h;
        DialogPtr dp;
        INTEGER ihit, i;
        int done, sav;
        Rect r, scrollrect;
        HIDDEN_Handle pnhand, ejhand, drhand, sahand;
        OSErr err;
        ControlHandle scrollh;
        fltype f;
        GrafPtr gp;
        INTEGER openorsave, promptitem, nmlistitem, diskname, ejectitem, driveitem;
        BOOLEAN transform;
        EventRecord evt;
        ParamBlockRec pbr;
        CInfoPBRec hpb;
        ControlHandle new_folder_button;

        done = FALSE;
        memset(&f, 0, sizeof f);
        f.magicfp = fp;
        f.mydata = yourdatap;
        f.flavor = flavor;
        f.choice = choice;
        f.flreplyp = rep;

        if (p.h < 2 || p.v < CW(MBarHeight) + 7)
            get_starting_point(&p);

        *SF_GOOD_XP(&f) = CBC(FALSE);
        if (f.flavor == original_sf)
            f.flreplyp.oreplyp->version = CBC(0);
        else {
            f.flreplyp.nreplyp->sfIsFolder = 0;
            f.flreplyp.nreplyp->sfIsVolume = 0;
        }
        *SF_FTYPE_XP(&f) = CLC(0);

        new_folder_button = NULL;
        if (getorput == put) {
            str31assign(SF_NAME(&f), name ? name : (StringPtr) "");
            if (f.flavor == original_sf) {
                openorsave = putSave;
                promptitem = putPrompt;
                nmlistitem = putNmList;
                diskname = putDiskName;
                ejectitem = putEject;
                driveitem = putDrive;
                f.fl_cancel_item = putCancel;
            }
            else {
                openorsave = sfItemOpenButton;
                promptitem = sfItemPromptStaticText;
                nmlistitem = sfItemFileListUser;
                diskname = sfItemVolumeUser;
                ejectitem = sfItemEjectButton;
                driveitem = sfItemDesktopButton;
                f.fl_cancel_item = sfItemCancelButton;
            }
        }
        else {
            if (f.flavor == original_sf) {
                openorsave = getOpen;
                promptitem = 10; /* according to whom? bill? */
                nmlistitem = getNmList;
                diskname = getDiskName;
                ejectitem = getEject;
                driveitem = getDrive;
                f.fl_cancel_item = getCancel;
            }
            else {
                openorsave = sfItemOpenButton;
                promptitem = sfItemPromptStaticText;
                nmlistitem = sfItemFileListUser;
                diskname = sfItemVolumeUser;
                ejectitem = sfItemEjectButton;
                driveitem = sfItemDesktopButton;
                f.fl_cancel_item = sfItemCancelButton;
            }
        }
        gp = thePort;
        dp = ROMlib_get_new_dialog(dig, (Ptr)0, (WindowPtr)-1, TRUE);
        if (!dp) {
            status = memFullErr;
            goto finished;
        }
        status = noErr;
        SetWTitle((WindowPtr)dp, getorput == put ? (StringPtr) "\004Save" : (StringPtr) "\004Open");
        if (choice) {
            StringPtr title = choice == romlib_choose_folder   ? (StringPtr)"\015Choose Folder"
                              : choice == romlib_choose_volume ? (StringPtr)"\015Choose Volume"
                              : choice == romlib_create_folder ? (StringPtr)"\012New Folder"
                                                              : (StringPtr)"\015Choose Object";
            SetWTitle((WindowPtr)dp, prompt && prompt[0] ? prompt : title);
        }
        resolve_file_starting_directory();
        bumpsavedisk(dp, FALSE);
        SetPort(dp);
        ((DialogPeek)dp)->aDefItem = CW(openorsave);
        GetDItem(dp, openorsave, &i, &sahand, &r);
        if (dig == getDlgID || dig == sfGetDialogID || dig == putDlgID || dig == sfPutDialogID)
            SetCTitle((ControlHandle)HIDDEN_NATIVE(sahand), getorput == get ? (StringPtr) "\x04"
                                                                                          "Open"
                                                                            : (StringPtr) "\x04"
                                                                                          "Save");
        if (choice)
            SetCTitle((ControlHandle)HIDDEN_NATIVE(sahand),
                      choice == romlib_create_folder ? (StringPtr)"\012New Folder" : (StringPtr)"\006Choose");

        if (getorput == put && SF_NAME(&f)[0]) {
            boolean_t writable;

            getdiskname(NULL, &writable);
            sav = TRUE;
        }
        else {
            sav = FALSE;
            HiliteControl((ControlHandle)HIDDEN_NATIVE(sahand), 255);
        }
        GetDItem(dp, ejectitem, &i, &ejhand, &r);

        SetCTitle((ControlHandle)HIDDEN_NATIVE(ejhand), (StringPtr) "\010Use Host");
        HiliteControl((ControlHandle)HIDDEN_NATIVE(ejhand), 0);
        if (choice)
            HideControl((ControlHandle)HIDDEN_NATIVE(ejhand));
        GetDItem(dp, driveitem, &i, &drhand, &r);

        adjustdrivebutton(dp);
        if (choice == romlib_choose_volume) {
            HideControl((ControlHandle)HIDDEN_NATIVE(drhand));
            select_file_desktop(&f);
        }

        if (getorput == put) {
            GetDItem(dp, promptitem, &i, &h, &r);

            SetIText(HIDDEN_NATIVE(h), prompt ? prompt : (StringPtr) "");
        }

        GetDItem(dp, nmlistitem, &i, &h, &scrollrect);

        if (getorput == put) {
            INTEGER putname;

            if (f.flavor == original_sf) {
                putname = putName;
                transform = CW(scrollrect.right) - CW(scrollrect.left) == 1;
            }
            else {
                putname = sfItemFileNameTextEdit;
                transform = FALSE;
                new_folder_button = create_new_folder_button(dp);
            }

            GetDItem(dp, putname, &i, &pnhand, &r);

            SetIText(HIDDEN_NATIVE(pnhand), SF_NAME(&f));
            SelIText((DialogPtr)dp, putname, 0, 32767);
        }
        else {
            if (f.flavor == original_sf) {
                GetDItem(dp, getScroll, &i, &h, &r);

                transform = CW(r.right) - CW(r.left) == 16;
                GetDItem(dp, getDotted, &i, &h, &r);

                SetDItem(dp, getDotted, userItem, (Handle)P_ROMlib_filebox, &r);
            }
            else
                transform = FALSE;
        }

        if (transform)
            transformsfpdialog(dp, &p, &scrollrect, getorput == get);

        if (getorput == put)
            add_save_extension_hint(dp, f.flavor == original_sf ? putName : sfItemFileNameTextEdit, SF_NAME(&f));

        SetDItem(dp, nmlistitem, userItem, (Handle)P_ROMlib_filebox, &scrollrect);

        GetDItem(dp, diskname, &i, &h, &r);

        SetDItem(dp, diskname, userItem, (Handle)P_ROMlib_filebox, &r);

        r.left = CW(CW(scrollrect.left) + 1);
        r.right = CW(CW(scrollrect.right) - 16);
        r.top = CW(CW(scrollrect.top) + 1);
        r.bottom = CW(CW(scrollrect.bottom) - 1);
        scrollrect.left = CW(CW(scrollrect.right) - 16);
        scrollh = NewControl((WindowPtr)dp, &scrollrect, (StringPtr) "", TRUE, 0, 0, 0, scrollBarProc, 0L);
        flinit(&f, &r, scrollh);
        f.flfilef = filef;
        f.flnumt = numt;
        f.fltl = tl;
        f.flch = (ControlHandle)HIDDEN_NATIVE(sahand);
        f.flgraynondirs = getorput == get ? 0 : GRAYBIT;

        if (getorput == get) {
            if (f.flnmfil > 0) {
                StringPtr ip;

                ip = (StringPtr)((char*)STARH((Handle)f.flstrs)) +
                     ((struct flinfostr*)STARH((Handle)f.flinfo))[0].floffs;
                str31assign(SF_NAME(&f), ip);
            }
            else
                (SF_NAME(&f))[0] = 0;
        }
        SetWRefCon((WindowPtr)dp, (LONGINT)US_TO_SYN68K(&f));
        f.list_item = nmlistitem;
        f.minimum_width = CW(dp->portRect.right) - CW(dp->portRect.left);
        f.minimum_height = CW(dp->portRect.bottom) - CW(dp->portRect.top) + 16;
        SizeWindow((WindowPtr)dp, f.minimum_width, f.minimum_height, FALSE);
        int width = f.minimum_width, height = f.minimum_height;
        int left = p.h, top = p.v;
#if defined(SDL)
        file_dialog_prefs_load(getorput == put, &width, &height, &left, &top);
#endif
        Rect desktop = screenBitsX.bounds;
        desktop.left = CW(CW(desktop.left) + 4);
        desktop.top = CW(CW(MBarHeight) + 24);
        desktop.right = CW(CW(desktop.right) - 4);
        desktop.bottom = CW(CW(desktop.bottom) - 4);
        fit_file_dialog(&desktop, f.minimum_width, f.minimum_height, &width, &height, &left, &top);
        MoveWindow((WindowPtr)dp, left, top, FALSE);

        ihit = -1;
        if (dh.odh)
            ihit = ROMlib_CALLDHOOK(&f, -1, dp, dh); /* the mac does this */

        resolve_file_starting_directory();
        flfill(&f); /* moved to after dhook call of -1 suggested by
         Wieslaw Kuzmicz */
        if (choice == romlib_create_folder)
            HiliteControl(f.flch, 0);

        if (width != f.minimum_width || height != f.minimum_height)
            resize_file_dialog((DialogPeek)dp, &f, width, height);
        ShowWindow((WindowPtr)dp);
        SelectWindow((WindowPtr)dp);
        while (!done) {
            if (choice)
                ROMlib_run_internal_modal_dialog((DialogPtr)dp, (ProcPtr)P_ROMlib_stdffilt, &ihit);
            else
                ModalDialog((ProcPtr)P_ROMlib_stdffilt, &ihit);
            ihit = CW(ihit);
            if (getorput == put)
                read_reply_name(&f, HIDDEN_NATIVE(pnhand));
            if (dh.odh)
                ihit = ROMlib_CALLDHOOK(&f, ihit, dp, dh);
            if (ihit == openorsave && choice == romlib_create_folder) {
                if ((!f.desktop || f.desktop_files) && do_new_folder(&f)) {
                    status = new_catalog_folder(&f);
                    done = TRUE;
                }
            }
            else if (ihit == openorsave && choice) {
                status = accept_catalog_choice(&f);
                done = TRUE;
            }
            else if (ihit == openorsave) {
                makeworking(&f);
                if (getorput == get) {
                    if (*SF_FTYPE_XP(&f))     /* will never happen unless someone */
                        (SF_NAME(&f))[0] = 0; /* has a tricky filterproc */
                    else
                        *SF_FTYPE_XP(&f) = gettypeX(SF_NAME(&f), SF_VREFNUM(&f), SF_DIRID(&f));
                    done = TRUE;
                    *SF_GOOD_XP(&f) = CBC(TRUE);
                }
                else {
                    read_reply_name(&f, HIDDEN_NATIVE(pnhand));
                    FIELD_ASSIGN_RAW(hpb.dirInfo.ioCompletion, 0);
                    PP_ASSIGN(hpb.dirInfo.ioNamePtr, (StringPtr)SF_NAME(&f));
                    hpb.dirInfo.ioVRefNum = SF_VREFNUM_X(&f);
                    hpb.dirInfo.ioFDirIndex = CWC(0);
                    hpb.dirInfo.ioDrDirID = CurDirStore;
                    err = PBGetCatInfo(&hpb, FALSE);
                    switch (err) {
                    case noErr:
                        ParamText(SF_NAME(&f), (StringPtr)0, (StringPtr)0, (StringPtr)0);
                        if (movealert(-3996) == 1) /* overwrite ... */
                            break;
                        *SF_FTYPE_XP(&f) = hpb.hFileInfo.ioFlFndrInfo.fdType;
                        /* FALL THROUGH */
                    case fnfErr:
                        if (flavor != original_sf)
                            rep.nreplyp->sfReplacing = err == noErr;
                        done = TRUE;
                        *SF_GOOD_XP(&f) = CBC(TRUE); /* great.  That's a take */
                        break;
                    case bdNamErr:
                    case nsvErr:
                    case paramErr:
                        movealert(-3994); /* disknotfound */
                        break;
                    default:
                        warning_unexpected("err = %d", err);
                        break;
                    }
                }
            }
            else if ((ihit == f.fl_cancel_item) || (ihit == putCancel)) { /* MYM 6.0 suggests that putCancel
  cancels a get, too */
                done = TRUE;
            }
            else if (ihit == ejectitem && !choice) {
                HideWindow((WindowPtr)dp);
                done = use_host_file_dialog(&f, getorput, prompt);
                ShowWindow((WindowPtr)dp);
                SelectWindow((WindowPtr)dp);
                SetPort(dp);
                if (done)
                    break;
            }
            else if (ihit == driveitem) {
                select_file_desktop(&f);
                ihit = FAKEREDRAW;
            }
            else if (ihit == diskname && choice != romlib_choose_volume) {
                f.desktop = FALSE;
                if (moveuponedir(&f))
                    ihit = FAKEREDRAW;
            }
            else if (ihit == FAKECURDIR && choice != romlib_choose_volume) {
                if (trackdirs((DialogPeek)dp))
                    ihit = FAKEREDRAW;
            }
            else if (ihit == FAKEOPENDIR) {
                if (f.selected_volume)
                    SFSaveDisk = CW(-f.selected_volume);
                f.desktop = FALSE;
                CurDirStore = *SF_FTYPE_XP(&f);
                unixcd(&f);
                ihit = FAKEREDRAW;
            }
            else if (ihit == sfItemNewFolderUser) {
                if (do_new_folder(&f)) {
                    CurDirStore = *SF_FTYPE_XP(&f);
                    unixcd(&f);
                    ihit = FAKEREDRAW;
                }
            }
            if (getorput == put) {
                Str255 file_name;
                boolean_t writable;

                GetIText(HIDDEN_NATIVE(pnhand), file_name);
                str31assign(SF_NAME(&f), file_name);
                getdiskname(NULL, &writable);
                if ((SF_NAME(&f))[0] && writable && (!f.desktop || f.desktop_files) && !sav) {
                    HiliteControl((ControlHandle)HIDDEN_NATIVE(sahand), 0);
                    sav = TRUE;
                }
                else if ((!(SF_NAME(&f)[0]) || !writable || (f.desktop && !f.desktop_files)) && sav) {
                    HiliteControl((ControlHandle)HIDDEN_NATIVE(sahand), 255);
                    sav = FALSE;
                }
            }
            const BOOLEAN disk_event = choice ? GetOSEvent(diskMask, &evt) : WaitNextEvent(diskMask, &evt, 4, 0);
            if (disk_event && (evt.message & CLC(0xFFFF0000)) == 0) {
                FIELD_ASSIGN_RAW(pbr.volumeParam.ioNamePtr, 0);
                pbr.volumeParam.ioVolIndex = 0;
                pbr.volumeParam.ioVRefNum = CW(CL(evt.message) & 0xFFFF);
                err = PBGetVInfo(&pbr, FALSE);
                gui_assert(err == noErr);
                if (err == noErr) {
                    adjustdrivebutton(dp);
                    SFSaveDisk = CW(-CW(pbr.volumeParam.ioVRefNum));
                    CurDirStore = CLC(2);
                    ihit = FAKEREDRAW;
                }
            }
            if (ihit == FAKEREDRAW)
                realcd((DialogPeek)dp, CL(CurDirStore));
            if (!done && choice == romlib_create_folder)
                HiliteControl(f.flch, f.desktop && !f.desktop_files ? 255 : 0);
        }
        if (f.flavor != original_sf && dh.odh)
            ihit = ROMlib_CALLDHOOK(&f, -2, dp, dh); /* the mac does this */
#if defined(SDL)
        file_dialog_prefs_save(getorput == put, CW(dp->portRect.right) - CW(dp->portRect.left),
                               CW(dp->portRect.bottom) - CW(dp->portRect.top),
                               -CW(PORT_BOUNDS((WindowPtr)dp).left) + CW(dp->portRect.left),
                               -CW(PORT_BOUNDS((WindowPtr)dp).top) + CW(dp->portRect.top));
#endif
        flfinit(&f);
        destroy_new_folder_button(dp, new_folder_button);
        CloseDialog((DialogPtr)dp);
        if (!choice)
            makeworking(&f);
        SetPort(gp);
    }
finished:
    TRAPEND();
    return status;
}

OSErr ROMlib_choose_catalog(ROMlib_catalog_choice choice, StringPtr title, StandardFileReply* reply)
{
    return spfcommon((Point){-1, -1}, title, NULL, (dialog_hook_u){0}, (reply_u){.nreplyp = reply}, sfGetDialogID,
                     (filter_u){0}, (file_filter_u){0}, -1, NULL, get, new_sf, NULL, NULL, NULL, choice);
}

P7(PUBLIC pascal trap, void, SFPPutFile, Point, p, StringPtr, prompt, StringPtr, name, ProcPtr, dh, SFReply*, rep,
   INTEGER, dig, ProcPtr, fp)
{
    dialog_hook_u dhu;
    reply_u repu;
    filter_u filteru;
    file_filter_u zero_file_filter = {0};

    dhu.odh = dh;
    repu.oreplyp = rep;
    filteru.ofilterp = fp;

    spfcommon(p, prompt, name, dhu, repu, dig, filteru, zero_file_filter, -1, (OSType*)0, put, original_sf, 0, 0, 0,
              romlib_choose_default);
}

P5(PUBLIC pascal trap, void, SFPutFile, Point, p, StringPtr, prompt, StringPtr, name, ProcPtr, dh, SFReply*, rep)
{
    SFPPutFile(p, prompt, name, dh, rep, putDlgID, (ProcPtr)0);
}

P9(PUBLIC pascal trap, void, SFPGetFile, Point, p, StringPtr, prompt, ProcPtr, filef, INTEGER, numt, SFTypeList, tl,
   ProcPtr, dh, SFReply*, rep, INTEGER, dig, ProcPtr, fp)
{
    dialog_hook_u dhu;
    reply_u repu;
    filter_u filteru;
    file_filter_u file_filteru;

    dhu.odh = dh;
    repu.oreplyp = rep;
    filteru.ofilterp = fp;
    file_filteru.oflfilef = filef;

    spfcommon(p, prompt, (StringPtr) "", dhu, repu, dig, filteru, file_filteru, numt, tl, get, original_sf, 0, 0, 0,
              romlib_choose_default);
}

P7(PUBLIC pascal trap, void, SFGetFile, Point, p, StringPtr, prompt, ProcPtr, filef, INTEGER, numt, SFTypeList, tl,
   ProcPtr, dh, SFReply*, rep)
{
    SFPGetFile(p, prompt, filef, numt, tl, dh, rep, getDlgID, (ProcPtr)0);
}

P10(PUBLIC pascal trap, void, CustomPutFile, Str255, prompt, Str255, defaultName, StandardFileReply*, replyp, INTEGER,
    dlgid, Point, where, DlgHookYDProcPtr, dlghook, ModalFilterYDProcPtr, filterproc, Ptr, activeList,
    ActivateYDProcPtr, activateproc, UNIV Ptr, yourdatap)
{
    dialog_hook_u dhu;
    reply_u repu;
    filter_u filteru;
    file_filter_u file_filteru;

    dhu.cdh = dlghook;
    repu.nreplyp = replyp;
    file_filteru.cflfilef = 0;
    filteru.cfilterp = filterproc;

    if (dlgid == 0)
        dlgid = sfPutDialogID;

    spfcommon(where, prompt, defaultName, dhu, repu, dlgid, filteru, file_filteru, -1, 0, put, new_custom_sf,
              activeList, activateproc, yourdatap, romlib_choose_default);
}

P11(PUBLIC pascal trap, void, CustomGetFile, FileFilterYDProcPtr, filefilter, INTEGER, numtypes, SFTypeList, typelist,
    StandardFileReply*, replyp, INTEGER, dlgid, Point, where, DlgHookYDProcPtr, dlghook, ModalFilterYDProcPtr,
    filterproc, Ptr, activeList, ActivateYDProcPtr, activateproc, UNIV Ptr, yourdatap)
{
    dialog_hook_u dhu;
    reply_u repu;
    filter_u filteru;
    file_filter_u file_filteru;

    dhu.cdh = dlghook;
    repu.nreplyp = replyp;
    filteru.cfilterp = filterproc;
    file_filteru.cflfilef = filefilter;

    if (dlgid == 0)
        dlgid = sfGetDialogID;

    spfcommon(where, (StringPtr) "", (StringPtr) "", dhu, repu, dlgid, filteru, file_filteru, numtypes, typelist, get,
              new_custom_sf, activeList, activateproc, yourdatap, romlib_choose_default);
}

P4(PUBLIC pascal trap, void, StandardGetFile, ProcPtr, filef, INTEGER, numt, SFTypeList, tl, StandardFileReply*, replyp)
{
    Point p;
    reply_u repu;
    file_filter_u file_filteru;
    dialog_hook_u dhu;
    filter_u filteru;

    repu.nreplyp = replyp;
    file_filteru.oflfilef = filef;
    dhu.cdh = 0;
    filteru.cfilterp = 0;

    p.h = -1;
    p.v = -1;
    spfcommon(p, (StringPtr) "", (StringPtr) "", dhu, repu, sfGetDialogID, filteru, file_filteru, numt, tl, get, new_sf,
              0, 0, 0, romlib_choose_default);
}

P3(PUBLIC pascal trap, void, StandardPutFile, Str255, prompt, Str255, defaultname, StandardFileReply*, replyp)
{
    Point p;
    reply_u repu;
    file_filter_u file_filteru;
    dialog_hook_u dhu;
    filter_u filteru;

    filteru.cfilterp = 0;
    repu.nreplyp = replyp;
    file_filteru.cflfilef = 0;
    dhu.cdh = 0;

    p.h = -1;
    p.v = -1;
    spfcommon(p, prompt, defaultname, dhu, repu, sfPutDialogID, filteru, file_filteru, -1, 0, put, new_sf, 0, 0, 0,
              romlib_choose_default);
}
