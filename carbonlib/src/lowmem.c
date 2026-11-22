/*+===================================================================
  File:      lowmem.c

  Summary:   Native classic low-memory accessors. Guest addresses remain raw PPC words, including pointer-valued
             globals: translating them to host addresses here would corrupt the shared low-memory state. The registry
             retains 68K PEF fallback.

  Classes:   None.

  Functions: ROMlib_carbon_LMGetApFontID, ROMlib_carbon_LMGetApplZone, ROMlib_carbon_LMGetBootDrive,
             ROMlib_carbon_LMGetBufPtr, ROMlib_carbon_LMGetBufTgDate, ROMlib_carbon_LMGetBufTgFBkNum,
             ROMlib_carbon_LMGetBufTgFFlg, ROMlib_carbon_LMGetBufTgFNum, ROMlib_carbon_LMGetCPUFlag,
             ROMlib_carbon_LMGetCurApName, ROMlib_carbon_LMGetCurApRefNum, ROMlib_carbon_LMGetCurPageOption,
             ROMlib_carbon_LMGetCurPitch, ROMlib_carbon_LMGetCurStackBase, ROMlib_carbon_LMGetCursorNew,
             ROMlib_carbon_LMGetDefltStack, ROMlib_carbon_LMGetDeviceList, ROMlib_carbon_LMGetDiskFormatingHFSDefaults,
             ROMlib_carbon_LMGetFinderName, ROMlib_carbon_LMGetFractEnable, ROMlib_carbon_LMGetGZMoveHnd,
             ROMlib_carbon_LMGetGZRootHnd, ROMlib_carbon_LMGetHeapEnd, ROMlib_carbon_LMGetHighHeapMark,
             ROMlib_carbon_LMGetHiliteMode, ROMlib_carbon_LMGetHiliteRGB, ROMlib_carbon_LMGetIntlSpec,
             ROMlib_carbon_LMGetJStash, ROMlib_carbon_LMGetKbdLast, ROMlib_carbon_LMGetKbdType,
             ROMlib_carbon_LMGetKeyRepThresh, ROMlib_carbon_LMGetKeyThresh, ROMlib_carbon_LMGetLastFOND,
             ROMlib_carbon_LMGetLastSPExtra, ROMlib_carbon_LMGetLvl2DT, ROMlib_carbon_LMGetMainDevice,
             ROMlib_carbon_LMGetMemErr, ROMlib_carbon_LMGetMemTop, ROMlib_carbon_LMGetMinStack,
             ROMlib_carbon_LMGetMinusOne, ROMlib_carbon_LMGetOneOne, ROMlib_carbon_LMGetPrintErr,
             ROMlib_carbon_LMGetQDColors, ROMlib_carbon_LMGetResErr, ROMlib_carbon_LMGetResLoad,
             ROMlib_carbon_LMGetRndSeed, ROMlib_carbon_LMGetSEvtEnb, ROMlib_carbon_LMGetScrDmpEnb,
             ROMlib_carbon_LMGetScrHRes, ROMlib_carbon_LMGetScrVRes, ROMlib_carbon_LMGetSdVolume,
             ROMlib_carbon_LMGetSoundBase, ROMlib_carbon_LMGetSoundLevel, ROMlib_carbon_LMGetSoundPtr,
             ROMlib_carbon_LMGetStackLowPoint, ROMlib_carbon_LMGetSysFontFam, ROMlib_carbon_LMGetSysFontSize,
             ROMlib_carbon_LMGetSysMap, ROMlib_carbon_LMGetSysResName, ROMlib_carbon_LMGetSysZone,
             ROMlib_carbon_LMGetTheGDevice, ROMlib_carbon_LMGetTheMenu, ROMlib_carbon_LMGetTmpResLoad,
             ROMlib_carbon_LMGetToExtFS, ROMlib_carbon_LMGetToolScratch, ROMlib_carbon_LMGetWidthListHand,
             ROMlib_carbon_LMGetWidthPtr, ROMlib_carbon_LMGetWidthTabHandle, ROMlib_carbon_LMGetWordRedraw,
             ROMlib_carbon_LMSetApFontID, ROMlib_carbon_LMSetApplZone, ROMlib_carbon_LMSetBootDrive,
             ROMlib_carbon_LMSetBufPtr, ROMlib_carbon_LMSetBufTgDate, ROMlib_carbon_LMSetBufTgFBkNum,
             ROMlib_carbon_LMSetBufTgFFlg, ROMlib_carbon_LMSetBufTgFNum, ROMlib_carbon_LMSetCPUFlag,
             ROMlib_carbon_LMSetCurApName, ROMlib_carbon_LMSetCurApRefNum, ROMlib_carbon_LMSetCurPageOption,
             ROMlib_carbon_LMSetCurPitch, ROMlib_carbon_LMSetCurStackBase, ROMlib_carbon_LMSetCursorNew,
             ROMlib_carbon_LMSetDefltStack, ROMlib_carbon_LMSetDeviceList, ROMlib_carbon_LMSetDiskFormatingHFSDefaults,
             ROMlib_carbon_LMSetFinderName, ROMlib_carbon_LMSetFractEnable, ROMlib_carbon_LMSetGZMoveHnd,
             ROMlib_carbon_LMSetGZRootHnd, ROMlib_carbon_LMSetHeapEnd, ROMlib_carbon_LMSetHighHeapMark,
             ROMlib_carbon_LMSetHiliteMode, ROMlib_carbon_LMSetHiliteRGB, ROMlib_carbon_LMSetIntlSpec,
             ROMlib_carbon_LMSetJStash, ROMlib_carbon_LMSetKbdLast, ROMlib_carbon_LMSetKbdType,
             ROMlib_carbon_LMSetKeyRepThresh, ROMlib_carbon_LMSetKeyThresh, ROMlib_carbon_LMSetLastFOND,
             ROMlib_carbon_LMSetLastSPExtra, ROMlib_carbon_LMSetLvl2DT, ROMlib_carbon_LMSetMainDevice,
             ROMlib_carbon_LMSetMemErr, ROMlib_carbon_LMSetMemTop, ROMlib_carbon_LMSetMinStack,
             ROMlib_carbon_LMSetMinusOne, ROMlib_carbon_LMSetOneOne, ROMlib_carbon_LMSetPrintErr,
             ROMlib_carbon_LMSetQDColors, ROMlib_carbon_LMSetResErr, ROMlib_carbon_LMSetResLoad,
             ROMlib_carbon_LMSetRndSeed, ROMlib_carbon_LMSetSEvtEnb, ROMlib_carbon_LMSetScrDmpEnb,
             ROMlib_carbon_LMSetScrHRes, ROMlib_carbon_LMSetScrVRes, ROMlib_carbon_LMSetSdVolume,
             ROMlib_carbon_LMSetSoundBase, ROMlib_carbon_LMSetSoundLevel, ROMlib_carbon_LMSetSoundPtr,
             ROMlib_carbon_LMSetStackLowPoint, ROMlib_carbon_LMSetSysFontFam, ROMlib_carbon_LMSetSysFontSize,
             ROMlib_carbon_LMSetSysMap, ROMlib_carbon_LMSetSysResName, ROMlib_carbon_LMSetSysZone,
             ROMlib_carbon_LMSetTheGDevice, ROMlib_carbon_LMSetTmpResLoad, ROMlib_carbon_LMSetToExtFS,
             ROMlib_carbon_LMSetToolScratch, ROMlib_carbon_LMSetWidthListHand, ROMlib_carbon_LMSetWidthPtr,
             ROMlib_carbon_LMSetWidthTabHandle, ROMlib_carbon_LMSetWordRedraw

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Native classic low-memory accessors. Guest addresses remain raw PPC words,
   including pointer-valued globals: translating them to host addresses here would
   corrupt the shared low-memory state. The registry retains 68K PEF fallback. */
