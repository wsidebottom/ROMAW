/*+===================================================================
  File:      dcconvert.c

  Summary:   Implements dcconvert support for Macintosh QuickDraw graphics.

  Classes:   None.

  Functions: No externally linked function definitions.

  Origin:    Existing repository source in executor/src/quickdraw; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "rsys/depthconv.h"
#include "rsys/cquick.h"

/* This file contains all of the low-level depth conversion routines,
 * and works hand in hand with dcmaketables.c.  The routines in this
 * file can handle mapping any type of indirect or RGB pixel to any
 * type of indirect or RGB pixel.
 *
 * None of the routines in this file should ever be called directly;
 * instead, you call one of the table-creating routines in
 * dcmaketables.c, and that routine will return a function pointer
 * that points to the appropriate function in this file.
 *
 * All of the depth conversion functions share the same structure; the
 * key thing that differs is the actual loop that perform the
 * translation.  Therefore each depth conversion function is created
 * by instantating the generic depth conversion macro with appropriate
 * parameters.
 *
 * The most important such parameter is the "convert" macro.  This
 * supplies code for the inner loop that converts source pixels to
 * destination pixels.  Since this loop is time-critical, the generic
 * C versions of these inner loops can be overridden with inline
 * assembly versions; each generic C loop is wrapped in an "#ifndef"
 * so any preceding host-specific version will override it.  Currently
 * several of the most common inner loops have been written in x86
 * assembly.  These cases are especially time-critical since they are
 * frequently used to convert data into a format used by the host
 * windowing system.
 */

#define CONVERT_FUNC(func_name, convert, log2_out_ratio, abs_log2_out_ratio, compute_add, table_type)               \
    void func_name(const void* raw_table, const uint8* src_base, int src_row_bytes, uint8* dst_base,                \
                   int dst_row_bytes, int top, int left, int bottom, int right)                                     \
    {                                                                                                               \
        int byte_width, height;                                                                                     \
        int left_byte, right_byte, in_add, out_add;                                                                 \
        int log2_in_bpp, log2_out_bpp;                                                                              \
        IF_TABLE(const table_type* table);                                                                          \
                                                                                                                    \
        EXTRA_DECLS;                                                                                                \
                                                                                                                    \
        /* Grab log2_in_bpp from the supplied table. */                                                             \
        log2_in_bpp = *(const uint32*)raw_table;                                                                    \
        log2_out_bpp = log2_in_bpp + log2_out_ratio;                                                                \
                                                                                                                    \
        /* Line up incoming data on byte boundaries. */                                                             \
        left_byte = (left << log2_in_bpp) >> 3;                                                                     \
        right_byte = ((right << log2_in_bpp) + 7) >> 3;                                                             \
                                                                                                                    \
        /* Line up the outgoing data on byte boundaries. */                                                         \
        if (log2_out_ratio < 0) {                                                                                   \
            left_byte &= ~((1L << (abs_log2_out_ratio)) - 1);                                                       \
            right_byte = ((right_byte + ((1L << (abs_log2_out_ratio)) - 1)) & ~((1L << (abs_log2_out_ratio)) - 1)); \
        }                                                                                                           \
                                                                                                                    \
        byte_width = right_byte - left_byte;                                                                        \
        height = bottom - top;                                                                                      \
                                                                                                                    \
        if (byte_width <= 0 || height <= 0)                                                                         \
            return;                                                                                                 \
                                                                                                                    \
        /* Compute the actual aligned base of the table. */                                                         \
        IF_TABLE(table = ((const table_type*)_ALIGN_TABLE(raw_table, log2_in_bpp, log2_out_bpp)));                  \
                                                                                                                    \
        EXTRA_SETUP();                                                                                              \
                                                                                                                    \
        /* Compute the quantities to add as we march through each row. */                                           \
        in_add = compute_add(src_row_bytes, byte_width);                                                            \
        if (log2_out_ratio >= 0)                                                                                    \
            out_add = compute_add(dst_row_bytes, byte_width << (abs_log2_out_ratio));                               \
        else                                                                                                        \
            out_add = compute_add(dst_row_bytes, byte_width >> (abs_log2_out_ratio));                               \
                                                                                                                    \
        {                                                                                                           \
            INP_OUTP_DECL;                                                                                          \
                                                                                                                    \
            /* Compute pointers to the beginning of the transfer areas. */                                          \
            inp = src_base + (top * src_row_bytes) + left_byte;                                                     \
            if (log2_out_ratio >= 0)                                                                                \
                outp = (dst_base + (top * dst_row_bytes) + (left_byte << (abs_log2_out_ratio)));                    \
            else                                                                                                    \
                outp = (dst_base + (top * dst_row_bytes) + (left_byte >> (abs_log2_out_ratio)));                    \
                                                                                                                    \
            for (; height > 0; height--) {                                                                          \
                convert();                                                                                          \
                inp += in_add;                                                                                      \
                outp += out_add;                                                                                    \
            }                                                                                                       \
        }                                                                                                           \
                                                                                                                    \
        EXTRA_CLEANUP();                                                                                            \
    }

