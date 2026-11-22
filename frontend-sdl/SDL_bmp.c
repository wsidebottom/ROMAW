/*+===================================================================
  File:      SDL_bmp.c

  Summary:   > I *really* want to get 2.1pre0 out this weekend. Because of the > GWorld leakage I don't plan on
             finishing the cut and paste code, but > I'd like to actually include what I have now (but turned off by >
             default) so that I'll have something to compare the new code to.

  Classes:   None.

  Functions: SDL_BMP_read_helper, SDL_BMP_write_helper, SDL_LoadBMP_ctmhackage, SDL_LoadCF_DIB, SDL_SaveBMP_ctmhackage,
             SDL_SaveCF_DIB

  Origin:    http://www.devolution.com/~slouken/SDL/

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* NOTE: Normally SDL_bmp.c would be part of SDL, not ROMAW, but this
         hacked up copy is special.  See below.  */

/*
Return-Path: <ftp!devolution.com!slouken>
Received: from gw.ardi.com by beaut.ardi.com
        via rsmtp with bsmtp
        id <m10LZQX-000RjhC@beaut.ardi.com>
        for <ctm>; Fri, 12 Mar 1999 14:25:05 -0700 (MST)
        (Smail-3.2.0.91 1997-Jan-14 #16 built 1999-Feb-9)
Received: from roboto.devolution.com(really [204.247.175.130]) by gw.ardi.com
        via sendmail with esmtp (ident root using rfc1413)
        id <m10LZPt-000zOCC@gw.ardi.com>
        for <ctm@ardi.com>; Fri, 12 Mar 1999 14:24:25 -0700 (MST)
        (Smail-3.2.0.91 1997-Jan-14 #27 built 1998-May-1)
Received: from slouken by roboto.devolution.com with local (Exim 2.11 #1)
        id 10LZQ9-0004kq-00; Fri, 12 Mar 1999 13:24:41 -0800
X-Mailer: My Mailer 1.5  <slouken@devolution.com>
Message-Id: <E10LZQ9-0004kq-00@roboto.devolution.com>
From: Sam Lantinga <slouken@devolution.com>
To: "Clifford T. Matthews" <ctm@ardi.com>
Cc: slouken@devolution.com
Subject: Re: permission to include hacked up copy of SDL_bmp.c in Executor?
Date: Fri, 12 Mar 1999 13:24:41 -0800

> Hi Sam,

> I *really* want to get 2.1pre0 out this weekend.  Because of the
> GWorld leakage I don't plan on finishing the cut and paste code, but
> I'd like to actually include what I have now (but turned off by
> default) so that I'll have something to compare the new code to.

> Since you're going to redo SDL_BMP, I'd rather just use my tweaked
> code internally for this release and then cut it out when I can use
> the new SDL.  This violates GPL, but since you're the copyright holder
> you can allow me to use it under non-GPL terms.

Yup, go for it. :)

        -Sam Lantinga                           (slouken@devolution.com)

Lead Programmer, Loki Entertainment Software
--
Author of Simple DirectMedia Layer -
        http://www.devolution.com/~slouken/SDL/
--

 */

/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997, 1998  Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Sam Lantinga
    5635-34 Springhouse Dr.
    Pleasanton, CA 94588 (USA)
    slouken@devolution.com
*/

/*
   Code to load and save surfaces in Windows BMP format.

   Why support BMP format?  Well, it's a native format for Windows, and
   most image processing programs can read and write it.  It would be nice
   to be able to have at least one image format that we can natively load
   and save, and since PNG is so complex that it would bloat the library,
   BMP is a good alternative. 

   Clipboard DIB input supports indexed 1/4/8-bit and RGB/bitfields 16/24/32-bit
   images. Output is uncompressed indexed 8-bit or RGB 24-bit.
*/

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>

#include "SDL/SDL_error.h"
#include "SDL/SDL_video.h"
#include "SDL/SDL_endian.h"
#include "SDL/SDL_version.h"

#include "rsys/error.h"

#include "for_sam.h"

#if SDL_MAJOR_VERSION > 0 || SDL_MINOR_VERSION >= 9

#define READLE_DECL(width)                             \
    static Uint##width ctm_ReadLE##width(FILE* fp)     \
    {                                                  \
        Uint##width retval;                            \
                                                       \
        retval = 0;                                    \
        if (fread(&retval, sizeof retval, 1, fp) != 1) \
            SDL_SetError("EOF on datastream");         \
                                                       \
        return retval;                                 \
    }

READLE_DECL(16)
READLE_DECL(32)

#define WRITELE_DECL(width)                                     \
    static int ctm_WriteLE##width(FILE* fp, Uint##width value)  \
    {                                                           \
        int retval;                                             \
                                                                \
        if (fwrite(&value, sizeof value, 1, fp) == 1)           \
            retval = 1;                                         \
        else {                                                  \
            retval = 0;                                         \
            SDL_SetError("Couldn't write value to datastream"); \
        }                                                       \
        return retval;                                          \
    }

WRITELE_DECL(16)
WRITELE_DECL(32)