#include "internal.h"

static uint32 lowmem_read(uint32 address, unsigned width, unsigned sign_extend)
{
    const void* memory = ROMlib_carbon_guest_memory(address, width, 0);
    if (!memory)
        return 0;
    if (width == 1)
        return *(const Byte*)memory;
    if (width == 2) {
        uint16 value;
        memcpy(&value, memory, 2);
        return sign_extend ? (uint32)(int32)(int16)CW(value) : CW(value);
    }
    uint32 value;
    memcpy(&value, memory, 4);
    return CL(value);
}

static void lowmem_write(uint32 address, unsigned width, uint32 value)
{
    void* memory = ROMlib_carbon_guest_memory(address, width, 1);
    if (!memory)
        return;
    if (width == 1)
        *(Byte*)memory = (Byte)value;
    else if (width == 2) {
        uint16 word = CW((uint16)value);
        memcpy(memory, &word, 2);
    }
    else {
        value = CL(value);
        memcpy(memory, &value, 4);
    }
}

/* Preserve the two ordered operations in the original RGB accessor, including
   overlap. A single six-byte memmove would have different aliasing behavior. */
static void lowmem_color(uint32 source, uint32 destination)
{
    Byte word[4];
    for (unsigned i = 0; i < 6; i += 4) {
        unsigned count = i == 0 ? 4 : 2;
        const void* input = ROMlib_carbon_guest_memory(source + i, count, 0);
        if (!input)
            return;
        memcpy(word, input, count);
        void* output = ROMlib_carbon_guest_memory(destination + i, count, 1);
        if (!output)
            return;
        memcpy(output, word, count);
    }
}

