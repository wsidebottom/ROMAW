/*+===================================================================
  File:      manager_vectors.h

  Summary:   Wraps Toolbox manager dispatchers and related traps re-exported by CarbonLib.

  Classes:   None.

  Functions: Private frame wrappers for the export registry.

  Origin:    CarbonLib 1.4 import table, Universal Interfaces 3.4.2 trap
             declarations, and Executor's Pascal trap signatures.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_MANAGER_VECTORS_H
#define ROMAW_CARBON_MANAGER_VECTORS_H

/* List Manager. */
TOOLBOX_SELECTOR(LActivate, 0x159c, 0xd8e, 0x0, uint32_t)
TOOLBOX_SELECTOR(LAddColumn, 0x159c, 0x3aae, 0x4, int16_t)
TOOLBOX_SELECTOR(LAddRow, 0x159c, 0x3aae, 0x8, int16_t)
TOOLBOX_SELECTOR(LAddToCell, 0x159c, 0xfb8e, 0xc, uint32_t)
TOOLBOX_SELECTOR(LAutoScroll, 0x159c, 0x38e, 0x10, uint32_t)
TOOLBOX_SELECTOR(LCellSize, 0x159c, 0xf8e, 0x14, uint32_t)
TOOLBOX_SELECTOR(LClick, 0x159c, 0x3b9e, 0x18, uint8_t)
TOOLBOX_SELECTOR(LClrCell, 0x159c, 0xf8e, 0x1c, uint32_t)
TOOLBOX_SELECTOR(LDelColumn, 0x159c, 0x3a8e, 0x20, uint32_t)
TOOLBOX_SELECTOR(LDelRow, 0x159c, 0x3a8e, 0x24, uint32_t)
TOOLBOX_SELECTOR(LDispose, 0x159c, 0x38e, 0x28, uint32_t)
TOOLBOX_SELECTOR(LDraw, 0x159c, 0xf8e, 0x30, uint32_t)
TOOLBOX_SELECTOR(LGetCell, 0x159c, 0xff8e, 0x38, uint32_t)
TOOLBOX_SELECTOR(LGetSelect, 0x159c, 0x3d9e, 0x3c, uint8_t)
TOOLBOX_SELECTOR(LNew, 0x159c, 0x157bfbe, 0x44, uint32_t)
TOOLBOX_SELECTOR(LNextCell, 0x159c, 0xf59e, 0x48, uint8_t)
TOOLBOX_SELECTOR(LRect, 0x159c, 0x3f8e, 0x4c, uint32_t)
TOOLBOX_SELECTOR(LScroll, 0x159c, 0x3a8e, 0x50, uint32_t)
TOOLBOX_SELECTOR(LSearch, 0x159c, 0x3fb9e, 0x54, uint8_t)
TOOLBOX_SELECTOR(LSetCell, 0x159c, 0xfb8e, 0x58, uint32_t)
TOOLBOX_SELECTOR(LSetDrawingMode, 0x159c, 0xd8e, 0x2c, uint32_t)
TOOLBOX_SELECTOR(LSetSelect, 0x159c, 0x3d8e, 0x5c, uint32_t)
TOOLBOX_SELECTOR(LSize, 0x159c, 0x3a8e, 0x60, uint32_t)
TOOLBOX_SELECTOR(LUpdate, 0x159c, 0xf8e, 0x64, uint32_t)

