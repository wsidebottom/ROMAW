/*+===================================================================
  File:      dcomp0.h

  Summary:   System 7 dcmp 0 resource decoding. Format/table reference:

  Classes:   None.

  Functions: ROMlib_decode_dcmp0, ROMlib_decompress_dcmp0, ROMlib_decompress_dcmp0_prefix;
             internal stream helpers (all static).

  Origin:    https://github.com/dgelessus/python-rsrcfork/blob/master/src/rsrcfork/compress/dcmp0.py

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* System 7 dcmp 0 resource decoding. Format/table reference:
 * https://github.com/dgelessus/python-rsrcfork/blob/master/src/rsrcfork/compress/dcmp0.py
 * Native bounded implementation; keep the input separate from the output.
 *
 * MIT License
 *
 * Copyright (c) 2016 dgelessus
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef RSYS_DCOMP0_H
#define RSYS_DCOMP0_H
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t ROMlib_dcmp0_table[] = {
    0x00, 0x00, 0x4e, 0xba, 0x00, 0x08, 0x4e, 0x75, 0x00, 0x0c, 0x4e, 0xad, 0x20, 0x53, 0x2f, 0x0b, 0x61, 0x00, 0x00,
    0x10, 0x70, 0x00, 0x2f, 0x00, 0x48, 0x6e, 0x20, 0x50, 0x20, 0x6e, 0x2f, 0x2e, 0xff, 0xfc, 0x48, 0xe7, 0x3f, 0x3c,
    0x00, 0x04, 0xff, 0xf8, 0x2f, 0x0c, 0x20, 0x06, 0x4e, 0xed, 0x4e, 0x56, 0x20, 0x68, 0x4e, 0x5e, 0x00, 0x01, 0x58,
    0x8f, 0x4f, 0xef, 0x00, 0x02, 0x00, 0x18, 0x60, 0x00, 0xff, 0xff, 0x50, 0x8f, 0x4e, 0x90, 0x00, 0x06, 0x26, 0x6e,
    0x00, 0x14, 0xff, 0xf4, 0x4c, 0xee, 0x00, 0x0a, 0x00, 0x0e, 0x41, 0xee, 0x4c, 0xdf, 0x48, 0xc0, 0xff, 0xf0, 0x2d,
    0x40, 0x00, 0x12, 0x30, 0x2e, 0x70, 0x01, 0x2f, 0x28, 0x20, 0x54, 0x67, 0x00, 0x00, 0x20, 0x00, 0x1c, 0x20, 0x5f,
    0x18, 0x00, 0x26, 0x6f, 0x48, 0x78, 0x00, 0x16, 0x41, 0xfa, 0x30, 0x3c, 0x28, 0x40, 0x72, 0x00, 0x28, 0x6e, 0x20,
    0x0c, 0x66, 0x00, 0x20, 0x6b, 0x2f, 0x07, 0x55, 0x8f, 0x00, 0x28, 0xff, 0xfe, 0xff, 0xec, 0x22, 0xd8, 0x20, 0x0b,
    0x00, 0x0f, 0x59, 0x8f, 0x2f, 0x3c, 0xff, 0x00, 0x01, 0x18, 0x81, 0xe1, 0x4a, 0x00, 0x4e, 0xb0, 0xff, 0xe8, 0x48,
    0xc7, 0x00, 0x03, 0x00, 0x22, 0x00, 0x07, 0x00, 0x1a, 0x67, 0x06, 0x67, 0x08, 0x4e, 0xf9, 0x00, 0x24, 0x20, 0x78,
    0x08, 0x00, 0x66, 0x04, 0x00, 0x2a, 0x4e, 0xd0, 0x30, 0x28, 0x26, 0x5f, 0x67, 0x04, 0x00, 0x30, 0x43, 0xee, 0x3f,
    0x00, 0x20, 0x1f, 0x00, 0x1e, 0xff, 0xf6, 0x20, 0x2e, 0x42, 0xa7, 0x20, 0x07, 0xff, 0xfa, 0x60, 0x02, 0x3d, 0x40,
    0x0c, 0x40, 0x66, 0x06, 0x00, 0x26, 0x2d, 0x48, 0x2f, 0x01, 0x70, 0xff, 0x60, 0x04, 0x18, 0x80, 0x4a, 0x40, 0x00,
    0x40, 0x00, 0x2c, 0x2f, 0x08, 0x00, 0x11, 0xff, 0xe4, 0x21, 0x40, 0x26, 0x40, 0xff, 0xf2, 0x42, 0x6e, 0x4e, 0xb9,
    0x3d, 0x7c, 0x00, 0x38, 0x00, 0x0d, 0x60, 0x06, 0x42, 0x2e, 0x20, 0x3c, 0x67, 0x0c, 0x2d, 0x68, 0x66, 0x08, 0x4a,
    0x2e, 0x4a, 0xae, 0x00, 0x2e, 0x48, 0x40, 0x22, 0x5f, 0x22, 0x00, 0x67, 0x0a, 0x30, 0x07, 0x42, 0x67, 0x00, 0x32,
    0x20, 0x28, 0x00, 0x09, 0x48, 0x7a, 0x02, 0x00, 0x2f, 0x2b, 0x00, 0x05, 0x22, 0x6e, 0x66, 0x02, 0xe5, 0x80, 0x67,
    0x0e, 0x66, 0x0a, 0x00, 0x50, 0x3e, 0x00, 0x66, 0x0c, 0x2e, 0x00, 0xff, 0xee, 0x20, 0x6d, 0x20, 0x40, 0xff, 0xe0,
    0x53, 0x40, 0x60, 0x08, 0x04, 0x80, 0x00, 0x68, 0x0b, 0x7c, 0x44, 0x00, 0x41, 0xe8, 0x48, 0x41,
};

typedef struct {
    const uint8_t* src;
    size_t len, pos;
} ROMlib_dcmp0_reader;

static int ROMlib_dcmp0_integer(ROMlib_dcmp0_reader* r, int32_t* value)
{
    uint32_t v;
    unsigned head, n, i;
    if (r->pos == r->len)
        return 0;
    head = r->src[r->pos++];
    if (head < 0x80) {
        *value = head;
        return 1;
    }
    n = head == 0xff ? 4 : 1;
    if (n > r->len - r->pos)
        return 0;
    v = head == 0xff ? 0 : (head - 0xc0) & 0xff;
    for (i = 0; i < n; ++i)
        v = (v << 8) | r->src[r->pos++];
    if (n == 1 && (v & 0x8000))
        v |= UINT32_C(0xffff0000);
    *value = v <= INT32_MAX ? (int32_t)v : -1 - (int32_t)~v;
    return 1;
}

/* dcmp 0 may emit one padding byte for an odd declared output size. */
static int ROMlib_dcmp0_emit(uint8_t* dest, size_t size, size_t* pos, const uint8_t* data, size_t count)
{
    size_t remaining = *pos <= size ? size - *pos : 0;
    if (count > remaining && (!(size & 1) || *pos > size || count - remaining != 1))
        return 0;
    if (count && dest)
        memcpy(dest + (*pos < size ? *pos : size), data, count < remaining ? count : remaining);
    *pos += count;
    return 1;
}

