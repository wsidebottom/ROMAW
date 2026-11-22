/*+===================================================================
  File:      atsu.c

  Summary:   Implements ATSUI style, layout and font catalogue operations.

  Classes:   None.

  Functions: ROMlib_carbon_atsu.

  Origin:    CarbonLib 1.4 ATSUnicodeLib.CL and ATSGlue.CL records and
             Universal Interfaces 3.4.2 ATSUnicode.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "internal.h"
#include "atsu.h"
#include "cfx_internal.h"

#include <stdlib.h>

enum {
    invalid_offset = -8792,
    invalid_style = -8791,
    invalid_layout = -8790,
    missing_item = -8801,
    ats_record_signature = 0x4e95,
    ats_style_size = 0xcc,
    ats_layout_header_size = 0x9c,
    ats_layout_size = 0xb8
};

/* These records are packed for classic PowerPC and include unaligned words. */
static uint32_t atsu_read(const void* bytes, size_t width)
{
    const unsigned char* data = bytes;
    uint32_t result = 0;
    for (size_t i = 0; i < width; ++i)
        result = (result << 8) | data[i];
    return result;
}

static void atsu_write(void* bytes, uint32_t value, size_t width)
{
    unsigned char* data = bytes;
    while (width) {
        data[--width] = (unsigned char)value;
        value >>= 8;
    }
}

static int atsu_result(ROMlib_carbon_ppc_frame* call, int32_t value)
{
    call->words[0] = (uint32_t)value;
    return 1;
}

static int atsu_fallback(const char* name, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference original;
    return ROMlib_carbon_reference_function("CarbonLib", name, &original) &&
           ROMlib_carbon_invoke_frame(original.function, call);
}

static int atsu_profile(const char* name, uint32_t offset, ROMlib_carbon_reference* ref)
{
    return ROMlib_carbon_probe_reference("ATSUnicodeLib.CL", name, ref) && ref->code_offset == offset &&
           ref->code_size == 0x3151c && ref->toc_offset == 0x508 && ref->toc_size == 0xc044 && ref->toc;
}

static uint32_t atsu_toc_address(const ROMlib_carbon_reference* ref, int32_t offset)
{
    return atsu_read((const unsigned char*)ref->function + 4, 4) + offset;
}

static int atsu_worker(const ROMlib_carbon_reference* ref, uint32_t offset, const uint32_t* arguments, unsigned count,
                       uint32_t* result)
{
    return cfp_private(ref, offset, arguments, count, result);
}

static unsigned char* atsu_style(uint32_t address, const ROMlib_carbon_reference* ref, int writing)
{
    unsigned char* style = ROMlib_carbon_guest_memory(address, ats_style_size, writing);
    if (!style || atsu_read(style + 0xb8, 2) != ats_record_signature ||
        atsu_read(style, 4) != atsu_toc_address(ref, 0x89c))
        return NULL;
    return style;
}

static unsigned char* atsu_layout(uint32_t address, int writing)
{
    unsigned char* layout = ROMlib_carbon_guest_memory(address, ats_layout_header_size, writing);
    return layout && atsu_read(layout + 8, 2) == ats_record_signature && !layout[0x8a] ? layout : NULL;
}

static int atsu_virtual(const unsigned char* style, uint32_t offset, uint32_t first, uint32_t second, uint32_t* result)
{
    const void* method = ROMlib_carbon_guest_memory(atsu_read(style, 4) + offset, 4, 0);
    if (!method)
        return 0;
    const uint32_t arguments[] = {first, second};
    return cfp_callback(atsu_read(method, 4), arguments, 2, result);
}

static int create_style(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    if (!call->words[0])
        return atsu_result(call, paramErr);
    const uint32_t output_address = call->words[0];
    void* output = ROMlib_carbon_guest_memory(output_address, 4, 1);
    if (!output)
        return 0;
    ROMlib_carbon_reference base;
    if (!ROMlib_carbon_probe_reference("ATSGlue.CL", "__ct__8ATSStyleFv", &base) || !base.toc ||
        base.code_size != 0x3424 || base.code_offset != 0xb24)
        return atsu_fallback("ATSUCreateStyle", call);
    void* serial = ROMlib_carbon_guest_memory(atsu_toc_address(ref, 0x7a0), 4, 1);
    if (!serial)
        return 0;
    atsu_write(output, 0, 4);
    const uint32_t size = ats_style_size;
    uint32_t address;
    if (!atsu_worker(ref, 0x667c, &size, 1, &address))
        return 0;
    if (!address)
        return atsu_result(call, memFullErr);

    /* Build the subclass here. The underlying ATSStyle constructor owns the
       glyph engine's base fields and remains in ATSGlue while it is shared. */
    uint32_t ignored;
    if (!ROMlib_carbon_invoke_callback(base.function, &address, 1, &ignored))
        return 0;
    unsigned char* style = ROMlib_carbon_guest_memory(address, ats_style_size, 1);
    if (!style)
        return 0;
    atsu_write(style, atsu_toc_address(ref, 0x89c), 4);
    memset(style + 0xb4, 0, ats_style_size - 0xb4);
    const uint32_t next_serial = atsu_read(serial, 4) + 1;
    atsu_write(serial, next_serial, 4);
    atsu_write(style + 0xb4, next_serial, 4);
    atsu_write(style + 0xb8, ats_record_signature, 2);
    atsu_write(style + 0xca, UINT32_C(0xff80), 2);
    uint32_t error;
    if (!atsu_virtual(style, 0x10c, address, 0, &error))
        return 0;
    if (error) {
        if (!atsu_virtual(style, 0x108, address, 1, &ignored))
            return 0;
    }
    else
        atsu_write(output, address, 4);
    return atsu_result(call, error);
}

static int set_attributes(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    if (!args[2] || !args[3] || !args[4])
        return atsu_result(call, paramErr);
    const uint32_t count = args[1];
    if (!count)
        return atsu_result(call, noErr);
    if (count > INT32_MAX / 4)
        return atsu_result(call, paramErr);
    const unsigned char* tags = ROMlib_carbon_guest_memory(args[2], count * 4, 0);
    const unsigned char* sizes = ROMlib_carbon_guest_memory(args[3], count * 4, 0);
    const unsigned char* values = ROMlib_carbon_guest_memory(args[4], count * 4, 0);
    if (!tags || !sizes || !values)
        return 0;

    /* Validate the entire set first, then call the tag-specific setters in
       input order. Snapshot descriptors in case a setter moves guest data. */
    uint32_t(*entries)[4] = calloc(count, sizeof *entries);
    if (!entries)
        return atsu_result(call, memFullErr);
    int success = 1;
    uint32_t error = 0;
    for (uint32_t i = 0; success && !error && i < count; ++i) {
        entries[i][0] = args[0];
        entries[i][1] = atsu_read(tags + 4 * i, 4);
        entries[i][2] = atsu_read(values + 4 * i, 4);
        entries[i][3] = atsu_read(sizes + 4 * i, 4);
        if (entries[i][2] && entries[i][3] && !ROMlib_carbon_guest_memory(entries[i][2], entries[i][3], 0))
            success = 0;
        else
            success = atsu_worker(ref, 0xaef8, entries[i], 4, &error);
    }
    for (uint32_t i = 0; success && !error && i < count; ++i)
        success = atsu_worker(ref, 0xb10c, entries[i], 4, &error);
    free(entries);
    return success && atsu_result(call, error);
}

static int atsu_write_output(uint32_t address, uint32_t value, unsigned width)
{
    if (!address)
        return 1;
    void* output = ROMlib_carbon_guest_memory(address, width, 1);
    if (!output)
        return 0;
    atsu_write(output, value, width);
    return 1;
}

static int style_array(enum ats_operation op, const unsigned char* style, ROMlib_carbon_ppc_frame* call)
{
    const int variations = op == ats_get_variation || op == ats_get_all_variations;
    const unsigned offset = variations ? 0xac : 0xa4, width = variations ? 4 : 2;
    const uint32_t* args = call->words;
    const uint32_t count = atsu_read(style + offset, 4), address = atsu_read(style + offset + 4, 4);
    if (count > INT32_MAX / (2 * width) || (count && !address))
        return atsu_result(call, invalid_style);
    const unsigned char* array = count ? ROMlib_carbon_guest_memory(address, count * (2 * width), 0) : NULL;
    if (count && !array)
        return 0;
    if (op == ats_get_feature) {
        if (args[1] >= count)
            return atsu_result(call, paramErr);
        const unsigned char* pair = array + args[1] * 4;
        return atsu_write_output(args[2], atsu_read(pair, 2), 2) &&
               atsu_write_output(args[3], atsu_read(pair + 2, 2), 2) && atsu_result(call, noErr);
    }
    if (op == ats_get_variation) {
        if (!args[2])
            return atsu_result(call, noErr);
        for (uint32_t i = 0; i < count; ++i)
            if (atsu_read(array + i * 8, 4) == args[1])
                return atsu_write_output(args[2], atsu_read(array + i * 8 + 4, 4), 4) && atsu_result(call, noErr);
        return atsu_write_output(args[2], 0, 4) && atsu_result(call, missing_item);
    }

    const uint32_t copied = MIN(count, args[1]);
    unsigned char* keys = copied && args[2] ? ROMlib_carbon_guest_memory(args[2], copied * width, 1) : NULL;
    unsigned char* values = copied && args[3] ? ROMlib_carbon_guest_memory(args[3], copied * width, 1) : NULL;
    if (copied && ((args[2] && !keys) || (args[3] && !values)))
        return 0;
    if (!atsu_write_output(args[4], count, 4))
        return 0;
    for (uint32_t i = 0; i < copied; ++i) {
        const unsigned char* pair = array + i * (2 * width);
        if (keys)
            memmove(keys + i * width, pair, width);
        if (values)
            memmove(values + i * width, pair + width, width);
    }
    return atsu_result(call, noErr);
}

static int style_compare(const unsigned char* style, ROMlib_carbon_ppc_frame* call)
{
    static const struct {
        unsigned method;
        unsigned comparison;
    } comparisons[] = {{0x114, 2}, {0x11c, 3}, {0x120, 1}};

    void* output = ROMlib_carbon_guest_memory(call->words[2], 2, 1);
    if (!output)
        return 0;
    for (unsigned i = 0; i < NELEM(comparisons); ++i) {
        uint32_t matched;
        if (!atsu_virtual(style, comparisons[i].method, call->words[0], call->words[1], &matched))
            return 0;
        if (matched) {
            atsu_write(output, comparisons[i].comparison, 2);
            return atsu_result(call, noErr);
        }
    }
    atsu_write(output, 0, 2);
    return atsu_result(call, noErr);
}

static int style_copy(enum ats_operation op, const ROMlib_carbon_reference* ref, const unsigned char* style,
                      ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    if (op == ats_clone_style) {
        void* output = ROMlib_carbon_guest_memory(args[1], 4, 1);
        if (!output)
            return 0;
        const uint32_t size = ats_style_size;
        uint32_t address;
        if (!atsu_worker(ref, 0x667c, &size, 1, &address))
            return 0;
        const uint32_t copy[] = {address, args[0]};
        if (address && !atsu_worker(ref, 0x89f0, copy, 2, NULL))
            return 0;
        atsu_write(output, address, 4);
        return atsu_result(call, address ? noErr : memFullErr);
    }

    if (!args[1])
        return atsu_result(call, paramErr);
    if (!atsu_style(args[1], ref, op != ats_compare_styles))
        return atsu_result(call, invalid_style);
    if (op == ats_compare_styles)
        return style_compare(style, call);
    const uint32_t merge[] = {args[1], args[0]};
    static const uint32_t workers[] = {0x8cc0, 0xd130, 0xdae0};
    return atsu_worker(ref, workers[op - ats_copy_attributes], merge, 2, NULL) && atsu_result(call, noErr);
}

static int clear_style(const ROMlib_carbon_reference* ref, const unsigned char* style, ROMlib_carbon_ppc_frame* call)
{
    _Alignas(256) unsigned char defaults[ats_style_size];
    const uint32_t scratch = US_TO_SYN68K(defaults);
    const uint32_t assign[] = {call->words[0], scratch};
    uint32_t status;
    if (!atsu_worker(ref, 0x8970, &scratch, 1, NULL))
        return 0;
    const int updated =
        atsu_worker(ref, 0x8cc0, assign, 2, NULL) && atsu_virtual(style, 0x10c, call->words[0], 0, &status);
    const int disposed = atsu_worker(ref, 0x8abc, &scratch, 1, NULL);
    return updated && disposed && atsu_result(call, status);
}

static int style_get_attributes(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    _Alignas(16) uint32_t raw_count;
    const uint32_t query[] = {args[0], US_TO_SYN68K(&raw_count)};
    uint32_t status;
    if (!atsu_worker(ref, 0x971c, query, 2, &status))
        return 0;
    if (status)
        return atsu_result(call, status);
    const uint32_t count = atsu_read(&raw_count, 4);
    const uint32_t length = MIN(count, args[2]);
    if (length > UINT16_MAX || (length && !ROMlib_carbon_guest_memory(args[1], length * 8, 1)))
        return 0;
    if (!atsu_write_output(args[3], count, 4))
        return 0;
    for (uint32_t i = 0; !status && i < length; ++i) {
        const uint32_t lookup[] = {args[0], i, args[1] + i * 8};
        if (!atsu_worker(ref, 0x97d0, lookup, 3, &status))
            return 0;
    }
    return atsu_result(call, status);
}

static int publish_style_change(const ROMlib_carbon_reference* ref, unsigned char* style)
{
    void* serial = ROMlib_carbon_guest_memory(atsu_toc_address(ref, 0x7a0), 4, 1);
    if (!serial)
        return 0;
    const uint32_t next = atsu_read(serial, 4) + 1;
    atsu_write(serial, next, 4);
    atsu_write(style + 0xb4, next, 4);
    return 1;
}

