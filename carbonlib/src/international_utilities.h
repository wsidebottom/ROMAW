/*+===================================================================
  File:      international_utilities.h

  Summary:   Adapts Carbon international text and date APIs to ROMAW's
             International Utilities.

  Classes:   None.

  Functions: Private frame adapters for the Carbon export registry.

  Origin:    CarbonLib 1.4 and Universal Interfaces 3.4.2 StringCompare.h,
             DateTimeUtils.h and IntlResources.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_INTERNATIONAL_UTILITIES_H
#define ROMAW_CARBON_INTERNATIONAL_UTILITIES_H

#include "carbonlib.h"
#include "cf_batch.h"
#include "rsys/mixed_mode.h"

/* Pack6 takes a two-byte selector after the Pascal arguments. */
enum {
    intl_pack6_vector = 0x15b4,
    intl_pack6_pascal = kStackDispatchedPascalStackBased | (2 << 6),
};

static int carbon_classic_string_order(ROMlib_carbon_ppc_frame* call, int equality)
{
    ROMlib_carbon_ppc_frame arguments = *call;
    uint32_t lengths = 0;
    for (unsigned i = 0; i < 2; ++i) {
        const uint32_t address = call->words[i];
        const uint8_t* length = ROMlib_carbon_guest_memory(address, 1, 0);
        if (!length || address == UINT32_MAX || (*length && !ROMlib_carbon_guest_memory(address + 1, *length, 0)))
            return 0;
        arguments.words[i] = address + 1;
        lengths = (lengths << 16) | *length;
    }
    arguments.words[2] = lengths;
    const uint32_t vector = equality ? 0x04f0 : 0x0540;
    const uint32_t procedure = equality ? 0xef33032 : 0xef33022;
    const uint32_t instruction =
        (equality ? 0xa03c : 0xa050) | ((uint8_t)call->words[2] ? 0x400 : 0) | ((uint8_t)call->words[3] ? 0 : 0x200);
    uint32_t result;
    if (!ROMlib_carbon_call_register_vector(vector, procedure, instruction, 0, &arguments, &result))
        return 0;
    call->words[0] = equality ? !result : (int16_t)result;
    return 1;
}

static int carbon_EqualString(ROMlib_carbon_ppc_frame* call)
{
    return carbon_classic_string_order(call, 1);
}

static int carbon_RelString(ROMlib_carbon_ppc_frame* call)
{
    return carbon_classic_string_order(call, 0);
}

static int carbon_transform_text(ROMlib_carbon_ppc_frame* call, uint32_t transform, const char* name)
{
    const int16_t script = (int16_t)call->words[2];
    if (script != 0 && script != -1) {
        ROMlib_carbon_reference saved;
        if (ROMlib_carbon_load_reference("InterfaceLib", name, &saved))
            return ROMlib_carbon_invoke_frame(saved.function, call);
    }
    ROMlib_carbon_ppc_frame args = *call;
    /* The ScriptUtil dispatcher pops a four-byte selector followed by the
       two-byte text operation before it consumes the public arguments. */
    args.words[3] = transform;
    uint32_t ignored;
    return ROMlib_carbon_call_toolbox_vector(0x10d4, 0xabce, 0x800affb6, &args, &ignored);
}