static void lowmem_move(uint32 destination, uint32 source, unsigned length)
{
    const void* input = ROMlib_carbon_guest_memory(source, length, 0);
    void* output = ROMlib_carbon_guest_memory(destination, length, 1);
    if (input && output)
        memmove(output, input, length);
}

/* CarbonLib 1.4 code 0xa7568. */
uint32 ROMlib_carbon_LMGetApFontID(void)
{
    return lowmem_read(0x984, 2, 1);
}

/* CarbonLib 1.4 code 0xa73a0. */
uint32 ROMlib_carbon_LMGetApplZone(void)
{
    return lowmem_read(0x2aa, 4, 0);
}

/* CarbonLib 1.4 code 0xa7300. */
uint32 ROMlib_carbon_LMGetBootDrive(void)
{
    return lowmem_read(0x210, 2, 1);
}

/* CarbonLib 1.4 code 0xa7260. */
uint32 ROMlib_carbon_LMGetBufPtr(void)
{
    return lowmem_read(0x10c, 4, 0);
}

/* CarbonLib 1.4 code 0xa742c. */
uint32 ROMlib_carbon_LMGetBufTgDate(void)
{
    return lowmem_read(0x304, 4, 0);
}

/* CarbonLib 1.4 code 0xa741c. */
uint32 ROMlib_carbon_LMGetBufTgFBkNum(void)
{
    return lowmem_read(0x302, 2, 1);
}

/* CarbonLib 1.4 code 0xa740c. */
uint32 ROMlib_carbon_LMGetBufTgFFlg(void)
{
    return lowmem_read(0x300, 2, 1);
}

/* CarbonLib 1.4 code 0xa73fc. */
uint32 ROMlib_carbon_LMGetBufTgFNum(void)
{
    return lowmem_read(0x2fc, 4, 0);
}

/* CarbonLib 1.4 code 0xa7280. */
uint32 ROMlib_carbon_LMGetCPUFlag(void)
{
    return lowmem_read(0x12f, 1, 0);
}

/* CarbonLib 1.4 code 0xa74fc. */
uint32 ROMlib_carbon_LMGetCurApName(void)
{
    return 0x910;
}

/* CarbonLib 1.4 code 0xa74dc. */
uint32 ROMlib_carbon_LMGetCurApRefNum(void)
{
    return lowmem_read(0x900, 2, 1);
}

/* CarbonLib 1.4 code 0xa7538. */
uint32 ROMlib_carbon_LMGetCurPageOption(void)
{
    return lowmem_read(0x936, 2, 1);
}

/* CarbonLib 1.4 code 0xa7380. */
uint32 ROMlib_carbon_LMGetCurPitch(void)
{
    return lowmem_read(0x280, 2, 1);
}

/* CarbonLib 1.4 code 0xa74ec. */
uint32 ROMlib_carbon_LMGetCurStackBase(void)
{
    return lowmem_read(0x908, 4, 0);
}

/* CarbonLib 1.4 code 0xa7760. */
uint32 ROMlib_carbon_LMGetCursorNew(void)
{
    return lowmem_read(0x8ce, 1, 0);
}

/* CarbonLib 1.4 code 0xa744c. */
uint32 ROMlib_carbon_LMGetDefltStack(void)
{
    return lowmem_read(0x322, 4, 0);
}

/* CarbonLib 1.4 code 0xa749c. */
uint32 ROMlib_carbon_LMGetDeviceList(void)
{
    return lowmem_read(0x8a8, 4, 0);
}

/* CarbonLib 1.4 code 0xa7790. */
uint32 ROMlib_carbon_LMGetDiskFormatingHFSDefaults(void)
{
    return lowmem_read(0x39e, 4, 0);
}

/* CarbonLib 1.4 code 0xa73b0. */
uint32 ROMlib_carbon_LMGetFinderName(void)
{
    return 0x2e0;
}