#if !defined(INP_OUTP_DECL)
#define INP_OUTP_DECL \
    const uint8* inp; \
    uint8* outp
#endif /* !defined (INP_OUTP_DECL) */

#if !defined(CONVERT_COPY)
#define CONVERT_COPY() memcpy(outp, inp, byte_width)
#define COMPUTE_ADD_COPY(row_bytes, byte_width) (row_bytes)
#endif /* !CONVERT_COPY */

#if !defined(CONVERT_1_1)
#define CONVERT_1_1()                         \
    do {                                      \
        int w;                                \
        for (w = byte_width - 1; w >= 0; w--) \
            outp[w] = (*table)[inp[w]];       \
    } while (0)
#define COMPUTE_ADD_1_1(row_bytes, byte_width) (row_bytes)
#endif /* !CONVERT_1_1 */

#if !defined(CONVERT_1_2)
#define CONVERT_1_2()                                 \
    do {                                              \
        int w;                                        \
        for (w = byte_width - 1; w >= 0; w--)         \
            ((uint16*)outp)[w] = (*table)[inp[w]][0]; \
    } while (0)
#define COMPUTE_ADD_1_2(row_bytes, byte_width) (row_bytes)
#endif /* !CONVERT_1_2 */

#if !defined(CONVERT_1_4)
#define CONVERT_1_4()                              \
    do {                                           \
        int w;                                     \
        for (w = byte_width - 1; w >= 0; w--)      \
            ((uint32*)outp)[w] = (*table)[inp[w]]; \
    } while (0)
#define COMPUTE_ADD_1_4(row_bytes, byte_width) (row_bytes)
#endif /* !CONVERT_1_4 */

#if !defined(CONVERT_1_8)
#define CONVERT_1_8()                                     \
    do {                                                  \
        int w;                                            \
        for (w = byte_width - 1; w >= 0; w--) {           \
            unsigned in = inp[w];                         \
            ((uint32*)outp)[w * 2] = (*table)[in][0];     \
            ((uint32*)outp)[w * 2 + 1] = (*table)[in][1]; \
        }                                                 \
    } while (0)
#define COMPUTE_ADD_1_8(row_bytes, byte_width) (row_bytes)
#endif /* !CONVERT_1_8 */

#if !defined(CONVERT_1_16)
#define CONVERT_1_16()                                   \
    do {                                                 \
        int w;                                           \
        uint32* dp;                                      \
                                                         \
        dp = (uint32*)&outp[(byte_width - 1) * 16];      \
        for (w = byte_width - 1; w >= 0; dp -= 4, w--) { \
            const uint32* sp;                            \
            sp = &(*table)[inp[w]][0];                   \
            dp[0] = sp[0];                               \
            dp[1] = sp[1];                               \
            dp[2] = sp[2];                               \
            dp[3] = sp[3];                               \
        }                                                \
    } while (0)