static int style_change_array(enum ats_operation op, const ROMlib_carbon_reference* ref, unsigned char* style,
                              ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    const int features = op == ats_set_features || op == ats_clear_features;
    const int set = op == ats_set_features || op == ats_set_variations;
    const uint32_t count = args[1];
    uint32_t status = noErr;
    if (!set && count == UINT32_MAX) {
        if (op == ats_clear_attributes) {
            if (!atsu_worker(ref, 0xc670, args, 1, NULL))
                return 0;
        }
        else {
            const uint32_t empty[] = {args[0], 0, 0};
            const unsigned method = features ? 0x48 : 0x40;
            const uint32_t address = cfp_read(atsu_read(style, 4) + method);
            if (!address || !cfp_callback(address, empty, 3, NULL))
                return 0;
        }
    }
    else {
        const unsigned width = features ? 2 : 4;
        if (features && (!args[2] || !args[3] || count > UINT16_MAX))
            return (set || publish_style_change(ref, style)) && atsu_result(call, paramErr);
        if (set && (!args[2] || !args[3]))
            return atsu_result(call, paramErr);
        if (count > INT32_MAX / width)
            return atsu_result(call, paramErr);
        const unsigned char* keys = count && args[2] ? ROMlib_carbon_guest_memory(args[2], count * width, 0) : NULL;
        const unsigned char* values =
            count && (set || features) ? ROMlib_carbon_guest_memory(args[3], count * width, 0) : NULL;
        if ((count && args[2] && !keys) || (count && (set || features) && !values))
            return 0;
        for (uint32_t i = 0; keys && i < count && (!status || (set && features)); ++i) {
            const uint32_t key = atsu_read(keys + i * width, width);
            const uint32_t value = values ? atsu_read(values + i * width, width) : 0;
            if (op == ats_set_variations) {
                _Alignas(16) unsigned char pair[8];
                atsu_write(pair, key, 4);
                atsu_write(pair + 4, value, 4);
                const uint32_t edit[] = {args[0], US_TO_SYN68K(pair)};
                if (!atsu_worker(ref, 0xcb74, edit, 2, &status))
                    return 0;
            }
            else {
                const uint32_t edit[] = {args[0], op == ats_clear_variations ? args[2] + i * 4 : key, value};
                const uint32_t worker = features                     ? (set ? 0xc740 : 0xdeb8)
                                        : op == ats_clear_attributes ? 0xc120
                                                                     : 0xe204;
                if (!atsu_worker(ref, worker, edit, features ? 3 : 2, &status))
                    return 0;
            }
        }
        if (keys && op == ats_clear_attributes) {
            ROMlib_carbon_reference invalidate;
            if (!ROMlib_carbon_reference_function("ATSGlue.CL", "InvalidateAttributes__8ATSStyleFv", &invalidate) ||
                !ROMlib_carbon_invoke_callback(invalidate.function, args, 1, NULL))
                return 0;
        }
    }
    return (op == ats_set_features || (op == ats_set_variations && !count) || publish_style_change(ref, style)) &&
           atsu_result(call, status);
}

static int soft_breaks(const unsigned char* layout, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    const uint32_t begin = atsu_read(layout + 0x12, 4), length = atsu_read(layout + 0x16, 4);
    const uint32_t start = args[1] == UINT32_MAX ? begin : args[1];
    /* The 1.4 query subtracts the absolute start for its to-end sentinel. */
    const uint32_t span = args[2] == UINT32_MAX ? length - start : args[2];
    const uint64_t limit = (uint64_t)begin + length, finish = (uint64_t)start + span;
    if (start < begin || span > length || finish > limit || limit > UINT32_MAX)
        return atsu_result(call, invalid_offset);
    const uint64_t exclusive = finish + (finish == start || finish == limit);
    const uint32_t address = atsu_read(layout + 0x94, 4);
    const uint32_t count = address ? atsu_read(layout + 0x98, 4) : 0;
    if (count > INT32_MAX / 4)
        return atsu_result(call, invalid_layout);
    const unsigned char* breaks = count ? ROMlib_carbon_guest_memory(address, count * 4, 0) : NULL;
    if (count && !breaks)
        return 0;
    uint32_t found = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t offset = atsu_read(breaks + 4 * i, 4);
        found += offset > start && offset < exclusive;
    }
    uint32_t remaining = MIN(found, args[3]);
    unsigned char* output = remaining && args[4] ? ROMlib_carbon_guest_memory(args[4], remaining * 4, 1) : NULL;
    if ((remaining && args[4] && !output) || !atsu_write_output(args[5], found, 4))
        return 0;
    for (uint32_t i = 0; output && remaining && i < count; ++i) {
        const uint32_t offset = atsu_read(breaks + 4 * i, 4);
        if (offset > start && offset < exclusive) {
            atsu_write(output, offset, 4);
            output += 4;
            --remaining;
        }
    }
    return atsu_result(call, noErr);
}

static int font_fallbacks(enum ats_operation op, const char* name, const ROMlib_carbon_reference* ref,
                          ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    const void* properties = ROMlib_carbon_guest_memory(atsu_toc_address(ref, -0x108), 12, 0);
    if (!properties)
        return 0;
    const uint32_t method_address = atsu_read(properties, 4), list_address = atsu_read((const Byte*)properties + 4, 4),
                   count_address = atsu_read((const Byte*)properties + 8, 4);
    void* method = ROMlib_carbon_guest_memory(method_address, 2, op == ats_set_fallbacks);
    void* list = ROMlib_carbon_guest_memory(list_address, 4, op == ats_set_fallbacks);
    void* count_field = ROMlib_carbon_guest_memory(count_address, 4, op == ats_set_fallbacks);
    if (!method || !list || !count_field)
        return 0;
    const uint32_t count = atsu_read(count_field, 4), address = atsu_read(list, 4);
    if (op == ats_set_fallbacks) {
        const unsigned next = (uint16_t)args[2];
        if (next >= 4 || (args[0] && !args[1]))
            return atsu_result(call, paramErr);
        if (args[0] && next >= 2)
            return atsu_fallback(name, call); /* Scaler checks and font filtering. */
        if (address) {
            uint32_t ignored;
            if (!atsu_worker(ref, 0x6138, &address, 1, &ignored))
                return 0;
        }
        atsu_write(list, 0, 4);
        atsu_write(count_field, 0, 4);
        atsu_write(method, next & 1, 2);
        uint32_t ignored;
        return atsu_worker(ref, 0x1a6b0, NULL, 0, &ignored) && atsu_result(call, noErr);
    }

    if (args[0] && !args[1])
        return atsu_result(call, paramErr);
    const uint32_t copied = MIN(args[0], count);
    if (copied > INT32_MAX / 4 || (copied && !address))
        return atsu_result(call, paramErr);
    const void* input = copied ? ROMlib_carbon_guest_memory(address, copied * 4, 0) : NULL;
    void* output = copied ? ROMlib_carbon_guest_memory(args[1], copied * 4, 1) : NULL;
    if ((copied && (!input || !output)) || !atsu_write_output(args[2], atsu_read(method, 2), 2) ||
        !atsu_write_output(args[3], count, 4))
        return 0;
    if (copied)
        memmove(output, input, copied * 4);
    return atsu_result(call, noErr);
}

static int allocate_layout(const ROMlib_carbon_reference* ref, uint32_t source, uint32_t* address)
{
    const uint32_t size = ats_layout_size;
    if (!atsu_worker(ref, 0x667c, &size, 1, address))
        return 0;
    if (!*address)
        return 1;
    uint32_t direction;
    if (source) {
        const uint32_t copy[] = {*address, source};
        return atsu_worker(ref, 0x125ec, copy, 2, &direction);
    }
    unsigned char* layout = ROMlib_carbon_guest_memory(*address, size, 1);
    if (!layout || !atsu_worker(ref, 0x30a28, NULL, 0, &direction))
        return 0;
    memset(layout, 0, size);
    atsu_write(layout, atsu_toc_address(ref, 0xbe4), 4);
    atsu_write(layout + 8, ats_record_signature, 2);
    atsu_write(layout + 0x36, INT32_MAX, 4);
    atsu_write(layout + 0x3a, INT32_MAX, 4);
    layout[0x42] = direction != 0;
    atsu_write(layout + 0x44, 0xff80, 2);
    atsu_write(layout + 0xa0, atsu_toc_address(ref, 0x824), 4);
    return 1;
}

static int assign_layout_runs(const ROMlib_carbon_reference* ref, uint32_t address, const uint32_t* args,
                              uint32_t start, uint32_t length, uint32_t* status)
{
    const uint32_t count = args[4];
    if (count > INT32_MAX / 4) {
        *status = paramErr;
        return 1;
    }
    const unsigned char* lengths = ROMlib_carbon_guest_memory(args[5], count * 4, 0);
    const unsigned char* styles = ROMlib_carbon_guest_memory(args[6], count * 4, 0);
    if (!lengths || !styles)
        return 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t style = atsu_read(styles + i * 4, 4);
        if (!style || !atsu_style(style, ref, 0)) {
            *status = style ? invalid_style : paramErr;
            return 1;
        }
    }
    uint32_t run_start = start;
    for (uint32_t i = 0; i < count && !*status; ++i) {
        const uint32_t run_length = atsu_read(lengths + i * 4, 4);
        const uint32_t run[] = {address, atsu_read(styles + i * 4, 4), run_start, run_length};
        if (!atsu_worker(ref, 0x30260, run, 4, status))
            return 0;
        *status = (int16_t)*status;
        run_start += run_length; /* The saved run entry validates each extent. */
    }
    if (!*status && run_start < start + length) {
        const uint32_t tail[] = {address, atsu_read(styles + (count - 1) * 4, 4), run_start,
                                 start + length - run_start};
        if (!atsu_worker(ref, 0x30260, tail, 4, status))
            return 0;
        *status = (int16_t)*status;
    }
    return 1;
}

static int create_layout(enum ats_operation op, const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    const int with_text = op != ats_create_layout, handle = op == ats_create_layout_handle;
    const uint32_t output = args[with_text ? 7 : 0];
    if (!output)
        return atsu_result(call, paramErr);
    uint32_t text = with_text ? args[0] : 0;
    if (handle && text) {
        const void* master = ROMlib_carbon_guest_memory(text, 4, 0);
        if (!master)
            return 0;
        if (!atsu_read(master, 4))
            text = 0;
    }
    const uint32_t start = args[1] == UINT32_MAX ? 0 : args[1];
    const uint32_t length = args[2] == UINT32_MAX ? args[3] : args[2];
    if (with_text && ((text && (uint64_t)start + length > args[3]) || (args[4] && (!args[5] || !args[6]))))
        return atsu_result(call, paramErr);
    if (!ROMlib_carbon_guest_memory(output, 4, 1))
        return 0;

    uint32_t address;
    if (!allocate_layout(ref, 0, &address) || !atsu_write_output(output, address, 4))
        return 0;
    if (!address)
        return atsu_result(call, memFullErr);
    uint32_t status = noErr;
    if (text) {
        const uint32_t buffer[] = {address, text, start, length, args[3]};
        if (!atsu_worker(ref, handle ? 0x30278 : 0x302a8, buffer, 5, &status) ||
            (!status && args[4] && !assign_layout_runs(ref, address, args, start, length, &status)))
            return 0;
    }
    return atsu_result(call, status);
}

static int layout_range(const unsigned char* layout, uint32_t* start, uint32_t* length)
{
    const uint32_t begin = atsu_read(layout + 0x12, 4), count = atsu_read(layout + 0x16, 4);
    if (*start == UINT32_MAX)
        *start = begin;
    if (!length)
        return *start >= begin && *start <= (uint64_t)begin + count;
    if (*length == UINT32_MAX)
        *length = count - *start;
    return *start >= begin && *length <= count && (uint64_t)*start + *length <= (uint64_t)begin + count;
}

static int layout_worker(const ROMlib_carbon_reference* ref, uint32_t worker, const uint32_t* parameters,
                         unsigned count, ROMlib_carbon_ppc_frame* call)
{
    uint32_t status, ignored;
    const uint32_t address = call->words[0];
    if (!atsu_worker(ref, 0x1a4d0, &address, 1, &status))
        return 0;
    if (!status &&
        (!atsu_worker(ref, worker, parameters, count, &status) || !atsu_worker(ref, 0x1a598, &address, 1, &ignored)))
        return 0;
    return atsu_result(call, status);
}

static int set_text_location(enum ats_operation op, const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    if (!args[1] || (uint64_t)args[2] + args[3] > args[4])
        return atsu_result(call, paramErr);
    const int handle = op == ats_set_text_handle;
    if (handle) {
        const void* master = ROMlib_carbon_guest_memory(args[1], 4, 0);
        if (!master)
            return 0;
        if (!atsu_read(master, 4))
            return atsu_result(call, paramErr);
    }
    return layout_worker(ref, handle ? 0x12ca0 : 0x12c08, args, 5, call);
}

/* Public layout and line APIs share the saved control record implementation.
   A layout control uses the all-lines sentinel instead of a character offset. */
static int layout_controls(enum ats_operation op, const ROMlib_carbon_reference* ref, const unsigned char* layout,
                           ROMlib_carbon_ppc_frame* call)
{
    static const struct {
        uint32_t worker;
        unsigned count;
    } controls[] = {{0x19318, 6}, {0x1998c, 6}, {0x19cd4, 5}, {0x1a1d4, 4}};

    const int line = op >= ats_set_line_controls;
    const unsigned index = op - (line ? ats_set_line_controls : ats_set_layout_controls);
    const uint32_t* args = call->words;
    uint32_t parameters[6] = {args[0], line ? args[1] : UINT32_MAX};
    if (line && !layout_range(layout, &parameters[1], NULL))
        return atsu_result(call, invalid_offset);
    memcpy(parameters + 2, args + 1 + line, (controls[index].count - 2) * sizeof *args);
    return layout_worker(ref, controls[index].worker, parameters, controls[index].count, call);
}

static int fond_to_font(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    const uint32_t output = args[2];
    if (!output)
        return atsu_result(call, paramErr);
    const void* font_source = ROMlib_carbon_guest_memory(atsu_toc_address(ref, -0x2fc), 4, 0);
    if (!font_source || !ROMlib_carbon_guest_memory(output, 4, 1))
        return 0;
    _Alignas(16) uint32_t auxiliary = 0;
    const uint32_t family = (int16_t)args[0], style = (uint8_t)args[1], local = US_TO_SYN68K(&auxiliary);
    uint32_t status;
    if (atsu_read(font_source, 4)) {
        const uint32_t query[] = {family, style, output, local};
        if (!atsu_write_output(output, 0, 4) || !atsu_worker(ref, 0x30770, query, 4, &status))
            return 0;
        const void* id = ROMlib_carbon_guest_memory(output, 4, 0);
        if (!id)
            return 0;
        if (status || !atsu_read(id, 4))
            return atsu_result(call, -8796);
        return atsu_result(call, (int16_t)atsu_read(&auxiliary, 2) == (int)style ? noErr : -8795);
    }

    const uint32_t query[] = {family, style, local};
    uint32_t object;
    if (!atsu_worker(ref, 0x30788, query, 3, &object))
        return 0;
    const uint32_t flags = CL(auxiliary);
    status = flags & 1 || !object ? -8796 + (int)(!object && !(flags & 1) && (flags & 8)) : noErr;
    uint32_t ignored, id;
    if ((!status && (!atsu_worker(ref, 0x306c8, &object, 1, &id) || !atsu_write_output(output, id, 4))) ||
        (object && !atsu_worker(ref, 0x30608, &object, 1, &ignored)))
        return 0;
    return atsu_result(call, status);
}

