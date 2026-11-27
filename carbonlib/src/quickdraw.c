/*+===================================================================
  File:      quickdraw.c

  Summary:   QuickDraw accessors used by MyABCD. Records retain guest byte order; the native error state is shared with
             ROMAW's existing QDError implementation.

  Classes:   None.

  Functions: ROMlib_carbon_pix_bounds, ROMlib_carbon_pix_depth, ROMlib_carbon_pix_rowbytes,
             ROMlib_carbon_port_back_color, ROMlib_carbon_port_bitmap, ROMlib_carbon_port_bounds,
             ROMlib_carbon_port_char_extra, ROMlib_carbon_port_clip_region, ROMlib_carbon_port_fore_color,
             ROMlib_carbon_port_fractional_pen, ROMlib_carbon_port_graf_procs, ROMlib_carbon_port_hilite_color,
             ROMlib_carbon_port_op_color, ROMlib_carbon_port_pen_location, ROMlib_carbon_port_pen_mode,
             ROMlib_carbon_port_pen_size, ROMlib_carbon_port_pen_visibility, ROMlib_carbon_port_pixmap,
             ROMlib_carbon_port_space_extra, ROMlib_carbon_port_text_face, ROMlib_carbon_port_text_font,
             ROMlib_carbon_port_text_mode, ROMlib_carbon_port_text_size, ROMlib_carbon_port_visible_region,
             ROMlib_carbon_qd_arrow, ROMlib_carbon_qd_black, ROMlib_carbon_qd_dark_gray, ROMlib_carbon_qd_globals_port,
             ROMlib_carbon_qd_gray, ROMlib_carbon_qd_light_gray, ROMlib_carbon_qd_random_seed,
             ROMlib_carbon_qd_screen_bits, ROMlib_carbon_qd_white, ROMlib_carbon_region_bounds,
             ROMlib_carbon_intersect_rects, ROMlib_carbon_set_qd_error, ROMlib_carbon_set_qd_random_seed,
             ROMlib_carbon_lock_port_bits, ROMlib_carbon_unlock_port_bits, ROMlib_carbon_port_is_color,
             ROMlib_carbon_port_is_offscreen, ROMlib_carbon_port_defining_region, ROMlib_carbon_port_defining_picture,
             ROMlib_carbon_port_defining_polygon, ROMlib_carbon_set_port_bounds, ROMlib_carbon_set_port_clip,
             ROMlib_carbon_set_port_visible, ROMlib_carbon_set_port_fractional_pen, ROMlib_carbon_set_port_graf_procs,
             ROMlib_carbon_set_port_op_color, ROMlib_carbon_set_port_pen_mode, ROMlib_carbon_set_port_pen_size,
             ROMlib_carbon_get_port_back_pixpat, ROMlib_carbon_get_port_back_pixpat_direct,
             ROMlib_carbon_get_port_pen_pixpat, ROMlib_carbon_get_port_pen_pixpat_direct,
             ROMlib_carbon_get_port_fill_pixpat, ROMlib_carbon_set_port_back_pixpat,
             ROMlib_carbon_set_port_back_pixpat_direct, ROMlib_carbon_set_port_pen_pixpat,
             ROMlib_carbon_set_port_pen_pixpat_direct, ROMlib_carbon_set_port_fill_pixpat,
             ROMlib_carbon_set_port_fill_pixpat_direct, ROMlib_carbon_set_qd_arrow, ROMlib_carbon_convert_region_handle,
             ROMlib_carbon_region_bits, ROMlib_carbon_create_port, ROMlib_carbon_dispose_port, ROMlib_carbon_valid_port,
             ROMlib_carbon_region_to_rects, ROMlib_carbon_get_port_printing_reference,
             ROMlib_carbon_set_port_printing_reference, ROMlib_carbon_nqd_put_pic_byte,
             ROMlib_carbon_nqd_put_pic_data, ROMlib_carbon_nqd_put_pic_long, ROMlib_carbon_nqd_put_pic_op,
             ROMlib_carbon_nqd_put_pic_word, ROMlib_carbon_nqd_check_picture, ROMlib_carbon_nqd_hilite_rect,
             ROMlib_carbon_nqd_hilite_region, ROMlib_carbon_nqd_unflatten_pixpat

  Origin:    Port state additions follow CarbonLib 1.4 and Universal Interfaces 3.4.2.
             Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* QuickDraw accessors used by MyABCD. Records retain guest byte order; the
   native error state is shared with ROMAW's existing QDError implementation. */
#include "internal.h"
#include "rsys/picture.h"
#include "cf_batch.h"
#include "rsys/cquick.h"
#include "rsys/mman.h"
#include "rsys/pattern_origin.h"
#include "rsys/region.h"

void ROMlib_carbon_set_qd_error(INTEGER error)
{
    QDErr_H = CW(error);
}

int ROMlib_carbon_convert_region_handle(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t input = call->words[0], output = call->words[1];
    Handle source = input ? ROMlib_carbon_try_guest_memory(input, sizeof *source, 0) : NULL;
    Handle destination = output ? ROMlib_carbon_try_guest_memory(output, sizeof *destination, 1) : NULL;
    OSErr error = paramErr;
    if (source && destination && HandleZone(source) && HandleZone(destination)) {
        const Size extent = GetHandleSize(source);
        const Region* region =
            extent >= (Size)sizeof(Region)
                ? ROMlib_carbon_try_guest_memory(CL((uint32_t)(uintptr_t)HIDDEN_RAW(*source)), sizeof *region, 0)
                : NULL;
        if (region) {
            const Size length = RGNP_SIZE(region);
            if (length >= (Size)sizeof(Region) && length <= extent && !(length & 1)) {
                error = noErr;
                if (source != destination) {
                    const SignedByte flags = HGetState(source);
                    HLock(source);
                    ReallocHandle(destination, length);
                    error = CW(MemErr);
                    if (!error)
                        memcpy(STARH(destination), STARH(source), length);
                    HSetState(source, flags);
                }
            }
        }
    }
    ROMlib_carbon_set_qd_error(error);
    const uint16_t encoded = CW((uint16_t)error);
    void* guest_error = ROMlib_carbon_guest_memory(0xd6e, sizeof encoded, 1);
    if (!guest_error)
        return 0;
    memcpy(guest_error, &encoded, sizeof encoded);
    return 1;
}

