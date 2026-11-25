/*+===================================================================
  File:      qPict2.c

  Summary:   Collect QuickDraw picture and pixmap information for the Picture Utilities.

  Classes:   None.

  Functions: ROMlib_pict_info_reinit and the Picture Utilities traps.

  Origin:    Existing repository source in executor/src/quickdraw; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1995, 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"

#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "MemoryMgr.h"

#include "rsys/picture.h"
#include "rsys/cquick.h"
#include "rsys/rgbutil.h"

enum {
    pict_return_table = 1,
    pict_return_palette = 2,
    pict_record_comments = 4,
    pict_record_fonts = 8,
    pict_suppress_mono = 16,
    pict_color_bins = 32768,
    pict_comment_ids = 65536,
};

typedef struct {
    uint32 count;
    RGBColor rgb;
} pict_color;

typedef struct {
    pict_color* colors;
    uint32* comments;
    int width, height, depth;
    Fixed horizontal_resolution, vertical_resolution;
    uint32 text, line, rect, rounded_rect, oval, arc, polygon, region, bitmap, pixmap, comment;
} pict_totals;

typedef struct pict_session {
    struct pict_session* next;
    Ptr token;
    uint16 verb, capacity;
    pict_totals totals;
} pict_session;

static pict_session* pict_sessions;

static pict_session** pict_session_link(PictInfoID token)
{
    pict_session** link = &pict_sessions;
    while (*link && US_TO_SYN68K((*link)->token) != (uint32)token)
        link = &(*link)->next;
    return link;
}

static void pict_totals_dispose(pict_totals* totals)
{
    free(totals->colors);
    free(totals->comments);
}

static OSErr pict_totals_init(pict_totals* totals, unsigned verb)
{
    *totals = (pict_totals){0};
    totals->colors = calloc(pict_color_bins, sizeof *totals->colors);
    if (verb & pict_record_comments)
        totals->comments = calloc(pict_comment_ids, sizeof *totals->comments);
    if (totals->colors && (!(verb & pict_record_comments) || totals->comments))
        return noErr;
    pict_totals_dispose(totals);
    return memFullErr;
}

static OSErr pict_options(uint16 verb, int16 capacity, int16 method, int16 version)
{
    if (version)
        return ROMLIB_PICT_VERSION_ERR;
    if (verb &
        ~(pict_return_table | pict_return_palette | pict_record_comments | pict_record_fonts | pict_suppress_mono))
        return ROMLIB_PICT_VERB_ERR;
    if ((verb & (pict_return_table | pict_return_palette)) && (capacity < 1 || capacity > 256))
        return ROMLIB_PICT_COLORS_ERR;
    if ((verb & pict_record_fonts) ||
        ((verb & (pict_return_table | pict_return_palette)) && method != 0 && method != 1))
        return ROMLIB_PICT_REFERENCE_REQUIRED;
    return noErr;
}

static uint32 pict_add_count(uint32 left, uint32 right)
{
    return left > INT32_MAX - right ? INT32_MAX : left + right;
}

static void pict_add_color(pict_totals* totals, RGBColor rgb)
{
    const unsigned index =
        ((uint16)CW(rgb.red) >> 11) << 10 | ((uint16)CW(rgb.green) >> 11) << 5 | ((uint16)CW(rgb.blue) >> 11);
    pict_color* bucket = &totals->colors[index];
    if (!bucket->count)
        bucket->rgb = rgb;
    bucket->count = pict_add_count(bucket->count, 1);
}

static void pict_merge(pict_totals* totals, const pict_totals* source)
{
    totals->width = MAX(totals->width, source->width);
    totals->height = MAX(totals->height, source->height);
    totals->depth = MAX(totals->depth, source->depth);
    totals->horizontal_resolution = MAX(totals->horizontal_resolution, source->horizontal_resolution);
    totals->vertical_resolution = MAX(totals->vertical_resolution, source->vertical_resolution);
    totals->text = pict_add_count(totals->text, source->text);
    totals->line = pict_add_count(totals->line, source->line);
    totals->rect = pict_add_count(totals->rect, source->rect);
    totals->rounded_rect = pict_add_count(totals->rounded_rect, source->rounded_rect);
    totals->oval = pict_add_count(totals->oval, source->oval);
    totals->arc = pict_add_count(totals->arc, source->arc);
    totals->polygon = pict_add_count(totals->polygon, source->polygon);
    totals->region = pict_add_count(totals->region, source->region);
    totals->bitmap = pict_add_count(totals->bitmap, source->bitmap);
    totals->pixmap = pict_add_count(totals->pixmap, source->pixmap);
    totals->comment = pict_add_count(totals->comment, source->comment);
    for (unsigned i = 0; i < pict_color_bins; ++i) {
        if (!totals->colors[i].count)
            totals->colors[i].rgb = source->colors[i].rgb;
        totals->colors[i].count = pict_add_count(totals->colors[i].count, source->colors[i].count);
    }
    if (totals->comments)
        for (unsigned i = 0; i < pict_comment_ids; ++i)
            totals->comments[i] = pict_add_count(totals->comments[i], source->comments[i]);
}

static uint16 pict_read_word(const Byte* bytes)
{
    return ((uint16)bytes[0] << 8) | bytes[1];
}

static uint32 pict_read_long(const Byte* bytes)
{
    return ((uint32)pict_read_word(bytes) << 16) | pict_read_word(bytes + 2);
}

static OSErr pict_count_opcode(uint16 opcode, const Byte* data, size_t length, void* opaque)
{
    pict_totals* totals = opaque;
    uint32* count = NULL;
    switch (opcode) {
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x1a:
    case 0x1b:
    case 0x1d: {
        RGBColor rgb;
        memcpy(&rgb, data + length - sizeof rgb, sizeof rgb);
        pict_add_color(totals, rgb);
        break;
    }
    case 0xa0:
    case 0xa1:
        if (totals->comments) {
            const uint16 id = pict_read_word(data);
            totals->comments[id] = pict_add_count(totals->comments[id], 1);
            count = &totals->comment;
        }
        break;
    case 0x0c00:
        if (pict_read_word(data) == 0xfffe) {
            totals->horizontal_resolution = pict_read_long(data + 4);
            totals->vertical_resolution = pict_read_long(data + 8);
            const int height = (int16)pict_read_word(data + 16) - (int16)pict_read_word(data + 12);
            const int width = (int16)pict_read_word(data + 18) - (int16)pict_read_word(data + 14);
            if (width > 0 && height > 0) {
                totals->width = width;
                totals->height = height;
            }
        }
        break;
    default:
        if (opcode >= 0x20 && opcode <= 0x23)
            count = &totals->line;
        else if (opcode >= 0x28 && opcode <= 0x2b)
            count = &totals->text;
        else if (opcode >= 0x30 && opcode < 0x90 && (opcode & 7) <= 4) {
            uint32* shapes[] = {&totals->rect, &totals->rounded_rect, &totals->oval,
                                &totals->arc,  &totals->polygon,      &totals->region};
            count = shapes[(opcode - 0x30) / 16];
        }
        break;
    }
    if (count)
        *count = pict_add_count(*count, 1);
    return noErr;
}

static OSErr pict_measure_picture(pict_totals* totals, PicHandle picture)
{
    if (!picture || !STARH(picture) || GetHandleSize((Handle)picture) < sizeof(Picture))
        return paramErr;
    const Rect picture_bounds = HxX(picture, picFrame);
    totals->width = MAX(0, RECT_WIDTH(&picture_bounds));
    totals->height = MAX(0, RECT_HEIGHT(&picture_bounds));
    totals->horizontal_resolution = totals->vertical_resolution = 72 << 16;
    totals->depth = 1;
    return ROMlib_walk_picture(picture, pict_count_opcode, totals);
}

static OSErr pict_measure_pixmap(pict_totals* totals, PixMapHandle handle)
{
    if (!handle || !STARH(handle) || GetHandleSize((Handle)handle) < sizeof(PixMap))
        return paramErr;
    const PixMap* map = STARH(handle);
    const int depth = CW(map->pixelSize), stride = PIXMAP_ROWBYTES(handle);
    const int width = RECT_WIDTH(&map->bounds), height = RECT_HEIGHT(&map->bounds);
    if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16 && depth != 32)
        return ROMLIB_PICT_REFERENCE_REQUIRED;
    if (width < 0 || height < 0 || (width * depth + 7) / 8 > stride)
        return paramErr;
    const Byte* base = (const Byte*)PIXMAP_BASEADDR(handle);
    if (!base && width && height)
        return paramErr;

    CTabHandle table = PIXMAP_TABLE(handle);
    const ColorSpec* colors = NULL;
    int color_count = 0;
    if (depth <= 8 && width && height) {
        if (!table || !STARH(table) || GetHandleSize((Handle)table) < offsetof(ColorTable, ctTable))
            return paramErr;
        color_count = (int16)CTAB_SIZE(table) + 1;
        if (color_count <= 0 || color_count > 256 ||
            GetHandleSize((Handle)table) < (Size)(offsetof(ColorTable, ctTable) + color_count * sizeof(ColorSpec)))
            return paramErr;
        colors = CTAB_TABLE(table);
    }
    const rgb_spec_t* format = depth > 8 ? pixmap_rgb_spec(map) : NULL;
    if (depth > 8 && !format)
        return ROMLIB_PICT_REFERENCE_REQUIRED;
    for (int y = 0; width && y < height; ++y) {
        const Byte* row = base + (size_t)y * stride;
        for (int x = 0; x < width; ++x) {
            RGBColor color;
            if (depth <= 8) {
                const unsigned shift = 8 - depth - ((x * depth) & 7);
                const unsigned index = (row[x * depth / 8] >> shift) & ((1U << depth) - 1);
                if (index >= (unsigned)color_count)
                    return ROMLIB_PICT_DATA_ERR;
                color = colors[index].rgb;
            }
            else if (depth == 16) {
                uint16 pixel;
                memcpy(&pixel, row + x * sizeof pixel, sizeof pixel);
                format->pixel_to_rgbcolor(format, pixel, &color);
            }
            else {
                uint32 pixel;
                memcpy(&pixel, row + x * sizeof pixel, sizeof pixel);
                format->pixel_to_rgbcolor(format, pixel, &color);
            }
            pict_add_color(totals, color);
        }
    }
    totals->width = width;
    totals->height = height;
    totals->depth = depth;
    totals->horizontal_resolution = CL(map->hRes);
    totals->vertical_resolution = CL(map->vRes);
    totals->pixmap = 1;
    return noErr;
}

typedef struct {
    uint32 count;
    unsigned index;
} pict_rank;

static int pict_color_rank(const void* left, const void* right)
{
    const pict_rank *a = left, *b = right;
    if (a->count != b->count)
        return a->count > b->count ? -1 : 1;
    return (a->index > b->index) - (a->index < b->index);
}

static OSErr pict_make_colors(const pict_totals* totals, unsigned verb, int requested, PictInfo* result)
{
    unsigned unique = 0;
    for (unsigned i = 0; i < pict_color_bins; ++i)
        unique += totals->colors[i].count != 0;
    result->uniqueColors = CL(unique);
    if (!(verb & (pict_return_table | pict_return_palette)))
        return noErr;
    pict_rank* ranked = unique ? malloc(unique * sizeof *ranked) : NULL;
    if (unique && !ranked)
        return memFullErr;
    unsigned selected = 0;
    for (unsigned i = 0; i < pict_color_bins; ++i) {
        if (!totals->colors[i].count || ((verb & pict_suppress_mono) && (i == 0 || i == pict_color_bins - 1)))
            continue;
        ranked[selected++] = (pict_rank){totals->colors[i].count, i};
    }
    if (selected > 1)
        qsort(ranked, selected, sizeof *ranked, pict_color_rank);
    selected = MIN(selected, (unsigned)requested);
    CTabHandle table = (CTabHandle)NewHandleClear(offsetof(ColorTable, ctTable) + requested * sizeof(ColorSpec));
    if (!table) {
        free(ranked);
        return memFullErr;
    }
    CTAB_SEED_X(table) = CL(C_GetCTSeed());
    CTAB_SIZE_X(table) = CW(requested - 1);
    for (int i = 0; i < requested; ++i) {
        ColorSpec* spec = &CTAB_TABLE(table)[i];
        spec->value = CW(i);
        if ((unsigned)i < selected)
            spec->rgb = totals->colors[ranked[i].index].rgb;
    }
    free(ranked);
    if ((verb & pict_return_palette) && selected) {
        PaletteHandle palette = C_NewPalette(selected, table, pmTolerant, 0);
        if (!palette) {
            DisposHandle((Handle)table);
            return memFullErr;
        }
        PP_ASSIGN(result->thePalette, palette);
    }
    if (verb & pict_return_table)
        PP_ASSIGN(result->theColorTable, table);
    else
        DisposHandle((Handle)table);
    return noErr;
}

static OSErr pict_publish(const pict_totals* totals, unsigned verb, int requested, PictInfo* output)
{
    PictInfo result = {
        .hRes = CL(totals->horizontal_resolution),
        .vRes = CL(totals->vertical_resolution),
        .depth = CW(totals->depth),
        .sourceRect = {0, 0, CW(MIN(INT16_MAX, totals->height)), CW(MIN(INT16_MAX, totals->width))},
        .textCount = CL(totals->text),
        .lineCount = CL(totals->line),
        .rectCount = CL(totals->rect),
        .rRectCount = CL(totals->rounded_rect),
        .ovalCount = CL(totals->oval),
        .arcCount = CL(totals->arc),
        .polyCount = CL(totals->polygon),
        .regionCount = CL(totals->region),
        .bitMapCount = CL(totals->bitmap),
        .pixMapCount = CL(totals->pixmap),
        .commentCount = CL(totals->comment),
    };
    Handle comments = NULL;
    if (totals->comment) {
        unsigned unique = 0;
        for (unsigned i = 0; i < pict_comment_ids; ++i)
            unique += totals->comments[i] != 0;
        comments = NewHandle(unique * sizeof(CommentSpec));
        if (!comments)
            return memFullErr;
        CommentSpec* slot = (CommentSpec*)STARH(comments);
        for (unsigned i = 0; i < pict_comment_ids; ++i)
            if (totals->comments[i])
                *slot++ = (CommentSpec){CW(MIN(totals->comments[i], INT16_MAX)), CW(i)};
        result.uniqueComments = CL(unique);
        PP_ASSIGN(result.commentHandle, (CommentSpecHandle)comments);
    }

    const OSErr status = pict_make_colors(totals, verb, requested, &result);
    if (!status)
        *output = result;
    else if (comments)
        DisposHandle(comments);
    return status;
}

P5(PUBLIC pascal trap, OSErr, NewPictInfo, PictInfoID*, token, int16, verb, int16, capacity, int16, method, int16,
   version)
{
    if (!token)
        return paramErr;
    OSErr error = pict_options(verb, capacity, method, version);
    if (error)
        return error;
    pict_session* session = calloc(1, sizeof *session);
    if (!session)
        return memFullErr;
    error = pict_totals_init(&session->totals, verb);
    if (!error) {
        session->token = NewPtr(4);
        if (!session->token) {
            pict_totals_dispose(&session->totals);
            error = memFullErr;
        }
    }
    if (error) {
        free(session);
        return error;
    }
    session->verb = verb;
    session->capacity = capacity;
    session->next = pict_sessions;
    pict_sessions = session;
    *token = CL(US_TO_SYN68K(session->token));
    return noErr;
}

P1(PUBLIC pascal trap, OSErr, DisposePictInfo, PictInfoID, token)
{
    pict_session** link = pict_session_link(token);
    if (!*link)
        return ROMLIB_PICT_ID_ERR;
    pict_session* session = *link;
    *link = session->next;
    DisposPtr(session->token);
    pict_totals_dispose(&session->totals);
    free(session);
    return noErr;
}

void ROMlib_pict_info_reinit(void)
{
    /* Called after a guest heap reset. The old guest token is no longer live. */
    while (pict_sessions) {
        pict_session* session = pict_sessions;
        pict_sessions = session->next;
        pict_totals_dispose(&session->totals);
        free(session);
    }
}

