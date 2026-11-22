/*+===================================================================
  File:      geometry.h

  Summary:   Implements native CarbonLib QuickDraw geometry and coordinate transforms.

  Classes:   None.

  Functions: Private PowerPC frame adapters and geometry helpers.

  Origin:    CarbonLib 1.4, System 9.2.1 NQD and InterfaceLib overlay 35,
             and the existing Executor QuickDraw record layouts.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_GEOMETRY_H
#define ROMAW_CARBON_GEOMETRY_H

#include "internal.h"
#include "rsys/cquick.h"
#include "rsys/pattern_origin.h"

static Point carbon_unpack_point(uint32_t word)
{
    return (Point){(int16_t)(word >> 16), (int16_t)word};
}

static int carbon_geometry_words(const ROMlib_carbon_ppc_frame* call, uint32_t address, void* record, size_t size,
                                 int writing)
{
    if (call->guest_words)
        return call->guest_words(call->guest_opaque, address, record, size, writing);
    void* guest = ROMlib_carbon_guest_memory(address, size, writing);
    if (!guest)
        return 0;
    if (writing)
        memcpy(guest, record, size);
    else
        memcpy(record, guest, size);
    return 1;
}

static Rect carbon_rect_from_edges(int16_t top, int16_t left, int16_t bottom, int16_t right)
{
    return (Rect){CW(top), CW(left), CW(bottom), CW(right)};
}

static void carbon_translate_rect(Rect* rect, int16_t dh, int16_t dv, int inset)
{
    *rect = carbon_rect_from_edges(CW(rect->top) + dv, CW(rect->left) + dh, CW(rect->bottom) + (inset ? -dv : dv),
                                   CW(rect->right) + (inset ? -dh : dh));
}

static int carbon_rect_is_empty(const Rect* rect)
{
    return CW(rect->top) >= CW(rect->bottom) || CW(rect->left) >= CW(rect->right);
}

static int carbon_translate_point(ROMlib_carbon_ppc_frame* call, int sign)
{
    const uint32_t address = call->words[1];
    ROMlib_carbon_set_qd_error(address ? noErr : paramErr);
    if (!address)
        return 1;
    Point output;
    if (!carbon_geometry_words(call, address, &output, sizeof output, 0))
        return 0;
    const Point point = carbon_unpack_point(call->words[0]);
    output.v = CW(CW(output.v) + sign * point.v);
    output.h = CW(CW(output.h) + sign * point.h);
    return carbon_geometry_words(call, address, &output, sizeof output, 1);
}

static int carbon_AddPt(ROMlib_carbon_ppc_frame* call)
{
    return carbon_translate_point(call, 1);
}

static int carbon_SubPt(ROMlib_carbon_ppc_frame* call)
{
    return carbon_translate_point(call, -1);
}

static int carbon_EqualPt(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_set_qd_error(noErr);
    call->words[0] = call->words[0] == call->words[1];
    return 1;
}

static int carbon_SetPt(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t address = call->words[0];
    ROMlib_carbon_set_qd_error(address ? noErr : paramErr);
    Point output = {CW(call->words[2]), CW(call->words[1])};
    return !address || carbon_geometry_words(call, address, &output, sizeof output, 1);
}

static int carbon_SetRect(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t address = call->words[0];
    ROMlib_carbon_set_qd_error(address ? noErr : paramErr);
    Rect output = carbon_rect_from_edges(call->words[2], call->words[1], call->words[4], call->words[3]);
    return !address || carbon_geometry_words(call, address, &output, sizeof output, 1);
}

static int carbon_shift_rect(ROMlib_carbon_ppc_frame* call, int inset)
{
    const uint32_t address = call->words[0];
    ROMlib_carbon_set_qd_error(address ? noErr : paramErr);
    if (!address)
        return 1;
    Rect output;
    if (!carbon_geometry_words(call, address, &output, sizeof output, 0))
        return 0;
    carbon_translate_rect(&output, call->words[1], call->words[2], inset);
    return carbon_geometry_words(call, address, &output, sizeof output, 1);
}

static int carbon_OffsetRect(ROMlib_carbon_ppc_frame* call)
{
    return carbon_shift_rect(call, 0);
}

static int carbon_InsetRect(ROMlib_carbon_ppc_frame* call)
{
    return carbon_shift_rect(call, 1);
}

static int carbon_EmptyRect(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t address = call->words[0];
    ROMlib_carbon_set_qd_error(address ? noErr : paramErr);
    Rect rect;
    if (address && !carbon_geometry_words(call, address, &rect, sizeof rect, 0))
        return 0;
    call->words[0] = !address || carbon_rect_is_empty(&rect);
    return 1;
}

static int carbon_EqualRect(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* words = call->words;
    if (!words[0] || !words[1]) {
        ROMlib_carbon_set_qd_error(paramErr);
        call->words[0] = 0;
        return 1;
    }
    Rect first, second;
    if (!carbon_geometry_words(call, words[0], &first, sizeof first, 0) ||
        !carbon_geometry_words(call, words[1], &second, sizeof second, 0))
        return 0;
    ROMlib_carbon_set_qd_error(noErr);
    call->words[0] = memcmp(&first, &second, sizeof first) == 0;
    return 1;
}

static int carbon_rect_combination(ROMlib_carbon_ppc_frame* call, int intersection)
{
    const uint32_t* words = call->words;
    if (!words[0] || !words[1] || (!intersection && !words[2])) {
        ROMlib_carbon_set_qd_error(paramErr);
        call->words[0] = 0;
        return 1;
    }
    Rect first, second;
    if (!carbon_geometry_words(call, words[0], &first, sizeof first, 0) ||
        !carbon_geometry_words(call, words[1], &second, sizeof second, 0))
        return 0;

    ROMlib_carbon_set_qd_error(noErr);
    const int16_t top1 = CW(first.top), top2 = CW(second.top);
    const int16_t left1 = CW(first.left), left2 = CW(second.left);
    const int16_t bottom1 = CW(first.bottom), bottom2 = CW(second.bottom);
    const int16_t right1 = CW(first.right), right2 = CW(second.right);
    Rect output;
    if (intersection) {
        output = carbon_rect_from_edges(MAX(top1, top2), MAX(left1, left2), MIN(bottom1, bottom2), MIN(right1, right2));
        const int empty = carbon_rect_is_empty(&output);
        if (empty)
            output = (Rect){0};
        call->words[0] = !empty;
    }
    else
        output = carbon_rect_from_edges(MIN(top1, top2), MIN(left1, left2), MAX(bottom1, bottom2), MAX(right1, right2));
    return !words[2] || carbon_geometry_words(call, words[2], &output, sizeof output, 1);
}

static int carbon_SectRect(ROMlib_carbon_ppc_frame* call)
{
    return carbon_rect_combination(call, 1);
}

static int carbon_UnionRect(ROMlib_carbon_ppc_frame* call)
{
    return carbon_rect_combination(call, 0);
}

static int carbon_PtInRect(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t address = call->words[1];
    ROMlib_carbon_set_qd_error(address ? noErr : paramErr);
    Rect rect;
    if (address && !carbon_geometry_words(call, address, &rect, sizeof rect, 0))
        return 0;
    const Point point = carbon_unpack_point(call->words[0]);
    call->words[0] = address && point.v >= CW(rect.top) && point.v < CW(rect.bottom) && point.h >= CW(rect.left) &&
                     point.h < CW(rect.right);
    return 1;
}

static int carbon_Pt2Rect(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t address = call->words[2];
    ROMlib_carbon_set_qd_error(address ? noErr : paramErr);
    const Point first = carbon_unpack_point(call->words[0]);
    const Point second = carbon_unpack_point(call->words[1]);
    Rect output = carbon_rect_from_edges(MIN(first.v, second.v), MIN(first.h, second.h), MAX(first.v, second.v),
                                         MAX(first.h, second.h));
    return !address || carbon_geometry_words(call, address, &output, sizeof output, 1);
}

static int carbon_DeltaPoint(ROMlib_carbon_ppc_frame* call)
{
    const Point first = carbon_unpack_point(call->words[0]);
    const Point second = carbon_unpack_point(call->words[1]);
    call->words[0] = (uint32_t)(uint16_t)(first.v - second.v) << 16 | (uint16_t)(first.h - second.h);
    return 1;
}

static int carbon_deltapoint(ROMlib_carbon_ppc_frame* call)
{
    uint32_t first, second;
    if (!carbon_geometry_words(call, call->words[0], &first, sizeof first, 0) ||
        !carbon_geometry_words(call, call->words[1], &second, sizeof second, 0))
        return 0;
    call->words[0] = CL(first);
    call->words[1] = CL(second);
    return carbon_DeltaPoint(call);
}

static int16_t carbon_map_coordinate(int16_t value, int16_t start, int16_t end, int16_t target_start,
                                     int16_t target_end)
{
    const int source_extent = end - start, target_extent = target_end - target_start;
    int64_t delta = value - start;
    if (source_extent != target_extent) {
        /* NQD rounds the unsigned distance before restoring its sign.
           PowerPC mullw and add keep the low word of the numerator. */
        const uint32_t distance = delta < 0 ? -delta : delta;
        const int half = (source_extent - (source_extent < 0 && source_extent % 2)) / 2;
        const int32_t numerator = distance * (uint32_t)target_extent + (uint32_t)half;
        const int64_t scaled = source_extent ? (int64_t)numerator / source_extent : 0;
        delta = delta < 0 ? -scaled : scaled;
    }
    const int64_t translated = target_start + delta;
    return translated > INT16_MAX ? INT16_MAX : translated < INT16_MIN ? INT16_MIN : (int16_t)translated;
}