enum { region_bits_count, region_bits_save, region_bits_restore };

static int region_bits_devices(const ROMlib_carbon_reference* ref, unsigned mode, uint32_t clipped, uint32_t box,
                               uint32_t intersection, const Rect* bounds, uint32_t buffer, uint32_t count,
                               uint32_t* index)
{
    uint32_t device;
    if (!cfp_private(ref, 0xb17a0, NULL, 0, &device))
        return 0;
    while (device) {
        _Alignas(8) Rect device_rect;
        uint32_t record, next;
        if (!cfp_get(device, &record) || !cfp_get(record + 0x1e, &next))
            return 0;
        const Rect* rect = ROMlib_carbon_guest_memory(record + 0x22, sizeof device_rect, 0);
        if (!rect)
            return 0;
        memcpy(&device_rect, rect, sizeof device_rect);
        if (CW(device_rect.top) <= CW(bounds->bottom) && CW(device_rect.left) <= CW(bounds->right) &&
            CW(device_rect.bottom) >= CW(bounds->top) && CW(device_rect.right) >= CW(bounds->left)) {
            const uint32_t rectangle[] = {box, US_TO_SYN68K(&device_rect)};
            const uint32_t clip[] = {clipped, box, intersection};
            const uint32_t offset[] = {intersection, (int16_t)-CW(device_rect.left), (int16_t)-CW(device_rect.top)};
            const uint32_t pixel_worker[] = {mode, intersection, device, buffer, count, US_TO_SYN68K(index)};
            if (!cfp_private(ref, 0xb1200, rectangle, NELEM(rectangle), NULL) ||
                !cfp_private(ref, 0xb1560, clip, NELEM(clip), NULL) ||
                !cfp_private(ref, 0xb1530, offset, NELEM(offset), NULL) ||
                !cfp_private(ref, 0x7116c, pixel_worker, NELEM(pixel_worker), NULL))
                return 0;
        }
        device = next;
    }
    return 1;
}

static int region_bits_walk(const ROMlib_carbon_reference* ref, unsigned mode, uint32_t region, uint32_t buffer,
                            uint32_t* end)
{
    uint32_t clipped = 0, box = 0, intersection = 0, empty, count, origin_slot, shield_slot;
    int shielded = 0;
    *end = 1;
    int ok = cfp_private(ref, 0xb1488, NULL, 0, &clipped);
    if (!ok || !clipped)
        return ok;
    const uint32_t clip[] = {clipped, region, clipped};
    ok = cfp_private(ref, 0xb1230, &clipped, 1, NULL) && cfp_private(ref, 0xb1560, clip, NELEM(clip), NULL) &&
         cfp_private(ref, 0xb15c0, &clipped, 1, &empty);
    if (ok && !empty) {
        ok = cfp_private(ref, 0xb1488, NULL, 0, &box) && (!box || cfp_private(ref, 0xb1488, NULL, 0, &intersection));
        if (ok && box && intersection) {
            _Alignas(8) Point origin;
            _Alignas(8) Rect bounds;
            const uint32_t point = US_TO_SYN68K(&origin);
            const uint32_t bounds_args[] = {clipped, US_TO_SYN68K(&bounds)};
            uint32_t origin_bits, shield_offset;
            ok = cfp_get(US_TO_SYN68K(ref->toc) - 0x4ac, &origin_slot) && cfp_get(origin_slot, &origin_bits) &&
                 cfp_write(point, origin_bits) && cfp_private(ref, 0xb1650, &point, 1, NULL) &&
                 cfp_get(US_TO_SYN68K(ref->toc) - 0x4a8, &shield_slot) && cfp_get(shield_slot, &shield_offset);
            if (ok) {
                const uint32_t delta[] = {clipped, (int16_t)CW(origin.h), (int16_t)CW(origin.v)};
                const uint32_t cursor[] = {bounds_args[1], shield_offset};
                ok = cfp_private(ref, 0xb1530, delta, NELEM(delta), NULL) &&
                     cfp_private(ref, 0xb18f0, bounds_args, NELEM(bounds_args), NULL) &&
                     cfp_private(ref, 0xb1818, cursor, NELEM(cursor), NULL);
                shielded = ok;
                _Alignas(8) uint32_t index = CL(1);
                ok = ok && cfp_get(buffer, &count) &&
                     region_bits_devices(ref, mode, clipped, box, intersection, &bounds, buffer, count, &index);
                *end = CL(index);
            }
        }
    }
    if (shielded)
        ok = cfp_private(ref, 0xb1368, NULL, 0, NULL) && ok;
    if (intersection)
        ok = cfp_private(ref, 0xb14d0, &intersection, 1, NULL) && ok;
    if (box)
        ok = cfp_private(ref, 0xb14d0, &box, 1, NULL) && ok;
    return cfp_private(ref, 0xb14d0, &clipped, 1, NULL) && ok;
}

int ROMlib_carbon_region_bits(const char* name, uint32_t offset, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", name, &ref))
        return 0;
    if (ref.code_offset != offset || ref.code_size != 0xb8fb4 || !ref.toc || ref.toc_offset != 0x2f78 ||
        ref.toc_size != 0x10f0c)
        return ROMlib_carbon_invoke_frame(ref.function, call);

    const uint32_t region = call->words[0], snapshot = call->words[1];
    if (offset == 0x71908) {
        call->words[0] = region ? noErr : paramErr;
        return !region || cfp_private(&ref, 0xb21a8, &region, 1, NULL);
    }
    uint32_t end;
    if (offset == 0x71940) {
        if (!region || !snapshot) {
            call->words[0] = paramErr;
            return 1;
        }
        const int restored = region_bits_walk(&ref, region_bits_restore, region, snapshot, &end);
        call->words[0] = noErr;
        return cfp_private(&ref, 0xb21a8, &snapshot, 1, NULL) && restored;
    }

    call->words[0] = 0;
    if (!region)
        return 1;
    _Alignas(8) uint32_t words = 0;
    if (!region_bits_walk(&ref, region_bits_count, region, US_TO_SYN68K(&words), &end))
        return 0;
    uint32_t count = CL(words);
    if (!count || count >= INT32_MAX / 4)
        return 1;
    ++count;
    const uint32_t size = count * 4;
    uint32_t output;
    if (!cfp_private(&ref, 0xb2178, &size, 1, &output))
        return 0;
    if (!output)
        return 1;
    if (!cfp_write(output, count) || !region_bits_walk(&ref, region_bits_save, region, output, &end)) {
        cfp_private(&ref, 0xb21a8, &output, 1, NULL);
        return 0;
    }
    if (end != count)
        return cfp_private(&ref, 0xb21a8, &output, 1, NULL);
    call->words[0] = output;
    return 1;
}

