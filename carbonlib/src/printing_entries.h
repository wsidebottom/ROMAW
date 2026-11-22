/*+===================================================================
  File:      printing_entries.h

  Summary:   Printing Manager ticket operations and CarbonLib 1.4 ABI data.

  Classes:   None.

  Functions: Declares the native Printing Manager entry points when included
             with the declaration macro.

  Origin:    CarbonLib 1.4 Printing Manager code and Universal Interfaces
             3.4.2 PMCore.h and PMDefinitions.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

/* The caller defines CARBON_PRINT for declarations or implementations. */
CARBON_PRINT(PMCreatePrintSettings, 0x90df4, 0x85c34, pm_plain, 1, 1, 0, 0)
CARBON_PRINT(PMNewPrintSettings, 0x921ac, 0x85c34, pm_plain, 1, 1, 0, 0)
CARBON_PRINT(PMCreatePageFormat, 0x92330, 0x8726c, pm_plain, 1, 1, 0, 0)
CARBON_PRINT(PMNewPageFormat, 0x932d0, 0x8726c, pm_plain, 1, 1, 0, 0)
CARBON_PRINT(PMCopyPrintSettings, 0x90e8c, 0x85cf8, pm_copy_settings, 2, 0, 0, 0)
CARBON_PRINT(PMCopyPageFormat, 0x923c8, 0x87300, pm_copy_page_format, 2, 0, 0, 0)
CARBON_PRINT(PMFlattenPrintSettings, 0x91088, 0x85e4c, pm_settings, 2, 2, 0, 0)
CARBON_PRINT(PMUnflattenPrintSettings, 0x921ec, 0x8606c, pm_plain, 2, 3, 0, 0)
CARBON_PRINT(PMFlattenPageFormat, 0x925b0, 0x87454, pm_page_format, 2, 2, 0, 0)
CARBON_PRINT(PMUnflattenPageFormat, 0x93310, 0x87674, pm_plain, 2, 3, 0, 0)
CARBON_PRINT(PMGetDestination, 0x91168, 0x86d14, pm_settings, 3, 2, 0, 0)
CARBON_PRINT(PMGetColorMode, 0x9124c, 0x86a5c, pm_settings, 2, 2, 0, 0)
CARBON_PRINT(PMGetCopies, 0x9132c, 0x86c4c, pm_settings, 2, 2, 0, 0)
CARBON_PRINT(PMGetFirstPage, 0x9140c, 0x861e4, pm_settings, 2, 2, 0, 0)
CARBON_PRINT(PMGetLastPage, 0x916a4, 0x86404, pm_settings, 2, 2, 0, 0)
CARBON_PRINT(PMGetPageRange, 0x91784, 0x86244, pm_settings, 3, 6, 0, 0)
CARBON_PRINT(PMGetJobNameCFString, 0x914ec, 0x86b2c, pm_settings, 2, 2, 0, 0)
CARBON_PRINT(PMGetPrintSettingsExtendedData, 0x9187c, 0x86f98, pm_get_settings_data, 4, 0, 0, 0)
CARBON_PRINT(PMSetColorMode, 0x9196c, 0x86ac4, pm_settings, 2, 0, 2, 0)
CARBON_PRINT(PMSetCopies, 0x91a38, 0x86cac, pm_settings, 3, 0, 0, 4)
CARBON_PRINT(PMSetFirstPage, 0x91b14, 0x86344, pm_settings, 3, 0, 0, 4)
CARBON_PRINT(PMSetLastPage, 0x91db8, 0x86464, pm_settings, 3, 0, 0, 4)
CARBON_PRINT(PMSetPageRange, 0x91e98, 0x8653c, pm_set_range, 3, 0, 0, 0)
CARBON_PRINT(PMSetJobNameCFString, 0x91bf4, 0x86bc8, pm_settings, 2, 2, 0, 0)
CARBON_PRINT(PMSetPrintSettingsExtendedData, 0x91f88, 0x87094, pm_settings, 4, 8, 0, 0)
CARBON_PRINT(PMGetPageFormatExtendedData, 0x92690, 0x889f8, pm_get_format_data, 4, 0, 0, 0)
CARBON_PRINT(PMSetPageFormatExtendedData, 0x92e10, 0x88af4, pm_page_format, 4, 8, 0, 0)
CARBON_PRINT(PMGetAdjustedPageRect, 0x92780, 0x885fc, pm_page_format, 2, 2, 0, 0)
CARBON_PRINT(PMGetAdjustedPaperRect, 0x92860, 0x884a8, pm_page_format, 2, 2, 0, 0)
CARBON_PRINT(PMGetUnadjustedPageRect, 0x92b00, 0x87cf8, pm_page_format, 2, 2, 0, 0)
CARBON_PRINT(PMGetUnadjustedPaperRect, 0x92c04, 0x87e68, pm_page_format, 2, 2, 0, 0)
CARBON_PRINT(PMGetPhysicalPageSize, 0x92be0, 0x87cf8, pm_page_format, 2, 2, 0, 0)
CARBON_PRINT(PMGetPhysicalPaperSize, 0x92ce4, 0x87e68, pm_page_format, 2, 2, 0, 0)
CARBON_PRINT(PMGetScale, 0x92d08, 0x88820, pm_get_scale, 2, 2, 0, 0)
CARBON_PRINT(PMSetScale, 0x931fc, 0x88898, pm_set_scale, 1, 0, 0, 0)
CARBON_PRINT(PMSetUnadjustedPaperRect, 0x92ef8, 0x88360, pm_set_paper_rect, 2, 2, 0, 0)
CARBON_PRINT(PMSetPhysicalPaperSize, 0x9301c, 0x88360, pm_set_paper_rect, 2, 2, 0, 0)
CARBON_PRINT(PMGetOrientation, 0x92940, 0x88750, pm_page_format, 2, 2, 0, 0)
CARBON_PRINT(PMSetOrientation, 0x93040, 0x887b8, pm_page_format, 3, 0, 2, 4)
CARBON_PRINT(PMGetResolution, 0x92a20, 0x88964, pm_page_format, 2, 2, 0, 0)
CARBON_PRINT(PMSetResolution, 0x9311c, 0x87800, pm_page_format, 2, 2, 0, 0)
CARBON_PRINT(PMSetAdjustedPageRect, 0x93e40, 0x88694, pm_set_rect, 2, 0, 0, 0)
CARBON_PRINT(PMSetAdjustedPaperRect, 0x93d4c, 0x88540, pm_set_rect, 2, 0, 0, 0)
CARBON_PRINT(PMRetain, 0x8e198, 0x816b0, pm_lifetime, 1, 0, 0, 0)
CARBON_PRINT(PMRelease, 0x8e20c, 0x81c38, pm_lifetime, 1, 0, 0, 0)
CARBON_PRINT(PMDisposePrintSettings, 0x91008, 0x81c38, pm_dispose_settings, 1, 0, 0, 0)
CARBON_PRINT(PMDisposePageFormat, 0x92530, 0x81c38, pm_dispose_page_format, 1, 0, 0, 0)