/* Icon Utilities. */
TOOLBOX_SELECTOR(AddIconToSuite, 0x1d24, 0x3fa8, 0x608, int16_t)
TOOLBOX_SELECTOR(DisposeIconSuite, 0x1d24, 0x7a8, 0x302, int16_t)
TOOLBOX_SELECTOR(ForEachIconDo, 0x1d24, 0xffa8, 0x80a, int16_t)
TOOLBOX_SELECTOR(GetIconCacheData, 0x1d24, 0xfa8, 0x419, int16_t)
TOOLBOX_SELECTOR(GetIconCacheProc, 0x1d24, 0xfa8, 0x41b, int16_t)
TOOLBOX_SELECTOR(GetIconFromSuite, 0x1d24, 0x3fa8, 0x609, int16_t)
TOOLBOX_SELECTOR(GetIconSuite, 0x1d24, 0x3ba8, 0x501, int16_t)
TOOLBOX_SELECTOR(GetLabel, 0x1d24, 0x3ea8, 0x50b, int16_t)
TOOLBOX_SELECTOR(GetSuiteLabel, 0x1d24, 0x3a8, 0x217, int16_t)
TOOLBOX_SELECTOR(IconIDToRgn, 0x1d24, 0xafa8, 0x613, int16_t)
TOOLBOX_SELECTOR(IconMethodToRgn, 0x1d24, 0x3efa8, 0x915, int16_t)
TOOLBOX_SELECTOR(IconSuiteToRgn, 0x1d24, 0xefa8, 0x714, int16_t)
TOOLBOX_SELECTOR(LoadIconCache, 0x1d24, 0xeba8, 0x606, int16_t)
TOOLBOX_SELECTOR(MakeIconCache, 0x1d24, 0x3fa8, 0x604, int16_t)
TOOLBOX_SELECTOR(NewIconSuite, 0x1d24, 0x3a8, 0x207, int16_t)
TOOLBOX_SELECTOR(PlotCIconHandle, 0x1d24, 0xeba8, 0x61f, int16_t)
TOOLBOX_SELECTOR(PlotIconHandle, 0x1d24, 0xeba8, 0x61d, int16_t)
TOOLBOX_SELECTOR(PlotIconID, 0x1d24, 0xaba8, 0x500, int16_t)
TOOLBOX_SELECTOR(PlotIconMethod, 0x1d24, 0x3eba8, 0x805, int16_t)
TOOLBOX_SELECTOR(PlotIconSuite, 0x1d24, 0xeba8, 0x603, int16_t)
TOOLBOX_SELECTOR(PlotSICNHandle, 0x1d24, 0xeba8, 0x61e, int16_t)
TOOLBOX_SELECTOR(PtInIconID, 0x1d24, 0xaf98, 0x60d, uint8_t)
TOOLBOX_SELECTOR(PtInIconMethod, 0x1d24, 0x3ef98, 0x90f, uint8_t)
TOOLBOX_SELECTOR(PtInIconSuite, 0x1d24, 0xef98, 0x70e, uint8_t)
TOOLBOX_SELECTOR(RectInIconID, 0x1d24, 0xaf98, 0x610, uint8_t)
TOOLBOX_SELECTOR(RectInIconMethod, 0x1d24, 0x3ef98, 0x912, uint8_t)
TOOLBOX_SELECTOR(RectInIconSuite, 0x1d24, 0xef98, 0x711, uint8_t)
TOOLBOX_SELECTOR(SetIconCacheData, 0x1d24, 0xfa8, 0x41a, int16_t)
TOOLBOX_SELECTOR(SetIconCacheProc, 0x1d24, 0xfa8, 0x41c, int16_t)
TOOLBOX_SELECTOR(SetSuiteLabel, 0x1d24, 0xba8, 0x316, int16_t)