static int ROMlib_decode_dcmp0_stream(const uint8_t* src, size_t len, uint8_t* dest, size_t size, size_t* decoded_size,
                                      size_t* consumed_size)
{
    struct literal {
        size_t offset, length;
    }* literals = NULL;

    size_t nlit = 0, capacity = 0, out = 0, count, index;
    ROMlib_dcmp0_reader r = {src, len, 0};
    int ok = 0;
    if ((!src && len) || (!dest && size && !decoded_size && !consumed_size) || len > SIZE_MAX / sizeof(*literals))
        return 0;
    while (r.pos < len) {
        unsigned tag = src[r.pos++];
        uint8_t bytes[8];
        if (tag == 0xff) {
            ok = (consumed_size || r.pos == len) && (decoded_size || out == size || ((size & 1) && out == size + 1));
            break;
        }
        if (tag < 0x20) {
            count = tag & 15;
            if (!count) {
                if (r.pos == len)
                    break;
                count = src[r.pos++];
            }
            count *= 2;
            if (count > len - r.pos)
                break;
            if (tag >= 0x10) {
                if (nlit == capacity) {
                    size_t next = capacity ? capacity * 2 : 16;
                    if (next > len)
                        next = len;
                    if (next <= capacity || next > SIZE_MAX / sizeof(*literals))
                        break;
                    void* grown = realloc(literals, next * sizeof(*literals));
                    if (!grown)
                        break;
                    literals = grown;
                    capacity = next;
                }
                literals[nlit].offset = r.pos;
                literals[nlit++].length = count;
            }
            if (!ROMlib_dcmp0_emit(dest, size, &out, src + r.pos, count))
                break;
            r.pos += count;
        }
        else if (tag < 0x4b) {
            if (tag <= 0x22) {
                if (r.pos == len)
                    break;
                index = src[r.pos++];
                if (tag == 0x22) {
                    if (r.pos == len)
                        break;
                    index = (index << 8) | src[r.pos++];
                }
                else
                    index |= (tag - 0x20) << 8;
                index += 0x28;
            }
            else
                index = tag - 0x23;
            if (index >= nlit)
                break;
            if (!ROMlib_dcmp0_emit(dest, size, &out, src + literals[index].offset, literals[index].length))
                break;
        }
        else if (tag < 0xfe) {
            if (!ROMlib_dcmp0_emit(dest, size, &out, ROMlib_dcmp0_table + (tag - 0x4b) * 2, 2))
                break;
        }
        else {
            unsigned kind, width;
            int32_t value, number, delta;
            uint32_t current;
            size_t i;
            if (r.pos == len)
                break;
            kind = src[r.pos++];
            if (kind > 6)
                break;
            if (!ROMlib_dcmp0_integer(&r, &value) || !ROMlib_dcmp0_integer(&r, &number))
                break;
            if (number < 0 && kind != 1)
                break;
            current = (uint32_t)value;
            if (kind == 0) {
                if (value < 0 || value > 65535)
                    break;
                bytes[2] = 0x3f;
                bytes[3] = 0x3c;
                bytes[4] = current >> 8;
                bytes[5] = current;
                bytes[6] = 0xa9;
                bytes[7] = 0xf0;
                if (!ROMlib_dcmp0_emit(dest, size, &out, bytes + 2, 6))
                    break;
                if ((size_t)number > (size - (out < size ? out : size) + (size & 1)) / 8)
                    break;
                for (i = 0; i < (size_t)number; ++i) {
                    if (!ROMlib_dcmp0_integer(&r, &delta))
                        goto done;
                    if (i == 0) {
                        if (delta < 0 || delta > 65535)
                            goto done;
                        current = delta;
                    }
                    else
                        current = (current + (uint32_t)delta - 6) & 0xffff;
                    bytes[0] = current >> 8;
                    bytes[1] = current;
                    if (!ROMlib_dcmp0_emit(dest, size, &out, bytes, 8))
                        goto done;
                }
            }
            else if (kind == 1) {
                int32_t repeat, a5_offset;
                if (!ROMlib_dcmp0_integer(&r, &repeat) || !ROMlib_dcmp0_integer(&r, &a5_offset) || repeat < 0 ||
                    (size_t)repeat + 1 > (size - (out < size ? out : size) + (size & 1)) / 8)
                    break;
                for (i = 0; i <= (size_t)repeat; ++i) {
                    if (i) {
                        current -= 8;
                        if (number)
                            a5_offset = (int32_t)((uint32_t)a5_offset + (uint32_t)number);
                        else if (!ROMlib_dcmp0_integer(&r, &a5_offset))
                            goto done;
                    }
                    bytes[0] = 0x61;
                    bytes[1] = 0;
                    bytes[2] = current >> 8;
                    bytes[3] = current;
                    bytes[4] = 0x4e;
                    bytes[5] = 0xed;
                    bytes[6] = (uint32_t)a5_offset >> 8;
                    bytes[7] = a5_offset;
                    if (!ROMlib_dcmp0_emit(dest, size, &out, bytes, 8))
                        goto done;
                }
            }
            else if (kind == 2 || kind == 3) {
                width = kind - 1;
                if (value < 0 || (uint32_t)value > (width == 1 ? 255U : 65535U))
                    break;
                bytes[0] = width == 1 ? current : current >> 8;
                bytes[1] = current;
                count = (size_t)number + 1;
                if (count > (size - (out < size ? out : size) + (size & 1)) / width)
                    break;
                for (i = 0; i < count; ++i)
                    if (!ROMlib_dcmp0_emit(dest, size, &out, bytes, width))
                        goto done;
            }
            else {
                width = kind == 6 ? 4 : 2;
                if (kind == 4 && (value < -32768 || value > 32767))
                    break;
                count = (size_t)number + 1;
                if (count > (size - (out < size ? out : size) + (size & 1)) / width)
                    break;
                for (i = 0; i < count; ++i) {
                    if (i) {
                        if (kind == 4) {
                            if (r.pos == len)
                                goto done;
                            delta = src[r.pos] < 128 ? src[r.pos] : (int)src[r.pos] - 256;
                            ++r.pos;
                        }
                        else if (!ROMlib_dcmp0_integer(&r, &delta))
                            goto done;
                        current += (uint32_t)delta;
                    }
                    bytes[0] = current >> (width == 2 ? 8 : 24);
                    bytes[1] = current >> (width == 2 ? 0 : 16);
                    bytes[2] = current >> 8;
                    bytes[3] = current;
                    if (!ROMlib_dcmp0_emit(dest, size, &out, bytes, width))
                        goto done;
                }
            }
        }
    }
done:
    if (ok && decoded_size)
        *decoded_size = out < size ? out : size;
    if (ok && consumed_size)
        *consumed_size = r.pos;
    free(literals);
    return ok;
}

static int ROMlib_decode_dcmp0(const uint8_t* src, size_t len, uint8_t* dest, size_t size, size_t* decoded_size)
{
    return ROMlib_decode_dcmp0_stream(src, len, dest, size, decoded_size, NULL);
}

/* The Classic dcmp ABI supplies an output size in D0, but no input length.
   Accept the first complete stream within a known readable input extent. */
static int ROMlib_decompress_dcmp0_prefix(const uint8_t* src, size_t limit, uint8_t* dest, size_t size,
                                          size_t* consumed_size)
{
    size_t consumed;
    return ROMlib_decode_dcmp0_stream(src, limit, dest, size, NULL, consumed_size ? consumed_size : &consumed);
}

static int ROMlib_decompress_dcmp0(const uint8_t* src, size_t len, uint8_t* dest, size_t size)
{
    return ROMlib_decode_dcmp0(src, len, dest, size, NULL);
}
#endif