/* The saved worker fills a classic region, including its bounding rectangle. */
static int highlight_region_valid(uint32_t handle)
{
    const void* master = handle ? ROMlib_carbon_guest_memory(handle, 4, 0) : NULL;
    const uint32_t address = master ? atsu_read(master, 4) : 0;
    const unsigned char* region = address ? ROMlib_carbon_guest_memory(address, 10, 0) : NULL;
    return region && (int16_t)atsu_read(region + 2, 2) <= (int16_t)atsu_read(region + 6, 2) &&
           (int16_t)atsu_read(region + 4, 2) <= (int16_t)atsu_read(region + 8, 2);
}

static int resolve_render_origin(const ROMlib_carbon_reference* ref, uint32_t* origin)
{
    if (origin[0] != UINT32_MAX && origin[1] != UINT32_MAX)
        return 1;

    _Alignas(16) uint32_t point = 0;
    const uint32_t output = US_TO_SYN68K(&point);
    uint32_t ignored;
    if (!atsu_worker(ref, 0x30a70, &output, 1, &ignored))
        return 0;
    const uint32_t coordinates = atsu_read(&point, 4);
    if (origin[0] == UINT32_MAX)
        origin[0] = coordinates << 16;
    if (origin[1] == UINT32_MAX)
        origin[1] = coordinates & 0xffff0000U;
    return 1;
}

static int render_layout(enum ats_operation op, const ROMlib_carbon_reference* ref, const unsigned char* layout,
                         ROMlib_carbon_ppc_frame* call)
{
    static const struct {
        uint32_t worker;
        unsigned words;
    } operations[] = {{0x153c0, 5}, {0x164e4, 6}, {0x15e04, 7}, {0x15598, 6}};

    const unsigned index = op - ats_draw_text;
    const unsigned count = operations[index].words;
    uint32_t arguments[7], status, ignored;
    memcpy(arguments, call->words, count * sizeof *arguments);
    if (!atsu_worker(ref, 0x1a4d0, arguments, 1, &status))
        return 0;
    if (status)
        return atsu_result(call, status);

    const int highlight = op == ats_get_text_highlight;
    uint32_t* range = arguments + (highlight ? 3 : 1);
    int invoked = 1;
    if (!layout_range(layout, range, range + 1))
        status = invalid_offset;
    else if (op == ats_measure_text || resolve_render_origin(ref, arguments + (highlight ? 1 : 3)))
        invoked = atsu_worker(ref, operations[index].worker, arguments, count, &status);
    else
        invoked = 0;
    const int unlocked = atsu_worker(ref, 0x1a598, arguments, 1, &ignored);
    return invoked && unlocked && atsu_result(call, status);
}

static int copy_layout_controls(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    uint32_t status;
    if (!atsu_worker(ref, 0x1a4d0, args, 1, &status))
        return 0;
    if (status)
        return atsu_result(call, status);

    int ok = 1;
    if (!args[1])
        status = paramErr;
    else if (!atsu_layout(args[1], 0))
        status = invalid_layout;
    else {
        ok = atsu_worker(ref, 0x1a4d0, args + 1, 1, &status);
        if (ok && !status) {
            const uint32_t assign[] = {args[1], args[0]};
            const int copied = atsu_worker(ref, 0x12a94, assign, 2, NULL);
            const int unlocked = atsu_worker(ref, 0x1a598, args + 1, 1, NULL);
            ok = copied && unlocked;
        }
    }
    const int unlocked = atsu_worker(ref, 0x1a598, args, 1, NULL);
    return ok && unlocked && atsu_result(call, status);
}

static int move_layout_text(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t address = call->words[0], text = call->words[1];
    uint32_t status;
    if (!atsu_worker(ref, 0x1a4d0, &address, 1, &status))
        return 0;
    if (status)
        return atsu_result(call, status);
    unsigned char* layout = ROMlib_carbon_guest_memory(address, ats_layout_header_size, 1);
    if (!layout || !layout[0xe])
        status = invalid_layout;
    else if (layout[0x10])
        status = paramErr;
    else
        atsu_write(layout + 4, text, 4);
    return atsu_worker(ref, 0x1a598, &address, 1, NULL) && atsu_result(call, status);
}

static int copy_line_controls(const ROMlib_carbon_reference* ref, const unsigned char* source,
                              ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    const uint32_t from = args[0], to = args[2];
    uint32_t status, from_offset = args[1], to_offset = args[3];
    if (!atsu_worker(ref, 0x1a4d0, &from, 1, &status))
        return 0;
    if (status)
        return atsu_result(call, status);

    int ok = 1, destination_locked = 0;
    if (!layout_range(source, &from_offset, NULL))
        status = invalid_offset;
    else if (!to)
        status = paramErr;
    else {
        const unsigned char* destination = to == from ? source : atsu_layout(to, 0);
        if (!destination || !destination[0xe])
            status = invalid_layout;
        else {
            if (to != from) {
                ok = atsu_worker(ref, 0x1a4d0, &to, 1, &status);
                destination_locked = ok && !status;
            }
            if (ok && !status) {
                if (!layout_range(destination, &to_offset, NULL))
                    status = invalid_offset;
                else {
                    const uint32_t copy[] = {to, from, from_offset, to_offset};
                    ok = atsu_worker(ref, 0x18fe0, copy, NELEM(copy), NULL);
                }
            }
        }
    }
    const int destination_unlocked = !destination_locked || atsu_worker(ref, 0x1a598, &to, 1, NULL);
    const int source_unlocked = atsu_worker(ref, 0x1a598, &from, 1, NULL);
    return ok && destination_unlocked && source_unlocked && atsu_result(call, status);
}

static int navigate_layout(enum ats_operation op, const ROMlib_carbon_reference* ref, uint32_t* args, uint32_t* status)
{
    const uint16_t movement = args[2];
    const uint32_t result = args[3];
    if (op == ats_next_cursor || op == ats_previous_cursor) {
        const uint32_t step[] = {args[0], op == ats_previous_cursor, args[1], movement, result};
        return atsu_worker(ref, 0x13c24, step, NELEM(step), status);
    }
    static const uint32_t left[] = {0x13cc0, 0x142b8, 0x14c60, 0x142b8};
    static const uint32_t right[] = {0x13fc0, 0x14794, 0x15018, 0x14794};
    const unsigned index = movement < NELEM(left) ? movement : 0;
    const uint32_t worker = op == ats_left_cursor ? left[index] : right[index];
    const uint32_t step[] = {args[0], args[1], index & 1 ? movement : result, result};
    return atsu_worker(ref, worker, step, index & 1 ? 4 : 3, status);
}

static int layout_edit_or_position(enum ats_operation op, const ROMlib_carbon_reference* ref,
                                   const unsigned char* layout, ROMlib_carbon_ppc_frame* call)
{
    if (op == ats_copy_line_controls)
        return copy_line_controls(ref, layout, call);

    uint32_t args[6], status;
    memcpy(args, call->words, sizeof args);
    if (!atsu_worker(ref, 0x1a4d0, args, 1, &status))
        return 0;
    if (status)
        return atsu_result(call, status);

    int ok = 1;
    const int highlight = op == ats_highlight || op == ats_unhighlight;
    const uint32_t cache_offset = args[1];
    if (op == ats_position_to_offset) {
        void* output = ROMlib_carbon_try_guest_memory(args[3], 4, 1);
        uint32_t offset = output ? atsu_read(output, 4) : 0, length = 0;
        if (!output)
            status = paramErr;
        else if (layout_range(layout, &offset, &length)) {
            atsu_write(output, offset, 4);
            ok = resolve_render_origin(ref, args + 1) && atsu_worker(ref, 0x168f8, args, 6, &status);
        }
        else
            status = invalid_offset;
    }
    else if (highlight) {
        if (layout_range(layout, args + 3, args + 4))
            ok = resolve_render_origin(ref, args + 1) &&
                 atsu_worker(ref, op == ats_highlight ? 0x167a4 : 0x168a4, args, 5, &status);
        else
            status = invalid_offset;
    }
    else if (op != ats_idle && !layout_range(layout, args + 1, op == ats_text_deleted ? args + 2 : NULL))
        status = invalid_offset;
    else {
        switch (op) {
        case ats_clear_layout_cache:
            args[1] = cache_offset;
            ok = atsu_worker(ref, 0x12b9c, args, 2, &status);
            break;
        case ats_idle:
            /* The 1.4 idle operation validates and locks the layout. */
            break;
        case ats_text_deleted:
        case ats_text_inserted:
            ok = atsu_worker(ref, op == ats_text_deleted ? 0x12d8c : 0x13030, args, 3, &status);
            break;
        case ats_offset_to_position:
            args[2] = (uint8_t)args[2];
            ok = atsu_worker(ref, 0x16ca4, args, 6, &status);
            break;
        case ats_break_line:
            args[3] = (uint8_t)args[3];
            ok = atsu_worker(ref, 0x17ee8, args, 5, &status);
            break;
        default:
            ok = navigate_layout(op, ref, args, &status);
            break;
        }
    }
    const int unlocked = atsu_worker(ref, 0x1a598, args, 1, NULL);
    return ok && unlocked && atsu_result(call, status);
}

typedef struct {
    const Byte* data;
    uint32_t size;
} ATSFontTable;

static const Byte* font_table_span(const ATSFontTable* table, uint64_t offset, uint32_t size)
{
    return table->data && offset + size <= table->size ? table->data + offset : NULL;
}

static int write_table_words(uint32_t address, const Byte* records, unsigned count, unsigned stride, unsigned width)
{
    if (!address || !count)
        return 1;
    Byte* output = ROMlib_carbon_guest_memory(address, count * width, 1);
    if (!output)
        return 0;
    for (unsigned i = 0; i < count; ++i)
        memcpy(output + i * width, records + i * stride, width);
    return 1;
}

static int mark_default_selectors(const ROMlib_carbon_reference* ref, const uint32_t* args, unsigned count)
{
    if (!args[4] || !count)
        return 1;
    Byte* defaults = ROMlib_carbon_guest_memory(args[4], count, 1);
    if (!defaults)
        return 0;
    memset(defaults, 0, count);
    const uint32_t query[] = {args[0], 0};
    uint32_t total;
    if (!atsu_worker(ref, 0x44a8, query, NELEM(query), &total))
        return 0;
    if ((int32_t)total <= 0 || !args[3])
        return 1;
    if (total > UINT32_MAX / 4)
        return 0;
    const uint32_t extent = total * 4;
    uint32_t address;
    if (!atsu_worker(ref, 0x667c, &extent, 1, &address))
        return 0;
    if (!address)
        return 1;
    const uint32_t read_defaults[] = {args[0], address};
    const int loaded = atsu_worker(ref, 0x44a8, read_defaults, NELEM(read_defaults), NULL);
    const Byte* pairs = loaded ? ROMlib_carbon_try_guest_memory(address, extent, 0) : NULL;
    const Byte* selectors = pairs ? ROMlib_carbon_try_guest_memory(args[3], count * 2, 0) : NULL;
    if (selectors) {
        for (uint32_t i = 0; i < total; ++i) {
            const Byte* pair = pairs + i * 4;
            if (atsu_read(pair, 2) == (uint16_t)args[1]) {
                const uint32_t selected = atsu_read(pair + 2, 2);
                for (unsigned j = 0; j < count; ++j)
                    defaults[j] |= atsu_read(selectors + 2 * j, 2) == selected;
            }
        }
    }
    return atsu_worker(ref, 0x6734, &address, 1, NULL) && selectors;
}

static int font_feature_table(enum ats_operation op, const ATSFontTable* table, const uint32_t* args, uint32_t* status,
                              unsigned* defaults_count)
{
    const Byte* header = font_table_span(table, 0, 12);
    const unsigned count = header ? atsu_read(header + 4, 2) : 0;
    const Byte* features = font_table_span(table, 12, 12 * count);
    if (!features)
        return 1;
    if (op == ats_count_font_features)
        return atsu_write_output(args[1], count, 4);
    if (op == ats_get_font_features)
        return atsu_write_output(args[3], count, 4) && write_table_words(args[2], features, MIN(count, args[1]), 12, 2);

    const uint16_t type = args[1];
    for (unsigned i = 0; i < count; ++i) {
        const Byte* record = features + 12 * i;
        if (atsu_read(record, 2) != type)
            continue;
        const unsigned selectors = atsu_read(record + 2, 2), flags = atsu_read(record + 8, 2);
        if (op == ats_count_font_selectors)
            return atsu_write_output(args[2], selectors, 4);
        uint16_t selector = args[2];
        if (op == ats_font_feature_name && selector == UINT16_MAX) {
            *status = noErr;
            return atsu_write_output(args[3], atsu_read(record + 10, 2), 4);
        }
        const Byte* records = font_table_span(table, atsu_read(record + 4, 4), 4 * selectors);
        if (!records)
            return 1;
        if (op == ats_get_font_selectors) {
            const unsigned copied = MIN(args[2], selectors);
            *defaults_count = copied;
            return atsu_write_output(args[5], selectors, 4) && atsu_write_output(args[6], flags >> 15, 1) &&
                   write_table_words(args[3], records, copied, 4, 2);
        }
        if (!(flags & 0x8000) && (selector & 1))
            selector += type;
        for (unsigned j = 0; j < selectors; ++j) {
            if (atsu_read(records + 4 * j, 2) == selector) {
                *status = noErr;
                return atsu_write_output(args[3], atsu_read(records + 4 * j + 2, 2), 4);
            }
        }
        break;
    }
    return 1;
}

static int font_tracking_table(enum ats_operation op, const ATSFontTable* table, const uint32_t* args, uint32_t* status)
{
    const Byte* header = font_table_span(table, 0, 10);
    if (!header)
        return 1;
    const unsigned offset = atsu_read(header + (uint16_t)args[1] * 2 + 6, 2);
    const Byte* tracking = offset ? font_table_span(table, offset, 8) : NULL;
    const unsigned count = tracking ? atsu_read(tracking, 2) : 0;
    if (op == ats_count_font_tracking)
        return atsu_write_output(args[2], count, 4);
    if (args[2] >= count)
        return 1;
    const Byte* record = font_table_span(table, offset + 8 + 8 * args[2], 8);
    if (!record)
        return 1;
    *status = noErr;
    return atsu_write_output(args[3], atsu_read(record, 4), 4) &&
           atsu_write_output(args[4], atsu_read(record + 4, 2), 4);
}