#define SDL_ReadLE16 ctm_ReadLE16
#define SDL_ReadLE32 ctm_ReadLE32
#define SDL_WriteLE16 ctm_WriteLE16
#define SDL_WriteLE32 ctm_WriteLE32

#endif

/* Compression encodings for BMP files */
#ifndef BI_RGB
#define BI_RGB 0
#define BI_RLE8 1
#define BI_RLE4 2
#define BI_BITFIELDS 3
#endif

#define MAGIC "BM"

/* NOTE: We are *NOT* trying to construct a one to one mapping for the
   stdio routines open, close, read, write, etc.  Instead we're custom
   crafting an object that can handle reading of BMP and CF_DIB and a
   separate object for writing.  You can't mix and match the reading
   routines with the writing routines, which is why we have them separated
   in the file.  Mixing them together into one object that looks like it
   can handle reads and writes would be a bad idea, since we'd have to
   worry a lot more about the interaction between the state information. */

/* BMP-specific state information for reading*/
typedef struct {
    const char* filename;
    FILE* fp;
    Sint32 bfOffBits;
} private_bmp_read_t;

/* CF_DIB-specific state information for reading */
typedef struct {
    const unsigned char* p;
    size_t remaining;
} private_cfdib_read_t;

/* union of all supported bmp/dib thingies for reading */
typedef union {
    private_bmp_read_t bmp;
    private_cfdib_read_t cfdib;
} dib_read_obj_private_t;

typedef struct dib_read_obj dib_read_obj_t;

/* access methods and common state information for reading */
struct dib_read_obj {
    void (*read_init)(dib_read_obj_t*);
    void (*read_seek)(dib_read_obj_t*);
    Uint32 (*readle32)(dib_read_obj_t*);
    Uint16 (*readle16)(dib_read_obj_t*);
    int (*readle8)(dib_read_obj_t*);
    int (*read)(dib_read_obj_t*, void*, int);
    void (*read_close)(dib_read_obj_t*);
    bool (*read_no_masks)(dib_read_obj_t*, int);
    bool error;
    dib_read_obj_private_t u;
};

/* bmp-specific initialization routine */

static void bmp_read_init(dib_read_obj_t* obj)
{
    obj->error = false;
    obj->u.bmp.fp = fopen(obj->u.bmp.filename, "rb");
    if (obj->u.bmp.fp == NULL) {
        SDL_SetError("Couldn't open %s for reading", obj->u.bmp.filename);
        obj->error = true;
    }
    else {
        char magic[2];

        /* Read in the BMP file header */
        SDL_ClearError();
        if (fread(magic, sizeof magic, 1, obj->u.bmp.fp) != 1) {
            SDL_SetError("Error reading from %s", obj->u.bmp.filename);
            obj->error = true;
        }
        else if (memcmp(magic, MAGIC, sizeof magic) != 0) {
            SDL_SetError("%s is not a Windows BMP file", obj->u.bmp.filename);
            obj->error = true;
        }
        else {
#if SDL_MAJOR_VERSION == 0 && SDL_MINOR_VERSION < 9
            SDL_CalculateEndian();
#endif
            obj->readle32(obj); /* bfSize */
            obj->readle16(obj); /* bfReserved1 */
            obj->readle16(obj); /* bfReserved2 */
            obj->u.bmp.bfOffBits = obj->readle32(obj);
        }
    }
}

static void cfdib_read_init(dib_read_obj_t* obj)
{
    obj->error = false;
}

static void bmp_read_seek(dib_read_obj_t* obj)
{
    obj->error = (obj->error || fseek(obj->u.bmp.fp, obj->u.bmp.bfOffBits, SEEK_SET) != 0);
}

static void cfdib_read_seek(dib_read_obj_t* obj)
{
    /* Nothing to do? -- might need to round up some bytes*/
}

static bool SDL_ERROR(void)
{
    bool retval;

    retval = strcmp(SDL_GetError(), "") != 0;
    return retval;
}

static Uint32 bmp_readle32(dib_read_obj_t* obj)
{
    Uint32 retval;

    if (obj->error)
        retval = 0;
    else {
        retval = SDL_ReadLE32(obj->u.bmp.fp);
        obj->error = SDL_ERROR();
    }
    return retval;
}

static int cfdib_read(dib_read_obj_t* obj, void* dest, int count)
{
    if (obj->error || count < 0 || (size_t)count > obj->u.cfdib.remaining) {
        obj->error = true;
        SDL_SetError("Truncated clipboard bitmap");
        return -1;
    }
    if (count)
        memcpy(dest, obj->u.cfdib.p, count);
    obj->u.cfdib.p += count;
    obj->u.cfdib.remaining -= count;
    return count;
}

static Uint32 cfdib_readle32(dib_read_obj_t* obj)
{
    unsigned char p[4];
    if (cfdib_read(obj, p, 4) < 0)
        return 0;
    return (Uint32)p[0] | (Uint32)p[1] << 8 | (Uint32)p[2] << 16 | (Uint32)p[3] << 24;
}