/* CarbonLib 1.4 code 0xa7708. */
uint32 ROMlib_carbon_LMGetFractEnable(void)
{
    return lowmem_read(0xbf4, 1, 0);
}

/* CarbonLib 1.4 code 0xa746c. */
uint32 ROMlib_carbon_LMGetGZMoveHnd(void)
{
    return lowmem_read(0x330, 4, 0);
}

/* CarbonLib 1.4 code 0xa745c. */
uint32 ROMlib_carbon_LMGetGZRootHnd(void)
{
    return lowmem_read(0x328, 4, 0);
}

/* CarbonLib 1.4 code 0xa7270. */
uint32 ROMlib_carbon_LMGetHeapEnd(void)
{
    return lowmem_read(0x114, 4, 0);
}

/* CarbonLib 1.4 code 0xa7770. */
uint32 ROMlib_carbon_LMGetHighHeapMark(void)
{
    return lowmem_read(0xbae, 4, 0);
}

/* CarbonLib 1.4 code 0xa7548. */
uint32 ROMlib_carbon_LMGetHiliteMode(void)
{
    return lowmem_read(0x938, 1, 0);
}

/* CarbonLib 1.4 code 0xa7728. */
uint32 ROMlib_carbon_LMGetHiliteRGB(uint32 destination)
{
    lowmem_color(0xda0, destination);
    return destination;
}

/* CarbonLib 1.4 code 0xa76b8. */
uint32 ROMlib_carbon_LMGetIntlSpec(void)
{
    return lowmem_read(0xba0, 4, 0);
}

/* CarbonLib 1.4 code 0xa74cc. */
uint32 ROMlib_carbon_LMGetJStash(void)
{
    return lowmem_read(0x8f8, 4, 0);
}

/* CarbonLib 1.4 code 0xa7310. */
uint32 ROMlib_carbon_LMGetKbdLast(void)
{
    return lowmem_read(0x218, 1, 0);
}

/* CarbonLib 1.4 code 0xa7320. */
uint32 ROMlib_carbon_LMGetKbdType(void)
{
    return lowmem_read(0x21e, 1, 0);
}

/* CarbonLib 1.4 code 0xa72c0. */
uint32 ROMlib_carbon_LMGetKeyRepThresh(void)
{
    return lowmem_read(0x190, 2, 1);
}

/* CarbonLib 1.4 code 0xa72b0. */
uint32 ROMlib_carbon_LMGetKeyThresh(void)
{
    return lowmem_read(0x18e, 2, 1);
}

/* CarbonLib 1.4 code 0xa76f8. */
uint32 ROMlib_carbon_LMGetLastFOND(void)
{
    return lowmem_read(0xbc2, 4, 0);
}

/* CarbonLib 1.4 code 0xa7698. */
uint32 ROMlib_carbon_LMGetLastSPExtra(void)
{
    return lowmem_read(0xb4c, 4, 0);
}

/* CarbonLib 1.4 code 0xa72d0. */
uint32 ROMlib_carbon_LMGetLvl2DT(uint32 index)
{
    return lowmem_read(0x1b2 + (uint32)(int32)(int16)index * 4, 4, 0);
}

/* CarbonLib 1.4 code 0xa748c. */
uint32 ROMlib_carbon_LMGetMainDevice(void)
{
    return lowmem_read(0x8a4, 4, 0);
}

/* CarbonLib 1.4 code 0xa7330. */
uint32 ROMlib_carbon_LMGetMemErr(void)
{
    return lowmem_read(0x220, 2, 1);
}

/* CarbonLib 1.4 code 0xa7250. */
uint32 ROMlib_carbon_LMGetMemTop(void)
{
    return lowmem_read(0x108, 4, 0);
}

/* CarbonLib 1.4 code 0xa743c. */
uint32 ROMlib_carbon_LMGetMinStack(void)
{
    return lowmem_read(0x31e, 4, 0);
}

/* CarbonLib 1.4 code 0xa75c4. */
uint32 ROMlib_carbon_LMGetMinusOne(void)
{
    return lowmem_read(0xa06, 4, 0);
}

/* CarbonLib 1.4 code 0xa75b4. */
uint32 ROMlib_carbon_LMGetOneOne(void)
{
    return lowmem_read(0xa02, 4, 0);
}

/* CarbonLib 1.4 code 0xa7558. */
uint32 ROMlib_carbon_LMGetPrintErr(void)
{
    return lowmem_read(0x944, 2, 1);
}