static int font_variation_table(enum ats_operation op, const ATSFontTable* table, const uint32_t* args,
                                uint32_t* status)
{
    const Byte* header = font_table_span(table, 0, 16);
    if (!header)
        return 1;
    const unsigned axes = atsu_read(header + 8, 2), instances = atsu_read(header + 12, 2);
    if (op == ats_count_font_variations || op == ats_count_font_instances)
        return atsu_write_output(args[1], op == ats_count_font_variations ? axes : instances, 4);
    const Byte* records = font_table_span(table, 16, 20 * axes);
    if (!records)
        return 1;
    if (op == ats_get_font_variation && args[1] < axes) {
        const Byte* record = records + 20 * args[1];
        *status = noErr;
        return atsu_write_output(args[2], atsu_read(record, 4), 4) &&
               atsu_write_output(args[3], atsu_read(record + 4, 4), 4) &&
               atsu_write_output(args[4], atsu_read(record + 12, 4), 4) &&
               atsu_write_output(args[5], atsu_read(record + 8, 4), 4);
    }
    if (op == ats_font_variation_name) {
        for (unsigned i = 0; i < axes; ++i) {
            const Byte* record = records + 20 * i;
            if (atsu_read(record, 4) == args[1]) {
                *status = noErr;
                return atsu_write_output(args[2], (int16_t)atsu_read(record + 18, 2), 4);
            }
        }
    }
    const unsigned limit = op == ats_get_font_instance ? MIN(args[2], axes) : 0;
    if (op == ats_get_font_instance &&
        (!atsu_write_output(args[5], axes, 4) || !write_table_words(args[3], records, limit, 20, 4)))
        return 0;
    if ((op == ats_font_instance_name || op == ats_get_font_instance) && args[1] < instances) {
        const unsigned stride = atsu_read(header + 14, 2);
        const uint64_t start = atsu_read(header + 4, 2) + (uint64_t)axes * atsu_read(header + 10, 2);
        const Byte* instance = font_table_span(table, start + (uint64_t)args[1] * stride, stride);
        if (instance && stride >= 4 + 4 * limit) {
            *status = noErr;
            if (op == ats_font_instance_name)
                return atsu_write_output(args[2], (int16_t)atsu_read(instance, 2), 4);
            return write_table_words(args[4], instance + 4, limit, 4, 4);
        }
    }
    return 1;
}

static int font_table_query(enum ats_operation op, const char* name, const ROMlib_carbon_reference* ref,
                            ROMlib_carbon_ppc_frame* call)
{
    enum { invalid_font = -8796 };

    const uint32_t* args = call->words;
    const int feature = op <= ats_font_feature_name;
    const int tracking = op == ats_count_font_tracking || op == ats_get_font_tracking;
    const uint32_t tag = feature ? T('f', 'e', 'a', 't') : tracking ? T('t', 'r', 'a', 'k') : T('f', 'v', 'a', 'r');
    uint32_t count_output = 0, first_output = 0;
    switch (op) {
    case ats_count_font_features:
    case ats_count_font_variations:
    case ats_count_font_instances:
        first_output = count_output = args[1];
        break;
    case ats_count_font_selectors:
    case ats_count_font_tracking:
        first_output = count_output = args[2];
        break;
    case ats_get_font_features:
        count_output = args[3];
        first_output = args[2] | args[3];
        break;
    case ats_get_font_selectors:
        count_output = args[5];
        first_output = args[3] | args[4] | args[5] | args[6];
        break;
    case ats_font_feature_name:
        first_output = args[3];
        break;
    case ats_font_variation_name:
    case ats_font_instance_name:
        first_output = args[2];
        break;
    case ats_get_font_tracking:
        first_output = 1;
        break;
    case ats_get_font_instance:
        first_output = args[3] | args[4] | args[5];
        break;
    default:
        first_output = args[2] | args[3] | args[4] | args[5];
        break;
    }
    if (!first_output || (tracking && (uint16_t)args[1] > 1))
        return atsu_result(call, paramErr);

    ROMlib_carbon_reference length_query;
    if (!ROMlib_carbon_probe_reference("FontObjects", "FOGetFontTable", &length_query) ||
        length_query.code_offset != 0x5dc0)
        return atsu_fallback(name, call);
    if (!atsu_write_output(count_output, 0, 4) ||
        (op == ats_get_font_tracking && (!atsu_write_output(args[3], 0, 4) || !atsu_write_output(args[4], 0, 4))))
        return 0;

    uint32_t font;
    if (!atsu_worker(ref, 0x305d8, args, 1, &font))
        return 0;
    if (!font)
        return atsu_result(call, invalid_font);
    uint32_t status =
        (count_output || op == ats_get_font_features || op == ats_get_font_selectors) ? noErr : missing_item;
    _Alignas(4) uint32_t length = 0;
    const uint32_t read_size[] = {font, tag, 0, 0, 0, US_TO_SYN68K(&length)};
    uint32_t size_error, address = 0;
    unsigned defaults_count = 0;
    int ok = ROMlib_carbon_invoke_callback(length_query.function, read_size, NELEM(read_size), &size_error);
    if (ok && !size_error && CL(length)) {
        const uint32_t find[] = {font, tag};
        ok = atsu_worker(ref, 0x30638, find, NELEM(find), &address);
    }
    if (ok && address) {
        const ATSFontTable table = {ROMlib_carbon_try_guest_memory(address, CL(length), 0), CL(length)};
        if (table.data && table.size >= 4 && (int32_t)atsu_read(table.data, 4) <= 0x10000) {
            if (feature)
                ok = font_feature_table(op, &table, args, &status, &defaults_count);
            else if (tracking)
                ok = font_tracking_table(op, &table, args, &status);
            else
                ok = font_variation_table(op, &table, args, &status);
        }
    }
    const uint32_t release[] = {font, address};
    const int released = !address || atsu_worker(ref, 0x30650, release, NELEM(release), NULL);
    const int unlocked = atsu_worker(ref, 0x30608, &font, 1, NULL);
    return ok && released && unlocked && mark_default_selectors(ref, args, defaults_count) && atsu_result(call, status);
}

static int count_catalogue_items(enum ats_operation op, const ROMlib_carbon_reference* ref,
                                 ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    uint32_t count;
    if (op == ats_font_count) {
        if (!args[0])
            return atsu_result(call, paramErr);
        if (!atsu_worker(ref, 0x42a8, NULL, 0, NULL) || !atsu_worker(ref, 0x30758, NULL, 0, &count))
            return 0;
        const uint32_t excluded = atsu_read((const Byte*)ref->toc + 0x7ac, 4);
        return atsu_write_output(args[0], count - excluded, 4) && atsu_result(call, noErr);
    }
    if (!args[1])
        return atsu_result(call, paramErr);
    uint32_t font;
    if (!atsu_worker(ref, 0x305d8, args, 1, &font))
        return 0;
    if (!font)
        return atsu_result(call, -8796);
    const int read = atsu_worker(ref, 0x307a0, &font, 1, &count);
    const int unlocked = atsu_worker(ref, 0x30608, &font, 1, NULL);
    return read && unlocked && atsu_write_output(args[1], count, 4) && atsu_result(call, noErr);
}

static int font_has_scaler(const ROMlib_carbon_reference* ref, uint32_t font, uint32_t* available)
{
    uint32_t format;
    if (!atsu_worker(ref, 0x30680, &font, 1, &format))
        return 0;
    const void* globals = ROMlib_carbon_guest_memory(atsu_toc_address(ref, -0x1c8), 8, 0);
    if (!globals)
        return 0;
    void* previous = ROMlib_carbon_guest_memory(atsu_read(globals, 4), 4, 1);
    Byte* failed = ROMlib_carbon_guest_memory(atsu_read((const Byte*)globals + 4, 4), 1, 1);
    if (!previous || !failed)
        return 0;
    if (format == atsu_read(previous, 4)) {
        *available = !*failed;
        return 1;
    }
    if (format == 0xa56c7374) {
        *available = 0;
        return 1;
    }
    _Alignas(16) uint32_t scaler = 0;
    const uint32_t query[] = {format, US_TO_SYN68K(&scaler)};
    uint32_t status;
    if (!atsu_worker(ref, 0x30698, query, NELEM(query), &status))
        return 0;
    atsu_write(previous, format, 4);
    *failed = status != noErr;
    *available = !*failed;
    return 1;
}

static int get_font_ids(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    if (!args[0] && !args[2])
        return atsu_result(call, paramErr);
    uint32_t total;
    if (!atsu_worker(ref, 0x42a8, NULL, 0, NULL) || !atsu_worker(ref, 0x30758, NULL, 0, &total))
        return 0;
    uint32_t written = 0;
    for (uint32_t index = 1; args[0] && index && index <= total && written < args[1]; ++index) {
        uint32_t object;
        if (!atsu_worker(ref, 0x30668, &index, 1, &object))
            return 0;
        if (!object)
            continue;
        uint32_t id, font = 0, available = 0;
        int ok = atsu_worker(ref, 0x306c8, &object, 1, &id) && atsu_worker(ref, 0x305d8, &id, 1, &font);
        if (ok && font) {
            ok = font_has_scaler(ref, font, &available);
            ok = atsu_worker(ref, 0x30608, &font, 1, NULL) && ok;
        }
        if (ok && available) {
            const uint64_t address = (uint64_t)args[0] + (uint64_t)written * 4;
            ok = address <= UINT32_MAX - 3 && atsu_write_output((uint32_t)address, id, 4);
            ++written;
        }
        if (!atsu_worker(ref, 0x30608, &object, 1, NULL) || !ok)
            return 0;
    }
    const uint32_t excluded = atsu_read((const Byte*)ref->toc + 0x7ac, 4);
    return atsu_write_output(args[2], total - excluded, 4) && atsu_result(call, noErr);
}

static int font_to_fond(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    const void* font_source = ROMlib_carbon_guest_memory(atsu_toc_address(ref, -0x2d8), 4, 0);
    if (!font_source)
        return 0;
    const int instance = atsu_read(font_source, 4) != 0;
    if (!args[1] && (instance || !args[2]))
        return atsu_result(call, paramErr);
    if (!args[0])
        return atsu_result(call, -8796);
    if ((args[1] && !ROMlib_carbon_guest_memory(args[1], 2, 1)) ||
        (args[2] && !ROMlib_carbon_guest_memory(args[2], 1, 1)))
        return 0;
    uint32_t font, status;
    if (!atsu_worker(ref, 0x305d8, args, 1, &font))
        return 0;
    if (!font)
        return atsu_result(call, -8796);
    _Alignas(16) unsigned char auxiliary[4] = {0};
    const uint32_t local = US_TO_SYN68K(auxiliary);
    const uint32_t query[] = {font, args[1] ? args[1] : local, instance || !args[2] ? local + 2 : args[2]};
    int ok = atsu_worker(ref, instance ? 0x305f0 : 0x30620, query, NELEM(query), &status);
    if (ok && instance && !status)
        ok = atsu_write_output(args[2], atsu_read(auxiliary + 2, 2), 1);
    return atsu_worker(ref, 0x30608, &font, 1, NULL) && ok && atsu_result(call, status ? -8795 : noErr);
}

static int font_name_query(enum ats_operation op, const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    uint32_t last;
    if (!ROMlib_carbon_frame_word(call, 8, &last))
        return 0;
    const int find = op == ats_find_font_name;
    const uint32_t buffer = args[find ? 6 : 3], capacity = args[find ? 5 : 2];
    const uint32_t size_output = args[find ? 7 : 4];
    if ((buffer && !capacity) || (!buffer && !size_output && !last && (find || !(args[5] || args[6] || args[7]))))
        return atsu_result(call, paramErr);
    if (!find) {
        const uint32_t metadata[] = {args[5], args[6], args[7], last};
        for (unsigned i = 0; i < NELEM(metadata); ++i)
            if (metadata[i] && !ROMlib_carbon_guest_memory(metadata[i], 4, 1))
                return 0;
    }
    uint32_t font;
    if (!atsu_worker(ref, 0x305d8, args, 1, &font))
        return 0;
    if (!font)
        return atsu_result(call, -8796);

    _Alignas(16) uint32_t found_index = 0;
    uint32_t index = args[1] + 1, length, status = noErr;
    int ok;
    if (find) {
        const uint32_t query[] = {font, args[1], args[2], args[3], args[4], 0, US_TO_SYN68K(&found_index)};
        ok = atsu_worker(ref, 0x307d0, query, NELEM(query), &length);
        index = CL(found_index);
    }
    else {
        const uint32_t query[] = {font, index, args[5], args[6], args[7], last, 0};
        ok = atsu_worker(ref, 0x307b8, query, NELEM(query), &length);
    }
    if (ok)
        ok = atsu_write_output(size_output, length, 4);
    if (ok && find) {
        if (length)
            ok = atsu_write_output(last, index - 1, 4);
        else
            status = missing_item;
    }
    if (ok && buffer && length) {
        if (!ROMlib_carbon_guest_memory(buffer, MIN(length, capacity), 1))
            ok = 0;
        else {
            const uint32_t query[] = {font, index, length, capacity, buffer};
            ok = atsu_worker(ref, 0x43d8, query, NELEM(query), &status);
        }
    }
    return atsu_worker(ref, 0x30608, &font, 1, NULL) && ok && atsu_result(call, status);
}

static int find_font(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    if (!args[6])
        return atsu_result(call, paramErr);
    if (args[0] && args[1] && !ROMlib_carbon_guest_memory(args[0], args[1], 0))
        return 0;
    if (!atsu_write_output(args[6], 0, 4))
        return 0;
    for (uint32_t index = 1; index; ++index) {
        uint32_t font, available, matched, id = 0;
        if (!atsu_worker(ref, 0x30668, &index, 1, &font))
            return 0;
        if (!font)
            break;
        int ok = font_has_scaler(ref, font, &available);
        if (ok && available) {
            const uint32_t query[] = {font, args[1], args[2], args[3], args[4], args[5], args[0]};
            ok = atsu_worker(ref, 0x306b0, query, NELEM(query), &matched);
            if (ok && matched)
                ok = atsu_worker(ref, 0x306c8, &font, 1, &id) && atsu_write_output(args[6], id, 4);
        }
        if (!atsu_worker(ref, 0x30608, &font, 1, NULL) || !ok)
            return 0;
        if (id)
            return atsu_result(call, noErr);
    }
    return atsu_result(call, -8796);
}