static int port_reference_matches(const ROMlib_carbon_reference* ref, uint32_t offset)
{
    return ref->code_offset == offset && ref->code_size == 0xb8fb4 && ref->toc && ref->toc_offset == 0x2f78 &&
           ref->toc_size == 0x10f0c;
}

int ROMlib_carbon_create_port(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", "CreateNewPort", &ref))
        return 0;
    if (!port_reference_matches(&ref, 0x74968))
        return ROMlib_carbon_invoke_frame(ref.function, call);

    call->words[0] = 0;
    CGrafPtr port = (CGrafPtr)NewPtr(sizeof *port);
    if (!port)
        return 1;
    if (!ROMlib_open_color_port(port)) {
        const OSErr saved = MemErr;
        DisposPtr((Ptr)port);
        MemErr = saved;
        return 1;
    }
    call->words[0] = US_TO_SYN68K(port);
    return 1;
}

int ROMlib_carbon_dispose_port(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t address = call->words[0];
    if (!address)
        return 1;
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", "DisposePort", &ref))
        return 0;
    if (!port_reference_matches(&ref, 0x749cc))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    CGrafPtr port = ROMlib_carbon_try_guest_memory(address, sizeof *port, 1);
    if (!port)
        return 0;
    CloseCPort(port);
    DisposPtr((Ptr)port);
    return 1;
}

int ROMlib_carbon_valid_port(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t address = call->words[0];
    GrafPtr port = address ? ROMlib_carbon_try_guest_memory(address, sizeof *port, 0) : NULL;
    call->words[0] = ROMlib_valid_qd_port(port);
    return 1;
}

/* CarbonLib's extended GrafVars record keeps a borrowed, opaque printing
   reference at guest offset 0x3e. Legacy ports start out with a small handle. */
enum { printing_reference_offset = 0x3e, extended_graf_vars_size = 0xa4 };

static Byte* port_printing_storage(uint32_t address)
{
    CGrafPtr port = address ? ROMlib_carbon_try_guest_memory(address, sizeof *port, 1) : theCPort;
    if (!port || !CGrafPort_p(port)) {
        ROMlib_carbon_set_qd_error(paramErr);
        return NULL;
    }

    const uint32_t master_address = CL((uint32_t)(uintptr_t)PP_RAW(CPORT_GRAFVARS_X(port)));
    Handle vars = master_address ? ROMlib_carbon_try_guest_memory(master_address, sizeof *vars, 1) : NULL;
    if (!vars || !HIDDEN_RAW(*vars) ||
        !ROMlib_carbon_try_guest_memory(CL((uint32_t)(uintptr_t)HIDDEN_RAW(*vars)), sizeof(GrafVars), 0)) {
        ROMlib_carbon_set_qd_error(paramErr);
        return NULL;
    }
    THz zone = HandleZone(vars);
    const Size size = zone ? GetHandleSize(vars) : 0;
    if (size < (Size)sizeof(GrafVars)) {
        ROMlib_carbon_set_qd_error(paramErr);
        return NULL;
    }
    if (size >= printing_reference_offset + (Size)sizeof(uint32_t))
        return (Byte*)STARH(vars) + printing_reference_offset;

    Handle extended;
    ZONE_SAVE_EXCURSION(RM(zone), { extended = NewHandleClear(extended_graf_vars_size); });
    if (!extended) {
        ROMlib_carbon_set_qd_error(MemError());
        return NULL;
    }
    memcpy(STARH(extended), STARH(vars), size);
    PP_RAW(CPORT_GRAFVARS_X(port)) = (uintptr_t)RM(extended);
    DisposHandle(vars);
    return (Byte*)STARH(extended) + printing_reference_offset;
}

int ROMlib_carbon_get_port_printing_reference(ROMlib_carbon_ppc_frame* call)
{
    const Byte* storage = port_printing_storage(call->words[0]);
    uint32_t reference = 0;
    if (storage)
        memcpy(&reference, storage, sizeof reference);
    call->words[0] = CL(reference);
    return 1;
}

int ROMlib_carbon_set_port_printing_reference(ROMlib_carbon_ppc_frame* call)
{
    Byte* storage = port_printing_storage(call->words[0]);
    const uint32_t reference = CL(call->words[1]);
    if (storage)
        memcpy(storage, &reference, sizeof reference);
    return 1;
}

static int region_parse_message(uint32_t callback, uint32_t context, uint32_t message, uint32_t region,
                                const Rect* rect, uint32_t* status)
{
    ROMlib_carbon_ppc_frame arguments = {
        .words = {message, region, rect ? US_TO_SYN68K(rect) : 0, context},
    };
    return ROMlib_carbon_invoke_mixed_upp(callback, 0x3fb1, &arguments, status);
}

struct region_scan_row {
    int16_t y;
    uint16_t start;
    uint16_t end;
};

/* Each row contains ordered XOR transitions. Merge them with the previous
   band, cancelling matching transitions to preserve holes and disjoint spans. */
static unsigned region_scan_merge(int16_t* output, const int16_t* active, unsigned count, const int16_t* changes,
                                  unsigned length)
{
    unsigned i = 0, j = 0, used = 0;
    while (i < count && j < length) {
        if (active[i] < changes[j])
            output[used++] = active[i++];
        else if (active[i] > changes[j])
            output[used++] = changes[j++];
        else {
            ++i;
            ++j;
        }
    }
    memcpy(output + used, active + i, (count - i) * sizeof *output);
    used += count - i;
    memcpy(output + used, changes + j, (length - j) * sizeof *output);
    return used + length - j;
}