static Uint16 bmp_readle16(dib_read_obj_t* obj)
{
    Uint16 retval;

    if (obj->error)
        retval = 0;
    else {
        retval = SDL_ReadLE16(obj->u.bmp.fp);
        obj->error = SDL_ERROR();
    }
    return retval;
}

static Uint16 cfdib_readle16(dib_read_obj_t* obj)
{
    unsigned char p[2];
    if (cfdib_read(obj, p, 2) < 0)
        return 0;
    return p[0] | (Uint16)p[1] << 8;
}

static int bmp_readle8(dib_read_obj_t* obj)
{
    int retval;

    if (obj->error)
        retval = (Uint8)-1;
    else {
        retval = fgetc(obj->u.bmp.fp);
        obj->error = retval == EOF;
        if (obj->error)
            SDL_SetError("Unexpected EOF reading %s", obj->u.bmp.filename);
    }
    return retval;
}

static int cfdib_readle8(dib_read_obj_t* obj)
{
    unsigned char value;
    return cfdib_read(obj, &value, 1) < 0 ? EOF : value;
}

static int bmp_read(dib_read_obj_t* obj, void* dest, int count)
{
    int retval;

    if (obj->error)
        retval = -1;
    else {
        retval = fread(dest, 1, count, obj->u.bmp.fp);
        obj->error = retval != count;
    }
    return retval;
}

static void bmp_read_close(dib_read_obj_t* obj)
{
    if (obj->u.bmp.fp) {
        int fret;

        fret = fclose(obj->u.bmp.fp);
        obj->u.bmp.fp = NULL;
        obj->error = obj->error || fret != 0;
    }
}

static void cfdib_read_close(dib_read_obj_t* obj)
{
    /* anything to do here? */
}

static bool bmp_read_no_masks(dib_read_obj_t* obj, int biSize)
{
    bool retval;

    retval = true; /* BI_RGB uses implicit color masks. */
    return retval;
}

static bool cfdib_read_no_masks(dib_read_obj_t* obj, int biSize)
{
    bool retval;

    retval = true;
    return retval;
}