/* CarbonLib 1.4 code 0xa74ac. */
uint32 ROMlib_carbon_LMGetQDColors(void)
{
    return lowmem_read(0x8b0, 4, 0);
}

/* CarbonLib 1.4 code 0xa75fc. */
uint32 ROMlib_carbon_LMGetResErr(void)
{
    return lowmem_read(0xa60, 2, 1);
}

/* CarbonLib 1.4 code 0xa75ec. */
uint32 ROMlib_carbon_LMGetResLoad(void)
{
    return lowmem_read(0xa5e, 1, 0);
}

/* CarbonLib 1.4 code 0xa7290. */
uint32 ROMlib_carbon_LMGetRndSeed(void)
{
    return lowmem_read(0x156, 4, 0);
}

/* CarbonLib 1.4 code 0xa72a0. */
uint32 ROMlib_carbon_LMGetSEvtEnb(void)
{
    return lowmem_read(0x15c, 1, 0);
}

/* CarbonLib 1.4 code 0xa73ec. */
uint32 ROMlib_carbon_LMGetScrDmpEnb(void)
{
    return lowmem_read(0x2f8, 1, 0);
}

/* CarbonLib 1.4 code 0xa7240. */
uint32 ROMlib_carbon_LMGetScrHRes(void)
{
    return lowmem_read(0x104, 2, 1);
}

/* CarbonLib 1.4 code 0xa7230. */
uint32 ROMlib_carbon_LMGetScrVRes(void)
{
    return lowmem_read(0x102, 2, 1);
}

/* CarbonLib 1.4 code 0xa7340. */
uint32 ROMlib_carbon_LMGetSdVolume(void)
{
    return lowmem_read(0x260, 1, 0);
}

/* CarbonLib 1.4 code 0xa7360. */
uint32 ROMlib_carbon_LMGetSoundBase(void)
{
    return lowmem_read(0x266, 4, 0);
}

/* CarbonLib 1.4 code 0xa7370. */
uint32 ROMlib_carbon_LMGetSoundLevel(void)
{
    return lowmem_read(0x27f, 1, 0);
}

/* CarbonLib 1.4 code 0xa7350. */
uint32 ROMlib_carbon_LMGetSoundPtr(void)
{
    return lowmem_read(0x262, 4, 0);
}

/* CarbonLib 1.4 code 0xa7780. */
uint32 ROMlib_carbon_LMGetStackLowPoint(void)
{
    return lowmem_read(0x110, 4, 0);
}

/* CarbonLib 1.4 code 0xa76d8. */
uint32 ROMlib_carbon_LMGetSysFontFam(void)
{
    return lowmem_read(0xba6, 2, 1);
}

/* CarbonLib 1.4 code 0xa76e8. */
uint32 ROMlib_carbon_LMGetSysFontSize(void)
{
    return lowmem_read(0xba8, 2, 1);
}

/* CarbonLib 1.4 code 0xa75dc. */
uint32 ROMlib_carbon_LMGetSysMap(void)
{
    return lowmem_read(0xa58, 2, 1);
}

/* CarbonLib 1.4 code 0xa7634. */
uint32 ROMlib_carbon_LMGetSysResName(void)
{
    return 0xad8;
}

/* CarbonLib 1.4 code 0xa7390. */
uint32 ROMlib_carbon_LMGetSysZone(void)
{
    return lowmem_read(0x2a6, 4, 0);
}

/* CarbonLib 1.4 code 0xa7718. */
uint32 ROMlib_carbon_LMGetTheGDevice(void)
{
    return lowmem_read(0xcc8, 4, 0);
}

/* CarbonLib 1.4 code 0xa75d4. */
uint32 ROMlib_carbon_LMGetTheMenu(void)
{
    return lowmem_read(0xa26, 2, 1);
}

/* CarbonLib 1.4 code 0xa76a8. */
uint32 ROMlib_carbon_LMGetTmpResLoad(void)
{
    return lowmem_read(0xb9f, 1, 0);
}

/* CarbonLib 1.4 code 0xa747c. */
uint32 ROMlib_carbon_LMGetToExtFS(void)
{
    return lowmem_read(0x3f2, 4, 0);
}

/* CarbonLib 1.4 code 0xa7578. */
uint32 ROMlib_carbon_LMGetToolScratch(void)
{
    return 0x9ce;
}

/* CarbonLib 1.4 code 0xa74bc. */
uint32 ROMlib_carbon_LMGetWidthListHand(void)
{
    return lowmem_read(0x8e4, 4, 0);
}