static int region_scan_read(const Region* body, unsigned length, struct region_scan_row* rows, int16_t* changes)
{
    const Byte* bytes = (const Byte*)body + sizeof *body;
    const int special = RGNP_SPECIAL_P(body);
    unsigned cursor = 0, used = 0, count = 0;
    int previous_y = INT16_MIN - 1;
    while (cursor < length) {
        uint16_t word;
        memcpy(&word, bytes + cursor++ * sizeof word, sizeof word);
        const int16_t y = CW(word);
        if (y == RGN_STOP)
            return cursor == length ? (int)count : -1;
        if (y <= previous_y)
            return -1;
        previous_y = y;
        const unsigned start = used;
        int previous_x = INT16_MIN - 1;
        while (cursor < length) {
            memcpy(&word, bytes + cursor++ * sizeof word, sizeof word);
            const int16_t x = special ? word : CW(word);
            if (x == RGN_STOP)
                break;
            if (x <= previous_x)
                return -1;
            previous_x = x;
            changes[used++] = x;
        }
        if ((used - start) & 1)
            return -1;
        rows[count++] = (struct region_scan_row){y, start, used};
    }
    return -1;
}

static int region_scan_rectangles(const Region* body, unsigned length, const uint32_t* arguments, uint32_t* status)
{
    struct region_scan_row* rows = malloc(length * sizeof *rows);
    int16_t* changes = malloc(3 * length * sizeof *changes);
    if (!rows || !changes) {
        free(rows);
        free(changes);
        *status = memFullErr;
        return 1;
    }
    const int row_count = region_scan_read(body, length, rows, changes);
    const int downward = arguments[1] & 1;
    const int rightward = arguments[1] & 4;
    int16_t* active = changes + length;
    int16_t* next = active + length;
    int invoked = 1;
    unsigned count = 0;
    if (row_count < 0)
        *status = paramErr;
    for (int i = 1; i < row_count && !*status && invoked; ++i) {
        const unsigned current = downward ? i - 1 : row_count - i;
        const struct region_scan_row row = rows[current];
        count = region_scan_merge(next, active, count, changes + row.start, row.end - row.start);
        int16_t* previous = active;
        active = next;
        next = previous;
        const int16_t other_y = rows[downward ? current + 1 : current - 1].y;
        _Alignas(4) Rect rect = {.top = CW(MIN(row.y, other_y)), .bottom = CW(MAX(row.y, other_y))};
        for (unsigned j = 0; j < count && !*status && invoked; j += 2) {
            const unsigned x = rightward ? j : count - j - 2;
            rect.left = CW(active[x]);
            rect.right = CW(active[x + 1]);
            invoked = region_parse_message(arguments[2], arguments[3], 2, arguments[0], &rect, status);
        }
    }
    free(changes);
    free(rows);
    return invoked;
}

int ROMlib_carbon_region_to_rects(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t arguments[] = {call->words[0], call->words[1], call->words[2], call->words[3]};
    uint32_t status = paramErr;
    const uint32_t direction = arguments[1];
    if (!arguments[0] || !arguments[2] || direction > 15 || !(direction & 3) || (direction & 3) == 3 ||
        !(direction & 12) || (direction & 12) == 12) {
        call->words[0] = status;
        return 1;
    }
    Handle handle = ROMlib_carbon_try_guest_memory(arguments[0], sizeof *handle, 0);
    if (!handle || !HIDDEN_RAW(*handle) ||
        !ROMlib_carbon_try_guest_memory(CL((uint32_t)(uintptr_t)HIDDEN_RAW(*handle)), sizeof(Region), 0) ||
        !HandleZone(handle)) {
        call->words[0] = status;
        return 1;
    }
    const SignedByte state = HGetState(handle);
    HLock(handle);
    int invoked = region_parse_message(arguments[2], arguments[3], 1, 0, NULL, &status);
    if (invoked && !status) {
        const uint32_t body_address = CL((uint32_t)(uintptr_t)HIDDEN_RAW(*handle));
        const Size extent = GetHandleSize(handle);
        const Region* body = ROMlib_carbon_try_guest_memory(body_address, sizeof *body, 0);
        const unsigned size = body ? RGNP_SIZE(body) : 0;
        if (extent < (Size)sizeof *body || size < sizeof *body || size > (unsigned)extent || (size & 1) ||
            !ROMlib_carbon_try_guest_memory(body_address, size, 0))
            status = paramErr;
        else if (size == sizeof *body) {
            _Alignas(4) Rect rect = body->rgnBBox;
            if (CW(rect.top) < CW(rect.bottom) && CW(rect.left) < CW(rect.right)) {
                invoked = region_parse_message(arguments[2], arguments[3], 2, arguments[0], &rect, &status);
                /* The compact rectangle path returns the termination callback's status. */
                status = 0;
            }
        }
        else
            invoked = region_scan_rectangles(body, (size - sizeof *body) / sizeof(INTEGER), arguments, &status);
    }
    HSetState(handle, state);
    if (invoked && !status)
        invoked = region_parse_message(arguments[2], arguments[3], 3, 0, NULL, &status);
    call->words[0] = status;
    return invoked;
}

/* System 9 NQD:RSect takes a destination followed by variadic Rect pointers. */
int ROMlib_carbon_intersect_rects(ROMlib_carbon_ppc_frame* call)
{
    Rect intersection = {0};
    Boolean nonempty = FALSE;
    Rect* output = ROMlib_carbon_guest_memory(call->words[0], sizeof *output, 1);
    if (!output)
        return 0;
    ROMlib_carbon_set_qd_error(noErr);

    const unsigned count = MAX((int32)call->words[1], 0);
    for (unsigned i = 0; i < count; ++i) {
        uint32 address;
        if (!ROMlib_carbon_frame_word(call, i + 2, &address))
            return 0;
        const Rect* input = ROMlib_carbon_guest_memory(address, sizeof *input, 0);
        if (!input)
            return 0;
        if (!i)
            intersection = *input;
        else {
            intersection.top = CW(MAX(CW(intersection.top), CW(input->top)));
            intersection.left = CW(MAX(CW(intersection.left), CW(input->left)));
            intersection.bottom = CW(MIN(CW(intersection.bottom), CW(input->bottom)));
            intersection.right = CW(MIN(CW(intersection.right), CW(input->right)));
        }
        nonempty = CW(intersection.top) < CW(intersection.bottom) && CW(intersection.left) < CW(intersection.right);
        if (!nonempty) {
            intersection = (Rect){0};
            break;
        }
    }
    *output = intersection;
    call->words[0] = nonempty;
    return 1;
}

