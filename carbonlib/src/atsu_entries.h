/*+===================================================================
  File:      atsu_entries.h

  Summary:   Defines native ATSUI export entry points.

  Classes:   None.

  Functions: Private wrappers for the export registry.

  Origin:    CarbonLib 1.4 ATSUnicodeLib.CL and ATSGlue.CL fragments, and
             Universal Interfaces 3.4.2 ATSUnicode.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_ATSU_ENTRIES_H
#define ROMAW_CARBON_ATSU_ENTRIES_H

#include "atsu.h"

#define CARBON_ATSU(name, operation, offset)                       \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)        \
    {                                                              \
        return ROMlib_carbon_atsu(operation, #name, offset, call); \
    }

CARBON_ATSU(ATSUCreateStyle, ats_create_style, 0x2b48)
CARBON_ATSU(ATSUDisposeStyle, ats_dispose_style, 0x2cc8)
CARBON_ATSU(ATSUGetStyleRefCon, ats_get_style_refcon, 0x2ddc)
CARBON_ATSU(ATSUSetStyleRefCon, ats_set_style_refcon, 0x2d54)
CARBON_ATSU(ATSUStyleIsEmpty, ats_style_empty, 0x327c)
CARBON_ATSU(ATSUSetAttributes, ats_set_attributes, 0x33dc)
CARBON_ATSU(ATSUGetFontFeature, ats_get_feature, 0x370c)
CARBON_ATSU(ATSUGetAllFontFeatures, ats_get_all_features, 0x3794)
CARBON_ATSU(ATSUGetFontVariationValue, ats_get_variation, 0x3954)
CARBON_ATSU(ATSUGetAllFontVariations, ats_get_all_variations, 0x39e4)
CARBON_ATSU(ATSUGetTextLayoutRefCon, ats_get_layout_refcon, 0xb8c)
CARBON_ATSU(ATSUSetTextLayoutRefCon, ats_set_layout_refcon, 0xb04)
CARBON_ATSU(ATSUGetTextLocation, ats_get_text_location, 0xcb4)
CARBON_ATSU(ATSUGetTransientFontMatching, ats_get_matching, 0x2a9c)
CARBON_ATSU(ATSUSetTransientFontMatching, ats_set_matching, 0x29f8)
CARBON_ATSU(ATSUGetSoftLineBreaks, ats_get_soft_breaks, 0x2750)
CARBON_ATSU(ATSUGetFontFallbacks, ats_get_fallbacks, 0x4874)
CARBON_ATSU(ATSUSetFontFallbacks, ats_set_fallbacks, 0x480c)
CARBON_ATSU(ATSUCreateTextLayout, ats_create_layout, 0x4cc)
CARBON_ATSU(ATSUCreateTextLayoutWithTextPtr, ats_create_layout_pointer, 0x624)
CARBON_ATSU(ATSUCreateTextLayoutWithTextHandle, ats_create_layout_handle, 0x8a4)
CARBON_ATSU(ATSUCreateAndCopyTextLayout, ats_copy_layout, 0x738)
CARBON_ATSU(ATSUDisposeTextLayout, ats_dispose_layout, 0x9d0)
CARBON_ATSU(ATSUSetTextPointerLocation, ats_set_text_pointer, 0x56c)
CARBON_ATSU(ATSUSetTextHandleLocation, ats_set_text_handle, 0x7e0)
CARBON_ATSU(ATSUSetRunStyle, ats_set_run_style, 0x15c)
CARBON_ATSU(ATSUGetRunStyle, ats_get_run_style, 0x17a8)
CARBON_ATSU(ATSUSetLayoutControls, ats_set_layout_controls, 0x1090)
CARBON_ATSU(ATSUGetLayoutControl, ats_get_layout_control, 0x1124)
CARBON_ATSU(ATSUGetAllLayoutControls, ats_get_layout_controls, 0x11b8)
CARBON_ATSU(ATSUClearLayoutControls, ats_clear_layout_controls, 0x1244)
CARBON_ATSU(ATSUSetLineControls, ats_set_line_controls, 0x1430)
CARBON_ATSU(ATSUGetLineControl, ats_get_line_control, 0x1514)
CARBON_ATSU(ATSUGetAllLineControls, ats_get_line_controls, 0x15f8)
CARBON_ATSU(ATSUClearLineControls, ats_clear_line_controls, 0x16d4)
CARBON_ATSU(ATSUSetSoftLineBreak, ats_set_soft_break, 0x267c)
CARBON_ATSU(ATSUClearSoftLineBreaks, ats_clear_soft_breaks, 0x2838)
CARBON_ATSU(ATSUDrawText, ats_draw_text, 0x199c)
CARBON_ATSU(ATSUGetTextHighlight, ats_get_text_highlight, 0x1e6c)
CARBON_ATSU(ATSUMeasureText, ats_measure_text, 0x1a90)
CARBON_ATSU(ATSUMeasureTextImage, ats_measure_text_image, 0x1b80)
CARBON_ATSU(ATSUFONDtoFontID, ats_fond_to_font, 0x4acc)
CARBON_ATSU(ATSUClearLayoutCache, ats_clear_layout_cache, 0xa2c)
CARBON_ATSU(ATSUIdle, ats_idle, 0xc20)
CARBON_ATSU(ATSUTextDeleted, ats_text_deleted, 0xd68)
CARBON_ATSU(ATSUTextInserted, ats_text_inserted, 0xe40)
CARBON_ATSU(ATSUCopyLineControls, ats_copy_line_controls, 0x12c8)
CARBON_ATSU(ATSUHighlightText, ats_highlight, 0x1c84)
CARBON_ATSU(ATSUUnhighlightText, ats_unhighlight, 0x1d78)
CARBON_ATSU(ATSUPositionToOffset, ats_position_to_offset, 0x1fb0)
CARBON_ATSU(ATSUOffsetToPosition, ats_offset_to_position, 0x20ac)
CARBON_ATSU(ATSUNextCursorPosition, ats_next_cursor, 0x2190)
CARBON_ATSU(ATSUPreviousCursorPosition, ats_previous_cursor, 0x2268)
CARBON_ATSU(ATSURightwardCursorPosition, ats_right_cursor, 0x2340)
CARBON_ATSU(ATSULeftwardCursorPosition, ats_left_cursor, 0x2470)
CARBON_ATSU(ATSUBreakLine, ats_break_line, 0x25a0)
CARBON_ATSU(ATSULoadGlyphVector, ats_load_glyph_vector, 0x3ca0)
CARBON_ATSU(ATSUConvertCharToGlyphs, ats_convert_characters, 0x1ca2c)
CARBON_ATSU(ATSUGlyphGetCurvePaths, ats_get_curve_paths, 0x1d9e4)
CARBON_ATSU(ATSUGlyphGetQuadraticPaths, ats_get_quadratic_paths, 0x1cf2c)
CARBON_ATSU(ATSUGlyphGetCubicPaths, ats_get_cubic_paths, 0x1d89c)
CARBON_ATSU(ATSUCopyToHandle, ats_copy_to_handle, 0x3af4)
CARBON_ATSU(ATSUPasteFromHandle, ats_paste_from_handle, 0x3bd8)
CARBON_ATSU(ATSUGetContinuousAttributes, ats_continuous_attributes, 0x1884)
CARBON_ATSU(ATSUMatchFontsToText, ats_match_fonts, 0x2910)
CARBON_ATSU(ATSUDrawGlyphInfo, ats_draw_glyph_info, 0x1e46c)
CARBON_ATSU(ATSUGetGlyphInfo, ats_get_glyph_info, 0x3d98)
CARBON_ATSU(ATSUGetGlyphBounds, ats_get_glyph_bounds, 0x3a8)
CARBON_ATSU(ATSUGlyphGetIdealMetrics, ats_get_ideal_metrics, 0x1e1f0)
CARBON_ATSU(ATSUGlyphGetScreenMetrics, ats_get_screen_metrics, 0x4160)
CARBON_ATSU(ATSUCalculateBaselineDeltas, ats_baseline_deltas, 0x3320)
CARBON_ATSU(ATSUGetStyleGroup, ats_get_style_group, 0x3eac)
CARBON_ATSU(ATSUDisposeStyleGroup, ats_dispose_style_group, 0x3f64)
CARBON_ATSU(ATSUGetStyleIndex, ats_get_style_index, 0x1c8ec)
CARBON_ATSU(ATSUGetNativeCurveType, ats_get_native_curve, 0x1cc70)
CARBON_ATSU(ATSUFontCount, ats_font_count, 0x48bc)
CARBON_ATSU(ATSUCountFontNames, ats_count_font_names, 0x5cb0)
CARBON_ATSU(ATSUGetFontIDs, ats_get_font_ids, 0x4914)
CARBON_ATSU(ATSUFontIDtoFOND, ats_font_to_fond, 0x4c00)
CARBON_ATSU(ATSUGetIndFontName, ats_get_font_name, 0x5d38)
CARBON_ATSU(ATSUFindFontName, ats_find_font_name, 0x5e58)
CARBON_ATSU(ATSUFindFontFromName, ats_find_font, 0x5f78)
CARBON_ATSU(ATSUCountFontFeatureTypes, ats_count_font_features, 0x4d64)
CARBON_ATSU(ATSUGetFontFeatureTypes, ats_get_font_features, 0x4e20)
CARBON_ATSU(ATSUCountFontFeatureSelectors, ats_count_font_selectors, 0x4f50)
CARBON_ATSU(ATSUGetFontFeatureSelectors, ats_get_font_selectors, 0x5038)
CARBON_ATSU(ATSUGetFontFeatureNameCode, ats_font_feature_name, 0x52b8)
CARBON_ATSU(ATSUCountFontTracking, ats_count_font_tracking, 0x5430)
CARBON_ATSU(ATSUGetIndFontTracking, ats_get_font_tracking, 0x5534)
CARBON_ATSU(ATSUCountFontVariations, ats_count_font_variations, 0x568c)
CARBON_ATSU(ATSUGetIndFontVariation, ats_get_font_variation, 0x5748)
CARBON_ATSU(ATSUGetFontVariationNameCode, ats_font_variation_name, 0x5890)
CARBON_ATSU(ATSUCountFontInstances, ats_count_font_instances, 0x5980)
CARBON_ATSU(ATSUGetFontInstance, ats_get_font_instance, 0x5a3c)
CARBON_ATSU(ATSUGetFontInstanceNameCode, ats_font_instance_name, 0x5bcc)

CARBON_ATSU(ATSUCreateAndCopyStyle, ats_clone_style, 0x2c20)
CARBON_ATSU(ATSUCompareStyles, ats_compare_styles, 0x2e74)
CARBON_ATSU(ATSUCopyAttributes, ats_copy_attributes, 0x2fac)
CARBON_ATSU(ATSUOverwriteAttributes, ats_overwrite_attributes, 0x3064)
CARBON_ATSU(ATSUUnderwriteAttributes, ats_underwrite_attributes, 0x311c)
CARBON_ATSU(ATSUClearStyle, ats_clear_style, 0x31d4)
CARBON_ATSU(ATSUGetAttribute, ats_get_attribute, 0x34a4)
CARBON_ATSU(ATSUGetAllAttributes, ats_get_attributes, 0x3534)
CARBON_ATSU(ATSUClearAttributes, ats_clear_attributes, 0x35e4)
CARBON_ATSU(ATSUSetFontFeatures, ats_set_features, 0x3664)
CARBON_ATSU(ATSUClearFontFeatures, ats_clear_features, 0x3824)
CARBON_ATSU(ATSUSetVariations, ats_set_variations, 0x38ac)
CARBON_ATSU(ATSUClearFontVariations, ats_clear_variations, 0x3a74)
CARBON_ATSU(ATSUCopyLayoutControls, ats_copy_layout_controls, 0xfd4)
CARBON_ATSU(ATSUTextMoved, ats_text_moved, 0xf1c)

#undef CARBON_ATSU

#endif
