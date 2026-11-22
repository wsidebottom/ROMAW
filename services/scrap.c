/*+===================================================================
  File:      scrap.c

  Summary:   Implements scrap support for Macintosh Toolbox services.

  Classes:   None.

  Functions: ROMlib_ZeroScrap, SDL_Surface_pitch, SDL_Surface_pixels, SDL_colors, SDL_n_colors, count_char,
             get_scrap_helper, get_scrap_helper_dib, put_scrap_helper_dib

  Origin:    Existing repository source in executor/src/services; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1987, 1989, 1990, 1999 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ScrapMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "FileMgr.h"
#include "ScrapMgr.h"
#include "MemoryMgr.h"
#include "ResourceMgr.h"
#include "OSUtil.h"
#include "CQuickDraw.h"

#include "rsys/notmac.h"
#include "rsys/file.h"
#include "rsys/scrap.h"
#include "rsys/romaw.h"
#include "rsys/cquick.h"

#if defined(CYGWIN32) && defined(SDL)
#include "sdlscrap.h"
#include "win_clip.h"
#endif

P0(PUBLIC pascal trap, PScrapStuff, InfoScrap)
{
    if (Cx(ScrapState) < 0)
        ZeroScrap();
    return ((PScrapStuff)&ScrapSize);
}

/* Keep the classic Scrap Manager handle for applications that inspect it,
   but never spill it to a Macintosh file. Host clipboard backends publish
   and retrieve the actual clipboard formats. */
P0(PUBLIC pascal trap, LONGINT, UnloadScrap)
{
    if (ScrapHandle && Cx(ScrapState) == 0)
        ScrapState = CWC(1);
    return noErr;
}

P0(PUBLIC pascal trap, LONGINT, LoadScrap)
{
    if (!ScrapHandle || Cx(ScrapState) < 0)
        return noScrapErr;
    ScrapState = CWC(1);
    return noErr;
}

A0(PUBLIC, LONGINT, ROMlib_ZeroScrap)
{
    THz saveZone;

    if (!ScrapHandle || Cx(ScrapState) < 0) {
        Handle handle;
        saveZone = TheZone;
        HIDDEN_RAW_SET(TheZone_H, SysZone);
        handle = NewHandle((Size)0);
        HIDDEN_RAW_SET(TheZone_H, saveZone);
        if (!handle)
            return memFullErr;
        HIDDEN_RAW_SET(ScrapHandle_H, RM(handle));
        ScrapCount = 0;
    }
    else {
        SetHandleSize(MR(ScrapHandle), (Size)0);
        if (MemErr != noErr)
            return CW(MemErr);
    }
    HIDDEN_RAW_SET(ScrapName_H, RM((StringPtr) "\000"));
    ScrapState = CWC(1);
    ScrapSize = 0;
    ScrapCount = CW((uint16)CW(ScrapCount) + 1);
    return noErr;
}

P0(PUBLIC pascal trap, LONGINT, ZeroScrap)
{
    return ROMlib_ZeroScrap();
}

P3(PUBLIC pascal trap, LONGINT, PutScrap, LONGINT, len, ResType, rest, Ptr, p)
{
    LONGINT old_size, padded, total;
    unsigned char* data;
    ptrdiff_t source_offset = -1;
    Handle handle;
    OSErr retval;

    if (len < 0 || (len && !p) || len > INT32_MAX - 9)
        return paramErr;
    if (Cx(ScrapState) < 0 || !ScrapHandle) {
        retval = ZeroScrap();
        if (retval != noErr)
            return retval;
    }
    old_size = Cx(ScrapSize);
    padded = (len + 1) & ~1L;
    if (old_size < 0 || old_size > INT32_MAX - 8 - padded)
        return memFullErr;
    total = old_size + 8 + padded;
    handle = MR(ScrapHandle);
    data = (unsigned char*)STARH(handle);
    if (len && (uintptr_t)p >= (uintptr_t)data && (uintptr_t)p - (uintptr_t)data < (uintptr_t)old_size) {
        source_offset = (uintptr_t)p - (uintptr_t)data;
        if (len > old_size - source_offset)
            return paramErr;
    }
    SetHandleSize(handle, total);
    if (MemErr != noErr)
        return CW(MemErr);
    data = (unsigned char*)STARH(handle);
    if (source_offset >= 0)
        p = (Ptr)(data + source_offset);
    {
        LONGINT fields[2] = {CL(rest), CL(len)};
        memcpy(data + old_size, fields, sizeof fields);
    }
    if (len)
        memmove(data + old_size + 8, p, len);
    if (padded != len)
        data[total - 1] = 0;
    ScrapState = CWC(1);
    ScrapSize = CL(total);
#if defined(X) || defined(SDL)
    PutScrapX(rest, len, (char*)(data + old_size + 8), CW(ScrapCount));
#endif
    return noErr;
}