static GrafPtr current_port(GrafPtr port)
{
    return port ? port : thePort;
}

static GrafPtr checked_port(GrafPtr port, BOOLEAN color)
{
    port = current_port(port);
    if (!port || (color && !CGrafPort_p(port))) {
        ROMlib_carbon_set_qd_error(paramErr);
        return NULL;
    }
    return port;
}

Rect* ROMlib_carbon_port_bounds(GrafPtr port, Rect* bounds)
{
    port = current_port(port);
    if (!port || !bounds)
        ROMlib_carbon_set_qd_error(paramErr);
    else
        *bounds = PORT_RECT(port);
    return bounds;
}

PixMapHandle ROMlib_carbon_port_pixmap(GrafPtr port)
{
    port = current_port(port);
    if (!port || !CGrafPort_p(port)) {
        ROMlib_carbon_set_qd_error(paramErr);
        return NULL;
    }
    return CPORT_PIXMAP(port);
}

static RgnHandle port_region(GrafPtr port, RgnHandle output, BOOLEAN clip)
{
    port = current_port(port);
    RgnHandle source = port ? (clip ? PORT_CLIP_REGION(port) : PORT_VIS_REGION(port)) : NULL;
    if (!source || !output)
        ROMlib_carbon_set_qd_error(paramErr);
    else
        C_CopyRgn(source, output);
    return output;
}

RgnHandle ROMlib_carbon_port_visible_region(GrafPtr port, RgnHandle output)
{
    return port_region(port, output, FALSE);
}

RgnHandle ROMlib_carbon_port_clip_region(GrafPtr port, RgnHandle output)
{
    return port_region(port, output, TRUE);
}

INTEGER ROMlib_carbon_port_text_font(GrafPtr port)
{
    port = current_port(port);
    if (!port) {
        ROMlib_carbon_set_qd_error(paramErr);
        return 0;
    }
    return PORT_TX_FONT(port);
}

INTEGER ROMlib_carbon_port_text_size(GrafPtr port)
{
    port = current_port(port);
    if (!port) {
        ROMlib_carbon_set_qd_error(paramErr);
        return 0;
    }
    return PORT_TX_SIZE(port);
}

LONGINT ROMlib_carbon_pix_rowbytes(PixMapHandle pixmap)
{
    if (!pixmap)
        return 0;
    if (!STARH(pixmap)) {
        ROMlib_carbon_set_qd_error(paramErr);
        return 0;
    }
    /* CarbonLib's classic fallback masks only bit 15, rather than using the
       runtime's narrower packed-pixel rowBytes helper. */
    return (uint16)CW(HxX(pixmap, rowBytes)) & 0x7fff;
}

GrafPtr ROMlib_carbon_qd_globals_port(void)
{
    return thePort;
}

static Pattern* copy_pattern(Pattern* output, const Byte* source)
{
    if (!output)
        ROMlib_carbon_set_qd_error(paramErr);
    else
        memmove(output, source, sizeof(Pattern));
    return output;
}

Pattern* ROMlib_carbon_qd_black(Pattern* output)
{
    return copy_pattern(output, black);
}

Pattern* ROMlib_carbon_qd_white(Pattern* output)
{
    return copy_pattern(output, white);
}

Pattern* ROMlib_carbon_qd_gray(Pattern* output)
{
    return copy_pattern(output, gray);
}

Pattern* ROMlib_carbon_qd_light_gray(Pattern* output)
{
    return copy_pattern(output, ltGray);
}

Pattern* ROMlib_carbon_qd_dark_gray(Pattern* output)
{
    return copy_pattern(output, dkGray);
}

LONGINT ROMlib_carbon_port_text_face(GrafPtr port)
{
    port = checked_port(port, FALSE);
    return port ? (Byte)PORT_TX_FACE(port) : 0;
}

INTEGER ROMlib_carbon_port_text_mode(GrafPtr port)
{
    port = checked_port(port, FALSE);
    return port ? PORT_TX_MODE(port) : 1;
}

INTEGER ROMlib_carbon_port_pen_mode(GrafPtr port)
{
    port = checked_port(port, FALSE);
    return port ? PORT_PEN_MODE(port) : 0;
}

INTEGER ROMlib_carbon_port_pen_visibility(GrafPtr port)
{
    port = checked_port(port, FALSE);
    return port ? PORT_PEN_VIS(port) : 0;
}

Fixed ROMlib_carbon_port_space_extra(GrafPtr port)
{
    port = checked_port(port, FALSE);
    return port ? PORT_SP_EXTRA(port) : 0;
}

INTEGER ROMlib_carbon_port_char_extra(GrafPtr port)
{
    port = checked_port(port, TRUE);
    return port ? CPORT_CH_EXTRA(port) : 0;
}

INTEGER ROMlib_carbon_port_fractional_pen(GrafPtr port)
{
    port = checked_port(port, TRUE);
    return port ? CPORT_PENLOC_HFRAC(port) : INT16_MIN;
}

Point* ROMlib_carbon_port_pen_size(GrafPtr port, Point* output)
{
    port = checked_port(port, FALSE);
    if (!output)
        ROMlib_carbon_set_qd_error(paramErr);
    else if (port)
        *output = PORT_PEN_SIZE(port);
    return output;
}

Point* ROMlib_carbon_port_pen_location(GrafPtr port, Point* output)
{
    port = checked_port(port, FALSE);
    if (!output)
        ROMlib_carbon_set_qd_error(paramErr);
    else if (port)
        *output = PORT_PEN_LOC(port);
    return output;
}

void* ROMlib_carbon_port_graf_procs(GrafPtr port)
{
    port = checked_port(port, FALSE);
    return port ? PORT_GRAF_PROCS(port) : NULL;
}

const BitMap* ROMlib_carbon_port_bitmap(GrafPtr port)
{
    port = checked_port(port, FALSE);
    /* A color port exposes its classic flagged BitMap overlay here, not the
       separately allocated PixMap record. CopyBits recognizes that overlay. */
    return port ? (const BitMap*)((const Byte*)port + 2) : NULL;
}