static int16_t carbon_scale_coordinate(int16_t value, int source_extent, int target_extent)
{
    if (source_extent == target_extent)
        return value;
    if (value <= 0)
        return 0;
    const uint32_t numerator = (uint32_t)value * (uint32_t)target_extent + ((uint32_t)source_extent >> 1);
    const int32_t scaled = numerator / (uint32_t)source_extent;
    return scaled > INT16_MAX ? INT16_MAX : scaled == 0 ? 1 : (int16_t)scaled;
}

static int carbon_map_geometry(ROMlib_carbon_ppc_frame* call, unsigned points, int scale)
{
    const uint32_t* words = call->words;
    if (!words[0] || !words[1] || !words[2]) {
        ROMlib_carbon_set_qd_error(paramErr);
        return 1;
    }
    Point output[2];
    size_t output_size = sizeof(Point) * points;
    Rect source, destination;
    if (!carbon_geometry_words(call, words[0], output, output_size, 0) ||
        !carbon_geometry_words(call, words[1], &source, sizeof source, 0) ||
        !carbon_geometry_words(call, words[2], &destination, sizeof destination, 0))
        return 0;
    ROMlib_carbon_set_qd_error(noErr);
    const int16_t top = CW(source.top), left = CW(source.left);
    const int16_t bottom = CW(source.bottom), right = CW(source.right);
    const int16_t target_top = CW(destination.top), target_left = CW(destination.left);
    const int16_t target_bottom = CW(destination.bottom), target_right = CW(destination.right);
    if (scale) {
        if (bottom == top)
            return 1;
        output->v = CW(carbon_scale_coordinate(CW(output->v), bottom - top, target_bottom - target_top));
        if (right != left)
            output->h = CW(carbon_scale_coordinate(CW(output->h), right - left, target_right - target_left));
    }
    else {
        for (unsigned index = 0; index < points; ++index) {
            Point value = output[index];
            output[index].v = CW(carbon_map_coordinate(CW(value.v), top, bottom, target_top, target_bottom));
            output[index].h = CW(carbon_map_coordinate(CW(value.h), left, right, target_left, target_right));
        }
    }
    return carbon_geometry_words(call, words[0], output, output_size, 1);
}

