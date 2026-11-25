/*+===================================================================
  File:      text_conversion.h

  Summary:   Declares native TextCommon and UnicodeConverter operations.

  Classes:   None.

  Functions: Declares the text conversion dispatcher and collation key comparator.

  Origin:    Mac OS 9.2.1 Text Encoding Converter and Universal Interfaces 3.4.2.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_TEXT_CONVERSION_H
#define ROMAW_CARBON_TEXT_CONVERSION_H

#include "internal.h"

enum {
    text_make_encoding,
    text_get_base,
    text_get_variant,
    text_get_format,
    text_resolve_default,
    text_nearest_mac,
    text_encoding_name,
    text_map_script_iso,
    text_revert_script,
    text_upgrade_script,
    text_application_encoding,
    text_get_property,
    text_use_property,
    text_load_property,
    text_unload_property,
    text_reset_text,
    text_reset_unicode,
    text_reset_run,
    text_dispose_text,
    text_dispose_unicode,
    text_dispose_run,
    text_create_text,
    text_create_unicode,
    text_create_text_by_encoding,
    text_create_unicode_by_encoding,
    text_set_fallback,
    text_from_pascal,
    text_to_pascal,
    text_to_unicode,
    text_from_unicode,
    text_change_text,
    text_change_unicode,
    text_truncate_text,
    text_truncate_unicode,
    text_lock_mapping,
    text_lock_text,
    text_lock_unicode,
    text_count_mappings,
    text_query_mappings,
    text_valid_conversion_length,
    text_create_run,
    text_create_run_by_encoding,
    text_create_run_by_script,
    text_from_unicode_run,
    text_from_unicode_script_run,
};

int ROMlib_carbon_text_conversion(const char* name, uint32_t offset, int operation, ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_compare_collation_keys(ROMlib_carbon_ppc_frame* call);

#ifdef CARBON_TEXT_CONVERSION_EXPORTS
#define CARBON_TEXT(name, library, offset, operation)                         \
    static int carbon_text_##name(ROMlib_carbon_ppc_frame* call)              \
    {                                                                         \
        return ROMlib_carbon_text_conversion(#name, offset, operation, call); \
    }

CARBON_TEXT(CreateTextEncoding, TextCommon, 0x22b4, text_make_encoding)
CARBON_TEXT(GetTextEncodingBase, TextCommon, 0x22cc, text_get_base)
CARBON_TEXT(GetTextEncodingVariant, TextCommon, 0x22d4, text_get_variant)
CARBON_TEXT(GetTextEncodingFormat, TextCommon, 0x22dc, text_get_format)
CARBON_TEXT(ResolveDefaultTextEncoding, TextCommon, 0x232c, text_resolve_default)
CARBON_TEXT(NearestMacTextEncodings, TextCommon, 0x2f74, text_nearest_mac)
CARBON_TEXT(GetTextEncodingName, TextCommon, 0x25bc, text_encoding_name)
CARBON_TEXT(MapScriptInfoAndISOCodes, TextCommon, 0x4028, text_map_script_iso)
CARBON_TEXT(RevertTextEncodingToScriptInfo, TextCommon, 0x3bd4, text_revert_script)
CARBON_TEXT(UpgradeScriptInfoToTextEncoding, TextCommon, 0x3580, text_upgrade_script)
CARBON_TEXT(GetApplicationTextEncoding, CarbonLib, 0x56174, text_application_encoding)
CARBON_TEXT(UCGetCharProperty, TextCommon, 0x28b8, text_get_property)
CARBON_TEXT(UCUseCharPropsTablePriv, TextCommon, 0x2868, text_use_property)
CARBON_TEXT(UCLoadCharPropsTablePriv, TextCommon, 0x29d8, text_load_property)
CARBON_TEXT(UCUnloadCharPropsTablePriv, TextCommon, 0x2a98, text_unload_property)
CARBON_TEXT(ResetTextToUnicodeInfo, UnicodeConverter, 0xa60, text_reset_text)
CARBON_TEXT(ResetUnicodeToTextInfo, UnicodeConverter, 0xdbc, text_reset_unicode)
CARBON_TEXT(ResetUnicodeToTextRunInfo, UnicodeConverter, 0x773c, text_reset_run)
CARBON_TEXT(DisposeTextToUnicodeInfo, UnicodeConverter, 0x11f0, text_dispose_text)
CARBON_TEXT(DisposeUnicodeToTextRunInfo, UnicodeConverter, 0x7b78, text_dispose_run)
CARBON_TEXT(DisposeUnicodeToTextInfo, UnicodeConverter, 0x12a0, text_dispose_unicode)
CARBON_TEXT(CreateTextToUnicodeInfo, UnicodeConverter, 0xa8c, text_create_text)
CARBON_TEXT(CreateUnicodeToTextInfo, UnicodeConverter, 0xe14, text_create_unicode)
CARBON_TEXT(CreateTextToUnicodeInfoByEncoding, UnicodeConverter, 0x1170, text_create_text_by_encoding)
CARBON_TEXT(CreateUnicodeToTextInfoByEncoding, UnicodeConverter, 0x11b0, text_create_unicode_by_encoding)
CARBON_TEXT(SetFallbackUnicodeToText, UnicodeConverter, 0x1910, text_set_fallback)
CARBON_TEXT(SetFallbackUnicodeToTextRun, UnicodeConverter, 0x1930, text_set_fallback)
CARBON_TEXT(ConvertFromPStringToUnicode, UnicodeConverter, 0x3864, text_from_pascal)
CARBON_TEXT(ConvertFromUnicodeToPString, UnicodeConverter, 0x22d0, text_to_pascal)
CARBON_TEXT(ConvertFromTextToUnicode, UnicodeConverter, 0x3448, text_to_unicode)

CARBON_TEXT(ConvertFromUnicodeToText, UnicodeConverter, 0x1b48, text_from_unicode)

CARBON_TEXT(ChangeTextToUnicodeInfo, UnicodeConverter, 0x13c8, text_change_text)
CARBON_TEXT(ChangeUnicodeToTextInfo, UnicodeConverter, 0x162c, text_change_unicode)
CARBON_TEXT(TruncateForTextToUnicode, UnicodeConverter, 0x38bc, text_truncate_text)
CARBON_TEXT(TruncateForUnicodeToText, UnicodeConverter, 0x233c, text_truncate_unicode)
CARBON_TEXT(LockMappingTable, UnicodeConverter, 0x930, text_lock_mapping)
CARBON_TEXT(LockTextToUnicodeInfo, UnicodeConverter, 0x750, text_lock_text)
CARBON_TEXT(LockUnicodeToTextInfo, UnicodeConverter, 0x850, text_lock_unicode)
CARBON_TEXT(CountUnicodeMappings, UnicodeConverter, 0x707c, text_count_mappings)
CARBON_TEXT(QueryUnicodeMappings, UnicodeConverter, 0x731c, text_query_mappings)
CARBON_TEXT(UnicodeValidConversionLength, UnicodeConverter, 0x6fd4, text_valid_conversion_length)
CARBON_TEXT(CreateUnicodeToTextRunInfo, UnicodeConverter, 0x7c48, text_create_run)
CARBON_TEXT(CreateUnicodeToTextRunInfoByEncoding, UnicodeConverter, 0x8350, text_create_run_by_encoding)
CARBON_TEXT(CreateUnicodeToTextRunInfoByScriptCode, UnicodeConverter, 0x8114, text_create_run_by_script)
CARBON_TEXT(ConvertFromUnicodeToTextRun, UnicodeConverter, 0x66a4, text_from_unicode_run)
CARBON_TEXT(ConvertFromUnicodeToScriptCodeRun, UnicodeConverter, 0x6a14, text_from_unicode_script_run)

#undef CARBON_TEXT
#endif

#endif