#if defined(CYGWIN32)
PUBLIC int count_char(const char* p, int len, char c)
{
    int retval;

    retval = 0;
    while (--len >= 0)
        if (*p++ == c)
            ++retval;
    return retval;
}

PRIVATE void memcpy_but_delete_char(char* destp, const char* srcp, int len, char to_del)
{
    while (--len >= 0) {
        char c;

        c = *srcp++;
        if (c != to_del)
            *destp++ = c;
    }
}

PUBLIC int get_scrap_helper(void* vh, void* lp, int len, boolean_t convert_text)
{
    int retval;
    int new_len;
    Handle h;

    if (convert_text)
        new_len = len - count_char(lp, len, '\n'); /* won't copy linefeeds */
    else
        new_len = len;
    h = (Handle)vh;
    ReallocHandle(h, new_len);
    if (MemErr != noErr)
        retval = -1;
    else {
        if (convert_text)
            memcpy_but_delete_char(STARH(h), lp, len, '\n');
        else
            memcpy(STARH(h), lp, len);
        retval = new_len;
    }
    return retval;
}
#endif

#define RETURN(x) return (temph ? (DisposHandle(temph), 0) : 0), x

/* A null destination asks for an offset into InfoScrap's handle. Import host
   data there without publishing it back to Windows or discarding other types. */
static OSErr cache_scrap_query(Handle source, ResType type, LONGINT length, LONGINT* offset_out)
{
    LONGINT size, at = 0, replaced = 0, padded, total;
    Handle scrap;
    unsigned char* data;
    OSErr err = noErr;

    if (length < 0 || length > INT32_MAX - 9 || length > GetHandleSize(source))
        return noTypeErr;
    HLock(source);
    if (Cx(ScrapState) < 0 || !ScrapHandle) {
        err = ZeroScrap();
        if (err != noErr)
            goto done;
    }
    scrap = MR(ScrapHandle);
    size = Cx(ScrapSize);
    if (size < 0 || size > GetHandleSize(scrap)) {
        err = noTypeErr;
        goto done;
    }
    data = (unsigned char*)STARH(scrap);
    while (at < size) {
        LONGINT fields[2], bytes;
        if (size - at < 8) {
            err = noTypeErr;
            goto done;
        }
        memcpy(fields, data + at, sizeof fields);
        bytes = CL(fields[1]);
        if (bytes < 0 || bytes > size - at - 8 || ((bytes + 1) & ~1L) > size - at - 8) {
            err = noTypeErr;
            goto done;
        }
        if (CL(fields[0]) == type) {
            if (bytes == length && (!length || !memcmp(data + at + 8, STARH(source), length))) {
                *offset_out = CL(at + 8);
                goto done;
            }
            replaced = 8 + ((bytes + 1) & ~1L);
            break;
        }
        at += 8 + ((bytes + 1) & ~1L);
    }
    padded = (length + 1) & ~1L;
    if (size - replaced > INT32_MAX - 8 - padded) {
        err = memFullErr;
        goto done;
    }
    total = size - replaced + 8 + padded;
    if (total > GetHandleSize(scrap)) {
        SetHandleSize(scrap, total);
        if (MemErr != noErr) {
            err = CW(MemErr);
            goto done;
        }
    }
    data = (unsigned char*)STARH(scrap);
    memmove(data + at + 8 + padded, data + at + replaced, size - at - replaced);
    {
        LONGINT fields[2] = {CL(type), CL(length)};
        memcpy(data + at, fields, sizeof fields);
    }
    if (length)
        memcpy(data + at + 8, STARH(source), length);
    if (padded != length)
        data[at + 8 + length] = 0;
    ScrapSize = CL(total);
    ScrapCount = CW((uint16)CW(ScrapCount) + 1);
    *offset_out = CL(at + 8);
done:
    HUnlock(source);
    return err;
}