Rect* ROMlib_carbon_pix_bounds(PixMapHandle pixels, Rect* output)
{
    if (!pixels || !STARH(pixels) || !output)
        ROMlib_carbon_set_qd_error(paramErr);
    else
        *output = PIXMAP_BOUNDS(pixels);
    return output;
}

INTEGER ROMlib_carbon_pix_depth(PixMapHandle pixels)
{
    if (!pixels || !STARH(pixels)) {
        ROMlib_carbon_set_qd_error(paramErr);
        return 0;
    }
    return PIXMAP_PIXEL_SIZE(pixels);
}

Rect* ROMlib_carbon_region_bounds(RgnHandle region, Rect* output)
{
    if (!region || !STARH(region) || !output)
        ROMlib_carbon_set_qd_error(paramErr);
    else
        *output = STARH(region)->rgnBBox;
    return output;
}

RGBColor* ROMlib_carbon_port_fore_color(GrafPtr port, RGBColor* output)
{
    port = checked_port(port, TRUE);
    if (!output)
        ROMlib_carbon_set_qd_error(paramErr);
    else if (port)
        *output = CPORT_RGB_FG_COLOR(port);
    return output;
}

RGBColor* ROMlib_carbon_port_back_color(GrafPtr port, RGBColor* output)
{
    port = checked_port(port, TRUE);
    if (!output)
        ROMlib_carbon_set_qd_error(paramErr);
    else if (port)
        *output = CPORT_RGB_BK_COLOR(port);
    return output;
}

static RGBColor* port_graf_color(GrafPtr port, RGBColor* output, BOOLEAN use_hilite)
{
    port = checked_port(port, TRUE);
    GrafVarsHandle variables = port ? CPORT_GRAFVARS(port) : NULL;
    if (!output || !variables || !STARH(variables))
        ROMlib_carbon_set_qd_error(paramErr);
    else
        *output = use_hilite ? HxX(variables, rgbHiliteColor) : HxX(variables, rgbOpColor);
    return output;
}

RGBColor* ROMlib_carbon_port_op_color(GrafPtr port, RGBColor* output)
{
    return port_graf_color(port, output, FALSE);
}

RGBColor* ROMlib_carbon_port_hilite_color(GrafPtr port, RGBColor* output)
{
    return port_graf_color(port, output, TRUE);
}

LONGINT ROMlib_carbon_qd_random_seed(void)
{
    uint32 encoded;
    const Byte* slot = STARH((HIDDEN_BytePtr*)SYN68K_TO_US(a5));
    memcpy(&encoded, slot - 126, sizeof encoded);
    return (LONGINT)CL(encoded);
}

void ROMlib_carbon_set_qd_random_seed(LONGINT seed)
{
    uint32 encoded = CL((uint32)seed);
    Byte* slot = STARH((HIDDEN_BytePtr*)SYN68K_TO_US(a5));
    memcpy(slot - 126, &encoded, sizeof encoded);
}

BitMap* ROMlib_carbon_qd_screen_bits(BitMap* output)
{
    if (!output)
        ROMlib_carbon_set_qd_error(paramErr);
    else
        memmove(output, &screenBitsX, sizeof *output);
    return output;
}

Cursor* ROMlib_carbon_qd_arrow(Cursor* output)
{
    if (!output)
        ROMlib_carbon_set_qd_error(paramErr);
    else
        memmove(output, &arrowX, sizeof *output);
    return output;
}

OSErr ROMlib_carbon_lock_port_bits(GrafPtr port)
{
    port = checked_port(port, FALSE);
    if (!port)
        return paramErr;
    if (!GWorld_p(port))
        return noErr;
    PixMapHandle pixels = CPORT_PIXMAP(port);
    if (!pixels || !STARH(pixels))
        return paramErr;
    return C_LockPixels(pixels) ? noErr : memFullErr;
}

OSErr ROMlib_carbon_unlock_port_bits(GrafPtr port)
{
    port = checked_port(port, FALSE);
    if (!port)
        return paramErr;
    if (GWorld_p(port)) {
        PixMapHandle pixels = CPORT_PIXMAP(port);
        if (!pixels || !STARH(pixels))
            return paramErr;
        if (ROMlib_carbon_pixels_state(pixels) & pixelsLocked)
            C_UnlockPixels(pixels);
    }
    return noErr;
}

BOOLEAN ROMlib_carbon_port_is_color(GrafPtr port)
{
    port = checked_port(port, FALSE);
    return port && (((const Byte*)port)[6] & 0x80);
}

BOOLEAN ROMlib_carbon_port_is_offscreen(GrafPtr port)
{
    port = checked_port(port, FALSE);
    return port && GWorld_p(port);
}

BOOLEAN ROMlib_carbon_port_defining_region(GrafPtr port)
{
    port = checked_port(port, FALSE);
    return port && PORT_REGION_SAVE(port);
}

BOOLEAN ROMlib_carbon_port_defining_picture(GrafPtr port)
{
    port = checked_port(port, FALSE);
    return port && PORT_PIC_SAVE(port);
}

BOOLEAN ROMlib_carbon_port_defining_polygon(GrafPtr port)
{
    port = checked_port(port, FALSE);
    return port && PORT_POLY_SAVE(port);
}

void ROMlib_carbon_set_port_bounds(GrafPtr port, const Rect* bounds)
{
    port = checked_port(port, FALSE);
    if (port && bounds)
        PORT_RECT(port) = *bounds;
    else
        ROMlib_carbon_set_qd_error(paramErr);
}

static void copy_port_region(GrafPtr port, RgnHandle source, BOOLEAN clip)
{
    port = checked_port(port, FALSE);
    RgnHandle target = port ? (clip ? PORT_CLIP_REGION(port) : PORT_VIS_REGION(port)) : NULL;
    if (!source || !target)
        ROMlib_carbon_set_qd_error(paramErr);
    else if (source != target)
        C_CopyRgn(source, target);
}

void ROMlib_carbon_set_port_clip(GrafPtr port, RgnHandle region)
{
    copy_port_region(port, region, TRUE);
}

void ROMlib_carbon_set_port_visible(GrafPtr port, RgnHandle region)
{
    copy_port_region(port, region, FALSE);
}