/* Color Picker and Palette Manager. */
TOOLBOX_VECTOR(AnimatePalette, 0x1868, 0xabc0, uint32_t)
TOOLBOX_SELECTOR(CMY2RGB, 0x0eb8, 0xf8e, 0x3, uint32_t)
TOOLBOX_VECTOR(CTab2Palette, 0x187c, 0x2bc0, uint32_t)
TOOLBOX_SELECTOR(Entry2Index, 0x1888, 0x278, 0x0, uint32_t)
TOOLBOX_SELECTOR(Fix2SmallFract, 0x0eb8, 0x3ae, 0x1, uint16_t)
TOOLBOX_SELECTOR(GetColor, 0x0eb8, 0xff9e, 0x9, uint8_t)
TOOLBOX_SELECTOR(GetGray, 0x1888, 0x3f98, 0xc19, uint8_t)
TOOLBOX_SELECTOR(GetPaletteUpdates, 0x1888, 0x3a8, 0x417, int16_t)
TOOLBOX_SELECTOR(HSL2RGB, 0x0eb8, 0xf8e, 0x5, uint32_t)
TOOLBOX_SELECTOR(HSV2RGB, 0x0eb8, 0xf8e, 0x7, uint32_t)
TOOLBOX_SELECTOR(HasDepth, 0x1888, 0xaba8, 0xa14, int16_t)
TOOLBOX_VECTOR(InitPalettes, 0x1840, 0x0, uint32_t)
TOOLBOX_SELECTOR(PMgrVersion, 0x1888, 0x68, 0x15, int16_t)
TOOLBOX_VECTOR(Palette2CTab, 0x1880, 0x3c0, uint32_t)
TOOLBOX_SELECTOR(RGB2CMY, 0x0eb8, 0xf8e, 0x4, uint32_t)
TOOLBOX_SELECTOR(RGB2HSL, 0x0eb8, 0xf8e, 0x6, uint32_t)
TOOLBOX_SELECTOR(RGB2HSV, 0x0eb8, 0xf8e, 0x8, uint32_t)
TOOLBOX_SELECTOR(RestoreBack, 0x1888, 0x388, 0x410, uint32_t)
TOOLBOX_SELECTOR(ResizePalette, 0x1888, 0xb48, 0x3, uint32_t)
TOOLBOX_SELECTOR(RestoreDeviceClut, 0x1888, 0x348, 0x2, uint32_t)
TOOLBOX_SELECTOR(RestoreFore, 0x1888, 0x388, 0x40f, uint32_t)
TOOLBOX_SELECTOR(SaveBack, 0x1888, 0x388, 0x40e, uint32_t)
TOOLBOX_SELECTOR(SaveFore, 0x1888, 0x388, 0x40d, uint32_t)
TOOLBOX_SELECTOR(SetDepth, 0x1888, 0xaba8, 0xa13, int16_t)
TOOLBOX_SELECTOR(SetPaletteUpdates, 0x1888, 0xb88, 0x616, uint32_t)
TOOLBOX_SELECTOR(SmallFract2Fix, 0x0eb8, 0x2be, 0x2, uint32_t)

/* Script Manager and QuickDraw text. */
TOOLBOX_SELECTOR(FontScript, 0x10d4, 0xee, 0x82000000, int16_t)
TOOLBOX_SELECTOR(IntlScript, 0x10d4, 0xee, 0x82000002, int16_t)
TOOLBOX_SELECTOR(FontToScript, 0x10d4, 0x2ee, 0x82020006, int16_t)
TOOLBOX_SELECTOR(SetScriptVariable, 0x10d4, 0x3aee, 0x8208000e, int16_t)
TOOLBOX_SELECTOR(MeasureJustified, 0x10d4, 0x3effce, 0x801a0034, uint32_t)
TOOLBOX_SELECTOR(CharToPixel, 0x10d4, 0xfaffee, 0x821c0030, int16_t)
TOOLBOX_SELECTOR(CharacterByteType, 0x10d4, 0x2bee, 0xc2060010, int16_t)
TOOLBOX_SELECTOR(DrawJustified, 0x10d4, 0xfbfce, 0x80160032, uint32_t)
TOOLBOX_SELECTOR(FillParseTable, 0x10d4, 0xbde, 0xc2040022, uint8_t)
TOOLBOX_SELECTOR(GetScriptManagerVariable, 0x10d4, 0x2fe, 0x84020008, uint32_t)
TOOLBOX_SELECTOR(InitDateCache, 0x10d4, 0x3ee, 0x8204fff8, int16_t)
TOOLBOX_SELECTOR(KeyScript, 0x10d4, 0x2ce, 0x80020004, uint32_t)
TOOLBOX_SELECTOR(ReplaceText, 0x10d4, 0x3fee, 0x820cffdc, int16_t)
TOOLBOX_SELECTOR(LongDateToSeconds, 0x10d4, 0xfce, 0x8008fff2, uint32_t)
TOOLBOX_SELECTOR(LongSecondsToDate, 0x10d4, 0xfce, 0x8008fff0, uint32_t)
TOOLBOX_VECTOR(MeasureText, 0x0edc, 0xf80, uint32_t)
TOOLBOX_SELECTOR(PixelToChar, 0x10d4, 0x3efffee, 0x8222002e, int16_t)
TOOLBOX_SELECTOR(PortionLine, 0x10d4, 0x3effe, 0x84120036, uint32_t)
TOOLBOX_SELECTOR(StringToDate, 0x10d4, 0x3ffee, 0x8214fff6, int16_t)
TOOLBOX_SELECTOR(StringToTime, 0x10d4, 0x3ffee, 0x8214fff4, int16_t)
TOOLBOX_SELECTOR(StyledLineBreak, 0x10d4, 0x3fffde, 0x821cfffe, int8_t)
TOOLBOX_SELECTOR(TruncString, 0x10d4, 0x2eee, 0x8208ffe0, int16_t)
TOOLBOX_SELECTOR(TruncText, 0x10d4, 0xbeee, 0x820cffde, int16_t)
TOOLBOX_SELECTOR(VisibleLength, 0x10d4, 0xffe, 0x84080028, uint32_t)