static OSErr pict_collect(PicHandle picture, PixMapHandle pixmap, pict_session* session)
{
    pict_totals sample;
    OSErr status = pict_totals_init(&sample, session->verb);
    if (status)
        return status;
    status = picture ? pict_measure_picture(&sample, picture) : pict_measure_pixmap(&sample, pixmap);
    if (!status)
        pict_merge(&session->totals, &sample);
    pict_totals_dispose(&sample);
    return status;
}

P2(PUBLIC pascal trap, OSErr, RecordPictInfo, PictInfoID, token, PicHandle, picture)
{
    pict_session* session = *pict_session_link(token);
    if (!session)
        return ROMLIB_PICT_ID_ERR;
    return pict_collect(picture, NULL, session);
}

P2(PUBLIC pascal trap, OSErr, RecordPixMapInfo, PictInfoID, token, PixMapHandle, pixmap)
{
    pict_session* session = *pict_session_link(token);
    if (!session)
        return ROMLIB_PICT_ID_ERR;
    return pict_collect(NULL, pixmap, session);
}

P3(PUBLIC pascal trap, OSErr, RetrievePictInfo, PictInfoID, token, PictInfo*, output, int16, requested)
{
    pict_session* session = *pict_session_link(token);
    if (!session)
        return ROMLIB_PICT_ID_ERR;
    if (!output)
        return paramErr;
    if ((session->verb & (pict_return_table | pict_return_palette)) && (requested < 1 || requested > session->capacity))
        return ROMLIB_PICT_COLORS_ERR;
    return pict_publish(&session->totals, session->verb, requested, output);
}

static OSErr pict_analyze(PicHandle picture, PixMapHandle pixmap, PictInfo* output, uint16 verb, int16 requested,
                          int16 method, int16 version)
{
    if (!output)
        return paramErr;
    OSErr status = pict_options(verb, requested, method, version);
    if (status)
        return status;
    pict_totals totals;
    status = pict_totals_init(&totals, verb);
    if (status)
        return status;
    status = picture ? pict_measure_picture(&totals, picture) : pict_measure_pixmap(&totals, pixmap);
    if (!status)
        status = pict_publish(&totals, verb, requested, output);
    pict_totals_dispose(&totals);
    return status;
}

P6(PUBLIC pascal trap, OSErr, GetPictInfo, PicHandle, picture, PictInfo*, output, int16, verb, int16, requested, int16,
   method, int16, version)
{
    return pict_analyze(picture, NULL, output, verb, requested, method, version);
}

P6(PUBLIC pascal trap, OSErr, GetPixMapInfo, PixMapHandle, pixmap, PictInfo*, output, int16, verb, int16, requested,
   int16, method, int16, version)
{
    return pict_analyze(NULL, pixmap, output, verb, requested, method, version);
}