#define COMPUTE_ADD_1_16(row_bytes, byte_width) (row_bytes)
#endif /* !CONVERT_1_16 */

#if !defined(CONVERT_1_32)
#define CONVERT_1_32()                                   \
    do {                                                 \
        int w;                                           \
        uint32* dp;                                      \
                                                         \
        dp = (uint32*)&outp[(byte_width - 1) * 32];      \
        for (w = byte_width - 1; w >= 0; dp -= 8, w--) { \
            const uint32* sp;                            \
            sp = &(*table)[inp[w]][0];                   \
            dp[0] = sp[0];                               \
            dp[1] = sp[1];                               \
            dp[2] = sp[2];                               \
            dp[3] = sp[3];                               \
            dp[4] = sp[4];                               \
            dp[5] = sp[5];                               \
            dp[6] = sp[6];                               \
            dp[7] = sp[7];                               \
        }                                                \
    } while (0)
#define COMPUTE_ADD_1_32(row_bytes, byte_width) (row_bytes)
#endif /* !CONVERT_1_32 */

#if !defined(CONVERT_2_1)
#define CONVERT_2_1()                                                          \
    do {                                                                       \
        int w;                                                                 \
        for (w = (byte_width / 2) - 1; w >= 0; w--)                            \
            outp[w] = ((*table)[0][inp[w * 2]] | (*table)[1][inp[w * 2 + 1]]); \
    } while (0)
#define COMPUTE_ADD_2_1(row_bytes, byte_width) (row_bytes)
#endif /* !CONVERT_2_1 */

#if !defined(CONVERT_4_1)
#define CONVERT_4_1()                                                                                        \
    do {                                                                                                     \
        int w;                                                                                               \
        for (w = (byte_width / 4) - 1; w >= 0; w--)                                                          \
            outp[w] = ((*table)[0][inp[w * 4]] | (*table)[1][inp[w * 4 + 1]] | (*table)[2][inp[w * 4 + 2]] | \
                       (*table)[3][inp[w * 4 + 3]]);                                                         \
    } while (0)
#define COMPUTE_ADD_4_1(row_bytes, byte_width) (row_bytes)
#endif /* !CONVERT_4_1 */

#if !defined(CONVERT_8_1)
#define CONVERT_8_1()                                                                                            \
    do {                                                                                                         \
        int w;                                                                                                   \
        for (w = (byte_width / 8) - 1; w >= 0; w--)                                                              \
            outp[w] = ((*table)[0][inp[w * 8]] | (*table)[1][inp[w * 8 + 1]] | (*table)[2][inp[w * 8 + 2]] |     \
                       (*table)[3][inp[w * 8 + 3]] | (*table)[4][inp[w * 8 + 4]] | (*table)[5][inp[w * 8 + 5]] | \
                       (*table)[6][inp[w * 8 + 6]] | (*table)[7][inp[w * 8 + 7]]);                               \
    } while (0)
#define COMPUTE_ADD_8_1(row_bytes, byte_width) (row_bytes)
#endif /* !CONVERT_8_1 */

#define CONVERT_RGB_IND(in_type, out_bpp)                              \
    do {                                                               \
        int w, shift;                                                  \
        uint8 v;                                                       \
                                                                       \
        shift = 8 - (out_bpp);                                         \
        v = 0;                                                         \
                                                                       \
        for (w = byte_width; w > 0; w -= sizeof(in_type)) {            \
            RGBColor r;                                                \
            in_type in;                                                \
                                                                       \
            in = *(const in_type*)inp;                                 \
            inp += sizeof(in_type);                                    \
            (*src_rgb_spec->pixel_to_rgbcolor)(src_rgb_spec, in, &r);  \
            v |= (Color2Index(&r) & ((1L << (out_bpp)) - 1)) << shift; \
            shift -= (out_bpp);                                        \
            if (shift < 0) {                                           \
                *outp++ = v;                                           \
                shift = 8 - (out_bpp);                                 \
                v = 0;                                                 \
            }                                                          \
        }                                                              \
    } while (0)