void ROMlib_carbon_set_port_fractional_pen(GrafPtr port, INTEGER fraction)
{
    port = checked_port(port, TRUE);
    if (port)
        CPORT_PENLOC_HFRAC_X(port) = CW(fraction);
}

void ROMlib_carbon_set_port_graf_procs(GrafPtr port, QDProcsPtr procs)
{
    port = checked_port(port, FALSE);
    if (port)
        PP_RAW(PORT_GRAF_PROCS_X(port)) = RM(procs);
}

void ROMlib_carbon_set_port_op_color(GrafPtr port, const RGBColor* color)
{
    port = checked_port(port, TRUE);
    GrafVarsHandle variables = port ? CPORT_GRAFVARS(port) : NULL;
    if (!variables || !STARH(variables) || !color)
        ROMlib_carbon_set_qd_error(paramErr);
    else
        HxX(variables, rgbOpColor) = *color;
}

void ROMlib_carbon_set_port_pen_mode(GrafPtr port, INTEGER mode)
{
    port = checked_port(port, FALSE);
    if (port)
        PORT_PEN_MODE_X(port) = CW(mode);
}

void ROMlib_carbon_set_port_pen_size(GrafPtr port, uint32 size)
{
    port = checked_port(port, FALSE);
    if (port) {
        const Point dimensions = {CW((INTEGER)(size >> 16)), CW((INTEGER)size)};
        PORT_PEN_SIZE(port) = dimensions;
    }
}

static void copy_port_pixpat(PixPatHandle source, PixPatHandle target)
{
    if (!source || !STARH(source) || !target || !STARH(target))
        ROMlib_carbon_set_qd_error(paramErr);
    else if (source != target)
        C_CopyPixPat(source, target);
}

PixPatHandle ROMlib_carbon_get_port_back_pixpat_direct(GrafPtr port)
{
    port = checked_port(port, TRUE);
    return port ? CPORT_BK_PIXPAT(port) : NULL;
}

PixPatHandle ROMlib_carbon_get_port_pen_pixpat_direct(GrafPtr port)
{
    port = checked_port(port, TRUE);
    return port ? CPORT_PEN_PIXPAT(port) : NULL;
}

static PixPatHandle get_port_fill_pixpat(GrafPtr port)
{
    port = checked_port(port, TRUE);
    return port ? CPORT_FILL_PIXPAT(port) : NULL;
}

PixPatHandle ROMlib_carbon_get_port_back_pixpat(GrafPtr port, PixPatHandle output)
{
    copy_port_pixpat(ROMlib_carbon_get_port_back_pixpat_direct(port), output);
    return output;
}

PixPatHandle ROMlib_carbon_get_port_pen_pixpat(GrafPtr port, PixPatHandle output)
{
    copy_port_pixpat(ROMlib_carbon_get_port_pen_pixpat_direct(port), output);
    return output;
}

PixPatHandle ROMlib_carbon_get_port_fill_pixpat(GrafPtr port, PixPatHandle output)
{
    copy_port_pixpat(get_port_fill_pixpat(port), output);
    return output;
}

void ROMlib_carbon_set_port_back_pixpat(GrafPtr port, PixPatHandle input)
{
    copy_port_pixpat(input, ROMlib_carbon_get_port_back_pixpat_direct(port));
}

void ROMlib_carbon_set_port_pen_pixpat(GrafPtr port, PixPatHandle input)
{
    copy_port_pixpat(input, ROMlib_carbon_get_port_pen_pixpat_direct(port));
}

void ROMlib_carbon_set_port_fill_pixpat(GrafPtr port, PixPatHandle input)
{
    copy_port_pixpat(input, get_port_fill_pixpat(port));
}

void ROMlib_carbon_set_port_back_pixpat_direct(GrafPtr port, PixPatHandle input)
{
    if (!port || !CGrafPort_p(port))
        ROMlib_carbon_set_qd_error(paramErr);
    else
        PP_RAW(CPORT_BK_PIXPAT_X(port)) = RM(input);
}

void ROMlib_carbon_set_port_pen_pixpat_direct(GrafPtr port, PixPatHandle input)
{
    if (!port || !CGrafPort_p(port))
        ROMlib_carbon_set_qd_error(paramErr);
    else
        PP_RAW(CPORT_PEN_PIXPAT_X(port)) = RM(input);
}

void ROMlib_carbon_set_port_fill_pixpat_direct(GrafPtr port, PixPatHandle input)
{
    if (!port || !CGrafPort_p(port))
        ROMlib_carbon_set_qd_error(paramErr);
    else
        PP_RAW(CPORT_FILL_PIXPAT_X(port)) = RM(input);
}

void ROMlib_carbon_set_qd_arrow(const Cursor* cursor)
{
    if (cursor)
        memmove(&arrowX, cursor, sizeof *cursor);
}

static void nqd_put_pic_number(const void* bytes, uint16_t length)
{
    if (CW(QDErr_H) || !thePort)
        return;
    if (PORT_GRAF_PROCS(thePort)) {
        // Custom picture procedures must receive an address in guest memory.
        Ptr buffer = NewPtr(length);
        if (!buffer) {
            ROMlib_carbon_set_qd_error(MemError());
            return;
        }
        memcpy(buffer, bytes, length);
        PICWRITE(buffer, length);
        DisposPtr(buffer);
    }
    else
        PICWRITE(bytes, length);
}

int ROMlib_carbon_nqd_put_pic_byte(ROMlib_carbon_ppc_frame* call)
{
    const uint8_t value = call->words[0];
    nqd_put_pic_number(&value, sizeof value);
    return 1;
}

int ROMlib_carbon_nqd_put_pic_word(ROMlib_carbon_ppc_frame* call)
{
    const uint16_t value = CW(call->words[0]);
    nqd_put_pic_number(&value, sizeof value);
    return 1;
}

int ROMlib_carbon_nqd_put_pic_long(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t value = CL(call->words[0]);
    nqd_put_pic_number(&value, sizeof value);
    return 1;
}

int ROMlib_carbon_nqd_put_pic_data(ROMlib_carbon_ppc_frame* call)
{
    if (CW(QDErr_H) || !thePort)
        return 1;
    const int16_t length = call->words[1];
    if (!length)
        return 1;
    const void* bytes = length > 0 && call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], length, 0) : NULL;
    if (bytes)
        PICWRITE(bytes, length);
    else
        ROMlib_carbon_set_qd_error(paramErr);
    return 1;
}