P3(PUBLIC pascal trap, LONGINT, GetScrap, Handle, h, ResType, rest, LONGINT*, off)
{
    OSErr retval;
    LONGINT offset = 0, size, length;
    Handle temph = NULL;
    Handle scrap;
    unsigned char* data;

    /* MacWrite passes guest address zero for this required output. */
    if (!off)
        off = (LONGINT*)SYN68K_TO_US(0);
    if (!h) {
        temph = NewHandle((Size)0);
        if (!temph)
            return memFullErr;
        h = temph;
    }
#if defined(X) || defined(SDL)
    length = GetScrapX(rest, (char**)h);
    if (length >= 0) {
        if (temph) {
            retval = cache_scrap_query(temph, rest, length, off);
            RETURN(retval == noErr ? length : retval);
        }
        *off = 0;
        RETURN(length);
    }
#endif
    if (Cx(ScrapState) < 0 || !ScrapHandle) {
        retval = ZeroScrap();
        if (retval != noErr)
            RETURN(retval);
    }
    scrap = MR(ScrapHandle);
    size = Cx(ScrapSize);
    if (size < 0 || size > GetHandleSize(scrap))
        RETURN(noTypeErr);
    HLock(scrap);
    data = (unsigned char*)STARH(scrap);
    while (offset <= size - 8) {
        LONGINT fields[2];
        memcpy(fields, data + offset, sizeof fields);
        length = CL(fields[1]);
        if (length < 0 || length > size - offset - 8)
            break;
        if (rest == CL(fields[0])) {
            retval = PtrToXHand((Ptr)(data + offset + 8), h, length);
            HUnlock(scrap);
            if (retval != noErr)
                RETURN(retval);
            *off = CL(offset + 8);
            RETURN(length);
        }
        if (((length + 1) & ~1L) > size - offset - 8)
            break;
        offset += 8 + ((length + 1) & ~1L);
    }
    HUnlock(scrap);
    RETURN(noTypeErr);
}

#if defined(CYGWIN32) && defined(SDL)

#include "SDL/SDL.h"

PRIVATE int SDL_Surface_depth(const SDL_Surface* surfp)
{
    int retval;
    retval = surfp->format->BitsPerPixel;
    return retval;
}

PRIVATE int SDL_Surface_width(const SDL_Surface* surfp)
{
    int retval;
    retval = surfp->w;
    return retval;
}

PRIVATE int SDL_Surface_height(const SDL_Surface* surfp)
{
    int retval;
    retval = surfp->h;
    return retval;
}

PUBLIC void* SDL_Surface_pixels(const SDL_Surface* surfp)
{
    void* retval;
    retval = surfp->pixels;
    return retval;
}

PUBLIC int SDL_Surface_pitch(const SDL_Surface* surfp)
{
    int retval;
    retval = surfp->pitch;
    return retval;
}

PUBLIC int SDL_n_colors(const SDL_Surface* surfp)
{
    int retval;
    retval = surfp->format->palette->ncolors;
    return retval;
}

PUBLIC SDL_Color* SDL_colors(const SDL_Surface* surfp)
{
    SDL_Color* retval;
    retval = surfp->format->palette->colors;
    return retval;
}

typedef struct {
    uint8 blue PACKED;
    uint8 red PACKED;
    uint8 green PACKED;
} sdl_pixel24;

typedef struct {
    uint8 zero PACKED;
    uint8 red PACKED;
    uint8 green PACKED;
    uint8 blue PACKED;
} mac_pixel32;

#define advance_n_bytes(ptrp, n_bytes)                          \
    ({                                                          \
        typeof(ptrp) _ptrp;                                     \
                                                                \
        _ptrp = (ptrp);                                         \
        *(_ptrp) = (typeof(*_ptrp))((char*)*(_ptrp) + n_bytes); \
    })

#define MAC_COLOR_COMPONENT_FROM_SDL_CC(x) \
    ({                                     \
        typeof(x) _x;                      \
                                           \
        _x = (x);                          \
        (_x << 8) | (uint8)_x;             \
    })

PRIVATE CTabHandle ctab_from_surface(SDL_Surface* surfp)
{
    CTabHandle retval;
    int n_colors;
    SDL_Color* ip;
    ColorSpec* op;
    int i;

    retval = NULL;

    n_colors = SDL_n_colors(surfp);
    if (n_colors <= 0 || n_colors > 256)
        return NULL;
    retval = (CTabHandle)NewHandle(CTAB_STORAGE_FOR_SIZE(n_colors - 1));
    if (!retval)
        return NULL;
    CTAB_SIZE_X(retval) = CW(n_colors - 1);
    CTAB_SEED_X(retval) = CL(GetCTSeed());
    CTAB_FLAGS_X(retval) = CTAB_GDEVICE_BIT_X;

    for (i = 0, ip = SDL_colors(surfp), op = CTAB_TABLE(retval); i < n_colors; ++i, ++ip, ++op) {
        op->value = CWC(0);
        op->rgb.red = MAC_COLOR_COMPONENT_FROM_SDL_CC(ip->r);
        op->rgb.green = MAC_COLOR_COMPONENT_FROM_SDL_CC(ip->g);
        op->rgb.blue = MAC_COLOR_COMPONENT_FROM_SDL_CC(ip->b);
    }

    return retval;
}