#define CARBON_TEXT_TRANSFORM(name, operation)                \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)   \
    {                                                         \
        return carbon_transform_text(call, operation, #name); \
    }

CARBON_TEXT_TRANSFORM(LowercaseText, 0x0000)
CARBON_TEXT_TRANSFORM(StripDiacritics, 0x0200)
CARBON_TEXT_TRANSFORM(UppercaseText, 0x0400)
CARBON_TEXT_TRANSFORM(UppercaseStripDiacritics, 0x0600)

#undef CARBON_TEXT_TRANSFORM

static int carbon_intl_access(ROMlib_carbon_ppc_frame* call, uint32_t selector, uint32_t proc_info, int pascal_string)
{
    ROMlib_carbon_ppc_frame arguments = *call;
    if (pascal_string) {
        /* The Pascal-string APIs share the explicit-length Pack6 workers. */
        for (unsigned index = 0; index < 2; ++index) {
            const uint32_t address = call->words[index];
            if (address == UINT32_MAX)
                return ROMlib_carbon_report_failure("Carbon Pascal string address overflow");
            const uint8_t* length = ROMlib_carbon_guest_memory(address, 1, 0);
            if (!length || (*length && !ROMlib_carbon_guest_memory(address + 1, *length, 0)))
                return 0;
            arguments.words[index] = address + 1;
            arguments.words[index + 2] = *length;
        }
        const unsigned trailing = selector == 0x22 ? 4 : 1;
        for (unsigned index = 0; index < trailing; ++index)
            arguments.words[4 + index] = call->words[2 + index];
    }
    return ROMlib_carbon_call_toolbox_vector(intl_pack6_vector, proc_info, selector, &arguments, &call->words[0]);
}

#define CARBON_INTL(name, selector, widths, result_size, pascal_string)                        \
    static int carbon_intl_##name(ROMlib_carbon_ppc_frame* call)                               \
    {                                                                                          \
        const uint32_t procedure = intl_pack6_pascal | ((widths) << 8) | ((result_size) << 4); \
        if (!carbon_intl_access(call, selector, procedure, pascal_string))                     \
            return 0;                                                                          \
        if ((result_size) == 2)                                                                \
            call->words[0] = (int16_t)call->words[0];                                          \
        return 1;                                                                              \
    }

/* Parameter widths use the Mixed Mode two-bit encoding, least significant first. */
CARBON_INTL(DateString, 0x0e, 0xf7, 0, 0)
CARBON_INTL(TimeString, 0x10, 0xf7, 0, 0)
CARBON_INTL(LongDateString, 0x14, 0xf7, 0, 0)
CARBON_INTL(LongTimeString, 0x16, 0xf7, 0, 0)
CARBON_INTL(CompareString, 0x1a, 0x3af, 2, 1)
CARBON_INTL(IdenticalString, 0x1c, 0x3af, 2, 1)
CARBON_INTL(StringOrder, 0x22, 0xaaaf, 2, 1)
CARBON_INTL(CompareText, 0x1a, 0x3af, 2, 0)
CARBON_INTL(IdenticalText, 0x1c, 0x3af, 2, 0)
CARBON_INTL(TextOrder, 0x22, 0xaaaf, 2, 0)
CARBON_INTL(LanguageOrder, 0x20, 0xa, 2, 0)
CARBON_INTL(ScriptOrder, 0x1e, 0xa, 2, 0)
CARBON_INTL(GetIntlResource, 0x06, 0x2, 3, 0)
CARBON_INTL(GetIntlResourceTable, 0x24, 0x3fa, 0, 0)

#undef CARBON_INTL

static int carbon_text_converter(const ROMlib_carbon_reference* ref, uint32_t slot, uint32_t* converter,
                                 uint32_t* status)
{
    if (!cfp_get(slot, converter))
        return 0;
    if (*converter)
        return cfp_private(ref, 0xb6ab0, converter, 1, status);

    uint32_t encoding, previous_zone, application_zone;
    if (!cfp_private(ref, 0xb4368, NULL, 0, &encoding) || !cfp_private(ref, 0xb53b8, NULL, 0, &previous_zone) ||
        !cfp_private(ref, 0xb57f0, NULL, 0, &application_zone) ||
        !cfp_private(ref, 0xb5448, &application_zone, 1, NULL))
        return 0;
    _Alignas(4) uint32_t encoded = CL(encoding);
    const uint32_t create[] = {UINT32_C(0x80000000), US_TO_SYN68K(&encoded), slot};
    const int created = cfp_private(ref, 0xb6a98, create, NELEM(create), status);
    const int restored = cfp_private(ref, 0xb5448, &previous_zone, 1, NULL);
    return created && restored && (*status || cfp_get(slot, converter));
}

static int carbon_text_in_system_encoding(ROMlib_carbon_ppc_frame* call, uint32_t mapping_error)
{
    enum { roman = 0, tec_no_output = -8750, tec_output_full = -8785 };

    const uint32_t output = call->words[1], capacity = call->words[2];
    const uint32_t bytes_written = call->words[3], result_encoding = call->words[4];
    const uint32_t encoding = ROMlib_cfstring_system_encoding();
    if (mapping_error && encoding != roman) {
        call->words[0] = mapping_error;
        return 1;
    }
    void* object = ROMlib_carbon_guest_memory(call->words[0], 1, 0);
    if (!object)
        return 0;
    const LONGINT length = ROMlib_carbon_cf_string_length(object);
    if (capacity > INT32_MAX || length < 0 || (mapping_error && !output)) {
        call->words[0] = paramErr;
        return 1;
    }

    /* The installed Unicode TOC can lack a default-variant Roman map.
       Check the entire string without substitution before writing any bytes. */
    _Alignas(4) uint32_t used = 0;
    const uint32_t loss_byte = mapping_error ? 0 : '?';
    if (mapping_error &&
        ROMlib_carbon_cf_string_bytes(object, 0, length, encoding, loss_byte, 0, NULL, 0, &used) != length) {
        call->words[0] = mapping_error;
        return 1;
    }
    void* buffer = output && capacity ? ROMlib_carbon_guest_memory(output, capacity, 1) : NULL;
    if (output && capacity && !buffer)
        return 0;
    used = 0;
    const LONGINT count = output && !capacity ? 0
                                              : ROMlib_carbon_cf_string_bytes(object, 0, length, encoding, loss_byte, 0,
                                                                              buffer, capacity, &used);
    call->words[0] = noErr;
    if (count != length)
        call->words[0] = mapping_error && !count ? tec_no_output : tec_output_full;
    return (!bytes_written || cfp_write(bytes_written, CL(used))) &&
           (count != length || !result_encoding || cfp_write(result_encoding, encoding));
}

static int carbon_GetTextAndEncodingFromCFString(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", "GetTextAndEncodingFromCFString", &ref))
        return 0;
    if (ref.code_offset != 0x33518 || ref.code_size != 0xb8fb4 || !ref.toc || ref.toc_offset != 0x2f78 ||
        ref.toc_size != 0x10f0c)
        return ROMlib_carbon_invoke_frame(ref.function, call);
    const uint32_t string = call->words[0], output = call->words[1], capacity = call->words[2],
                   bytes_written = call->words[3], result_encoding = call->words[4];
    if ((bytes_written && !ROMlib_carbon_guest_memory(bytes_written, 4, 1)) ||
        (result_encoding && !ROMlib_carbon_guest_memory(result_encoding, 4, 1)))
        return 0;
    if (!string) {
        uint32_t system_encoding;
        if (result_encoding &&
            (!cfp_private(&ref, 0xb4368, NULL, 0, &system_encoding) || !cfp_write(result_encoding, system_encoding)))
            return 0;
        call->words[0] = noErr;
        return !bytes_written || cfp_write(bytes_written, 0);
    }

    const uint32_t slot = cfp_toc(&ref, (uint32_t)-0xf78);
    uint32_t converter, status, length, characters;
    if (!slot || !carbon_text_converter(&ref, slot, &converter, &status))
        return 0;
    if (status == (uint32_t)-8738) /* kTextUnsupportedEncodingErr */
        return carbon_text_in_system_encoding(call, status);
    if (status) {
        call->words[0] = status;
        return 1;
    }
    if (!converter)
        return carbon_text_in_system_encoding(call, 0);
    if (!cfp_private(&ref, 0xb6288, &string, 1, &length) || !cfp_private(&ref, 0xb6ac8, &string, 1, &characters))
        return 0;
    if (length > INT32_MAX / 2) {
        call->words[0] = (uint32_t)memFullErr;
        return 1;
    }
    const uint32_t unicode_bytes = 2 * length;
    const bool needs_copy = !characters;
    if (needs_copy) {
        if (!cfp_private(&ref, 0xb2178, &unicode_bytes, 1, &characters))
            return 0;
        if (!characters) {
            call->words[0] = (uint32_t)memFullErr;
            return 1;
        }
    }

    const uint32_t copy[] = {string, 0, length, characters};
    const uint32_t fallback[] = {converter, cfp_toc(&ref, (uint32_t)-0xf7c), 2, 0};
    _Alignas(16) uint32_t produced[4] = {0};
    const uint32_t convert[] = {converter,
                                unicode_bytes,
                                characters,
                                0x21,
                                0,
                                0,
                                0,
                                0,
                                capacity,
                                US_TO_SYN68K(&produced[0]),
                                bytes_written,
                                output,
                                1,
                                US_TO_SYN68K(&produced[1]),
                                US_TO_SYN68K(&produced[2])};
    const int converted = (!needs_copy || cfp_private(&ref, 0xb62a0, copy, NELEM(copy), NULL)) &&
                          cfp_private(&ref, 0xb6ae0, fallback, NELEM(fallback), NULL) &&
                          cfp_private(&ref, 0xb6af8, convert, NELEM(convert), &status);
    const int released = !needs_copy || cfp_private(&ref, 0xb21a8, &characters, 1, NULL);
    if (!converted || !released)
        return 0;
    if ((int32_t)status == -8783)
        status = noErr; /* The single run is usable when the source contains additional encodings. */
    call->words[0] = status;
    return status || !result_encoding || cfp_write(result_encoding, CL(produced[3]));
}

#endif
