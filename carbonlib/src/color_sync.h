/*+===================================================================
  File:      color_sync.h

  Summary:   Convert packed color arrays and query ColorSync installation information.

  Classes:   None.

  Functions: Private PowerPC frame adapters for the Carbon export registry.

  Origin:    ColorSync Extension from the Mac OS 9.2.1 Installer Tome,
             CarbonLib 1.4 re-exports, and Universal Interfaces 3.4.2.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_COLOR_SYNC_H
#define ROMAW_CARBON_COLOR_SYNC_H

enum color_conversion {
    color_rgb_to_gray,
    color_xyz_to_fixed,
    color_fixed_to_xyz,
    color_xyz_to_yxy,
    color_yxy_to_xyz,
    color_rgb_to_hsv,
    color_hsv_to_rgb,
    color_rgb_to_hls,
    color_hls_to_rgb,
    color_xyz_to_lab,
    color_lab_to_xyz,
    color_xyz_to_luv,
    color_luv_to_xyz,
};

static uint32_t color_read(const unsigned char* bytes, unsigned width)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < width; ++i)
        value = (value << 8) | bytes[i];
    return value;
}

static void color_write(unsigned char* bytes, unsigned width, uint32_t value)
{
    while (width) {
        bytes[--width] = (unsigned char)value;
        value >>= 8;
    }
}

static int carbon_CMGetColorSyncVersion(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t address = call->words[0];
    unsigned char* version = address ? ROMlib_carbon_try_guest_memory(address, 4, 1) : NULL;
    if (version)
        color_write(version, 4, 0x0304);
    call->words[0] = version ? noErr : paramErr;
    return 1;
}

static int carbon_CMGetColorSyncFolderSpec(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t volume = call->words[2], directory = call->words[3];
    if (!volume || !directory || !ROMlib_carbon_try_guest_memory(volume, 2, 1) ||
        !ROMlib_carbon_try_guest_memory(directory, 4, 1)) {
        call->words[0] = paramErr;
        return 1;
    }
    ROMlib_carbon_ppc_frame arguments = {0};
    arguments.words[0] = (int16_t)call->words[0];
    arguments.words[1] = TICK("prof");
    arguments.words[2] = (uint8_t)call->words[1];
    arguments.words[3] = volume;
    arguments.words[4] = directory;
    uint32_t result;
    if (!ROMlib_carbon_call_toolbox_vector(0x0e8c, 0x3dea8, 0, &arguments, &result))
        return 0;
    call->words[0] = (int16_t)result;
    return 1;
}

typedef struct {
    uint32_t first, second, third;
} color_triplet;

enum { color_out_of_range = -4214 };

static color_triplet color_rgb_to_hue(color_triplet rgb, int hls)
{
    const uint32_t high = MAX(rgb.first, MAX(rgb.second, rgb.third));
    const uint32_t low = MIN(rgb.first, MIN(rgb.second, rgb.third));
    const uint32_t delta = high - low;
    uint32_t saturation = high ? ((delta << 16) + high / 2) / high : 0;
    saturation = MIN(saturation, UINT16_MAX);
    uint16_t hue = 0;
    if (saturation) {
        int32_t offset, difference;
        if (high == rgb.first) {
            offset = 0;
            difference = (int32_t)rgb.second - (int32_t)rgb.third;
        }
        else if (high == rgb.second) {
            offset = 0x5555;
            difference = (int32_t)rgb.third - (int32_t)rgb.first;
        }
        else {
            offset = -0x5555;
            difference = (int32_t)rgb.first - (int32_t)rgb.second;
        }
        const int32_t magnitude = difference < 0 ? -difference : difference;
        const uint32_t divisor = 6 * delta;
        const int32_t step = (((uint64_t)magnitude << 16) + divisor / 2) / divisor;
        hue = (uint16_t)(offset + (difference < 0 ? -step : step));
    }
    if (!hls)
        return (color_triplet){hue, saturation, high};

    const uint32_t chroma = (high * saturation + 0x8000) >> 16;
    const uint32_t sum = 2 * high - chroma;
    const uint32_t divisor = sum > 0x10000 ? 0x20000 - sum : sum;
    const uint32_t light = (sum + 1) >> 1;
    const uint32_t hls_saturation = divisor ? ((chroma << 16) + divisor / 2) / divisor : 0;
    return (color_triplet){hue, MIN(light, UINT16_MAX), MIN(hls_saturation, UINT16_MAX)};
}

static color_triplet color_hue_to_rgb(color_triplet color, int hls)
{
    uint32_t saturation = color.second;
    uint32_t high = color.third;
    if (hls) {
        const uint32_t light = color.second;
        const uint32_t side = light <= 0x8000 ? light : UINT16_MAX - light;
        const uint32_t spread = (side * color.third + 0x8000) >> 16;
        high = light + spread;
        saturation = high ? ((2 * spread << 16) + high / 2) / high : 0;
        saturation = MIN(saturation, UINT16_MAX);
        high = MIN(high, UINT16_MAX);
    }
    const uint32_t delta = (saturation * high + 0x8000) >> 16;
    const uint32_t sector = 6 * color.first;
    const uint32_t fade = (delta * (sector & 0xffff) + 0x8000) >> 16;
    const uint32_t low = high - delta, rising = low + fade, falling = high - fade;
    switch (sector >> 16) {
    case 0:
        return (color_triplet){high, rising, low};
    case 1:
        return (color_triplet){falling, high, low};
    case 2:
        return (color_triplet){low, high, rising};
    case 3:
        return (color_triplet){low, falling, high};
    case 4:
        return (color_triplet){rising, low, high};
    default:
        return (color_triplet){high, low, falling};
    }
}

/* Digit-by-digit cube root used by the 16.16 XYZ converters. The second
   pass refines the root after the integer groups have been consumed. */