PRIVATE GWorldPtr gworld_from_surface(SDL_Surface* surfp)
{
    GWorldPtr retval;

    retval = NULL;

    if (surfp) {
        QDErr err;
        int surf_depth;
        CTabHandle ctab;

        ctab = NULL;

        surf_depth = SDL_Surface_depth(surfp);
        switch (surf_depth) {
        case 8:
            ctab = ctab_from_surface(surfp);
            if (!ctab)
                return NULL;
            break;
        case 32:
            break;
        default:
            warning_unexpected("surf_depth = %d", surf_depth);
            surf_depth = 0;
            break;
        }

        if (surf_depth) {
            int n_lines;
            int pixels_per_line;
            Rect r;

            n_lines = SDL_Surface_height(surfp);
            pixels_per_line = SDL_Surface_width(surfp);

            r.top = CWC(0);
            r.left = CWC(0);
            r.bottom = CW(n_lines);
            r.right = CW(pixels_per_line);
            {
                HIDDEN_CGrafPtr save_port;
                HIDDEN_GDHandle save_device;

                GetGWorld(&save_port, &save_device);
                HIDDEN_GWorldPtr world_out = {0};
                err = NewGWorld(&world_out, surf_depth, &r, ctab, NULL, keepLocal);
                retval = HIDDEN_NATIVE(world_out);
                SetGWorld(HIDDEN_NATIVE(save_port), HIDDEN_NATIVE(save_device));
            }
            if (ctab)
                DisposHandle((Handle)ctab);
            if (err != noErr)
                return NULL;
            if (retval) {
                PixMapHandle pm;

                pm = GetGWorldPixMap(retval);
                if (!pm || !LockPixels(pm)) {
                    DisposeGWorld(retval);
                    return NULL;
                }
                if (SDL_LockSurface(surfp) != 0) {
                    UnlockPixels(pm);
                    DisposeGWorld(retval);
                    return NULL;
                }
                if (!GetPixBaseAddr(pm)) {
                    SDL_UnlockSurface(surfp);
                    UnlockPixels(pm);
                    DisposeGWorld(retval);
                    return NULL;
                }

                switch (surf_depth) {
                case 8: {
                    uint8 *ip, *eip;
                    uint8* op;
                    int rowbytes;
                    int pitch;

                    pitch = SDL_Surface_pitch(surfp);
                    rowbytes = PIXMAP_ROWBYTES(pm);

                    ip = SDL_Surface_pixels(surfp);
                    op = (typeof(op))GetPixBaseAddr(pm);
                    eip = ip + n_lines * pitch;
                    for (; ip != eip; ip += pitch, op += rowbytes)
                        memcpy(op, ip, rowbytes);
                    break;
                }

                case 32: {
                    sdl_pixel24* ip;
                    mac_pixel32* op;

                    op = (typeof(op))GetPixBaseAddr(pm);
                    ip = SDL_Surface_pixels(surfp);

                    memcpy(op, ip, n_lines * pixels_per_line * sizeof *op);

                    break;
                }
                default:
                    warning_unexpected("surf_depth = %d", surf_depth);
                    break;
                }
                SDL_UnlockSurface(surfp);
                UnlockPixels(pm);
            }
        }
    }
    return retval;
}

PRIVATE PicHandle pict_from_gworld(GWorldPtr gp, int* lenp)
{
    PicHandle retval;

    if (!gp)
        retval = NULL;
    else {
        Rect pict_frame;
        PixMapHandle pm;

        pm = GetGWorldPixMap(gp);
        pict_frame = PIXMAP_BOUNDS(pm);
        retval = OpenPicture(&pict_frame);
        if (retval) {
            ClipRect(&pict_frame);
            HLock((Handle)pm);
            CopyBits((BitMap*)STARH(pm), PORT_BITS_FOR_COPY(thePort), &pict_frame, &pict_frame, srcCopy, NULL);
            OSErr picture_error = MemError();
            HUnlock((Handle)pm);
            ClosePicture();
            if (picture_error != noErr) {
                KillPicture(retval);
                retval = NULL;
            }
        }
    }
    return retval;
}