#if !defined(CONVERT_16_1)
#define CONVERT_16_1() CONVERT_RGB_IND(uint16, 1)
#define COMPUTE_ADD_16_1(row_bytes, byte_width) ((row_bytes) - (byte_width))
#endif /* !CONVERT_16_1 */

#if !defined(CONVERT_16_2)
#define CONVERT_16_2() CONVERT_RGB_IND(uint16, 2)
#define COMPUTE_ADD_16_2(row_bytes, byte_width) ((row_bytes) - (byte_width))
#endif /* !CONVERT_16_2 */

#if !defined(CONVERT_16_4)
#define CONVERT_16_4() CONVERT_RGB_IND(uint16, 4)
#define COMPUTE_ADD_16_4(row_bytes, byte_width) ((row_bytes) - (byte_width))
#endif /* !CONVERT_16_4 */

#if !defined(CONVERT_16_8)
#define CONVERT_16_8() CONVERT_RGB_IND(uint16, 8)
#define COMPUTE_ADD_16_8(row_bytes, byte_width) ((row_bytes) - (byte_width))
#endif /* !CONVERT_16_8 */

#if !defined(CONVERT_32_1)
#define CONVERT_32_1() CONVERT_RGB_IND(uint32, 1)
#define COMPUTE_ADD_32_1(row_bytes, byte_width) ((row_bytes) - (byte_width))
#endif /* !CONVERT_32_1 */

#if !defined(CONVERT_32_2)
#define CONVERT_32_2() CONVERT_RGB_IND(uint32, 2)
#define COMPUTE_ADD_32_2(row_bytes, byte_width) ((row_bytes) - (byte_width))
#endif /* !CONVERT_32_2 */

#if !defined(CONVERT_32_4)
#define CONVERT_32_4() CONVERT_RGB_IND(uint32, 4)
#define COMPUTE_ADD_32_4(row_bytes, byte_width) ((row_bytes) - (byte_width))
#endif /* !CONVERT_32_4 */

#if !defined(CONVERT_32_8)
#define CONVERT_32_8() CONVERT_RGB_IND(uint32, 8)
#define COMPUTE_ADD_32_8(row_bytes, byte_width) ((row_bytes) - (byte_width))
#endif /* !CONVERT_32_8 */

#define CONVERT_RGB_RGB(in_type, out_type)                                      \
    do {                                                                        \
        int w;                                                                  \
                                                                                \
        for (w = byte_width; w > 0; w -= sizeof(in_type)) {                     \
            RGBColor r;                                                         \
            out_type v;                                                         \
            in_type in;                                                         \
                                                                                \
            in = *(const in_type*)inp;                                          \
            inp += sizeof(in_type);                                             \
                                                                                \
            if (in == cache_in)                                                 \
                v = cache_out;                                                  \
            else {                                                              \
                /* Translate src RGB to canonical form. */                      \
                (*src_rgb_spec->pixel_to_rgbcolor)(src_rgb_spec, in, &r);       \
                                                                                \
                /* Translate canonical form to dest pixel. */                   \
                v = (*dst_rgb_spec->rgbcolor_to_pixel)(dst_rgb_spec, &r, TRUE); \
                                                                                \
                /* Record the last RGB translated, for speed. */                \
                cache_in = in;                                                  \
                cache_out = v;                                                  \
            }                                                                   \
                                                                                \
            *(out_type*)outp = v;                                               \
            outp += sizeof(out_type);                                           \
        }                                                                       \
    } while (0)

#if !defined(CONVERT_16_16)
#define CONVERT_16_16() CONVERT_RGB_RGB(uint16, uint16)
#define COMPUTE_ADD_16_16(row_bytes, byte_width) ((row_bytes) - (byte_width))
#endif /* !CONVERT_16_16 */