static uint32_t color_cube_root(uint32_t ratio)
{
    const uint32_t radicand = ratio << 14;
    if (!radicand)
        return 0;
    uint32_t remainder = radicand >> 30, bits = radicand & 0x3fffffff;
    uint32_t trial = 1, root = 0;
    for (unsigned i = 0; i < 10; ++i) {
        const uint32_t next = root * 2;
        if (remainder >= trial) {
            remainder -= trial;
            trial = root * 18 + 4 * trial + 15;
            root = next + 2;
        }
        else {
            trial = 4 * trial - root * 6 - 3;
            root = next;
        }
        remainder = remainder * 8 + (bits >> 27);
        bits = (bits << 3) & 0x3ffffff8;
    }

    unsigned remaining = 10;
    while (remainder && remaining) {
        const unsigned shift = 3 * (11 - remaining);
        if (remainder >= trial) {
            remainder -= trial;
            trial = (trial >> 1) + ((root * 18 + 15) >> shift);
            root = root * 2 + 2;
        }
        else {
            trial = (trial >> 1) - ((root * 6 + 3) >> shift);
            root *= 2;
        }
        --remaining;
    }
    if (!remainder)
        return (root << (remaining + 10)) >> 14;

    const uint32_t divisor = trial & 0xffff;
    const uint32_t fraction = !trial ? 0x400 : divisor ? (remainder << 10) / divisor : 0;
    return ((root << 10) + fraction) >> 14;
}

static uint32_t color_cie_curve(uint32_t value, uint32_t illuminant, int luv)
{
    const uint32_t ratio = (value << 16) / illuminant;
    return ratio < 0x245 ? ((ratio * 0x7c978 + (luv ? 0x8000 : 0)) >> 16) + 0x234f : color_cube_root(ratio);
}

static uint32_t color_cie_light(uint32_t curve, int* status)
{
    if (curve > 0x10000) {
        *status = color_out_of_range;
        return 0xff00;
    }
    return (uint16_t)(((curve * 0x93e6 + 0x4000) >> 15) - 0x28cc);
}

static uint32_t color_cie_chroma(int32_t value, int* status)
{
    if (value < 0 || value >= 0xff01) {
        *status = color_out_of_range;
        return value < 0 ? 0 : 0xff00;
    }
    return (uint32_t)value;
}

