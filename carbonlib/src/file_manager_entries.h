/*+===================================================================
  File:      file_manager_entries.h

  Summary:   Maps classic File Manager entry points to the native PPC wrappers.

  Classes:   None.

  Functions: Private frame wrappers for the export registry.

  Origin:    System 9.2.1 InterfaceLib overlay (fovr 50) and Universal Interfaces 3.4.2 Files.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_FILE_MANAGER_ENTRIES_H
#define ROMAW_CARBON_FILE_MANAGER_ENTRIES_H

#include "file_manager.h"

#define CARBON_FILE_PB(name, trap, selector, record)                                           \
    static int carbon_file_##name(ROMlib_carbon_ppc_frame* call_frame)                         \
    {                                                                                          \
        return ROMlib_carbon_file_parameter_block(call_frame, trap, selector, sizeof(record)); \
    }

#define CARBON_FILE_IO(name, trap)                                     \
    static int carbon_file_##name(ROMlib_carbon_ppc_frame* call_frame) \
    {                                                                  \
        return ROMlib_carbon_file_io(call_frame, trap);                \
    }

#define CARBON_FILE_HIERARCHY(name, trap, selector)                      \
    static int carbon_file_##name(ROMlib_carbon_ppc_frame* call_frame)   \
    {                                                                    \
        return ROMlib_carbon_file_hierarchy(call_frame, trap, selector); \
    }

#define CARBON_FILE_SPEC(name, trap, selector)                         \
    static int carbon_file_##name(ROMlib_carbon_ppc_frame* call_frame) \
    {                                                                  \
        return ROMlib_carbon_file_spec(call_frame, trap, selector);    \
    }

CARBON_FILE_PB(PBAllocContigAsync, 0xa610, -1, ioParam)
CARBON_FILE_PB(PBAllocContigSync, 0xa210, -1, ioParam)
CARBON_FILE_PB(PBAllocateAsync, 0xa410, -1, ioParam)
CARBON_FILE_PB(PBAllocateSync, 0xa010, -1, ioParam)
CARBON_FILE_PB(PBCatMoveAsync, 0xa660, 0x5, CMovePBRec)
CARBON_FILE_PB(PBCatMoveSync, 0xa260, 0x5, CMovePBRec)
CARBON_FILE_PB(PBCatSearchAsync, 0xa660, 0x18, catalog_search_pb_t)
CARBON_FILE_PB(PBCatSearchSync, 0xa260, 0x18, catalog_search_pb_t)
CARBON_FILE_PB(PBCloseAsync, 0xa401, -1, ioParam)
CARBON_FILE_PB(PBCloseSync, 0xa001, -1, ioParam)
CARBON_FILE_PB(PBDTAddAPPLAsync, 0xa660, 0x25, DTPBRec)
CARBON_FILE_PB(PBDTAddAPPLSync, 0xa260, 0x25, DTPBRec)
CARBON_FILE_PB(PBDTAddIconAsync, 0xa660, 0x22, DTPBRec)
CARBON_FILE_PB(PBDTAddIconSync, 0xa260, 0x22, DTPBRec)
CARBON_FILE_PB(PBDTCloseDown, 0xa260, 0x21, DTPBRec)
CARBON_FILE_PB(PBDTDeleteAsync, 0xa460, 0x2f, DTPBRec)
CARBON_FILE_PB(PBDTDeleteSync, 0xa060, 0x2f, DTPBRec)
CARBON_FILE_PB(PBDTFlushAsync, 0xa660, 0x2b, DTPBRec)
CARBON_FILE_PB(PBDTFlushSync, 0xa260, 0x2b, DTPBRec)
CARBON_FILE_PB(PBDTGetAPPLAsync, 0xa660, 0x27, DTPBRec)
CARBON_FILE_PB(PBDTGetAPPLSync, 0xa260, 0x27, DTPBRec)
CARBON_FILE_PB(PBDTGetCommentAsync, 0xa660, 0x2a, DTPBRec)
CARBON_FILE_PB(PBDTGetCommentSync, 0xa260, 0x2a, DTPBRec)
CARBON_FILE_PB(PBDTGetIconAsync, 0xa660, 0x23, DTPBRec)
CARBON_FILE_PB(PBDTGetIconInfoAsync, 0xa660, 0x24, DTPBRec)
CARBON_FILE_PB(PBDTGetIconInfoSync, 0xa260, 0x24, DTPBRec)
CARBON_FILE_PB(PBDTGetIconSync, 0xa260, 0x23, DTPBRec)
CARBON_FILE_PB(PBDTGetInfoAsync, 0xa660, 0x2d, DTPBRec)
CARBON_FILE_PB(PBDTGetInfoSync, 0xa260, 0x2d, DTPBRec)
CARBON_FILE_PB(PBDTGetPath, 0xa260, 0x20, DTPBRec)
CARBON_FILE_PB(PBDTOpenInform, 0xa060, 0x2e, DTPBRec)
CARBON_FILE_PB(PBDTRemoveAPPLAsync, 0xa660, 0x26, DTPBRec)
CARBON_FILE_PB(PBDTRemoveAPPLSync, 0xa260, 0x26, DTPBRec)
CARBON_FILE_PB(PBDTRemoveCommentAsync, 0xa660, 0x29, DTPBRec)
CARBON_FILE_PB(PBDTRemoveCommentSync, 0xa260, 0x29, DTPBRec)
CARBON_FILE_PB(PBDTResetAsync, 0xa660, 0x2c, DTPBRec)
CARBON_FILE_PB(PBDTResetSync, 0xa260, 0x2c, DTPBRec)
CARBON_FILE_PB(PBDTSetCommentAsync, 0xa660, 0x28, DTPBRec)
CARBON_FILE_PB(PBDTSetCommentSync, 0xa260, 0x28, DTPBRec)
CARBON_FILE_PB(PBDirCreateAsync, 0xa660, 0x6, HFileParam)
CARBON_FILE_PB(PBDirCreateSync, 0xa260, 0x6, HFileParam)
CARBON_FILE_PB(PBFlushFileAsync, 0xa445, -1, ioParam)
CARBON_FILE_PB(PBFlushFileSync, 0xa045, -1, ioParam)
CARBON_FILE_PB(PBFlushVolAsync, 0xa413, -1, ioParam)
CARBON_FILE_PB(PBFlushVolSync, 0xa013, -1, ioParam)
CARBON_FILE_PB(PBGetCatInfoAsync, 0xa660, 0x9, CInfoPBRec)
CARBON_FILE_PB(PBGetCatInfoSync, 0xa260, 0x9, CInfoPBRec)
CARBON_FILE_PB(PBGetEOFAsync, 0xa411, -1, ioParam)
CARBON_FILE_PB(PBGetEOFSync, 0xa011, -1, ioParam)
CARBON_FILE_PB(PBGetFCBInfoAsync, 0xa660, 0x8, FCBPBRec)
CARBON_FILE_PB(PBGetFCBInfoSync, 0xa260, 0x8, FCBPBRec)
CARBON_FILE_PB(PBGetFPosAsync, 0xa418, -1, ioParam)
CARBON_FILE_PB(PBGetFPosSync, 0xa018, -1, ioParam)
CARBON_FILE_PB(PBHCreateAsync, 0xa608, -1, HFileParam)
CARBON_FILE_PB(PBHCreateSync, 0xa208, -1, HFileParam)
CARBON_FILE_PB(PBHDeleteAsync, 0xa609, -1, HFileParam)
CARBON_FILE_PB(PBHDeleteSync, 0xa209, -1, HFileParam)
CARBON_FILE_PB(PBHGetFInfoAsync, 0xa60c, -1, HFileParam)
CARBON_FILE_PB(PBHGetFInfoSync, 0xa20c, -1, HFileParam)
CARBON_FILE_PB(PBHGetVInfoAsync, 0xa607, -1, HVolumeParam)
CARBON_FILE_PB(PBHGetVInfoSync, 0xa207, -1, HVolumeParam)
CARBON_FILE_PB(PBHGetVolAsync, 0xa614, -1, WDPBRec)
CARBON_FILE_PB(PBHGetVolParmsAsync, 0xa660, 0x30, HIoParam)
CARBON_FILE_PB(PBHGetVolParmsSync, 0xa260, 0x30, HIoParam)
CARBON_FILE_PB(PBHGetVolSync, 0xa214, -1, WDPBRec)
CARBON_FILE_PB(PBHOpenAsync, 0xa600, -1, HFileParam)
CARBON_FILE_PB(PBHOpenDFAsync, 0xa660, 0x1a, HFileParam)
CARBON_FILE_PB(PBHOpenDFSync, 0xa260, 0x1a, HFileParam)
CARBON_FILE_PB(PBHOpenRFAsync, 0xa60a, -1, HFileParam)
CARBON_FILE_PB(PBHOpenRFSync, 0xa20a, -1, HFileParam)
CARBON_FILE_PB(PBHOpenSync, 0xa200, -1, HFileParam)
CARBON_FILE_PB(PBHRenameAsync, 0xa60b, -1, HFileParam)
CARBON_FILE_PB(PBHRenameSync, 0xa20b, -1, HFileParam)
CARBON_FILE_PB(PBHRstFLockAsync, 0xa642, -1, HFileParam)
CARBON_FILE_PB(PBHRstFLockSync, 0xa242, -1, HFileParam)
CARBON_FILE_PB(PBHSetFInfoAsync, 0xa60d, -1, HFileParam)
CARBON_FILE_PB(PBHSetFInfoSync, 0xa20d, -1, HFileParam)
CARBON_FILE_PB(PBHSetFLockAsync, 0xa641, -1, HFileParam)
CARBON_FILE_PB(PBHSetFLockSync, 0xa241, -1, HFileParam)
CARBON_FILE_PB(PBHSetVolAsync, 0xa615, -1, WDPBRec)
CARBON_FILE_PB(PBHSetVolSync, 0xa215, -1, WDPBRec)
CARBON_FILE_PB(PBMakeFSSpecAsync, 0xa660, 0x1b, HFileParam)
CARBON_FILE_PB(PBLockRangeSync, 0xa260, 0x10, ioParam)
CARBON_FILE_PB(PBLockRangeAsync, 0xa660, 0x10, ioParam)
CARBON_FILE_PB(PBUnlockRangeSync, 0xa260, 0x11, ioParam)
CARBON_FILE_PB(PBUnlockRangeAsync, 0xa660, 0x11, ioParam)
CARBON_FILE_PB(PBMakeFSSpecSync, 0xa260, 0x1b, HFileParam)
CARBON_FILE_PB(PBReadAsync, 0xa402, -1, ioParam)
CARBON_FILE_PB(PBReadSync, 0xa002, -1, ioParam)
CARBON_FILE_PB(PBSetCatInfoAsync, 0xa660, 0xa, CInfoPBRec)
CARBON_FILE_PB(PBSetCatInfoSync, 0xa260, 0xa, CInfoPBRec)
CARBON_FILE_PB(PBSetEOFAsync, 0xa412, -1, ioParam)
CARBON_FILE_PB(PBSetEOFSync, 0xa012, -1, ioParam)
CARBON_FILE_PB(PBSetFPosAsync, 0xa444, -1, ioParam)
CARBON_FILE_PB(PBSetFPosSync, 0xa044, -1, ioParam)
CARBON_FILE_PB(PBSetVInfoAsync, 0xa660, 0xb, HVolumeParam)
CARBON_FILE_PB(PBSetVInfoSync, 0xa260, 0xb, HVolumeParam)
CARBON_FILE_PB(PBWriteAsync, 0xa403, -1, ioParam)
CARBON_FILE_PB(PBWriteSync, 0xa003, -1, ioParam)

CARBON_FILE_IO(Allocate, 0xa010)
CARBON_FILE_IO(AllocContig, 0xa210)
CARBON_FILE_IO(FSClose, 0xa001)
CARBON_FILE_IO(FSRead, 0xa002)
CARBON_FILE_IO(FSWrite, 0xa003)
CARBON_FILE_IO(GetEOF, 0xa011)
CARBON_FILE_IO(GetFPos, 0xa018)
CARBON_FILE_IO(SetEOF, 0xa012)
CARBON_FILE_IO(SetFPos, 0xa044)

CARBON_FILE_HIERARCHY(DirCreate, 0xa260, 0x6)
CARBON_FILE_HIERARCHY(HCreate, 0xa208, -1)
CARBON_FILE_HIERARCHY(HDelete, 0xa209, -1)
CARBON_FILE_HIERARCHY(HGetFInfo, 0xa20c, -1)
CARBON_FILE_HIERARCHY(HGetVol, 0xa214, -1)
CARBON_FILE_HIERARCHY(HOpen, 0xa200, -1)
CARBON_FILE_HIERARCHY(HOpenDF, 0xa260, 0x1a)
CARBON_FILE_HIERARCHY(HOpenRF, 0xa20a, -1)
CARBON_FILE_HIERARCHY(HRename, 0xa20b, -1)
CARBON_FILE_HIERARCHY(HRstFLock, 0xa242, -1)
CARBON_FILE_HIERARCHY(HSetFLock, 0xa241, -1)
CARBON_FILE_HIERARCHY(HSetVol, 0xa215, -1)

CARBON_FILE_SPEC(FSpCatMove, 0xa260, 0x5)
CARBON_FILE_SPEC(FSpCreate, 0xa208, -1)
CARBON_FILE_SPEC(FSpDelete, 0xa209, -1)
CARBON_FILE_SPEC(FSpDirCreate, 0xa260, 0x6)
CARBON_FILE_SPEC(FSpGetFInfo, 0xa20c, -1)
CARBON_FILE_SPEC(FSpOpenDF, 0xa260, 0x1a)
CARBON_FILE_SPEC(FSpOpenRF, 0xa20a, -1)
CARBON_FILE_SPEC(FSpRename, 0xa20b, -1)
CARBON_FILE_SPEC(FSpRstFLock, 0xa242, -1)
CARBON_FILE_SPEC(FSpSetFInfo, 0xa20d, -1)
CARBON_FILE_SPEC(FSpSetFLock, 0xa241, -1)

#undef CARBON_FILE_SPEC
#undef CARBON_FILE_HIERARCHY
#undef CARBON_FILE_IO
#undef CARBON_FILE_PB
#endif