/* CarbonLib 1.4 code 0xa7678. */
uint32 ROMlib_carbon_LMGetWidthPtr(void)
{
    return lowmem_read(0xb10, 4, 0);
}

/* CarbonLib 1.4 code 0xa7688. */
uint32 ROMlib_carbon_LMGetWidthTabHandle(void)
{
    return lowmem_read(0xb2a, 4, 0);
}

/* CarbonLib 1.4 code 0xa76c8. */
uint32 ROMlib_carbon_LMGetWordRedraw(void)
{
    return lowmem_read(0xba5, 1, 0);
}

/* CarbonLib 1.4 code 0xa7570. */
void ROMlib_carbon_LMSetApFontID(uint32 value)
{
    lowmem_write(0x984, 2, value);
}

/* CarbonLib 1.4 code 0xa73a8. */
void ROMlib_carbon_LMSetApplZone(uint32 value)
{
    lowmem_write(0x2aa, 4, value);
}

/* CarbonLib 1.4 code 0xa7308. */
void ROMlib_carbon_LMSetBootDrive(uint32 value)
{
    lowmem_write(0x210, 2, value);
}

/* CarbonLib 1.4 code 0xa7268. */
void ROMlib_carbon_LMSetBufPtr(uint32 value)
{
    lowmem_write(0x10c, 4, value);
}

/* CarbonLib 1.4 code 0xa7434. */
void ROMlib_carbon_LMSetBufTgDate(uint32 value)
{
    lowmem_write(0x304, 4, value);
}

/* CarbonLib 1.4 code 0xa7424. */
void ROMlib_carbon_LMSetBufTgFBkNum(uint32 value)
{
    lowmem_write(0x302, 2, value);
}

/* CarbonLib 1.4 code 0xa7414. */
void ROMlib_carbon_LMSetBufTgFFlg(uint32 value)
{
    lowmem_write(0x300, 2, value);
}

/* CarbonLib 1.4 code 0xa7404. */
void ROMlib_carbon_LMSetBufTgFNum(uint32 value)
{
    lowmem_write(0x2fc, 4, value);
}

/* CarbonLib 1.4 code 0xa7288. */
void ROMlib_carbon_LMSetCPUFlag(uint32 value)
{
    lowmem_write(0x12f, 1, value);
}

/* CarbonLib 1.4 code 0xa7508. */
void ROMlib_carbon_LMSetCurApName(uint32 source)
{
    lowmem_move(0x910, source, 32);
}

/* CarbonLib 1.4 code 0xa74e4. */
void ROMlib_carbon_LMSetCurApRefNum(uint32 value)
{
    lowmem_write(0x900, 2, value);
}

/* CarbonLib 1.4 code 0xa7540. */
void ROMlib_carbon_LMSetCurPageOption(uint32 value)
{
    lowmem_write(0x936, 2, value);
}

/* CarbonLib 1.4 code 0xa7388. */
void ROMlib_carbon_LMSetCurPitch(uint32 value)
{
    lowmem_write(0x280, 2, value);
}

/* CarbonLib 1.4 code 0xa74f4. */
void ROMlib_carbon_LMSetCurStackBase(uint32 value)
{
    lowmem_write(0x908, 4, value);
}

/* CarbonLib 1.4 code 0xa7768. */
void ROMlib_carbon_LMSetCursorNew(uint32 value)
{
    lowmem_write(0x8ce, 1, value);
}

/* CarbonLib 1.4 code 0xa7454. */
void ROMlib_carbon_LMSetDefltStack(uint32 value)
{
    lowmem_write(0x322, 4, value);
}

/* CarbonLib 1.4 code 0xa74a4. */
void ROMlib_carbon_LMSetDeviceList(uint32 value)
{
    lowmem_write(0x8a8, 4, value);
}

/* CarbonLib 1.4 code 0xa77a0. */
void ROMlib_carbon_LMSetDiskFormatingHFSDefaults(uint32 value)
{
    lowmem_write(0x39e, 4, value);
}

/* CarbonLib 1.4 code 0xa73bc. */
void ROMlib_carbon_LMSetFinderName(uint32 source)
{
    lowmem_move(0x2e0, source, 16);
}

/* CarbonLib 1.4 code 0xa7710. */
void ROMlib_carbon_LMSetFractEnable(uint32 value)
{
    lowmem_write(0xbf4, 1, value);
}

/* CarbonLib 1.4 code 0xa7474. */
void ROMlib_carbon_LMSetGZMoveHnd(uint32 value)
{
    lowmem_write(0x330, 4, value);
}