static color_triplet color_xyz_to_cie(color_triplet xyz, color_triplet illuminant, int luv, int* status)
{
    const uint32_t fy = color_cie_curve(xyz.second, illuminant.second, luv);
    const uint32_t light = color_cie_light(fy, status);
    if (!luv) {
        const int32_t fx = color_cie_curve(xyz.first, illuminant.first, 0);
        const int32_t fz = color_cie_curve(xyz.third, illuminant.third, 0);
        const int32_t a = ((fx - (int32_t)fy) * 500 + 0x800014) >> 8;
        const int32_t b = (((int32_t)fy - fz) * 200 + 0x800078) >> 8;
        return (color_triplet){light, color_cie_chroma(a, status), color_cie_chroma(b, status)};
    }
    const uint32_t sum = xyz.first + 15 * xyz.second + 3 * xyz.third;
    if (!sum)
        return (color_triplet){light, 0, 0};
    const uint32_t white_sum = illuminant.first + 15 * illuminant.second + 3 * illuminant.third;
    const int32_t u = (int32_t)(light * xyz.first + sum / 2) / (int32_t)sum -
                      (int32_t)(light * illuminant.first + white_sum / 2) / (int32_t)white_sum;
    const int32_t v = (int32_t)(light * xyz.second + sum / 2) / (int32_t)sum -
                      (int32_t)(light * illuminant.second + white_sum / 2) / (int32_t)white_sum;
    return (color_triplet){light, color_cie_chroma(((u * 0x519) >> 6) + 0x8000, status),
                           color_cie_chroma(((v * 0xb78) >> 6) + 0x8000, status)};
}

static uint32_t color_cie_y(uint32_t light, uint32_t illuminant, int luv, int* status)
{
    uint32_t curve = ((light * 0xdd8e) >> 16) + 0x234f;
    if (luv && curve >= 0x10000) {
        *status = color_out_of_range;
        curve = 0xffff;
    }
    if (curve < 0x34f8)
        return (((light * 0xe3a) >> 16) * illuminant) >> 15;
    const uint32_t squared = (curve * curve) >> 16;
    return (((curve * squared) >> 16) * illuminant) >> 16;
}

static uint32_t color_lab_channel(uint32_t curve, uint32_t illuminant, int* status)
{
    if (curve < 0x8d4)
        return 0;
    if (curve < 0xd3f)
        return ((((curve * 0x41c0) >> 16) - 0x244) * illuminant) >> 15;
    const uint32_t cubed = (curve * ((curve * curve) >> 14)) >> 14;
    const uint32_t result = (illuminant * cubed) >> 14;
    if (result > UINT16_MAX) {
        *status = color_out_of_range;
        return UINT16_MAX;
    }
    return result;
}

static uint32_t color_xyz_channel(int32_t value, int* status)
{
    if (value >= 0 && value <= UINT16_MAX)
        return (uint32_t)value;
    *status = color_out_of_range;
    return value < 0 ? 0 : UINT16_MAX;
}

static color_triplet color_cie_to_xyz(color_triplet cie, color_triplet illuminant, int luv, int* status)
{
    const uint32_t y = color_cie_y(cie.first, illuminant.second, luv, status);
    if (!luv) {
        const int32_t fy = (((cie.first * 0xdd8e) >> 16) + 0x234f) >> 2;
        const int32_t fx = fy + (int32_t)((cie.second * 0x20c5) >> 16) - 0x1062;
        const int32_t fz = fy - (int32_t)((cie.third * 0x51ec) >> 16) + 0x28f6;
        return (color_triplet){color_lab_channel(MAX(fx, 0), illuminant.first, status), y,
                               color_lab_channel(MAX(fz, 0), illuminant.third, status)};
    }
    if (!y)
        return (color_triplet){0, 0, 0};
    const uint32_t sum = illuminant.first + 15 * illuminant.second + 3 * illuminant.third;
    const int32_t u =
        ((int32_t)cie.second * 0x647 - 0x3238000) / (int32_t)cie.first + (int32_t)((illuminant.first << 15) / sum);
    const int32_t v =
        ((int32_t)cie.third * 0x647 - 0x3238000) / (int32_t)cie.first + (int32_t)((illuminant.second * 9 << 13) / sum);
    if (!v)
        return (color_triplet){0, y, 0};
    /* Convert the classic 32-bit products before division, including wrap. */
    const int64_t x_ratio = (int32_t)((uint32_t)u * y) / (int64_t)v;
    const int32_t x = (int32_t)((uint32_t)x_ratio * 9 + 1) >> 2;
    const uint32_t z_scale = 0x18000 - 20 * (uint32_t)v - 3 * (uint32_t)u;
    const int32_t z = (int32_t)((int32_t)(z_scale * (y >> 2)) / (int64_t)v);
    return (color_triplet){color_xyz_channel(x, status), y, color_xyz_channel(z, status)};
}