SDL_Surface* SDL_BMP_read_helper(dib_read_obj_t* obj)
{
    Uint32 ui;
    Sint32 i, pad, row;
    bool top_down = false;
    int c;
    SDL_Surface* surface;
    Uint32 Rmask;
    Uint32 Gmask;
    Uint32 Bmask;
    SDL_Palette* palette;
    Uint8* bits;
    int ExpandBMP;

    /* The Win32 BITMAPINFOHEADER struct (40 bytes) */
    Uint32 biSize;
    Sint32 biWidth;
    Sint32 biHeight;
    Uint16 biPlanes;
    Uint16 biBitCount;
    Uint32 biCompression;
    Uint32 biSizeImage;
    Sint32 biXPelsPerMeter;
    Sint32 biYPelsPerMeter;
    Uint32 biClrUsed;
    Uint32 biClrImportant;

    Uint16 desired_bit_depth;
    Uint16 pixels_per_bit;

    surface = NULL;

    obj->read_init(obj);

    /* Read the Win32 BITMAPINFOHEADER */
    biSize = obj->readle32(obj);
    if (biSize == 12) {
        biWidth = obj->readle16(obj);
        biHeight = obj->readle16(obj);
        biPlanes = obj->readle16(obj);
        biBitCount = obj->readle16(obj);
        biCompression = BI_RGB;
        biSizeImage = 0;
        biXPelsPerMeter = 0;
        biYPelsPerMeter = 0;
        biClrUsed = 0;
        biClrImportant = 0;
    }
    else {
        biWidth = obj->readle32(obj);
        biHeight = obj->readle32(obj);
        biPlanes = obj->readle16(obj);
        biBitCount = obj->readle16(obj);
        biCompression = obj->readle32(obj);
        biSizeImage = obj->readle32(obj);
        biXPelsPerMeter = obj->readle32(obj);
        biYPelsPerMeter = obj->readle32(obj);
        biClrUsed = obj->readle32(obj);
        biClrImportant = obj->readle32(obj);
    }

    if (obj->error) {
        /* SDL_Error will already have been set by readle{16,32} */
        goto ERROR_EXIT;
    }
    if ((biSize != 12 && biSize != 40) || biWidth <= 0 || biWidth > SHRT_MAX || biHeight == 0 || biHeight == INT_MIN ||
        biPlanes != 1 ||
        (biBitCount != 1 && biBitCount != 4 && biBitCount != 8 && biBitCount != 16 && biBitCount != 24 &&
         biBitCount != 32) ||
        (biBitCount <= 8 && biClrUsed > (1U << biBitCount))) {
        obj->error = true;
        SDL_SetError("Invalid bitmap dimensions or planes");
        goto ERROR_EXIT;
    }
    if (biHeight < 0) {
        top_down = true;
        biHeight = -biHeight;
    }
    if (biHeight > SHRT_MAX ||
        ((biWidth * (unsigned long long)(biBitCount < 8 ? 8 : biBitCount) + 31) / 32) * 4 > 65535 ||
        ((biWidth * (unsigned long long)(biBitCount < 8 ? 8 : biBitCount) + 31) / 32) * 4 * biHeight > INT_MAX) {
        obj->error = true;
        SDL_SetError("Bitmap exceeds supported surface dimensions");
        goto ERROR_EXIT;
    }
    /* Expand 1 and 4 bit bitmaps to 8 bits per pixel */
    switch (biBitCount) {
    case 1:
    case 4:
        ExpandBMP = biBitCount;
        desired_bit_depth = 8;
        break;
    default:
        ExpandBMP = 0;
        desired_bit_depth = biBitCount;
        break;
    }

    /* We don't support any BMP compression right now */
    Rmask = Gmask = Bmask = 0;
    switch (biCompression) {
    case BI_RGB:
        /* If there are no masks, use the defaults */
        if (obj->read_no_masks(obj, biSize)) {
            /* Default values for the BMP format */
            switch (desired_bit_depth) {
            case 15:
            case 16:
                Rmask = 0x7C00;
                Gmask = 0x03E0;
                Bmask = 0x001F;
                break;
            case 24:
            case 32:
                Rmask = 0x00FF0000;
                Gmask = 0x0000FF00;
                Bmask = 0x000000FF;
                break;
            default:
                break;
            }
            break; /* added by ctm -- original code looks suspicious */
        }
        /* Fall through -- read the RGB masks */

    case BI_BITFIELDS:
        switch (desired_bit_depth) {
        case 15:
        case 16:
        case 32:
            Rmask = obj->readle32(obj);
            Gmask = obj->readle32(obj);
            Bmask = obj->readle32(obj);
            break;
        default:
            break;
        }
        break;
    default:
        SDL_SetError("Compressed BMP files not supported");
        obj->error = true;
        goto ERROR_EXIT;
    }

    /* Create a compatible surface, note that the colors are RGB ordered */
    surface = SDL_AllocSurface(SDL_SWSURFACE, biWidth, biHeight, desired_bit_depth, Rmask, Gmask, Bmask, 0);
    if (surface == NULL) {
        obj->error = true;
        goto ERROR_EXIT;
    }

    /* Load the palette, if any */
    palette = (surface->format)->palette;
    if (palette) {
        if (biClrUsed == 0) {
            biClrUsed = 1 << biBitCount;
        }
        for (ui = 0; ui < biClrUsed; ++ui) {
            palette->colors[ui].b = obj->readle8(obj);
            palette->colors[ui].g = obj->readle8(obj);
            palette->colors[ui].r = obj->readle8(obj);
            if (biSize == 12)
                palette->colors[ui].unused = 0;
            else
                palette->colors[ui].unused = obj->readle8(obj);
        }
        palette->ncolors = biClrUsed;
    }

    /* Read the surface pixels.  Note that the bmp image is upside down */
    obj->read_seek(obj);
    bits = (Uint8*)surface->pixels + (surface->h * surface->pitch);
    switch (ExpandBMP) {
    case 1:
    case 4: {
        int bmp_pitch;

        pixels_per_bit = 8 / biBitCount;

        bmp_pitch = (biWidth + (pixels_per_bit - 1)) / pixels_per_bit;
        if (bmp_pitch % 4)
            pad = 4 - (bmp_pitch % 4);
        else
            pad = 0;
    } break;
    default:
        pixels_per_bit = 1; /* not used */
        pad = ((surface->pitch % 4) ? (4 - (surface->pitch % 4)) : 0);
        break;
    }
    for (row = 0; row < surface->h; ++row) {
        bits = (Uint8*)surface->pixels + (top_down ? row : surface->h - 1 - row) * surface->pitch;
        switch (ExpandBMP) {
        case 1:
        case 4: {
            Uint8 pixel;
            int shift;

            pixel = 0;
            shift = (8 - ExpandBMP);
            for (i = 0; i < biWidth; ++i) {
                if (i % pixels_per_bit == 0) {
                    c = obj->readle8(obj);
                    if (c == EOF) {
                        SDL_SetError("Error reading from BMP file");
                        obj->error = true;
                        goto ERROR_EXIT;
                    }
                    pixel = (Uint8)c;
                }
                *(bits + i) = (pixel >> shift);
                pixel <<= ExpandBMP;
            }
        } break;

        default:
            if (obj->read(obj, bits, surface->pitch) != surface->pitch) {
                SDL_SetError("Error reading from BMP file");
                obj->error = true;
                goto ERROR_EXIT;
            }
            break;
        }
        for (i = 0; i < pad; ++i)
            obj->readle8(obj);
    }

ERROR_EXIT:
    if (obj->error) {
        if (surface) {
            SDL_FreeSurface(surface);
            surface = NULL;
        }
    }
    obj->read_close(obj);
    return surface;
}