/* TextEdit. */
TOOLBOX_SELECTOR(TEContinuousStyle, 0x0ef4, 0x3f9e, 0xa, uint8_t)
TOOLBOX_SELECTOR(TEGetHeight, 0x0ef4, 0x3fbe, 0x9, uint32_t)
TOOLBOX_SELECTOR(TEGetStyle, 0x0ef4, 0x3fe8e, 0x3, uint32_t)
TOOLBOX_SELECTOR(TEGetStyleHandle, 0x0ef4, 0x3be, 0x4, uint32_t)
TOOLBOX_SELECTOR(TEGetStyleScrapHandle, 0x0ef4, 0x3be, 0x6, uint32_t)
TOOLBOX_SELECTOR(TENumStyles, 0x0ef4, 0x3fbe, 0xd, uint32_t)
TOOLBOX_SELECTOR(TEReplaceStyle, 0x0ef4, 0x37e8e, 0x2, uint32_t)
TOOLBOX_SELECTOR(TESetStyle, 0x0ef4, 0xde8e, 0x1, uint32_t)
TOOLBOX_SELECTOR(TESetStyleHandle, 0x0ef4, 0xf8e, 0x5, uint32_t)
TOOLBOX_SELECTOR(TEStyleInsert, 0x0ef4, 0xff8e, 0x7, uint32_t)
TOOLBOX_SELECTOR(TEStylePaste, 0x0ef4, 0x38e, 0x0, uint32_t)
TOOLBOX_SELECTOR(TEUseStyleScrap, 0x0ef4, 0x37f8e, 0xb, uint32_t)

/* Sound Manager. */
TOOLBOX_SELECTOR(GetSoundHeaderOffset, 0x0e00, 0xfe8, 0x4040018, int16_t)
TOOLBOX_SELECTOR(SndChannelStatus, 0x0e00, 0x3be8, 0x5100008, int16_t)
TOOLBOX_SELECTOR(SndGetSysBeepState, 0x0e00, 0x3c8, 0x2180008, uint32_t)
TOOLBOX_SELECTOR(SndManagerStatus, 0x0e00, 0xee8, 0x3140008, int16_t)
TOOLBOX_SELECTOR(SndSetSysBeepState, 0x0e00, 0x2e8, 0x11c0008, int16_t)

/* Windows, menus, dialogs, and pictures. */
TOOLBOX_VECTOR(CalcMenuSize, 0x1320, 0xc0, uint32_t)
TOOLBOX_VECTOR(CalcVis, 0x1224, 0xc0, uint32_t)
TOOLBOX_VECTOR(CalcVisBehind, 0x1228, 0x3c0, uint32_t)
TOOLBOX_VECTOR(ClipAbove, 0x122c, 0xc0, uint32_t)
TOOLBOX_VECTOR(GetMCEntry, 0x1790, 0x2b0, uint32_t)
TOOLBOX_VECTOR(GetMCInfo, 0x1784, 0x30, uint32_t)
TOOLBOX_SELECTOR(GetPixMapInfo, 0x0ec4, 0xaafa8, 0x801, int16_t)
TOOLBOX_SELECTOR(GetStdFilterProc, 0x17a0, 0x3a8, 0x203, int16_t)
TOOLBOX_VECTOR(InvalMenuBar, 0x0e74, 0x0, uint32_t)
TOOLBOX_VECTOR(SetMCEntries, 0x1794, 0x380, uint32_t)
TOOLBOX_VECTOR(SetMCInfo, 0x1788, 0xc0, uint32_t)

#endif