#if !defined(CONVERT_16_32)
#define CONVERT_16_32() CONVERT_RGB_RGB(uint16, uint32)
#define COMPUTE_ADD_16_32(row_bytes, byte_width) ((row_bytes) - (byte_width))
#endif /* !CONVERT_16_32 */

#if !defined(CONVERT_32_16)
#define CONVERT_32_16() CONVERT_RGB_RGB(uint32, uint16)
#define COMPUTE_ADD_32_16(row_bytes, byte_width) ((row_bytes) - (byte_width))
#endif /* !CONVERT_32_16 */

#if !defined(CONVERT_32_32)
#define CONVERT_32_32() CONVERT_RGB_RGB(uint32, uint32)
#define COMPUTE_ADD_32_32(row_bytes, byte_width) ((row_bytes) - (byte_width))
#endif /* !CONVERT_32_32 */

#define _ALIGN_TABLE(t, in, out) DEPTHCONV_ALIGN_TABLE(t, in, out)
#define EXTRA_DECLS
#define EXTRA_SETUP()
#define EXTRA_CLEANUP()

/* Handle bpp nondecreasing cases. */
#define IF_TABLE(x)
CONVERT_FUNC(depthconv_copy, CONVERT_COPY, 0, 0, COMPUTE_ADD_COPY, void)
#undef IF_TABLE
#define IF_TABLE(x) x
CONVERT_FUNC(depthconv_1_1, CONVERT_1_1, 0, 0, COMPUTE_ADD_1_1, depthconv_1_1_data_t)
CONVERT_FUNC(depthconv_1_2, CONVERT_1_2, 1, 1, COMPUTE_ADD_1_2, depthconv_1_2_data_t)
CONVERT_FUNC(depthconv_1_4, CONVERT_1_4, 2, 2, COMPUTE_ADD_1_4, depthconv_1_4_data_t)
CONVERT_FUNC(depthconv_1_8, CONVERT_1_8, 3, 3, COMPUTE_ADD_1_8, depthconv_1_8_data_t)
CONVERT_FUNC(depthconv_1_16, CONVERT_1_16, 4, 4, COMPUTE_ADD_1_16, depthconv_1_16_data_t)
CONVERT_FUNC(depthconv_1_32, CONVERT_1_32, 5, 5, COMPUTE_ADD_1_32, depthconv_1_32_data_t)

/* Handle bpp decreasing cases. */
CONVERT_FUNC(depthconv_2_1, CONVERT_2_1, -1, 1, COMPUTE_ADD_2_1, depthconv_2_1_data_t)
CONVERT_FUNC(depthconv_4_1, CONVERT_4_1, -2, 2, COMPUTE_ADD_4_1, depthconv_4_1_data_t)
CONVERT_FUNC(depthconv_8_1, CONVERT_8_1, -3, 3, COMPUTE_ADD_8_1, depthconv_8_1_data_t)

/* Handle RGB -> RGB cases. */
#undef _ALIGN_TABLE
#define _ALIGN_TABLE(t, in, out) (t)

#undef EXTRA_SETUP
#undef EXTRA_CLEANUP

#define EXTRA_SETUP()                          \
    do {                                       \
        src_rgb_spec = table->src_rgb_spec;    \
        dst_rgb_spec = table->dst_rgb_spec;    \
        cache_in = src_rgb_spec->white_pixel;  \
        cache_out = dst_rgb_spec->white_pixel; \
    } while (0)

#define EXTRA_CLEANUP()

#define SRC_DST_RGB_SPEC_DECLS      \
    const rgb_spec_t* src_rgb_spec; \
    const rgb_spec_t* dst_rgb_spec

#undef EXTRA_DECLS
#define EXTRA_DECLS   \
    uint16 cache_in;  \
    uint16 cache_out; \
    SRC_DST_RGB_SPEC_DECLS
CONVERT_FUNC(depthconv_16_16, CONVERT_16_16, 0, 0, COMPUTE_ADD_16_16, depthconv_rgb_to_rgb_data_t)