static int glyph_style_index(const ROMlib_carbon_reference* ref, Byte* vector, uint32_t style, uint32_t* index,
                             uint32_t* status)
{
    *index = 0;
    *status = noErr;
    uint32_t table = atsu_read(vector + 0x1c, 4);
    const unsigned count = atsu_read(vector + 0x20, 2);
    const Byte* entries = count ? ROMlib_carbon_guest_memory(table, count * 8, 0) : NULL;
    if (count && (!table || !entries))
        return 0;
    for (unsigned i = 0; i < count; ++i) {
        if (atsu_read(entries + i * 8, 4) == style) {
            *index = i;
            return 1;
        }
    }

    const uint32_t needed = (count + 1) * 8;
    uint32_t extent = 0;
    if (count == UINT16_MAX) {
        *status = memFullErr;
        return 1;
    }
    if (table && !atsu_worker(ref, 0x30980, &table, 1, &extent))
        return 0;
    if (extent < needed) {
        const uint32_t resize[] = {table, needed};
        if (!atsu_worker(ref, 0x6658, resize, NELEM(resize), &table))
            return 0;
    }
    if (!table) {
        *status = memFullErr;
        return 1;
    }
    Byte* output = ROMlib_carbon_guest_memory(table, needed, 1);
    if (!output)
        return 0;
    atsu_write(output + count * 8, style, 4);
    atsu_write(output + count * 8 + 4, 0, 4);
    atsu_write(vector + 0x1c, table, 4);
    atsu_write(vector + 0x20, count + 1, 2);
    *index = count;
    return 1;
}

static int map_glyph_ids(const ROMlib_carbon_reference* ref, uint32_t font, uint32_t text, uint32_t count,
                         uint32_t capacity, uint32_t output, Byte* glyphs, uint32_t* mapped)
{
    _Alignas(16) uint8_t symbolic = 0;
    const uint32_t codebook_query[] = {font, 0, US_TO_SYN68K(&symbolic)};
    uint32_t codebook;
    if (!atsu_worker(ref, 0x1ab20, codebook_query, NELEM(codebook_query), &codebook))
        return 0;
    *mapped = 0;
    if (!codebook || !count)
        return 1;
    const Byte* input = ROMlib_carbon_guest_memory(text, count * 2, 0);
    if (!input)
        return 0;
    _Alignas(16) uint32_t length = CL(count * 2);
    const uint32_t convert[] = {font, codebook, US_TO_SYN68K(&length), text, capacity, output, 0};
    if (!atsu_worker(ref, 0x30b60, convert, NELEM(convert), mapped) || *mapped > capacity)
        return 0;
    if (!symbolic)
        return 1;
    for (uint32_t i = 0; i < MIN(count, *mapped); ++i) {
        const uint32_t character = atsu_read(input + i * 2, 2);
        if (!atsu_read(glyphs + i * 2, 2) && character <= 0xff) {
            _Alignas(16) uint16_t alternate = CW(0xf000 + character);
            length = CL(2);
            const uint32_t retry[] = {font,           codebook, US_TO_SYN68K(&length), US_TO_SYN68K(&alternate), 1,
                                      output + i * 2, 0};
            if (!atsu_worker(ref, 0x30b60, retry, NELEM(retry), NULL))
                return 0;
        }
    }
    return 1;
}

static int convert_characters_to_glyphs(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    if (!args[5])
        return atsu_result(call, -8804);
    Byte* vector = ROMlib_carbon_guest_memory(args[5], 0x22, 1);
    if (!vector)
        return 0;
    const uint32_t start = args[4], capacity = atsu_read(vector + 4, 4);
    if (start >= capacity)
        return atsu_result(call, -8804);
    const uint32_t room = capacity - start, count = args[3], stride = atsu_read(vector + 8, 4);
    const uint32_t records = atsu_read(vector + 0xc, 4);
    if (!args[0] || (count && (!args[1] || !records)) || stride < 0x1c || capacity > UINT32_MAX / stride ||
        count > UINT32_MAX / 2)
        return atsu_result(call, paramErr);
    Byte* output = count ? ROMlib_carbon_guest_memory(records, capacity * stride, 1) : NULL;
    if (count && !output)
        return 0;

    const uint32_t strike_query[] = {args[0], 0x7374726b, 0}; /* 'strk' */
    uint32_t strike, font_id, font;
    if (!atsu_worker(ref, 0x30860, strike_query, NELEM(strike_query), &strike))
        return 0;
    const uint32_t font_query[] = {strike, 0x666f6e74, 0}; /* 'font' */
    uint32_t attribute;
    if (!strike)
        return atsu_result(call, -8796);
    if (!atsu_worker(ref, 0x30860, font_query, NELEM(font_query), &attribute))
        return 0;
    if (!attribute)
        return atsu_result(call, -8796);
    if (!cfp_get(attribute, &font_id) || !atsu_worker(ref, 0x305d8, &font_id, 1, &font))
        return 0;

    uint32_t mapped = 0, index = 0, status = noErr;
    if (font) {
        uint32_t bitmap_font;
        int ok = atsu_worker(ref, 0x47c0, NULL, 0, &bitmap_font);
        if (ok && bitmap_font == font_id) {
            mapped = MIN(count, room);
            for (uint32_t i = 0; i < mapped; ++i)
                atsu_write(output + i * 2, 5, 2);
        }
        else if (ok && count)
            ok = map_glyph_ids(ref, font, args[1], count, room, records, output, &mapped);
        if (ok && mapped)
            ok = glyph_style_index(ref, vector, args[0], &index, &status);
        if (!atsu_worker(ref, 0x30608, &font, 1, NULL) || !ok)
            return 0;
    }
    if (status) {
        memset(output + start * stride, 0, MIN(count, room) * stride);
        return atsu_result(call, status);
    }

    /* The scaler stages 16-bit IDs in the record buffer, so expand from the end. */
    for (uint32_t i = mapped; i > 0; --i) {
        const uint32_t glyph = atsu_read(output + (i - 1) * 2, 2);
        Byte* record = output + (i - 1) * stride;
        memset(record, 0, 0x1c);
        atsu_write(record, glyph == UINT16_MAX ? 0 : glyph, 2);
        atsu_write(record + 2, index, 2);
        atsu_write(record + 4, 0x4000, 4);
        atsu_write(record + 8, (args[2] + i - 1) * 2, 4);
    }
    const uint32_t length = atsu_read(vector, 4);
    if (start + mapped > length)
        atsu_write(vector, start + mapped, 4);
    return atsu_result(call, noErr);
}

static int native_curve_type(const ROMlib_carbon_reference* ref, uint32_t group, uint32_t output,
                             ROMlib_carbon_ppc_frame* call)
{
    const uint32_t query[] = {group, 0x7374726b, 0}; /* 'strk' */
    uint32_t strike;
    if (!atsu_worker(ref, 0x30860, query, NELEM(query), &strike))
        return 0;
    if (!strike)
        return atsu_result(call, -8796);
    if (!output)
        return atsu_result(call, paramErr);
    const void* context = ROMlib_carbon_guest_memory(atsu_toc_address(ref, -0xbc), 4, 0);
    if (!context)
        return 0;
    _Alignas(16) uint32_t estimates[2] = {0};
    const uint32_t classify[] = {strike, 2, atsu_read(context, 4), US_TO_SYN68K(estimates)};
    uint32_t status;
    if (!atsu_worker(ref, 0x30878, classify, NELEM(classify), &status))
        return 0;
    const uint32_t quadratic = estimates[0] ? CL(estimates[0]) : UINT32_MAX;
    const uint32_t cubic = estimates[1] ? CL(estimates[1]) : UINT32_MAX;
    if (status || (quadratic == UINT32_MAX && cubic == UINT32_MAX))
        return atsu_result(call, -8796);
    return atsu_write_output(output, quadratic < cubic ? 2 : 1, 2) && atsu_result(call, noErr);
}

static int glyph_style_query(enum ats_operation op, const char* name, const ROMlib_carbon_reference* ref,
                             ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    if (op == ats_get_style_index) {
        if (!args[0] || !args[1] || !args[2])
            return atsu_result(call, paramErr);
        Byte* vector = ROMlib_carbon_guest_memory(args[0], 0x22, 1);
        uint32_t index, status;
        return vector && ROMlib_carbon_guest_memory(args[2], 4, 1) &&
               glyph_style_index(ref, vector, args[1], &index, &status) && atsu_write_output(args[2], index, 4) &&
               atsu_result(call, status);
    }
    if (op == ats_dispose_style_group)
        return atsu_worker(ref, 0x30590, args, 1, NULL) && atsu_result(call, noErr);
    if (!args[0] || (op == ats_get_style_group && !args[1]) ||
        (op == ats_baseline_deltas && (args[1] >= 32 || !args[2])))
        return atsu_result(call, paramErr);
    const Byte* style = ROMlib_carbon_guest_memory(args[0], ats_style_size, 0);
    if (!style || atsu_read(style + 0xb8, 2) != ats_record_signature)
        return atsu_result(call, invalid_style);
    const uint32_t vtable = atsu_read(style, 4);
    if (vtable != atsu_toc_address(ref, 0x89c))
        return atsu_fallback(name, call);
    uint32_t group;
    if (!atsu_virtual(style, 0x100, args[0], vtable, &group))
        return 0;
    if (op == ats_get_native_curve)
        return native_curve_type(ref, group, args[1], call);
    if (op == ats_baseline_deltas) {
        uint32_t status;
        const uint32_t query[] = {group, args[1], args[2]};
        return ROMlib_carbon_guest_memory(args[2], 32 * 4, 1) &&
               atsu_worker(ref, 0x30740, query, NELEM(query), &status) &&
               atsu_result(call, status ? missing_item : noErr);
    }
    if (!ROMlib_carbon_guest_memory(args[1], 4, 1))
        return 0;
    const uint32_t query[] = {group, 0};
    uint32_t copy;
    if (!atsu_worker(ref, 0x309e0, query, NELEM(query), &copy))
        return 0;
    if (!copy)
        return atsu_result(call, memFullErr);
    return atsu_write_output(args[1], copy, 4) && atsu_result(call, noErr);
}

static int layout_glyph_query(enum ats_operation op, const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    const int info = op == ats_get_glyph_info;
    if (!args[0] || !args[info ? 3 : 5])
        return atsu_result(call, paramErr);
    const Byte* layout = atsu_layout(args[0], 0);
    if (!layout || !layout[0xe])
        return atsu_result(call, invalid_layout);
    uint32_t status;
    if (!atsu_worker(ref, 0x1a4d0, args, 1, &status))
        return 0;
    if (status)
        return atsu_result(call, status);
    uint32_t query[] = {args[0], args[1], args[2], args[3], args[4], args[5]};
    int ok = 1;
    if (!layout_range(layout, &query[1], NULL))
        status = invalid_offset;
    else if (info) {
        uint32_t capacity = 4;
        ok = (!args[2] || (ROMlib_carbon_guest_memory(args[3], 4, 1) && (!args[4] || cfp_get(args[3], &capacity)))) &&
             (!args[4] || !capacity || ROMlib_carbon_guest_memory(args[4], capacity, 1)) &&
             (!args[2] || atsu_worker(ref, 0x122d0, query, 5, &status));
    }
    else {
        Byte* vector = ROMlib_carbon_guest_memory(args[5], 0x54, 1);
        query[3] = (uint8_t)query[3];
        if (!vector)
            ok = 0;
        else if (!args[2])
            memset(vector, 0, 0x54);
        else
            ok = atsu_worker(ref, 0x11e7c, query, NELEM(query), &status);
    }
    if (!atsu_worker(ref, 0x1a598, args, 1, NULL) || !ok)
        return 0;
    if (info && !status && !atsu_write_output(args[4], args[0], 4))
        return 0;
    return atsu_result(call, status);
}

static int glyph_bounds(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    uint32_t query[9];
    memcpy(query, call->words, sizeof call->words);
    if (!ROMlib_carbon_frame_word(call, 8, query + 8))
        return 0;
    query[5] = (uint16_t)query[5];
    if (query[5] > 2 || !query[0])
        return atsu_result(call, paramErr);
    const Byte* layout = atsu_layout(query[0], 0);
    if (!layout || !layout[0xe])
        return atsu_result(call, invalid_layout);
    uint32_t status;
    if (!atsu_worker(ref, 0x1a4d0, query, 1, &status))
        return 0;
    if (status)
        return atsu_result(call, status);
    int ok = 1;
    if (!layout_range(layout, query + 3, query + 4))
        status = invalid_offset;
    else if (query[4]) {
        const uint32_t capacity = MIN(query[6], UINT16_MAX);
        const uint32_t bounds[] = {query[0], query[3], query[4], query[1], query[2],
                                   query[5], query[6], query[7], query[8]};
        ok = (!capacity || !query[7] || ROMlib_carbon_guest_memory(query[7], capacity * 32, 1)) &&
             (!query[8] || ROMlib_carbon_guest_memory(query[8], 4, 1)) &&
             atsu_worker(ref, 0x157ec, bounds, NELEM(bounds), &status);
    }
    return atsu_worker(ref, 0x1a598, query, 1, NULL) && ok && atsu_result(call, status);
}

static int glyph_antialias(const ROMlib_carbon_reference* ref, const uint32_t* args, uint32_t strike, uint32_t* enabled,
                           uint32_t* status)
{
    if ((uint8_t)args[4]) {
        *enabled = args[5];
        return 1;
    }
    _Alignas(16) uint16_t threshold = 0;
    const uint32_t threshold_slot = US_TO_SYN68K(&threshold);
    if (!atsu_worker(ref, 0x308f0, &threshold_slot, 1, enabled))
        return 0;
    if (!*enabled)
        return 1;
    const uint32_t attributes[] = {strike, 0x7472616e, 0}; /* 'tran' */
    uint32_t transform;
    if (!atsu_worker(ref, 0x30860, attributes, NELEM(attributes), &transform))
        return 0;
    if (!transform) {
        *status = -8796;
        return 1;
    }
    const Byte* data = ROMlib_carbon_guest_memory(transform, 8, 0);
    if (!data)
        return 0;
    const uint32_t rounded = atsu_read(data + 4, 4) + 0x8000;
    *enabled = (int16_t)(rounded >> 16) >= (int16_t)CW(threshold);
    return 1;
}

static uint32_t scaled_fixed_bits(uint32_t bits, float scale)
{
    const float scaled = (float)(int32_t)bits * scale;
    memcpy(&bits, &scaled, sizeof bits);
    return bits;
}

static void write_glyph_metrics(Byte* output, const Byte* fixed, int screen, float scale)
{
    const unsigned words = screen ? 9 : 6;
    for (unsigned i = 0; i < words; ++i) {
        uint32_t bits = atsu_read(fixed + i * 4, 4);
        if (screen && i == 4) {
            atsu_write(output + 16, bits >> 16, 4);
            atsu_write(output + 20, bits & UINT16_MAX, 4);
        }
        else
            atsu_write(output + (i + (screen && i > 4)) * 4, scaled_fixed_bits(bits, scale), 4);
    }
}