PUBLIC int get_scrap_helper_dib(void* vh, void* lp, size_t length)
{
    SDL_Surface* surfp;
    GWorldPtr gp;
    PicHandle pich;
    Handle h;
    int retval;
    int len;

    if (!vh)
        return -1;
    surfp = surface_from_dib(lp, length);
    if (!surfp)
        return -1;
    gp = gworld_from_surface(surfp);
    SDL_FreeSurface(surfp);
    if (!gp)
        return -1;
    pich = pict_from_gworld(gp, &len);
    DisposeGWorld(gp);
    if (!pich)
        return -1;
    h = (Handle)vh;
    len = GetHandleSize((Handle)pich);
    if (MemErr != noErr)
        retval = -1;
    else {
        ReallocHandle(h, len);
        if (MemErr != noErr)
            retval = -1;
        else {
            memcpy(STARH(h), STARH(pich), len);
            retval = len;
        }
    }
    DisposHandle((Handle)pich);
    return retval;
}

PRIVATE PicHandle pict_from_lp(const void* lp)
{
    PicHandle retval;

    if (!lp)
        retval = NULL;
    else {
        int len;

        len = *(int*)lp;
        retval = (PicHandle)NewHandle(len);
        if (retval) {
            char* p;

            p = (char*)STARH(retval);
            if (p)
                memcpy(p, (char*)lp + sizeof(int), len);
            else {
                warning_unexpected(NULL_STRING);
                DisposHandle((Handle)retval);
                retval = NULL;
            }
        }
    }

    return retval;
}

PRIVATE GWorldPtr gworld_from_pict(PicHandle ph)
{
    GWorldPtr retval;

    retval = NULL;
    if (ph) {
        HIDDEN_CGrafPtr save_port;
        HIDDEN_GDHandle save_device;
        Rect r;
        OSErr err;

        GetGWorld(&save_port, &save_device);
        r = HxX(ph, picFrame);
        HIDDEN_GWorldPtr world_out = {0};
        err = NewGWorld(&world_out, 32, &r, NULL, NULL, keepLocal);
        retval = HIDDEN_NATIVE(world_out);
        if (retval) {
            PixMapHandle pm;

            SetGWorld(retval, NULL);
            pm = GetGWorldPixMap(retval);
            LockPixels(pm);
            DrawPicture(ph, &r);
#if 0
#warning THIS INTERFERES WITH PICT PASTING
	  {
	    char *p;

	    EraseRect (&r);
	    p = GetPixBaseAddr (pm);
	    memset (p, 0x00, 4 * RECT_HEIGHT(&r) * RECT_WIDTH (&r));
	    memset (p, 0xFF, 4 * RECT_HEIGHT(&r) * RECT_WIDTH (&r) / 2);
	  }
#endif
            UnlockPixels(pm);
        }
        SetGWorld(HIDDEN_NATIVE(save_port), HIDDEN_NATIVE(save_device));
    }
    return retval;
}

PRIVATE SDL_Surface* surface_from_gworld(GWorldPtr gp)
{
    SDL_Surface* retval;

    if (!gp)
        retval = NULL;
    else {
        int pixels_per_line;
        int n_lines;
        PixMapHandle pm;

        enum { A = 0x00000000, R = 0x0000FF00, G = 0x00FF0000, B = 0xFF000000 };

        mac_pixel32* ip;
        sdl_pixel24* op;
        Rect r;

        pm = GetGWorldPixMap(gp);
        LockPixels(pm);

        r = PIXMAP_BOUNDS(pm);
        n_lines = RECT_HEIGHT(&r);
        pixels_per_line = RECT_WIDTH(&r);
        retval = SDL_AllocSurface(SDL_SWSURFACE, pixels_per_line, n_lines, 32, R, G, B, A);
        SDL_LockSurface(retval);
        op = SDL_Surface_pixels(retval);
        ip = (typeof(ip))GetPixBaseAddr(pm);
        memcpy(op, ip, n_lines * pixels_per_line * sizeof *ip);
#if 0
#warning THIS IS BROKEN
      memset (op, 0x00, 4 * n_lines * pixels_per_line);
      memset (op, 0xFF, 4 * n_lines * pixels_per_line / 2);
#endif
        SDL_UnlockSurface(retval);
        UnlockPixels(pm);
    }

    return retval;
}

/* write_surfp_to_clipboard */

PUBLIC void put_scrap_helper_dib(void* lp)
{
    PicHandle pich;

    pich = pict_from_lp(lp);
    if (pich) {
        GWorldPtr gp;

        gp = gworld_from_pict(pich);
        if (gp) {
            SDL_Surface* surfp;

            surfp = surface_from_gworld(gp);
            if (surfp) {
                write_surfp_to_clipboard(surfp);
                SDL_FreeSurface(surfp);
            }
            DisposeGWorld(gp);
        }
        DisposHandle((Handle)pich);
    }
}

#endif