int ROMlib_carbon_nqd_put_pic_op(ROMlib_carbon_ppc_frame* call)
{
    const piccachehand cache = thePort ? (piccachehand)PORT_PIC_SAVE(thePort) : NULL;
    if (!cache || !STARH(cache))
        return 1;
    // OpenPicture and OpenCPicture in ROMAW create version 2 recordings.
    if (Hx(cache, pichowfar) & 1) {
        const uint8_t padding = 0;
        nqd_put_pic_number(&padding, sizeof padding);
    }
    return ROMlib_carbon_nqd_put_pic_word(call);
}

int ROMlib_carbon_nqd_check_picture(ROMlib_carbon_ppc_frame* call)
{
    const Boolean recording = thePort && PORT_PIC_SAVE(thePort) && PORT_PEN_VIS(thePort) > -2;
    if (recording)
        ROMlib_drawingpicupdate();
    call->words[0] = recording;
    return 1;
}

static int nqd_hilite(ROMlib_carbon_ppc_frame* call, Boolean region)
{
    const uint32_t address = call->words[0];
    void* shape =
        address ? ROMlib_carbon_try_guest_memory(address, region ? sizeof(HIDDEN_RgnPtr) : sizeof(Rect), 0) : NULL;
    ROMlib_carbon_set_qd_error(shape ? noErr : paramErr);
    if (!shape || !thePort || !CGrafPort_p(thePort))
        return 1;
    const INTEGER mode = PORT_PEN_MODE(thePort);
    RGBColor foreground = CPORT_RGB_FG_COLOR(theCPort), hilite_color = HiliteRGB;
    PenMode(39);
    RGBForeColor(&hilite_color);
    if (region)
        PaintRgn(shape);
    else
        PaintRect(shape);
    PenMode(mode);
    RGBForeColor(&foreground);
    return 1;
}

int ROMlib_carbon_nqd_hilite_rect(ROMlib_carbon_ppc_frame* call)
{
    return nqd_hilite(call, FALSE);
}

int ROMlib_carbon_nqd_hilite_region(ROMlib_carbon_ppc_frame* call)
{
    return nqd_hilite(call, TRUE);
}

/* Decode the 28-byte PixPat prefix and its resource-relative offsets.
   NQD 9.2.2 entry 0x18a48 and unflatten worker 0x16fd0. */
int ROMlib_carbon_nqd_unflatten_pixpat(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t source = call->words[0], destination = call->words[2];
    const int32_t size = (int32_t)call->words[1];
    call->words[0] = paramErr;
    if (!source || !destination || size < (int32_t)(sizeof(PixPat) + sizeof(PixMap)))
        return 1;

    const Byte* bytes = ROMlib_carbon_try_guest_memory(source, (uint32_t)size, 0);
    void* output = ROMlib_carbon_try_guest_memory(destination, sizeof(uint32_t), 1);
    if (!bytes || !output)
        return 1;

    PixPat pattern;
    memcpy(&pattern, bytes, sizeof pattern);
    const uint32_t map_offset = CL((uint32_t)PP_RAW(pattern.patMap));
    const uint32_t data_offset = CL((uint32_t)PP_RAW(pattern.patData));
    if ((uint16_t)CW(pattern.patType) > 3 || map_offset < sizeof pattern ||
        map_offset > (uint32_t)size - sizeof(PixMap))
        return 1;

    PixMap map;
    memcpy(&map, bytes + map_offset, sizeof map);
    const uint32_t table_offset = CL((uint32_t)PP_RAW(map.pmTable));
    if (data_offset < map_offset + sizeof map || table_offset < data_offset || table_offset > (uint32_t)size)
        return 1;

    enum { direct_pixel_type = 16 };

    const int table_required = CW(map.pixelType) != direct_pixel_type;
    const uint32_t pixel_bytes = table_offset - data_offset;
    uint32_t table_bytes = 0;
    if (table_required) {
        enum { table_header = offsetof(ColorTable, ctTable) };

        uint16_t last;
        if ((uint32_t)size - table_offset < table_header)
            return 1;
        memcpy(&last, bytes + table_offset + offsetof(ColorTable, ctSize), sizeof last);
        const int count = (int16)CW(last) + 1;
        if (count < 0 || (uint32_t)count > ((uint32_t)size - table_offset - table_header) / sizeof(ColorSpec))
            return 1;
        table_bytes = table_header + count * sizeof(ColorSpec);
    }

    /* Guest heap allocations can move an unlocked source handle. */
    Byte* payload = malloc(pixel_bytes + table_bytes + 1);
    if (!payload) {
        call->words[0] = memFullErr;
        return 1;
    }
    memcpy(payload, bytes + data_offset, pixel_bytes + table_bytes);

    PixPatHandle result = C_NewPixPat();
    OSErr status = result ? noErr : MemError();
    if (result) {
        PixMapHandle target_map = PIXPAT_MAP(result);
        Handle target_data = PIXPAT_DATA(result);
        CTabHandle target_table = PIXMAP_TABLE(target_map);
        SetHandleSize(target_data, pixel_bytes);
        status = MemError();
        if (!status && table_required) {
            SetHandleSize((Handle)target_table, table_bytes);
            status = MemError();
        }
        if (!status) {
            if (pixel_bytes)
                memcpy(STARH(target_data), payload, pixel_bytes);
            if (table_required) {
                memcpy(STARH(target_table), payload + pixel_bytes, table_bytes);
                CTAB_SEED_X(target_table) = CL(C_GetCTSeed());
            }
            else {
                DisposHandle((Handle)target_table);
                target_table = NULL;
            }
            FIELD_ASSIGN_RAW(map.pmTable, RM(target_table));
            map.pmReserved = 0;
            *STARH(target_map) = map;
            PIXPAT_TYPE_X(result) = pattern.patType;
            memcpy(PIXPAT_1DATA(result), pattern.pat1Data, sizeof(Pattern));
            const uint32_t encoded = CL(US_TO_SYN68K(result));
            memcpy(output, &encoded, sizeof encoded);
        }
        else
            C_DisposPixPat(result);
    }
    free(payload);
    call->words[0] = status;
    return 1;
}