SDL_Surface* SDL_LoadBMP_ctmhackage(const char* file)
{
    SDL_Surface* retval;
    dib_read_obj_t bmp_obj = {
        bmp_read_init, bmp_read_seek, bmp_readle32,   bmp_readle16,
        bmp_readle8,   bmp_read,      bmp_read_close, bmp_read_no_masks,
    };

    bmp_obj.u.bmp.filename = file;
    retval = SDL_BMP_read_helper(&bmp_obj);
    return retval;
}

static Uint32 cfdib_u32(const unsigned char* p)
{
    return (Uint32)p[0] | (Uint32)p[1] << 8 | (Uint32)p[2] << 16 | (Uint32)p[3] << 24;
}

/* Validate all offsets and allocation dimensions before using the shared
 * decoder. Extended headers are normalized after bounded validation. */
static bool valid_cfdib(const unsigned char* p, size_t size)
{
    Uint32 header, compression = BI_RGB, colors = 0, width, height, depth, planes;
    unsigned long long offset, stride, output_stride;
    if (!p || size < 12)
        return false;
    header = cfdib_u32(p);
    if (header == 12) {
        width = p[4] | (Uint32)p[5] << 8;
        height = p[6] | (Uint32)p[7] << 8;
        planes = p[8] | (Uint32)p[9] << 8;
        depth = p[10] | (Uint32)p[11] << 8;
    }
    else if ((header == 40 || header == 52 || header == 56 || header == 108 || header == 124) && size >= header) {
        Sint32 signed_height;
        width = cfdib_u32(p + 4);
        signed_height = (Sint32)cfdib_u32(p + 8);
        if (signed_height == INT_MIN)
            return false;
        height = signed_height < 0 ? -signed_height : signed_height;
        planes = p[12] | (Uint32)p[13] << 8;
        depth = p[14] | (Uint32)p[15] << 8;
        compression = cfdib_u32(p + 16);
        colors = cfdib_u32(p + 32);
        if (header == 124 && (cfdib_u32(p + 112) || cfdib_u32(p + 116)))
            return false; /* Embedded/linked color profiles are not decoded. */
    }
    else
        return false;
    if (!width || width > SHRT_MAX || !height || height > SHRT_MAX || planes != 1 ||
        (depth != 1 && depth != 4 && depth != 8 && depth != 16 && depth != 24 && depth != 32))
        return false;
    offset = header;
    if (compression == BI_BITFIELDS && (depth == 16 || depth == 32)) {
        Uint32 used = 0;
        int i;
        if (size < (header == 40 ? offset + 12 : 52))
            return false;
        for (i = 0; i < 3; ++i) {
            Uint32 mask = cfdib_u32(p + 40 + i * 4), shifted;
            if (!mask || (mask & used) || (depth == 16 && mask > 65535))
                return false;
            used |= mask;
            shifted = mask;
            while (!(shifted & 1))
                shifted >>= 1;
            if (shifted > 255 || (shifted & (shifted + 1)))
                return false;
        }
        if (header == 40)
            offset += 12;
    }
    else if (compression != BI_RGB)
        return false;
    if (depth <= 8) {
        if (colors > (1U << depth))
            return false;
        if (!colors)
            colors = 1U << depth;
        offset += colors * (header == 12 ? 3ULL : 4ULL);
    }
    else if (colors)
        return false;
    stride = ((width * (unsigned long long)depth + 31) / 32) * 4;
    output_stride = ((width * (unsigned long long)(depth < 8 ? 8 : depth) + 31) / 32) * 4;
    if (output_stride > 65535 || output_stride * height > INT_MAX || offset > size || stride * height > size - offset)
        return false;
    if (depth <= 8) {
        Uint32 x, y;
        for (y = 0; y < height; ++y)
            for (x = 0; x < width; ++x) {
                unsigned index = p[(size_t)offset + (size_t)stride * y + (size_t)x * depth / 8];
                index = (index >> (8 - depth - ((x * depth) % 8))) & ((1U << depth) - 1);
                if (index >= colors)
                    return false;
            }
    }
    return true;
}

SDL_Surface* SDL_LoadCF_DIB(void* mem, size_t length)
{
    SDL_Surface* retval;
    unsigned char* normalized = NULL;
    dib_read_obj_t cfdib_obj = {
        cfdib_read_init, cfdib_read_seek, cfdib_readle32,   cfdib_readle16,
        cfdib_readle8,   cfdib_read,      cfdib_read_close, cfdib_read_no_masks,
    };

    if (!valid_cfdib(mem, length)) {
        SDL_SetError("Invalid, truncated or unsupported clipboard bitmap");
        return NULL;
    }
    if (cfdib_u32(mem) > 40) {
        const unsigned char* source = mem;
        size_t header = cfdib_u32(source);
        size_t masks = cfdib_u32(source + 16) == BI_BITFIELDS ? 12 : 0;
        size_t normalized_length = length - header + 40 + masks;
        normalized = malloc(normalized_length);
        if (!normalized) {
            SDL_SetError("Couldn't allocate normalized clipboard bitmap");
            return NULL;
        }
        memcpy(normalized, source, 40);
        normalized[0] = 40;
        normalized[1] = normalized[2] = normalized[3] = 0;
        if (masks)
            memcpy(normalized + 40, source + 40, masks);
        memcpy(normalized + 40 + masks, source + header, length - header);
        mem = normalized;
        length = normalized_length;
    }
    cfdib_obj.u.cfdib.p = mem;
    cfdib_obj.u.cfdib.remaining = length;
    retval = SDL_BMP_read_helper(&cfdib_obj);
    free(normalized);
    return retval;
}

