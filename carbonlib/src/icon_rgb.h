/*+===================================================================
  File:      icon_rgb.h

  Summary:   Encode and decode classic ICNS RGB elements for Icon Services.

  Classes:   None.

  Functions: Private RGB element size, decoder and encoder helpers.

  Origin:    Universal Interfaces 3.4.2 Icons.h, classic ICNS resource
             encoding as documented by Pillow's IcnsImagePlugin, and the
             IconFamily project's use of the Carbon xRGB handle layout.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_ICON_RGB_H
#define ROMAW_CARBON_ICON_RGB_H

static unsigned icon_rgb_width(uint32 type)
{
    switch (type) {
    case T('i', 's', '3', '2'):
        return 16;
    case T('i', 'l', '3', '2'):
        return 32;
    case T('i', 'h', '3', '2'):
        return 48;
    case T('i', 't', '3', '2'):
        return 128;
    default:
        return 0;
    }
}

/* ICNS stores either interleaved RGB or three consecutive run-length
   encoded planes. IconFamilyData passes an unused byte before each RGB. */
static boolean_t icon_rgb_decode(const Byte* source, unsigned length, unsigned width, Byte* pixels)
{
    const unsigned count = width * width;
    if (width == 128) {
        if (length < 4 || source[0] || source[1] || source[2] || source[3])
            return FALSE;
        source += 4;
        length -= 4;
    }
    if (length == count * 3) {
        for (unsigned i = 0; i < count; ++i) {
            pixels[4 * i] = 0;
            memcpy(pixels + 4 * i + 1, source + 3 * i, 3);
        }
        return TRUE;
    }
    memset(pixels, 0, count * 4);
    unsigned input = 0;
    for (unsigned channel = 1; channel <= 3; ++channel) {
        for (unsigned position = 0; position < count;) {
            if (input == length)
                return FALSE;
            unsigned packet = source[input++];
            boolean_t repeat = packet >= 128;
            unsigned run = repeat ? packet - 125 : packet + 1;
            unsigned consumed = repeat ? 1 : run;
            if (run > count - position || consumed > length - input)
                return FALSE;
            for (unsigned i = 0; i < run; ++i)
                pixels[4 * (position + i) + channel] = source[input + (repeat ? 0 : i)];
            input += consumed;
            position += run;
        }
    }
    return input == length;
}

static unsigned icon_rgb_repeat(const Byte* channel, unsigned remaining)
{
    unsigned run = 1;
    while (run < 130 && run < remaining && channel[4 * run] == channel[0])
        ++run;
    return run;
}

/* The caller reserves three bytes per pixel plus the thumbnail prefix.
   Stop encoding as soon as the interleaved representation is smaller. */
static unsigned icon_rgb_encode(const Byte* pixels, unsigned width, Byte* encoded)
{
    const unsigned count = width * width;
    const unsigned prefix = width == 128 ? 4 : 0;
    const unsigned limit = prefix + count * 3;
    unsigned used = prefix;
    memset(encoded, 0, prefix);
    for (unsigned channel = 1; channel <= 3; ++channel) {
        for (unsigned position = 0; position < count;) {
            unsigned run = icon_rgb_repeat(pixels + 4 * position + channel, count - position);
            if (run >= 3) {
                if (limit - used <= 2)
                    goto interleaved;
                encoded[used++] = run + 125;
                encoded[used++] = pixels[4 * position + channel];
                position += run;
                continue;
            }
            unsigned start = position++;
            while (position < count && position - start < 128 &&
                   icon_rgb_repeat(pixels + 4 * position + channel, count - position) < 3)
                ++position;
            run = position - start;
            if (limit - used <= run + 1)
                goto interleaved;
            encoded[used++] = run - 1;
            for (unsigned i = start; i < position; ++i)
                encoded[used++] = pixels[4 * i + channel];
        }
    }
    return used;

interleaved:
    for (unsigned i = 0; i < count; ++i)
        memcpy(encoded + prefix + 3 * i, pixels + 4 * i + 1, 3);
    return limit;
}

#endif