/* CarbonLib 1.4 code 0xa7464. */
void ROMlib_carbon_LMSetGZRootHnd(uint32 value)
{
    lowmem_write(0x328, 4, value);
}

/* CarbonLib 1.4 code 0xa7278. */
void ROMlib_carbon_LMSetHeapEnd(uint32 value)
{
    lowmem_write(0x114, 4, value);
}

/* CarbonLib 1.4 code 0xa7778. */
void ROMlib_carbon_LMSetHighHeapMark(uint32 value)
{
    lowmem_write(0xbae, 4, value);
}

/* CarbonLib 1.4 code 0xa7550. */
void ROMlib_carbon_LMSetHiliteMode(uint32 value)
{
    lowmem_write(0x938, 1, value);
}

/* CarbonLib 1.4 code 0xa7744. */
void ROMlib_carbon_LMSetHiliteRGB(uint32 source)
{
    lowmem_color(source, 0xda0);
}

/* CarbonLib 1.4 code 0xa76c0. */
void ROMlib_carbon_LMSetIntlSpec(uint32 value)
{
    lowmem_write(0xba0, 4, value);
}

/* CarbonLib 1.4 code 0xa74d4. */
void ROMlib_carbon_LMSetJStash(uint32 value)
{
    lowmem_write(0x8f8, 4, value);
}

/* CarbonLib 1.4 code 0xa7318. */
void ROMlib_carbon_LMSetKbdLast(uint32 value)
{
    lowmem_write(0x218, 1, value);
}

/* CarbonLib 1.4 code 0xa7328. */
void ROMlib_carbon_LMSetKbdType(uint32 value)
{
    lowmem_write(0x21e, 1, value);
}

/* CarbonLib 1.4 code 0xa72c8. */
void ROMlib_carbon_LMSetKeyRepThresh(uint32 value)
{
    lowmem_write(0x190, 2, value);
}

/* CarbonLib 1.4 code 0xa72b8. */
void ROMlib_carbon_LMSetKeyThresh(uint32 value)
{
    lowmem_write(0x18e, 2, value);
}

/* CarbonLib 1.4 code 0xa7700. */
void ROMlib_carbon_LMSetLastFOND(uint32 value)
{
    lowmem_write(0xbc2, 4, value);
}

/* CarbonLib 1.4 code 0xa76a0. */
void ROMlib_carbon_LMSetLastSPExtra(uint32 value)
{
    lowmem_write(0xb4c, 4, value);
}

/* CarbonLib 1.4 code 0xa72e8. */
void ROMlib_carbon_LMSetLvl2DT(uint32 value, uint32 index)
{
    lowmem_write(0x1b2 + (uint32)(int32)(int16)index * 4, 4, value);
}

/* CarbonLib 1.4 code 0xa7494. */
void ROMlib_carbon_LMSetMainDevice(uint32 value)
{
    lowmem_write(0x8a4, 4, value);
}

/* CarbonLib 1.4 code 0xa7338. */
void ROMlib_carbon_LMSetMemErr(uint32 value)
{
    lowmem_write(0x220, 2, value);
}

/* CarbonLib 1.4 code 0xa7258. */
void ROMlib_carbon_LMSetMemTop(uint32 value)
{
    lowmem_write(0x108, 4, value);
}

/* CarbonLib 1.4 code 0xa7444. */
void ROMlib_carbon_LMSetMinStack(uint32 value)
{
    lowmem_write(0x31e, 4, value);
}

/* CarbonLib 1.4 code 0xa75cc. */
void ROMlib_carbon_LMSetMinusOne(uint32 value)
{
    lowmem_write(0xa06, 4, value);
}

/* CarbonLib 1.4 code 0xa75bc. */
void ROMlib_carbon_LMSetOneOne(uint32 value)
{
    lowmem_write(0xa02, 4, value);
}

/* CarbonLib 1.4 code 0xa7560. */
void ROMlib_carbon_LMSetPrintErr(uint32 value)
{
    lowmem_write(0x944, 2, value);
}

/* CarbonLib 1.4 code 0xa74b4. */
void ROMlib_carbon_LMSetQDColors(uint32 value)
{
    lowmem_write(0x8b0, 4, value);
}

/* CarbonLib 1.4 code 0xa7604. */
void ROMlib_carbon_LMSetResErr(uint32 value)
{
    lowmem_write(0xa60, 2, value);
}