static int read_glyph_metrics(const ROMlib_carbon_reference* ref, const uint32_t* args, uint32_t strike, int screen,
                              uint32_t antialias, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t count = args[1], result = args[screen ? 6 : 4], stride = screen ? 36 : 24;
    const unsigned output_stride = screen ? 40 : 24;
    if (!args[2] || !result)
        return atsu_result(call, paramErr);
    if (!count)
        return atsu_result(call, noErr);
    if (count > UINT32_MAX / output_stride)
        return atsu_result(call, paramErr);
    Byte* output = ROMlib_carbon_guest_memory(result, count * output_stride, 1);
    if (!output || !ROMlib_carbon_guest_memory(args[2], count * 2, 0))
        return 0;

    const int cache_offset = !screen ? -0x2b0 : antialias ? -0x2b4 : -0x2b8;
    uint32_t selector, cache, scale_table, scale_bits;
    if (!cfp_get(atsu_toc_address(ref, cache_offset), &selector) || !cfp_get(selector, &cache) ||
        !cfp_get(atsu_toc_address(ref, -0xc0), &scale_table) || !cfp_get(scale_table + 8, &scale_bits))
        return 0;
    float scale;
    memcpy(&scale, &scale_bits, sizeof scale);
    const uint32_t allocate[] = {count * stride, 1};
    uint32_t records;
    if (!atsu_worker(ref, 0x65b4, allocate, NELEM(allocate), &records))
        return 0;
    if (!records)
        return atsu_result(call, memFullErr);

    _Alignas(16) uint32_t index = 0;
    const uint32_t prepare[] = {strike, cache, US_TO_SYN68K(&index)};
    uint32_t status;
    int ok = atsu_worker(ref, 0x30890, prepare, NELEM(prepare), &status);
    int locked = ok && !status;
    uint32_t remaining = count, cursor = records;
    while (ok && !status && remaining) {
        _Alignas(16) uint32_t batch = CL(remaining);
        const uint32_t get[] = {CL(index), US_TO_SYN68K(&batch), args[2], args[3], cursor, stride};
        ok = atsu_worker(ref, 0x308a8, get, NELEM(get), &status);
        const uint32_t completed = CL(batch);
        if (!ok || status || !completed)
            break;
        if (completed > remaining) {
            ok = 0;
            break;
        }
        for (uint32_t i = 0; i < completed && ok; ++i) {
            uint32_t address;
            ok = cfp_get(cursor + i * stride, &address);
            const Byte* metrics = ok && address ? ROMlib_carbon_guest_memory(address, stride, 0) : NULL;
            if (!metrics)
                ok = 0;
            else
                write_glyph_metrics(output + i * output_stride, metrics, screen, scale);
        }
        output += completed * output_stride;
        cursor += completed * stride;
        remaining -= completed;
        if (ok && remaining) {
            const uint32_t previous = CL(index);
            ok = atsu_worker(ref, 0x308c0, &previous, 1, NULL);
            locked = 0;
            if (ok) {
                const uint32_t refresh[] = {previous, US_TO_SYN68K(&index)};
                ok = atsu_worker(ref, 0x30908, refresh, NELEM(refresh), &status);
                locked = ok && !status;
            }
        }
    }
    if (locked) {
        const uint32_t current = CL(index);
        ok = atsu_worker(ref, 0x308c0, &current, 1, NULL) && ok;
    }
    return atsu_worker(ref, 0x6138, &records, 1, NULL) && ok && atsu_result(call, status);
}

static int glyph_metrics(enum ats_operation op, const char* name, const ROMlib_carbon_reference* ref,
                         ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    if (!args[0])
        return atsu_result(call, paramErr);
    const Byte* style = ROMlib_carbon_guest_memory(args[0], ats_style_size, 0);
    if (!style || atsu_read(style + 0xb8, 2) != ats_record_signature)
        return atsu_result(call, invalid_style);
    const uint32_t vtable = atsu_read(style, 4);
    if (vtable != atsu_toc_address(ref, 0x89c))
        return atsu_fallback(name, call);
    uint32_t group, strike;
    if (!atsu_virtual(style, 0x100, args[0], vtable, &group))
        return 0;
    const uint32_t attributes[] = {group, 0x7374726b, 0}; /* 'strk' */
    if (!atsu_worker(ref, 0x30860, attributes, NELEM(attributes), &strike))
        return 0;
    if (!strike)
        return atsu_result(call, -8796);
    const int screen = op == ats_get_screen_metrics;
    uint32_t antialias = 0, status = noErr;
    if (screen && !glyph_antialias(ref, args, strike, &antialias, &status))
        return 0;
    if (status)
        return atsu_result(call, status);
    return read_glyph_metrics(ref, args, strike, screen, antialias, call);
}

static uint64_t curve_mask_size(uint32_t points)
{
    return (((uint64_t)points + 31) / 32) * 4;
}

static int curve_path_extent(uint32_t address, uint32_t* size)
{
    *size = 0;
    uint32_t contours;
    if (!address || !cfp_get(address, &contours))
        return address == 0;
    uint64_t cursor = (uint64_t)address + 4, total = 0;
    for (uint32_t i = 0; i < contours; ++i) {
        uint32_t points;
        if (cursor > UINT32_MAX - 3 || !cfp_get((uint32_t)cursor, &points))
            return 0;
        const uint64_t extent = 4 + curve_mask_size(points) + (uint64_t)points * 8;
        cursor += extent;
        total += extent + 4;
        if ((uint64_t)address + total > (uint64_t)UINT32_MAX + 1)
            return 0;
    }
    *size = (uint32_t)total;
    return 1;
}

static void curve_path_convert(Byte* data, float scale)
{
    const uint32_t contours = atsu_read(data, 4);
    Byte* contour = data + 4;
    for (uint32_t i = 0; i < contours; ++i) {
        const uint32_t points = atsu_read(contour, 4);
        Byte* coordinates = contour + 4 + curve_mask_size(points);
        for (uint32_t j = 0; j < points; ++j) {
            uint32_t x = scaled_fixed_bits(atsu_read(coordinates, 4), scale);
            uint32_t y = scaled_fixed_bits(atsu_read(coordinates + 4, 4), scale);
            atsu_write(coordinates, x, 4);
            atsu_write(coordinates + 4, y, 4);
            coordinates += 8;
        }
        contour = coordinates;
    }
}

static int cached_glyph(const ROMlib_carbon_reference* ref, uint32_t strike, uint16_t glyph, int cache_offset,
                        uint32_t* index, uint32_t* address, uint32_t* status)
{
    uint32_t selector, cache;
    if (!cfp_get(atsu_toc_address(ref, cache_offset), &selector) || !cfp_get(selector, &cache))
        return 0;
    _Alignas(16) uint32_t id = 0;
    const uint32_t lock[] = {strike, cache, US_TO_SYN68K(&id)};
    if (!atsu_worker(ref, 0x30890, lock, NELEM(lock), status))
        return 0;
    if (*status || !id) {
        *status = -8796;
        return 1;
    }
    *index = CL(id);
    _Alignas(16) uint16_t encoded = CW(glyph);
    _Alignas(16) uint32_t count = CL(1), record = 0;
    const uint32_t get[] = {*index, US_TO_SYN68K(&count), US_TO_SYN68K(&encoded), 0, US_TO_SYN68K(&record), 0};
    if (!atsu_worker(ref, 0x308a8, get, NELEM(get), status))
        return 0;
    if (*status || !count)
        *status = -8796;
    *address = CL(record);
    return 1;
}

static int curve_scale(const ROMlib_carbon_reference* ref, float* scale)
{
    uint32_t bits;
    if (!cfp_get(atsu_toc_address(ref, 0x7248), &bits))
        return 0;
    memcpy(scale, &bits, sizeof bits);
    return 1;
}

static int outline_supported(const ROMlib_carbon_reference* ref, uint32_t strike, int format_offset, uint32_t* status)
{
    uint32_t format;
    if (!cfp_get(atsu_toc_address(ref, format_offset), &format))
        return 0;
    _Alignas(16) uint32_t supported = 0;
    const uint32_t query[] = {strike, 1, format, US_TO_SYN68K(&supported)};
    if (!atsu_worker(ref, 0x30878, query, NELEM(query), status))
        return 0;
    if (*status || !supported)
        *status = -8796;
    return 1;
}

static int glyph_curve_paths(const char* name, const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    if (!args[0])
        return atsu_result(call, paramErr);
    const Byte* style = ROMlib_carbon_guest_memory(args[0], ats_style_size, 0);
    if (!style || atsu_read(style + 0xb8, 2) != ats_record_signature)
        return atsu_result(call, invalid_style);
    if (!args[2])
        return atsu_result(call, paramErr);
    const uint16_t glyph = args[1];
    if (glyph == UINT16_MAX)
        return atsu_result(call, noErr);
    const uint32_t vtable = atsu_read(style, 4);
    if (vtable != atsu_toc_address(ref, 0x89c))
        return atsu_fallback(name, call);
    uint32_t group, strike;
    if (!atsu_virtual(style, 0x100, args[0], vtable, &group))
        return 0;
    const uint32_t attributes[] = {group, 0x7374726b, 0}; /* 'strk' */
    if (!atsu_worker(ref, 0x30860, attributes, NELEM(attributes), &strike))
        return 0;
    if (!strike)
        return atsu_result(call, -8796);

    uint32_t status;
    if (!outline_supported(ref, strike, -0xcc, &status))
        return 0;
    if (status)
        return atsu_result(call, status);

    uint32_t index = 0, address = 0, size = 0;
    int ok = cached_glyph(ref, strike, glyph, -0x2a0, &index, &address, &status);
    if (ok && !status)
        ok = curve_path_extent(address, &size);
    if (ok && !status && args[3]) {
        uint32_t capacity;
        ok = cfp_get(args[2], &capacity);
        if (ok && size > capacity) {
            size = 0;
            status = paramErr;
        }
        if (ok && size) {
            float scale;
            const Byte* original = ROMlib_carbon_guest_memory(address, size, 0);
            Byte* output = ROMlib_carbon_guest_memory(args[3], size, 1);
            ok = original && output && curve_scale(ref, &scale);
            if (ok) {
                memmove(output, original, size);
                curve_path_convert(output, scale);
            }
        }
    }
    if (ok && (!status || status == (uint32_t)paramErr))
        ok = atsu_write_output(args[2], size, 4);
    return (!index || atsu_worker(ref, 0x308c0, &index, 1, NULL)) && ok && atsu_result(call, status);
}

typedef struct {
    float x, y;
} ATSOutlinePoint;

static ATSOutlinePoint outline_point(const Byte* points, uint32_t index)
{
    const Byte* xy = points + index * 8;
    const uint32_t x = atsu_read(xy, 4), y = atsu_read(xy + 4, 4);
    ATSOutlinePoint point;
    memcpy(&point.x, &x, sizeof x);
    memcpy(&point.y, &y, sizeof y);
    return point;
}

static int on_curve(const Byte* mask, uint32_t index)
{
    return !(mask[index / 8] & (0x80u >> (index % 8)));
}