/* BMP-specific state information for writing */
typedef struct {
    const char* filename;
    FILE* fp;
} private_bmp_write_t;

/* CF_DIB-specific state information for writing */
typedef struct {
    void* bytes;
    size_t n_bytes_allocated;
    size_t n_bytes_used;
    size_t block_size;
    size_t offset;
} private_cfdib_write_t;

/* union of all supported bmp/dib thingies for writing */
typedef union {
    private_bmp_write_t bmp;
    private_cfdib_write_t cfdib;
} dib_write_obj_private_t;

typedef struct dib_write_obj dib_write_obj_t;

/* access methods and common state information for writing */
struct dib_write_obj {
    void (*write_init)(dib_write_obj_t*);
    int (*write)(dib_write_obj_t*, const void*, int);
    void (*writele32)(dib_write_obj_t*, Uint32);
    void (*writele16)(dib_write_obj_t*, Uint16);
    void (*writele8)(dib_write_obj_t*, Uint8);
    void (*write_seek)(dib_write_obj_t*, Uint32);
    long (*write_ftell)(dib_write_obj_t*);
    int (*write_close)(dib_write_obj_t*);
    bool error;
    dib_write_obj_private_t u;
};

static void bmp_write_init(dib_write_obj_t* obj)
{
    obj->error = false;
    obj->u.bmp.fp = fopen(obj->u.bmp.filename, "wb");
    if (obj->u.bmp.fp == NULL) {
        SDL_SetError("Couldn't open %s for writing", obj->u.bmp.filename);
        obj->error = true;
    }
    else {
        char magic[2] = MAGIC;

#if SDL_MAJOR_VERSION == 0 && SDL_MINOR_VERSION < 9
        SDL_CalculateEndian();
#endif
        /* Write the BMP file header values */
        if (fwrite(magic, sizeof magic, 1, obj->u.bmp.fp) != 1) {
            SDL_SetError("Error writing to %s", obj->u.bmp.filename);
            obj->error = true;
        }
        else {
            /* The Win32 BMP file header (14 bytes) */
            Uint32 bfSize;
            Uint16 bfReserved1;
            Uint16 bfReserved2;
            Sint32 bfOffBits;

            /* Set the BMP file header values */
            bfSize = 0; /* We'll write this when we're done */
            bfReserved1 = 0;
            bfReserved2 = 0;
            bfOffBits = 0; /* We'll write this when we're done */

            obj->writele32(obj, bfSize);
            obj->writele16(obj, bfReserved1);
            obj->writele16(obj, bfReserved2);
            obj->writele32(obj, bfOffBits);
        }
    }
}

static void cfdib_write_init(dib_write_obj_t* obj)
{
    obj->u.cfdib.bytes = NULL;
    obj->u.cfdib.n_bytes_allocated = 0;
    obj->u.cfdib.n_bytes_used = 0;
    obj->u.cfdib.block_size = (1 << 12);
    obj->u.cfdib.offset = 0;
    obj->error = false;
}

static int bmp_write(dib_write_obj_t* obj, const void* p, int n_bytes)
{
    int retval;

    if (obj->error)
        retval = -1;
    else {
        retval = fwrite(p, 1, n_bytes, obj->u.bmp.fp);
        obj->error = retval != n_bytes;
    }
    return retval;
}

static void assure_room_for(dib_write_obj_t* obj, size_t n_bytes)
{
    size_t allocated = obj->u.cfdib.n_bytes_allocated;
    size_t offset = obj->u.cfdib.offset, needed;
    void* replacement;
    if (obj->error)
        return;
    if (offset > LONG_MAX || n_bytes > (size_t)LONG_MAX - offset) {
        obj->error = true;
        SDL_SetError("Clipboard bitmap is too large");
        return;
    }
    needed = offset + n_bytes;
    if (needed <= allocated)
        return;
    allocated = allocated ? allocated : obj->u.cfdib.block_size;
    while (allocated < needed)
        allocated = allocated > (size_t)LONG_MAX / 2 ? needed : allocated * 2;
    replacement = realloc(obj->u.cfdib.bytes, allocated);
    if (!replacement) {
        obj->error = true;
        SDL_SetError("Couldn't allocate clipboard bitmap");
        return;
    }
    memset((char*)replacement + obj->u.cfdib.n_bytes_allocated, 0, allocated - obj->u.cfdib.n_bytes_allocated);
    obj->u.cfdib.bytes = replacement;
    obj->u.cfdib.n_bytes_allocated = allocated;
}

