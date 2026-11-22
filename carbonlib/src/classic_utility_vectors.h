/*+===================================================================
  File:      classic_utility_vectors.h

  Summary:   Uses live traps for Carbon memory, time, script, and offscreen graphics APIs.

  Classes:   None.

  Functions: Private frame adapters for the Carbon export registry.

  Origin:    CarbonLib 1.4 QDOffscreen wrappers, Universal Interfaces 3.4.2
             trap declarations, and Executor's runtime trap dispatch.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_CLASSIC_UTILITY_VECTORS_H
#define ROMAW_CARBON_CLASSIC_UTILITY_VECTORS_H

TOOLBOX_REGISTER(BlockMove, 0x04b8, 0xef33002, 0xa02e, uint32_t)
TOOLBOX_REGISTER(BlockMoveData, 0x04b8, 0xef33002, 0xa22e, uint32_t)
TOOLBOX_REGISTER(GetHandleSize, 0x0494, 0x133832, 0xa025, uint32_t)
TOOLBOX_REGISTER(GetDateTime, 0x04e4, 0x133802, 0xa039, uint32_t)
TOOLBOX_REGISTER(ReadDateTime, 0x04e4, 0x133822, 0xa039, int16_t)
TOOLBOX_REGISTER(SetDateTime, 0x04e8, 0x33822, 0xa03a, int16_t)
TOOLBOX_REGISTER(WriteParam, 0x04e0, 0x3822, 0xa038, int16_t)
TOOLBOX_SELECTOR(GetScriptVariable, 0x10d4, 0xafe, 0x8404000c, uint32_t)
TOOLBOX_SELECTOR(NewGWorld, 0x1a74, 0xffbe8, 0x160000, int16_t)
TOOLBOX_SELECTOR(UpdateGWorld, 0x1a74, 0xffbf8, 0x160003, uint32_t)

TOOLBOX_VECTOR(CalcCMask, 0x173c, 0xfffc0, uint32_t)
TOOLBOX_VECTOR(SeedCFill, 0x1740, 0x3ebfc0, uint32_t)
TOOLBOX_VECTOR(SeedFill, 0xee4, 0x2aabc0, uint32_t)
TOOLBOX_VECTOR(CopyDeepMask, 0x1744, 0x3bffc0, uint32_t)
TOOLBOX_VECTOR(PtToAngle, 0x110c, 0xfc0, uint32_t)
TOOLBOX_VECTOR(LoadScrap, 0x15ec, 0x30, uint32_t)
TOOLBOX_VECTOR(UnloadScrap, 0x15e8, 0x30, uint32_t)
TOOLBOX_VECTOR(FindDialogItem, 0x1410, 0x3e0, int16_t)
TOOLBOX_VECTOR(HideDialogItem, 0xe9c, 0x2c0, uint32_t)
TOOLBOX_VECTOR(ShowDialogItem, 0xea0, 0x2c0, uint32_t)
TOOLBOX_VECTOR(SelectDialogItemText, 0x13f8, 0x2ac0, uint32_t)
TOOLBOX_VECTOR(SetDialogItem, 0x1438, 0xfac0, uint32_t)
TOOLBOX_VECTOR(SetDialogItemText, 0x143c, 0x3c0, uint32_t)
TOOLBOX_VECTOR(UpdateDialog, 0x13e0, 0x3c0, uint32_t)
TOOLBOX_REGISTER(GetPtrSize, 0x0484, 0x133832, 0xa021, uint32_t)

TOOLBOX_VECTOR(TESetAlignment, 0x157c, 0x380, uint32_t)
TOOLBOX_VECTOR(TETextBox, 0x1538, 0x2fc0, uint32_t)
TOOLBOX_VECTOR(TEStyleNew, 0xef8, 0x3f0, uint32_t)
TOOLBOX_VECTOR(DeleteMCEntries, 0x1780, 0x280, uint32_t)
TOOLBOX_VECTOR(DisposeMCInfo, 0x178c, 0xc0, uint32_t)
TOOLBOX_VECTOR(SetPalette, 0x1854, 0x7c0, uint32_t)
TOOLBOX_SELECTOR(LGetCellDataLocation, 0x159c, 0xff8e, 0x34, uint32_t)

#endif