static void color_convert_one(enum color_conversion operation, const unsigned char* source, unsigned char* output,
                              color_triplet illuminant, int* status)
{
    const unsigned width = operation == color_fixed_to_xyz ? 4 : 2;
    const uint32_t x = color_read(source, width);
    const uint32_t y = color_read(source + width, width);
    const uint32_t z = color_read(source + 2 * width, width);
    if (operation == color_rgb_to_gray) {
        color_write(output, 2, (77 * x + 151 * y + 28 * z + 128) >> 8);
    }
    else if (operation == color_xyz_to_fixed) {
        const uint32_t values[] = {x, y, z};
        for (unsigned i = 0; i < 3; ++i)
            color_write(output + 4 * i, 4, 2 * values[i] + (values[i] & 1));
    }
    else if (operation == color_fixed_to_xyz) {
        const uint32_t values[] = {x, y, z};
        for (unsigned i = 0; i < 3; ++i) {
            const uint32_t value = values[i];
            if (value > 0x1ffff)
                *status = color_out_of_range;
            color_write(output + 2 * i, 2, value > 0x1ffff ? UINT16_MAX : (value + 1) >> 1);
        }
    }
    else if (operation == color_xyz_to_yxy) {
        const uint32_t sum = x + y + z;
        const uint32_t luminance = MIN(y, 0x7fff);
        if (y > 0x7fff)
            *status = color_out_of_range;
        color_write(output, 2, 2 * luminance + (luminance & 1));
        color_write(output + 2, 2, sum ? (x << 16) / sum : 0);
        color_write(output + 4, 2, sum ? (luminance << 16) / sum : 0);
    }
    else if (operation == color_yxy_to_xyz) {
        /* The fixed-width products intentionally wrap before the shift. */
        const uint32_t scale = x > 1 && z ? ((x & ~1U) << 13) / z : 0;
        color_write(output, 2, (scale * y) >> 14);
        color_write(output + 2, 2, x >> 1);
        color_write(output + 4, 2, (scale * (UINT32_C(65535) - y - z)) >> 14);
    }
    else {
        const color_triplet input = {x, y, z};
        color_triplet result;
        switch (operation) {
        case color_rgb_to_hsv:
        case color_rgb_to_hls:
            result = color_rgb_to_hue(input, operation == color_rgb_to_hls);
            break;
        case color_hsv_to_rgb:
        case color_hls_to_rgb:
            result = color_hue_to_rgb(input, operation == color_hls_to_rgb);
            break;
        case color_xyz_to_lab:
        case color_xyz_to_luv:
            result = color_xyz_to_cie(input, illuminant, operation == color_xyz_to_luv, status);
            break;
        default:
            result = color_cie_to_xyz(input, illuminant, operation == color_luv_to_xyz, status);
            break;
        }
        /* The original Luv conversion leaves X untouched when Y rounds to zero. */
        if (operation != color_luv_to_xyz || result.second)
            color_write(output, 2, result.first);
        color_write(output + 2, 2, result.second);
        color_write(output + 4, 2, result.third);
    }
}