static int cfdib_write(dib_write_obj_t* obj, const void* p, int n_bytes)
{
    if (n_bytes < 0)
        obj->error = true;
    if (!obj->error) {
        assure_room_for(obj, n_bytes);
        if (!obj->error) {
            if (n_bytes)
                memcpy((char*)obj->u.cfdib.bytes + obj->u.cfdib.offset, p, n_bytes);
            obj->u.cfdib.offset += n_bytes;
            obj->u.cfdib.n_bytes_used = MAX_size_t(obj->u.cfdib.n_bytes_used, obj->u.cfdib.offset);
        }
    }
    return obj->error ? -1 : n_bytes;
}

static void bmp_writele32(dib_write_obj_t* obj, Uint32 val)
{
    if (!obj->error) {
        SDL_WriteLE32(obj->u.bmp.fp, val);
        obj->error = SDL_ERROR();
    }
}

static void cfdib_writele32(dib_write_obj_t* obj, Uint32 val)
{
    cfdib_write(obj, &val, sizeof val);
}

static void bmp_writele16(dib_write_obj_t* obj, Uint16 val)
{
    if (!obj->error) {
        SDL_WriteLE16(obj->u.bmp.fp, val);
        obj->error = SDL_ERROR();
    }
}

static void cfdib_writele16(dib_write_obj_t* obj, Uint16 val)
{
    cfdib_write(obj, &val, sizeof val);
}

static void bmp_writele8(dib_write_obj_t* obj, Uint8 val)
{
    if (!obj->error) {
        obj->error = fputc(val, obj->u.bmp.fp) == EOF;
        if (obj->error)
            SDL_SetError("Unexpected EOF reading %s", obj->u.bmp.filename);
    }
}

static void cfdib_writele8(dib_write_obj_t* obj, Uint8 val)
{
    cfdib_write(obj, &val, sizeof val);
}

static void bmp_write_seek(dib_write_obj_t* obj, Uint32 offset)
{
    obj->error = (obj->error || fseek(obj->u.bmp.fp, offset, SEEK_SET) != 0);
}

static void cfdib_write_seek(dib_write_obj_t* obj, Uint32 offset)
{
    if (!obj->error) {
        if (offset > obj->u.cfdib.offset)
            assure_room_for(obj, offset - obj->u.cfdib.offset);
        if (!obj->error)
            obj->u.cfdib.offset = offset;
    }
}

static long bmp_write_ftell(dib_write_obj_t* obj)
{
    long retval;

    if (obj->error)
        retval = -1;
    else {
        retval = ftell(obj->u.bmp.fp);
        obj->error = retval == -1;
    }
    return retval;
}

static long cfdib_write_ftell(dib_write_obj_t* obj)
{
    int retval;

    if (obj->error)
        retval = -1;
    else
        retval = obj->u.cfdib.offset;
    return retval;
}

static int bmp_write_close(dib_write_obj_t* obj)
{
    int retval;

    if (obj->error)
        retval = EOF;
    else {
        Uint32 bfSize;

        /* Write the BMP file size */
        bfSize = obj->write_ftell(obj);
        obj->write_seek(obj, 2);
        obj->writele32(obj, bfSize);
        obj->write_seek(obj, bfSize);

        retval = fclose(obj->u.bmp.fp);
        obj->error = retval == EOF;
    }
    return retval;
}

static int cfdib_write_close(dib_write_obj_t* obj)
{
    int retval;

    retval = 0;
    return retval;
}