static int carbon_MapPt(ROMlib_carbon_ppc_frame* call)
{
    return carbon_map_geometry(call, 1, 0);
}

static int carbon_MapRect(ROMlib_carbon_ppc_frame* call)
{
    return carbon_map_geometry(call, 2, 0);
}

static int carbon_ScalePt(ROMlib_carbon_ppc_frame* call)
{
    return carbon_map_geometry(call, 1, 1);
}

static int carbon_Random(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t seed = (uint32_t)ROMlib_carbon_qd_random_seed();
    const uint32_t next = (uint32_t)((uint64_t)seed * 16807 % INT32_MAX);
    ROMlib_carbon_set_qd_error(noErr);
    ROMlib_carbon_set_qd_random_seed(next);
    const int16_t result = (int16_t)next;
    call->words[0] = result == INT16_MIN ? 0 : result;
    return 1;
}

static int carbon_QDSetPatternOrigin(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_QDSetPatternOrigin(carbon_unpack_point(call->words[0]));
    return 1;
}

static int carbon_port_origin(uint32_t address, Point* origin)
{
    const GrafPort* port = ROMlib_carbon_guest_memory(address, offsetof(GrafPort, portBits) + sizeof(BitMap), 0);
    if (!port)
        return 0;
    const Rect* bounds = &port->portBits.bounds;
    if (CGrafPort_p(port)) {
        const CGrafPort* color = (const CGrafPort*)port;
        const uint32_t handle = CL(PP_RAW(color->portPixMap));
        const uint32_t* master = handle ? ROMlib_carbon_guest_memory(handle, 4, 0) : NULL;
        const uint32_t address = master ? CL(*master) : 0;
        const PixMap* pixmap = address ? ROMlib_carbon_guest_memory(address, sizeof(PixMap), 0) : NULL;
        if (!pixmap)
            return ROMlib_carbon_report_failure("Invalid QuickDraw port pixmap");
        bounds = &pixmap->bounds;
    }
    *origin = (Point){CW(bounds->top), CW(bounds->left)};
    return 1;
}