#undef EXTRA_DECLS
#define EXTRA_DECLS   \
    uint16 cache_in;  \
    uint32 cache_out; \
    SRC_DST_RGB_SPEC_DECLS
CONVERT_FUNC(depthconv_16_32, CONVERT_16_32, 1, 1, COMPUTE_ADD_16_32, depthconv_rgb_to_rgb_data_t)

#undef EXTRA_DECLS
#define EXTRA_DECLS   \
    uint32 cache_in;  \
    uint16 cache_out; \
    SRC_DST_RGB_SPEC_DECLS
CONVERT_FUNC(depthconv_32_16, CONVERT_32_16, -1, 1, COMPUTE_ADD_32_16, depthconv_rgb_to_rgb_data_t)

#undef EXTRA_DECLS
#define EXTRA_DECLS   \
    uint32 cache_in;  \
    uint32 cache_out; \
    SRC_DST_RGB_SPEC_DECLS
CONVERT_FUNC(depthconv_32_32, CONVERT_32_32, 0, 0, COMPUTE_ADD_32_32, depthconv_rgb_to_rgb_data_t)

/* Handle RGB -> indirect cases. */
#undef EXTRA_DECLS
#undef EXTRA_SETUP
#undef EXTRA_CLEANUP
#undef SRC_DST_RGB_SPEC_DECLS

#define EXTRA_DECLS            \
    GDHandle gdev;             \
    PixMapHandle gd_pmap;      \
    CTabHandle gdev_ctab_save; \
    ITabHandle gdev_itab_save; \
    const rgb_spec_t* src_rgb_spec

#define EXTRA_SETUP()                                                   \
    do {                                                                \
        gdev = MR(TheGDevice);                                          \
        gd_pmap = GD_PMAP(gdev);                                        \
        FIELD_ASSIGN_RAW(gdev_ctab_save, PIXMAP_TABLE_X(gd_pmap));      \
        FIELD_ASSIGN_RAW(gdev_itab_save, GD_ITABLE_X(gdev));            \
        FIELD_ASSIGN_RAW(PIXMAP_TABLE_X(gd_pmap), table->swapped_ctab); \
        FIELD_ASSIGN_RAW(GD_ITABLE_X(gdev), table->swapped_itab);       \
        src_rgb_spec = table->src_rgb_spec;                             \
    } while (0)

#define EXTRA_CLEANUP()                                            \
    do {                                                           \
        FIELD_ASSIGN_RAW(PIXMAP_TABLE_X(gd_pmap), gdev_ctab_save); \
        FIELD_ASSIGN_RAW(GD_ITABLE_X(gdev), gdev_itab_save);       \
    } while (0)

CONVERT_FUNC(depthconv_16_1, CONVERT_16_1, -4, 4, COMPUTE_ADD_16_1, depthconv_rgb_to_ind_data_t)
CONVERT_FUNC(depthconv_16_2, CONVERT_16_2, -3, 3, COMPUTE_ADD_16_2, depthconv_rgb_to_ind_data_t)
CONVERT_FUNC(depthconv_16_4, CONVERT_16_4, -2, 2, COMPUTE_ADD_16_4, depthconv_rgb_to_ind_data_t)
CONVERT_FUNC(depthconv_16_8, CONVERT_16_8, -1, 1, COMPUTE_ADD_16_8, depthconv_rgb_to_ind_data_t)

CONVERT_FUNC(depthconv_32_1, CONVERT_32_1, -5, 5, COMPUTE_ADD_32_1, depthconv_rgb_to_ind_data_t)
CONVERT_FUNC(depthconv_32_2, CONVERT_32_2, -4, 4, COMPUTE_ADD_32_2, depthconv_rgb_to_ind_data_t)
CONVERT_FUNC(depthconv_32_4, CONVERT_32_4, -3, 3, COMPUTE_ADD_32_4, depthconv_rgb_to_ind_data_t)
CONVERT_FUNC(depthconv_32_8, CONVERT_32_8, -2, 2, COMPUTE_ADD_32_8, depthconv_rgb_to_ind_data_t)