static ATSOutlinePoint midpoint(ATSOutlinePoint a, ATSOutlinePoint b)
{
    return (ATSOutlinePoint){(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
}

static int outline_callback(uint32_t callback, uint32_t context, const ATSOutlinePoint* points, unsigned count,
                            uint32_t* result)
{
    _Alignas(16) uint32_t guest[3][2];
    uint32_t args[4];
    for (unsigned i = 0; i < count; ++i) {
        uint32_t x, y;
        memcpy(&x, &points[i].x, sizeof x);
        memcpy(&y, &points[i].y, sizeof y);
        guest[i][0] = CL(x);
        guest[i][1] = CL(y);
        args[i] = US_TO_SYN68K(guest[i]);
    }
    args[count] = context;
    return cfp_callback(callback, args, count + 1, result);
}

static int outline_segment(const uint32_t* args, int cubic, int* moved, ATSOutlinePoint start, ATSOutlinePoint end,
                           const ATSOutlinePoint* control, uint32_t* result)
{
    if (cubic) {
        if (!*moved) {
            *moved = 1;
            if (!outline_callback(args[2], args[6], &start, 1, result))
                return 0;
            if (*result)
                return 1;
        }
        if (!control)
            return outline_callback(args[3], args[6], &end, 1, result);
        const ATSOutlinePoint points[] = {
            {((control->x + start.x) + control->x) * (1.0f / 3.0f),
             ((control->y + start.y) + control->y) * (1.0f / 3.0f)},
            {((control->x + control->x) + end.x) * (1.0f / 3.0f), ((control->y + control->y) + end.y) * (1.0f / 3.0f)},
            end,
        };
        return outline_callback(args[4], args[6], points, NELEM(points), result);
    }
    const ATSOutlinePoint points[] = {start, end, control ? *control : end};
    return outline_callback(args[control ? 4 : 3], args[6], points, control ? 3 : 2, result);
}

static ATSOutlinePoint translate_point(ATSOutlinePoint point, ATSOutlinePoint origin)
{
    return (ATSOutlinePoint){point.x + origin.x, point.y + origin.y};
}

static int outline_contour(const uint32_t* args, int cubic, ATSOutlinePoint origin, const Byte* mask,
                           const Byte* points, uint32_t count, uint32_t* result)
{
    uint32_t previous = count - 1, i = 0;
    ATSOutlinePoint current = outline_point(points, previous);
    if (!on_curve(mask, previous)) {
        const ATSOutlinePoint first = outline_point(points, 0);
        if (on_curve(mask, 0)) {
            current = first;
            previous = 0;
            i = 1;
        }
        else
            current = midpoint(first, current);
    }
    if (!cubic) {
        if (!outline_callback(args[2], args[6], NULL, 0, result))
            return 0;
        if (*result)
            return 1;
    }
    int moved = 0;
    for (; i < count; ++i) {
        const ATSOutlinePoint point = outline_point(points, i);
        const ATSOutlinePoint start = translate_point(current, origin);
        if (on_curve(mask, i)) {
            current = point;
            if (on_curve(mask, previous) &&
                !outline_segment(args, cubic, &moved, start, translate_point(point, origin), NULL, result))
                return 0;
        }
        else {
            const uint32_t next_index = (i + 1 == count ? 0 : i + 1);
            const ATSOutlinePoint next = outline_point(points, next_index);
            current = on_curve(mask, next_index) ? next : midpoint(point, next);
            const ATSOutlinePoint control = translate_point(point, origin);
            if (!outline_segment(args, cubic, &moved, start, translate_point(current, origin), &control, result))
                return 0;
        }
        if (*result)
            break;
        previous = i;
    }
    return outline_callback(args[5], args[6], NULL, 0, result);
}

static int emit_outline(const uint32_t* args, int cubic, const Byte* path, ATSOutlinePoint bearing, uint32_t* result)
{
    const uint32_t contours = atsu_read(path, 4);
    ATSOutlinePoint minimum = {0, 0};
    int have_point = 0;
    const Byte* row = path + 4;
    for (uint32_t i = 0; i < contours; ++i) {
        const uint32_t count = atsu_read(row, 4);
        const Byte* points = row + 4 + curve_mask_size(count);
        for (uint32_t j = 0; j < count; ++j) {
            const ATSOutlinePoint point = outline_point(points, j);
            if (!have_point || point.x < minimum.x)
                minimum.x = point.x;
            if (!have_point || point.y < minimum.y)
                minimum.y = point.y;
            have_point = 1;
        }
        row = points + count * 8;
    }
    const ATSOutlinePoint origin = {bearing.x - minimum.x, bearing.y + minimum.y};
    row = path + 4;
    for (uint32_t i = 0; i < contours; ++i) {
        const uint32_t count = atsu_read(row, 4);
        const Byte* mask = row + 4;
        const Byte* points = mask + curve_mask_size(count);
        if (count && !outline_contour(args, cubic, origin, mask, points, count, result))
            return 0;
        if (*result)
            break;
        row = points + count * 8;
    }
    return 1;
}

static int glyph_path_callbacks(enum ats_operation op, const char* name, const ROMlib_carbon_reference* ref,
                                ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    const uint16_t glyph = args[1];
    const int cubic = op == ats_get_cubic_paths;
    if (!cubic && glyph == UINT16_MAX)
        return atsu_result(call, noErr);
    if (!args[0] || !args[7])
        return atsu_result(call, paramErr);
    if (!ROMlib_carbon_guest_memory(args[7], 4, 1))
        return 0;
    if (glyph == UINT16_MAX)
        return atsu_write_output(args[7], 0, 4) && atsu_result(call, noErr);
    const Byte* style = ROMlib_carbon_guest_memory(args[0], ats_style_size, 0);
    if (!style || atsu_read(style + 0xb8, 2) != ats_record_signature)
        return atsu_result(call, invalid_style);
    const uint32_t vtable = atsu_read(style, 4);
    if (vtable != atsu_toc_address(ref, 0x89c))
        return atsu_fallback(name, call);
    uint32_t group, strike, status;
    if (!atsu_virtual(style, 0x100, args[0], vtable, &group))
        return 0;
    const uint32_t attributes[] = {group, 0x7374726b, 0}; /* 'strk' */
    if (!atsu_worker(ref, 0x30860, attributes, NELEM(attributes), &strike))
        return 0;
    if (!strike)
        return atsu_result(call, -8796);
    float scale;
    if (!outline_supported(ref, strike, -0xe0, &status) || !curve_scale(ref, &scale))
        return 0;
    if (status)
        return atsu_result(call, status);

    uint32_t index = 0, address = 0;
    int ok = cached_glyph(ref, strike, glyph, -0x2b4, &index, &address, &status);
    int32_t x = 0, y = 0;
    if (ok && !status) {
        const Byte* metric = address ? ROMlib_carbon_guest_memory(address, 16, 0) : NULL;
        if (!metric)
            ok = 0;
        else {
            x = (int32_t)atsu_read(metric + 8, 4);
            y = (int32_t)atsu_read(metric + 12, 4);
        }
    }
    if (index)
        ok = atsu_worker(ref, 0x308c0, &index, 1, NULL) && ok;
    if (!ok || status)
        return ok && atsu_result(call, status);

    index = 0;
    ok = cached_glyph(ref, strike, glyph, -0x2a0, &index, &address, &status);
    uint32_t size = 0, callback_error = 0;
    if (ok && !status)
        ok = curve_path_extent(address, &size);
    if (ok && !status && size) {
        const Byte* source = ROMlib_carbon_guest_memory(address, size, 0);
        Byte* path = source ? malloc(size) : NULL;
        if (path) {
            memcpy(path, source, size);
            curve_path_convert(path, scale);
            const ATSOutlinePoint bearing = {(float)x * scale, (float)y * scale};
            ok = emit_outline(args, cubic, path, bearing, &callback_error);
            free(path);
        }
        else if (!source)
            ok = 0;
        else
            status = memFullErr;
    }
    if (index)
        ok = atsu_worker(ref, 0x308c0, &index, 1, NULL) && ok;
    if (ok && (cubic || callback_error))
        ok = atsu_write_output(args[7], callback_error, 4);
    if (callback_error)
        status = -8796;
    return ok && atsu_result(call, status);
}

/* Version 0 stores a 16-byte header, tagged values, feature pairs and axis pairs. */
static int style_archive_extent(const Byte* data, size_t length, uint32_t* largest_value)
{
    if (length < 16 || atsu_read(data, 4))
        return 0;
    const uint32_t attributes = atsu_read(data + 4, 4), features = atsu_read(data + 8, 4);
    const uint32_t variations = atsu_read(data + 12, 4);
    const Byte* item = data + 16;
    size_t remaining = length - 16;
    *largest_value = 0;
    for (uint32_t i = 0; i < attributes; ++i) {
        if (remaining < 8)
            return 0;
        const uint32_t bytes = atsu_read(item + 4, 4);
        item += 8;
        remaining -= 8;
        if (bytes > remaining)
            return 0;
        *largest_value = MAX(*largest_value, bytes);
        item += bytes;
        remaining -= bytes;
    }
    return (uint64_t)features * 4 + (uint64_t)variations * 8 <= remaining;
}

static int restore_style_archive(const ROMlib_carbon_reference* ref, uint32_t style, const Byte* data, uint32_t largest,
                                 uint32_t* status)
{
    uint32_t scratch = 0;
    if (largest) {
        const uint32_t allocate[] = {largest, 1};
        if (!atsu_worker(ref, 0x6760, allocate, NELEM(allocate), &scratch))
            return 0;
        if (!scratch) {
            *status = memFullErr;
            return 1;
        }
    }
    int ok = atsu_worker(ref, 0xc670, &style, 1, NULL);
    void* value = scratch ? ROMlib_carbon_guest_memory(scratch, largest, 1) : NULL;
    ok = ok && (!scratch || value);
    const uint32_t attributes = atsu_read(data + 4, 4), features = atsu_read(data + 8, 4);
    const uint32_t variations = atsu_read(data + 12, 4);
    const Byte* item = data + 16;
    for (uint32_t i = 0; i < attributes && ok && !*status; ++i) {
        const uint32_t tag = atsu_read(item, 4), bytes = atsu_read(item + 4, 4);
        item += 8;
        if (bytes)
            memcpy(value, item, bytes);
        if (tag == 0x105) {
            const uint32_t assign[] = {style, scratch, bytes};
            ok = atsu_worker(ref, 0xbed4, assign, NELEM(assign), NULL);
        }
        else {
            const uint32_t assign[] = {style, tag, scratch, bytes};
            ok = atsu_worker(ref, 0xb10c, assign, NELEM(assign), status);
        }
        item += bytes;
    }
    for (unsigned kind = 0; kind < 2 && ok && !*status; ++kind) {
        const uint32_t count = kind ? variations : features;
        const unsigned width = kind ? 4 : 2;
        for (uint32_t i = 0; i < count && ok && !*status; ++i) {
            _Alignas(16) Byte pair[8];
            memcpy(pair, item, 2 * width);
            const uint32_t assign[] = {style, 1, US_TO_SYN68K(pair), US_TO_SYN68K(pair + width)};
            ok = atsu_worker(ref, kind ? 0xcccc : 0xcaf4, assign, NELEM(assign), status);
            item += 2 * width;
        }
    }
    return (!scratch || atsu_worker(ref, 0x6138, &scratch, 1, NULL)) && ok;
}

static int style_serialization(enum ats_operation op, const char* name, const ROMlib_carbon_reference* ref,
                               ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    if (!args[0] || !args[1])
        return atsu_result(call, paramErr);
    const Byte* master = ROMlib_carbon_guest_memory(args[1], 4, 0);
    if (!master || !atsu_read(master, 4))
        return atsu_result(call, paramErr);
    const Byte* style = ROMlib_carbon_guest_memory(args[0], ats_style_size, 0);
    if (!style || atsu_read(style + 0xb8, 2) != ats_record_signature)
        return atsu_result(call, invalid_style);
    if (atsu_read(style, 4) != atsu_toc_address(ref, 0x89c))
        return atsu_fallback(name, call);

    const int copy = op == ats_copy_to_handle;
    Handle handle = (Handle)SYN68K_TO_US(args[1]);
    uint32_t status = noErr;
    if (copy) {
        uint32_t estimated;
        if (!atsu_worker(ref, 0xb954, args, 1, &estimated))
            return 0;
        if (estimated < 16 || estimated > INT32_MAX)
            return atsu_result(call, memFullErr);
        SetHandleSize(handle, estimated);
        const OSErr error = MemError();
        if (error)
            return atsu_result(call, error);
    }
    const Size length = GetHandleSize(handle);
    if (length < 16)
        return atsu_result(call, -8800);
    const SignedByte state = HGetState(handle);
    HLock(handle);
    const OSErr lock_error = MemError();
    if (lock_error)
        return atsu_result(call, lock_error);
    const uint32_t address = atsu_read(master, 4);
    Byte* data = address ? ROMlib_carbon_guest_memory(address, length, copy) : NULL;
    int ok = data != NULL;
    if (ok && copy) {
        _Alignas(16) uint32_t cursor = CL(address);
        const uint32_t encode[] = {args[0], US_TO_SYN68K(&cursor)};
        ok = atsu_worker(ref, 0xbb54, encode, NELEM(encode), &status);
    }
    else if (ok) {
        uint32_t largest;
        if (style_archive_extent(data, length, &largest))
            ok = restore_style_archive(ref, args[0], data, largest, &status);
        else
            status = -8800;
    }
    HSetState(handle, state);
    return ok && atsu_result(call, status);
}

static int common_run_attributes(const ROMlib_carbon_reference* ref, const uint32_t* args, uint32_t source)
{
    _Alignas(16) Byte temporary[ats_style_size] = {0};
    const uint32_t address = US_TO_SYN68K(temporary);
    if (!atsu_worker(ref, 0x8970, &address, 1, NULL))
        return 0;
    int ok = 1;
    if (source) {
        const uint32_t assign[] = {address, source};
        ok = atsu_worker(ref, 0x8cc0, assign, NELEM(assign), NULL) && atsu_worker(ref, 0xad58, &address, 1, NULL);
    }
    const uint32_t result[] = {args[3], address};
    ok = ok && atsu_worker(ref, 0x8cc0, result, NELEM(result), NULL);
    return atsu_worker(ref, 0x8abc, &address, 1, NULL) && ok;
}

static int analyze_layout_runs(enum ats_operation op, const ROMlib_carbon_reference* ref, const uint32_t* args,
                               uint32_t* status)
{
    const int matching = op == ats_match_fonts;
    if (args[0] > UINT32_MAX - 0xa3)
        return 0;
    const uint32_t run_list = args[0] + 0xa0;
    const Byte* list = ROMlib_carbon_guest_memory(run_list, 4, 0);
    uint32_t count, selected = 0, source = 0;
    if (!list || !atsu_virtual(list, 4, run_list, 0, &count))
        return 0;
    const uint64_t end = (uint64_t)args[1] + args[2];
    for (uint32_t i = 0; i < count && args[2]; ++i) {
        uint32_t record;
        if (!atsu_virtual(list, 8, run_list, i, &record))
            return 0;
        const Byte* run = record ? ROMlib_carbon_guest_memory(record, 16, 0) : NULL;
        if (!run)
            return 0;
        const uint32_t begin = atsu_read(run + 4, 4);
        const uint64_t run_end = (uint64_t)begin + atsu_read(run + 8, 4);
        const uint64_t first = MAX(begin, args[1]), after = MIN(run_end, end);
        if (first >= after)
            continue;
        source = atsu_read(run + 12, 4);
        if (matching) {
            if (!source) {
                *status = invalid_layout;
                return atsu_write_output(args[4], first, 4) && atsu_write_output(args[5], after, 4);
            }
            return atsu_worker(ref, 0x1741c, args, 6, status);
        }
        if (++selected > 1)
            return atsu_worker(ref, 0x1334c, args, 4, status);
    }
    return matching || common_run_attributes(ref, args, source);
}

static int layout_analysis(enum ats_operation op, const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    uint32_t args[6];
    memcpy(args, call->words, sizeof args);
    if (!args[0])
        return atsu_result(call, paramErr);
    const Byte* layout = atsu_layout(args[0], 0);
    if (!layout || !layout[0xe])
        return atsu_result(call, invalid_layout);
    uint32_t status;
    if (!atsu_worker(ref, 0x1a4d0, args, 1, &status))
        return 0;
    if (status)
        return atsu_result(call, status);
    int ok = 1;
    if (!layout_range(layout, args + 1, args + 2))
        status = invalid_offset;
    else if (op == ats_continuous_attributes && !args[3])
        status = paramErr;
    else if (op == ats_continuous_attributes && !atsu_style(args[3], ref, 1))
        status = invalid_style;
    else
        ok = analyze_layout_runs(op, ref, args, &status);
    return atsu_worker(ref, 0x1a598, args, 1, NULL) && ok && atsu_result(call, status);
}

static float glyph_info_float(const Byte* data)
{
    const uint32_t bits = atsu_read(data, 4);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static uint32_t glyph_coordinate(float value)
{
    if (value >= 0x1p31f)
        return INT32_MAX;
    if (!(value >= -0x1p31f))
        return (uint32_t)INT32_MIN;
    return (uint32_t)(int32_t)value;
}

static int load_drawing_vector(const ROMlib_carbon_reference* ref, uint32_t layout, uint32_t first, uint32_t count,
                               uint32_t vector, uint32_t* status)
{
    ROMlib_carbon_ppc_frame load = {.words = {layout, first, count, 1, 0, vector}};
    if (!layout_glyph_query(ats_load_glyph_vector, ref, &load))
        return 0;
    if (load.words[0] == (uint32_t)invalid_offset) {
        const uint32_t retry[] = {layout, first, UINT32_MAX, 1, 0, vector};
        memcpy(load.words, retry, sizeof retry);
        if (!layout_glyph_query(ats_load_glyph_vector, ref, &load))
            return 0;
    }
    if (load.words[0] == (uint32_t)invalid_offset) {
        const uint32_t retry[] = {layout, 0, UINT32_MAX, 1, 0, vector};
        memcpy(load.words, retry, sizeof retry);
        if (!layout_glyph_query(ats_load_glyph_vector, ref, &load))
            return 0;
    }
    *status = load.words[0];
    return 1;
}

static int fill_drawing_vector(const ROMlib_carbon_reference* ref, uint32_t vector, const Byte* glyphs, uint32_t count,
                               float scale, uint32_t* status)
{
    Byte* header = ROMlib_carbon_guest_memory(vector, 0x54, 1);
    if (!header)
        return 0;
    const uint32_t loaded = atsu_read(header, 4);
    if (loaded < count) {
        if (count - loaded > INT16_MAX) {
            *status = memFullErr;
            return 1;
        }
        const uint32_t grow[] = {count - loaded, vector};
        if (!atsu_worker(ref, 0x30380, grow, NELEM(grow), status))
            return 0;
        if (*status)
            return 1;
    }
    const uint32_t stride = atsu_read(header + 8, 4), address = atsu_read(header + 0xc, 4);
    if (stride < 0x1c || count > UINT32_MAX / stride || !address)
        return 0;
    Byte* data = ROMlib_carbon_guest_memory(address, count * stride, 1);
    if (!data)
        return 0;
    uint32_t used = 0;
    for (uint32_t i = 0; i < count; ++i, glyphs += 0x1c) {
        const uint32_t glyph = atsu_read(glyphs, 2);
        if (glyph == UINT16_MAX)
            continue;
        const uint32_t style = atsu_read(glyphs + 12, 4);
        _Alignas(16) uint16_t index = 0;
        const uint32_t find[] = {style, vector, US_TO_SYN68K(&index)};
        uint32_t found;
        if (!atsu_worker(ref, 0x1e300, find, NELEM(find), &found))
            return 0;
        if (!found && !atsu_worker(ref, 0x1e3b8, find, NELEM(find), status))
            return 0;
        if (*status)
            return 1;
        Byte* record = data + used++ * stride;
        const float advance = glyph_info_float(glyphs + 20);
        atsu_write(record, glyph, 2);
        atsu_write(record + 2, CW(index), 2);
        atsu_write(record + 4, atsu_read(glyphs + 4, 4), 4);
        atsu_write(record + 8, atsu_read(glyphs + 8, 4) * 2, 4);
        atsu_write(record + 12, glyph_coordinate(advance * scale), 4);
        atsu_write(record + 16, atsu_read(glyphs + 24, 2) - glyph_coordinate(advance), 2);
        atsu_write(record + 18, glyph_coordinate((float)(int16_t)atsu_read(glyphs + 26, 2) * scale), 2);
        atsu_write(record + 20, 0, 4);
        atsu_write(record + 24, glyph_coordinate(glyph_info_float(glyphs + 16) * scale), 4);
    }
    atsu_write(header, used, 4);
    return 1;
}

static int draw_glyph_info(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    if (!args[0])
        return atsu_result(call, paramErr);
    const Byte* info = ROMlib_carbon_guest_memory(args[0], 8, 0);
    if (!info)
        return 0;
    const uint32_t count = atsu_read(info + 4, 4);
    if (!count)
        return atsu_result(call, noErr);
    if (count > (UINT32_MAX - 8) / 0x1c)
        return atsu_result(call, paramErr);
    info = ROMlib_carbon_guest_memory(args[0], 8 + count * 0x1c, 0);
    if (!info)
        return 0;
    const uint32_t layout = atsu_read(info, 4), first = atsu_read(info + 16, 4) * 2;
    uint32_t constants, bits;
    if (!cfp_get(atsu_toc_address(ref, -0xc8), &constants) || !cfp_get(constants, &bits))
        return 0;
    float scale, x, y;
    memcpy(&scale, &bits, sizeof scale);
    memcpy(&x, args + 1, sizeof x);
    memcpy(&y, args + 2, sizeof y);
    _Alignas(16) uint32_t origin[] = {CL(glyph_coordinate(x * scale)), CL(glyph_coordinate(y * scale))};

    const uint32_t allocate[] = {0x54, 1};
    uint32_t vector, status = noErr;
    if (!atsu_worker(ref, 0x65b4, allocate, NELEM(allocate), &vector))
        return 0;
    if (!vector)
        return atsu_result(call, memFullErr);
    int ok = load_drawing_vector(ref, layout, first, count, vector, &status);
    if (ok && !status)
        ok = fill_drawing_vector(ref, vector, info + 8, count, scale, &status);
    if (ok && !status) {
        uint32_t used;
        ok = cfp_get(vector, &used);
        if (ok) {
            const uint32_t draw[] = {vector, 0, used, US_TO_SYN68K(origin), 0};
            ok = atsu_worker(ref, 0x6e64, draw, NELEM(draw), &status);
        }
    }
    const int cleared = atsu_worker(ref, 0x303b0, &vector, 1, NULL);
    return atsu_worker(ref, 0x6138, &vector, 1, NULL) && cleared && ok && atsu_result(call, status);
}

static int layout_operation(enum ats_operation op, const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    if (op <= ats_create_layout_handle)
        return create_layout(op, ref, call);
    if (op == ats_fond_to_font)
        return fond_to_font(ref, call);
    if (!args[0] || (op == ats_text_moved && !args[1]) || (op == ats_position_to_offset && !args[3]) ||
        (op == ats_get_text_highlight && !highlight_region_valid(args[5])))
        return atsu_result(call, paramErr);
    const unsigned char* layout = ROMlib_carbon_guest_memory(args[0], ats_layout_header_size, 0);
    if (!layout || atsu_read(layout + 8, 2) != ats_record_signature)
        return atsu_result(call, invalid_layout);
    if (op == ats_dispose_layout) {
        if (atsu_read(layout, 4) != atsu_toc_address(ref, 0xbe4))
            return atsu_fallback("ATSUDisposeTextLayout", call);
        uint32_t ignored;
        return atsu_virtual(layout, 0, args[0], 1, &ignored) && atsu_result(call, noErr);
    }
    if (layout[0x8a])
        return atsu_result(call, invalid_layout);
    if (op == ats_copy_layout) {
        if (!args[1])
            return atsu_result(call, paramErr);
        if (!ROMlib_carbon_guest_memory(args[1], 4, 1))
            return 0;
        uint32_t status, address, ignored;
        if (!atsu_worker(ref, 0x1a4d0, args, 1, &status))
            return 0;
        if (status)
            return atsu_result(call, status);
        return allocate_layout(ref, args[0], &address) && atsu_write_output(args[1], address, 4) &&
               atsu_worker(ref, 0x1a598, args, 1, &ignored) && atsu_result(call, address ? noErr : memFullErr);
    }
    if (op == ats_set_text_pointer || op == ats_set_text_handle)
        return set_text_location(op, ref, call);
    if (op == ats_copy_layout_controls)
        return copy_layout_controls(ref, call);
    if (op < ats_set_layout_controls || op >= ats_set_line_controls) {
        if (!layout[0xe])
            return atsu_result(call, invalid_layout);
    }
    if (op >= ats_set_layout_controls && op <= ats_clear_line_controls)
        return layout_controls(op, ref, layout, call);
    if (op == ats_text_moved)
        return move_layout_text(ref, call);
    if (op >= ats_clear_layout_cache)
        return layout_edit_or_position(op, ref, layout, call);
    if (op >= ats_draw_text)
        return render_layout(op, ref, layout, call);
    if (op == ats_set_run_style && (!args[1] || !atsu_style(args[1], ref, 0)))
        return atsu_result(call, args[1] ? invalid_style : paramErr);
    uint32_t parameters[5] = {args[0], args[1], args[2], args[3], args[4]};
    switch (op) {
    case ats_set_run_style:
        return layout_range(layout, parameters + 2, parameters + 3) ? layout_worker(ref, 0x13204, parameters, 4, call)
                                                                    : atsu_result(call, invalid_offset);
    case ats_get_run_style:
        return layout_range(layout, parameters + 1, NULL) ? layout_worker(ref, 0x121f0, parameters, 5, call)
                                                          : atsu_result(call, invalid_offset);
    case ats_set_soft_break:
        if (!layout_range(layout, parameters + 1, NULL))
            return atsu_result(call, invalid_offset);
        parameters[2] = parameters[1];
        parameters[3] = 0;
        return layout_worker(ref, 0x17b14, parameters, 4, call);
    default:
        return layout_range(layout, parameters + 1, parameters + 2) ? layout_worker(ref, 0x18bf4, parameters, 3, call)
                                                                    : atsu_result(call, invalid_offset);
    }
}

int ROMlib_carbon_atsu(enum ats_operation operation, const char* name, uint32_t reference_offset,
                       ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!atsu_profile(name, reference_offset, &ref))
        return atsu_fallback(name, call);
    if (operation == ats_convert_characters)
        return convert_characters_to_glyphs(&ref, call);
    if (operation == ats_get_curve_paths)
        return glyph_curve_paths(name, &ref, call);
    if (operation == ats_get_quadratic_paths || operation == ats_get_cubic_paths)
        return glyph_path_callbacks(operation, name, &ref, call);
    if (operation == ats_copy_to_handle || operation == ats_paste_from_handle)
        return style_serialization(operation, name, &ref, call);
    if (operation == ats_match_fonts || operation == ats_continuous_attributes)
        return layout_analysis(operation, &ref, call);
    if (operation == ats_draw_glyph_info)
        return draw_glyph_info(&ref, call);
    if (operation == ats_load_glyph_vector || operation == ats_get_glyph_info)
        return layout_glyph_query(operation, &ref, call);
    if (operation == ats_get_glyph_bounds)
        return glyph_bounds(&ref, call);
    if (operation == ats_get_ideal_metrics || operation == ats_get_screen_metrics)
        return glyph_metrics(operation, name, &ref, call);
    if (operation >= ats_baseline_deltas && operation <= ats_get_native_curve)
        return glyph_style_query(operation, name, &ref, call);
    if (operation == ats_font_count || operation == ats_count_font_names)
        return count_catalogue_items(operation, &ref, call);
    if (operation == ats_get_font_ids)
        return get_font_ids(&ref, call);
    if (operation == ats_font_to_fond)
        return font_to_fond(&ref, call);
    if (operation == ats_get_font_name || operation == ats_find_font_name)
        return font_name_query(operation, &ref, call);
    if (operation == ats_find_font)
        return find_font(&ref, call);
    if (operation >= ats_count_font_features)
        return font_table_query(operation, name, &ref, call);
    if (operation >= ats_create_layout)
        return layout_operation(operation, &ref, call);
    if (operation == ats_create_style)
        return create_style(&ref, call);
    if (operation == ats_get_fallbacks || operation == ats_set_fallbacks)
        return font_fallbacks(operation, name, &ref, call);

    const uint32_t* args = call->words;
    if (!args[0] || (operation == ats_clone_style && !args[1]) || (operation == ats_compare_styles && !args[2]) ||
        (operation == ats_get_attributes && (!args[3] || (args[2] && !args[1]))) ||
        ((operation == ats_set_features || operation == ats_set_variations) && (!args[2] || !args[3])))
        return atsu_result(call, paramErr);
    if (operation < ats_get_layout_refcon) {
        const int mutates = operation == ats_set_style_refcon || operation == ats_set_attributes ||
                            operation == ats_clear_style || operation >= ats_clear_attributes;
        unsigned char* style = atsu_style(args[0], &ref, mutates);
        if (!style)
            return atsu_result(call, invalid_style);
        switch (operation) {
        case ats_dispose_style: {
            uint32_t ignored;
            return atsu_virtual(style, 0x108, args[0], 1, &ignored) && atsu_result(call, noErr);
        }
        case ats_get_style_refcon:
            return args[1] ? atsu_write_output(args[1], atsu_read(style + 0xba, 4), 4) && atsu_result(call, noErr)
                           : atsu_result(call, paramErr);
        case ats_set_style_refcon:
            atsu_write(style + 0xba, args[1], 4);
            return atsu_result(call, noErr);
        case ats_style_empty: {
            const int empty = !atsu_read(style + 0xa0, 4) && !style[0xc2] && !atsu_read(style + 0xbe, 4) &&
                              !atsu_read(style + 0xac, 4) && !atsu_read(style + 0xa4, 4);
            return args[1] ? atsu_write_output(args[1], empty, 1) && atsu_result(call, noErr)
                           : atsu_result(call, paramErr);
        }
        case ats_set_attributes:
            return set_attributes(&ref, call);
        case ats_clone_style:
        case ats_compare_styles:
        case ats_copy_attributes:
        case ats_overwrite_attributes:
        case ats_underwrite_attributes:
            return style_copy(operation, &ref, style, call);
        case ats_clear_style:
            return clear_style(&ref, style, call);
        case ats_get_attribute: {
            uint32_t status;
            return atsu_worker(&ref, 0xbb40, args, 5, &status) && atsu_result(call, status);
        }
        case ats_get_attributes:
            return style_get_attributes(&ref, call);
        case ats_clear_attributes:
        case ats_set_features:
        case ats_clear_features:
        case ats_set_variations:
        case ats_clear_variations:
            return style_change_array(operation, &ref, style, call);
        default:
            return style_array(operation, style, call);
        }
    }

    unsigned char* layout = atsu_layout(args[0], operation == ats_set_layout_refcon || operation == ats_set_matching);
    if (!layout || (operation >= ats_get_text_location && !layout[0xe]))
        return atsu_result(call, invalid_layout);
    switch (operation) {
    case ats_get_layout_refcon:
        return args[1] ? atsu_write_output(args[1], atsu_read(layout + 0xa, 4), 4) && atsu_result(call, noErr)
                       : atsu_result(call, paramErr);
    case ats_set_layout_refcon:
        atsu_write(layout + 0xa, args[1], 4);
        return atsu_result(call, noErr);
    case ats_get_text_location:
        return atsu_write_output(args[1], atsu_read(layout + 4, 4), 4) && atsu_write_output(args[2], layout[0x10], 1) &&
               atsu_write_output(args[3], atsu_read(layout + 0x12, 4), 4) &&
               atsu_write_output(args[4], atsu_read(layout + 0x16, 4), 4) &&
               atsu_write_output(args[5], atsu_read(layout + 0x1a, 4), 4) && atsu_result(call, noErr);
    case ats_get_matching:
        return atsu_write_output(args[1], layout[0xf], 1) && atsu_result(call, noErr);
    case ats_set_matching:
        if (layout[0xf] != (uint8_t)args[1] && atsu_read(layout + 0x90, 4))
            return atsu_fallback(name, call); /* Existing shaped line cache. */
        layout[0xf] = (uint8_t)args[1];
        return atsu_result(call, noErr);
    default:
        return soft_breaks(layout, call);
    }
}