/* CarbonLib 1.4 code 0xa75f4. */
void ROMlib_carbon_LMSetResLoad(uint32 value)
{
    lowmem_write(0xa5e, 1, value);
}

/* CarbonLib 1.4 code 0xa7298. */
void ROMlib_carbon_LMSetRndSeed(uint32 value)
{
    lowmem_write(0x156, 4, value);
}

/* CarbonLib 1.4 code 0xa72a8. */
void ROMlib_carbon_LMSetSEvtEnb(uint32 value)
{
    lowmem_write(0x15c, 1, value);
}

/* CarbonLib 1.4 code 0xa73f4. */
void ROMlib_carbon_LMSetScrDmpEnb(uint32 value)
{
    lowmem_write(0x2f8, 1, value);
}

/* CarbonLib 1.4 code 0xa7248. */
void ROMlib_carbon_LMSetScrHRes(uint32 value)
{
    lowmem_write(0x104, 2, value);
}

/* CarbonLib 1.4 code 0xa7238. */
void ROMlib_carbon_LMSetScrVRes(uint32 value)
{
    lowmem_write(0x102, 2, value);
}

/* CarbonLib 1.4 code 0xa7348. */
void ROMlib_carbon_LMSetSdVolume(uint32 value)
{
    lowmem_write(0x260, 1, value);
}

/* CarbonLib 1.4 code 0xa7368. */
void ROMlib_carbon_LMSetSoundBase(uint32 value)
{
    lowmem_write(0x266, 4, value);
}

/* CarbonLib 1.4 code 0xa7378. */
void ROMlib_carbon_LMSetSoundLevel(uint32 value)
{
    lowmem_write(0x27f, 1, value);
}

/* CarbonLib 1.4 code 0xa7358. */
void ROMlib_carbon_LMSetSoundPtr(uint32 value)
{
    lowmem_write(0x262, 4, value);
}

/* CarbonLib 1.4 code 0xa7788. */
void ROMlib_carbon_LMSetStackLowPoint(uint32 value)
{
    lowmem_write(0x110, 4, value);
}

/* CarbonLib 1.4 code 0xa76e0. */
void ROMlib_carbon_LMSetSysFontFam(uint32 value)
{
    lowmem_write(0xba6, 2, value);
}

/* CarbonLib 1.4 code 0xa76f0. */
void ROMlib_carbon_LMSetSysFontSize(uint32 value)
{
    lowmem_write(0xba8, 2, value);
}

/* CarbonLib 1.4 code 0xa75e4. */
void ROMlib_carbon_LMSetSysMap(uint32 value)
{
    lowmem_write(0xa58, 2, value);
}

/* CarbonLib 1.4 code 0xa7640. */
void ROMlib_carbon_LMSetSysResName(uint32 source)
{
    lowmem_move(0xad8, source, 16);
}

/* CarbonLib 1.4 code 0xa7398. */
void ROMlib_carbon_LMSetSysZone(uint32 value)
{
    lowmem_write(0x2a6, 4, value);
}

/* CarbonLib 1.4 code 0xa7720. */
void ROMlib_carbon_LMSetTheGDevice(uint32 value)
{
    lowmem_write(0xcc8, 4, value);
}

/* CarbonLib 1.4 code 0xa76b0. */
void ROMlib_carbon_LMSetTmpResLoad(uint32 value)
{
    lowmem_write(0xb9f, 1, value);
}

/* CarbonLib 1.4 code 0xa7484. */
void ROMlib_carbon_LMSetToExtFS(uint32 value)
{
    lowmem_write(0x3f2, 4, value);
}

/* CarbonLib 1.4 code 0xa7584. */
void ROMlib_carbon_LMSetToolScratch(uint32 source)
{
    lowmem_move(0x9ce, source, 8);
}

/* CarbonLib 1.4 code 0xa74c4. */
void ROMlib_carbon_LMSetWidthListHand(uint32 value)
{
    lowmem_write(0x8e4, 4, value);
}

/* CarbonLib 1.4 code 0xa7680. */
void ROMlib_carbon_LMSetWidthPtr(uint32 value)
{
    lowmem_write(0xb10, 4, value);
}

/* CarbonLib 1.4 code 0xa7690. */
void ROMlib_carbon_LMSetWidthTabHandle(uint32 value)
{
    lowmem_write(0xb2a, 4, value);
}

/* CarbonLib 1.4 code 0xa76d0. */
void ROMlib_carbon_LMSetWordRedraw(uint32 value)
{
    lowmem_write(0xba5, 1, value);
}