static int color_convert(ROMlib_carbon_ppc_frame* call, enum color_conversion operation)
{
    /* CMColor is an eight-byte union. Gray writes only its first word, and
       the fixed-point conversions use the standalone XYZ records. */
    const unsigned source_stride = operation == color_fixed_to_xyz ? 12 : operation == color_xyz_to_fixed ? 6 : 8;
    const unsigned output_stride = operation == color_fixed_to_xyz ? 6 : operation == color_xyz_to_fixed ? 12 : 8;
    const unsigned source_width = operation == color_fixed_to_xyz ? 12 : 6;
    const unsigned output_width = operation == color_xyz_to_fixed ? 12 : operation == color_rgb_to_gray ? 2 : 6;
    const unsigned white_point = operation >= color_xyz_to_lab;
    const uint32_t input = call->words[0], output = call->words[1 + white_point], count = call->words[2 + white_point];
    color_triplet illuminant = {0};
    call->words[0] = 0;
    if (!count)
        return 1;

    const uint64_t source_size = (uint64_t)(count - 1) * source_stride + source_width;
    const uint64_t output_size = (uint64_t)(count - 1) * output_stride + output_width;
    if (!input || !output || input + source_size > UINT64_C(0x100000000) ||
        output + output_size > UINT64_C(0x100000000) || source_size > SIZE_MAX || output_size > SIZE_MAX) {
        call->words[0] = paramErr;
        return 1;
    }
    const unsigned char* source = ROMlib_carbon_try_guest_memory(input, (size_t)source_size, 0);
    unsigned char* destination = ROMlib_carbon_try_guest_memory(output, (size_t)output_size, 1);
    if (!source || !destination) {
        call->words[0] = paramErr;
        return 1;
    }

    if (white_point) {
        const uint32_t address = call->words[1];
        const unsigned char* record = address ? ROMlib_carbon_try_guest_memory(address, 6, 0) : NULL;
        if (record)
            illuminant = (color_triplet){color_read(record, 2), color_read(record + 2, 2), color_read(record + 4, 2)};
        if (!record ||
            (operation == color_xyz_to_lab && (!illuminant.first || !illuminant.second || !illuminant.third)) ||
            (operation == color_xyz_to_luv && !illuminant.second) ||
            (operation == color_luv_to_xyz && !(illuminant.first | illuminant.second | illuminant.third))) {
            call->words[0] = paramErr;
            return 1;
        }
    }

    const int overlap = input < output + output_size && output < input + source_size;
    unsigned char* snapshot = NULL;
    if (overlap && input != output && source_stride != output_stride) {
        snapshot = malloc((size_t)source_size);
        if (!snapshot) {
            call->words[0] = memFullErr;
            return 1;
        }
        memcpy(snapshot, source, (size_t)source_size);
        source = snapshot;
    }
    const int backwards =
        overlap && !snapshot && (input < output || (input == output && source_stride < output_stride));
    int status = 0;
    for (uint32_t n = 0; n < count; ++n) {
        const uint32_t i = backwards ? count - n - 1 : n;
        color_convert_one(operation, source + (size_t)i * source_stride, destination + (size_t)i * output_stride,
                          illuminant, &status);
    }
    free(snapshot);
    call->words[0] = status;
    return 1;
}

static int carbon_CMConvertRGBToGray(ROMlib_carbon_ppc_frame* call)
{
    return color_convert(call, color_rgb_to_gray);
}

static int carbon_CMConvertXYZToFixedXYZ(ROMlib_carbon_ppc_frame* call)
{
    return color_convert(call, color_xyz_to_fixed);
}

static int carbon_CMConvertFixedXYZToXYZ(ROMlib_carbon_ppc_frame* call)
{
    return color_convert(call, color_fixed_to_xyz);
}

static int carbon_CMConvertXYZToYxy(ROMlib_carbon_ppc_frame* call)
{
    return color_convert(call, color_xyz_to_yxy);
}

static int carbon_CMConvertYxyToXYZ(ROMlib_carbon_ppc_frame* call)
{
    return color_convert(call, color_yxy_to_xyz);
}

static int carbon_CMConvertRGBToHSV(ROMlib_carbon_ppc_frame* call)
{
    return color_convert(call, color_rgb_to_hsv);
}

static int carbon_CMConvertHSVToRGB(ROMlib_carbon_ppc_frame* call)
{
    return color_convert(call, color_hsv_to_rgb);
}

static int carbon_CMConvertRGBToHLS(ROMlib_carbon_ppc_frame* call)
{
    return color_convert(call, color_rgb_to_hls);
}

static int carbon_CMConvertHLSToRGB(ROMlib_carbon_ppc_frame* call)
{
    return color_convert(call, color_hls_to_rgb);
}

static int carbon_CMConvertXYZToLab(ROMlib_carbon_ppc_frame* call)
{
    return color_convert(call, color_xyz_to_lab);
}

static int carbon_CMConvertLabToXYZ(ROMlib_carbon_ppc_frame* call)
{
    return color_convert(call, color_lab_to_xyz);
}

static int carbon_CMConvertXYZToLuv(ROMlib_carbon_ppc_frame* call)
{
    return color_convert(call, color_xyz_to_luv);
}

static int carbon_CMConvertLuvToXYZ(ROMlib_carbon_ppc_frame* call)
{
    return color_convert(call, color_luv_to_xyz);
}

#endif