int SDL_BMP_write_helper(SDL_Surface* surfp, dib_write_obj_t* obj)
{
    int i, pad, row, row_bytes;
    Uint8* bits;
    int retval;

    /* The Win32 BITMAPINFOHEADER struct (40 bytes) */
    Uint32 biSize;
    Sint32 biWidth;
    Sint32 biHeight;
    Uint16 biPlanes;
    Uint16 biBitCount;
    Uint32 biCompression;
    Uint32 biSizeImage;
    Sint32 biXPelsPerMeter;
    Sint32 biYPelsPerMeter;
    Uint32 biClrUsed;
    Uint32 biClrImportant;
    bool need_to_free;

    need_to_free = false;
    SDL_ClearError();
    if (!surfp || !surfp->format || surfp->w <= 0 || surfp->h <= 0 || surfp->w > SHRT_MAX || surfp->h > SHRT_MAX) {
        obj->error = true;
        SDL_SetError("Invalid surface for clipboard bitmap");
        goto ERROR_EXIT;
    }
    if (surfp->format->palette) {
        if (surfp->format->BitsPerPixel != 8) {
            SDL_SetError("%d bpp BMP files not supported", surfp->format->BitsPerPixel);
            obj->error = true;
            goto ERROR_EXIT;
        }
    }
    else {
        if (surfp->format->BitsPerPixel != 24) {
            SDL_Surface* temp_surface;

            /* Convert to 24 bits per pixel */
            temp_surface =
                SDL_AllocSurface(SDL_SWSURFACE, surfp->w, surfp->h, 24, 0x00FF0000, 0x0000FF00, 0x000000FF, 0);
            if (!temp_surface) {
                obj->error = true;
                goto ERROR_EXIT;
            }
            else {
#if SDL_MAJOR_VERSION == 0 && SDL_MINOR_VERSION < 9
                SDL_PixelFormat* format;

                format = surfp->dstfmt;
                if (SDL_MapSurface(surfp, temp_surface->format) == 0 &&
#else
                if (
#endif
                    SDL_BlitSurface(surfp, NULL, temp_surface, NULL) == 0) {
                    surfp = temp_surface;
                    need_to_free = true;
                }
                else {
                    SDL_FreeSurface(temp_surface);
                    obj->error = true;
                    goto ERROR_EXIT;
                }
            }
        }
    }

    row_bytes = surfp->w * surfp->format->BytesPerPixel;
    if (row_bytes <= 0 || row_bytes > surfp->pitch) {
        obj->error = true;
        goto ERROR_EXIT;
    }
    obj->write_init(obj);

    /* Set the BMP info values */
    biSize = 40;
    biWidth = surfp->w;
    biHeight = surfp->h;

    biPlanes = 1;
    biBitCount = surfp->format->BitsPerPixel;

    biCompression = BI_RGB;
    biSizeImage = surfp->h * ((row_bytes + 3) / 4 * 4);
    biXPelsPerMeter = 0;
    biYPelsPerMeter = 0;
    if (surfp->format->palette)
        biClrUsed = surfp->format->palette->ncolors;
    else
        biClrUsed = 0;
    biClrImportant = 0;

    /* Write the BMP info values */
    obj->writele32(obj, biSize);
    obj->writele32(obj, biWidth);
    obj->writele32(obj, biHeight);
    obj->writele16(obj, biPlanes);
    obj->writele16(obj, biBitCount);
    obj->writele32(obj, biCompression);
    obj->writele32(obj, biSizeImage);
    obj->writele32(obj, biXPelsPerMeter);
    obj->writele32(obj, biYPelsPerMeter);
    obj->writele32(obj, biClrUsed);
    obj->writele32(obj, biClrImportant);

    /* Write the palette (in BGR color order) */
    if (surfp->format->palette) {
        SDL_Color* colors;
        int ncolors;

        colors = surfp->format->palette->colors;
        ncolors = surfp->format->palette->ncolors;
        for (i = 0; i < ncolors; ++i) {
            obj->writele8(obj, colors[i].b);
            obj->writele8(obj, colors[i].g);
            obj->writele8(obj, colors[i].r);
            obj->writele8(obj, 0);
        }
    }

    if (obj->write_seek == bmp_write_seek) {
        Sint32 bfOffBits;

        /* Write the bitmap offset */
        bfOffBits = obj->write_ftell(obj);
        obj->write_seek(obj, 10);
        obj->writele32(obj, bfOffBits);
        obj->write_seek(obj, bfOffBits);
    }

    /* Serialize row data and explicit zero padding, never surface padding. */
    pad = (4 - row_bytes % 4) % 4;
    for (row = surfp->h - 1; row >= 0; --row) {
        bits = (Uint8*)surfp->pixels + row * surfp->pitch;
        if (obj->write(obj, bits, row_bytes) != row_bytes) {
            obj->error = true;
            SDL_SetError("Error writing bitmap pixels");
            goto ERROR_EXIT;
        }
        for (i = 0; i < pad; ++i)
            obj->writele8(obj, 0);
    }

    /* Close it up.. */
    if (obj->write_close(obj) != 0)
        SDL_SetError("Error writing to datastream");

ERROR_EXIT:
    if (need_to_free) {
        SDL_FreeSurface(surfp);
    }

    retval = obj->error ? -1 : 0;
    return retval;
}

int SDL_SaveBMP_ctmhackage(SDL_Surface* surfp, const char* file)
{
    int retval;
    dib_write_obj_t bmp_obj = {
        bmp_write_init, bmp_write,      bmp_writele32,   bmp_writele16,
        bmp_writele8,   bmp_write_seek, bmp_write_ftell, bmp_write_close,
    };

    bmp_obj.u.bmp.filename = file;
    retval = SDL_BMP_write_helper(surfp, &bmp_obj);
    return retval;
}

int SDL_SaveCF_DIB(SDL_Surface* surfp, char** dib_bytesp, size_t* dib_lenp)
{
    int retval;
    dib_write_obj_t cfdib_obj = {
        cfdib_write_init, cfdib_write,      cfdib_writele32,   cfdib_writele16,
        cfdib_writele8,   cfdib_write_seek, cfdib_write_ftell, cfdib_write_close,
    };

    if (!dib_bytesp || !dib_lenp)
        return -1;
    *dib_bytesp = NULL;
    *dib_lenp = 0;
    retval = SDL_BMP_write_helper(surfp, &cfdib_obj);
    if (retval == 0) {
        *dib_bytesp = cfdib_obj.u.cfdib.bytes;
        *dib_lenp = cfdib_obj.u.cfdib.n_bytes_used;
    }
    else
        free(cfdib_obj.u.cfdib.bytes);
    return retval;
}