enum carbon_coordinate_type { carbon_point, carbon_rect, carbon_region };

static int carbon_port_coordinates(ROMlib_carbon_ppc_frame* call, enum carbon_coordinate_type type, int direction)
{
    const uint32_t port = call->words[0], address = call->words[1];
    if (port && address) {
        Point origin;
        if (!carbon_port_origin(port, &origin))
            return 0;
        const int16_t dh = direction * origin.h, dv = direction * origin.v;
        if (type == carbon_point) {
            Point* point = ROMlib_carbon_guest_memory(address, sizeof(Point), 1);
            if (!point)
                return 0;
            *point = (Point){CW(CW(point->v) + dv), CW(CW(point->h) + dh)};
        }
        else if (type == carbon_rect) {
            Rect* rect = ROMlib_carbon_guest_memory(address, sizeof(Rect), 1);
            if (!rect)
                return 0;
            ROMlib_carbon_set_qd_error(noErr);
            carbon_translate_rect(rect, dh, dv, 0);
        }
        else {
            ROMlib_carbon_ppc_frame offset = {0};
            offset.words[0] = address;
            offset.words[1] = dh;
            offset.words[2] = dv;
            uint32_t result;
            if (!ROMlib_carbon_call_toolbox_vector(0x1180, 0xac0, 0, &offset, &result))
                return 0;
        }
    }
    call->words[0] = address;
    return 1;
}

#define CARBON_COORDINATE_WRAPPER(name, type, direction)       \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)    \
    {                                                          \
        return carbon_port_coordinates(call, type, direction); \
    }

CARBON_COORDINATE_WRAPPER(QDGlobalToLocalPoint, carbon_point, 1)
CARBON_COORDINATE_WRAPPER(QDGlobalToLocalRect, carbon_rect, 1)
CARBON_COORDINATE_WRAPPER(QDGlobalToLocalRegion, carbon_region, 1)
CARBON_COORDINATE_WRAPPER(QDLocalToGlobalPoint, carbon_point, -1)
CARBON_COORDINATE_WRAPPER(QDLocalToGlobalRect, carbon_rect, -1)
CARBON_COORDINATE_WRAPPER(QDLocalToGlobalRegion, carbon_region, -1)
#undef CARBON_COORDINATE_WRAPPER

static int carbon_IsRegionRectangular(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* handle = ROMlib_carbon_guest_memory(call->words[0], 4, 0);
    if (!handle)
        return 0;
    const uint16_t* size = ROMlib_carbon_guest_memory(CL(*handle), 2, 0);
    if (!size)
        return 0;
    call->words[0] = CW(*size) == sizeof(Region);
    return 1;
}

#endif
