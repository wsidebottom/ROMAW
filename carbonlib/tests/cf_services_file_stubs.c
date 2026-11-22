/*+===================================================================
  File:      cf_services_file_stubs.c

  Summary:   Reject File/Process Manager access in the isolated foreign-profile service checks.

  Classes:   None.

  Functions: C_ResError, ROMlib_initialize_resource_descriptor, ROMlib_open_data_resource_descriptor,
             ROMlib_create_data_resource_file, ROMlib_open_data_resource_file, ROMlib_resource_chain_refnum,
             ROMlib_detach_resource_file, ROMlib_insert_resource_file, ROMlib_open_orphan_resource_file,
             ROMlib_resource_file_already_open, ROMlib_setreserr, C_FindFolder, C_FSMakeFSSpec, C_FSpCreate, C_FSpDelete, C_FSpDirCreate, C_FSpOpenDF,
             C_GetCurrentProcess, C_GetProcessInformation, FSClose, FSRead, FSWrite, GetEOF, GetVRefNum,
             PBAllocContig, PBAllocate, PBCatMove, PBClose, PBDirCreate, PBFlushFile, PBGetCatInfo,
             PBGetEOF, PBGetFCBInfo, PBGetFPos, PBHCreate, PBHDelete, PBHGetVInfo, PBHOpenRF, PBHRename,
             PBHRstFLock, PBHSetFLock, PBOpenDF, PBRead, PBSetCatInfo, PBSetEOF, PBSetFPos, PBSetVInfo,
             PBWrite, ROMlib_file_complete, ROMlib_filename_from_fsspec, ROMlib_host_fsspec,
             ROMlib_host_get_file_id, ROMlib_host_locate_file_id, ROMlib_vcbbyvrn, ROMlib_win_volume_bytes,
             ROMlib_application_spec, filetype_map_lookup_path,
             SetEOF, SetFPos.

  Origin:    Original fail-closed link stubs for the shared CarbonLib host regression harness.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "../src/internal.h"
#include "rsys/application_registry.h"
#include "rsys/ecf.h"
#include "rsys/file.h"
#include "rsys/filetypemap.h"
#include "rsys/hfs.h"
#include "rsys/host_names.h"
#include "winfs.h"
#undef NDEBUG
#include <assert.h>

/* These tests must select foreign fallback before touching native managers.
   Native file/process behavior belongs to the grouped real-library scenarios. */
#define UNEXPECTED_MANAGER()                                                                     \
    do {                                                                                         \
        assert(!"Unexpected native File/Process Manager access during isolated service checks"); \
        return -1;                                                                               \
    } while (0)

OSErr C_FSpCreate(FSSpecPtr spec, OSType creator, OSType file_type, ScriptCode script)
{
    UNEXPECTED_MANAGER();
}

OSErr C_FSpDelete(FSSpecPtr spec)
{
    UNEXPECTED_MANAGER();
}

OSErr C_FSpDirCreate(FSSpecPtr spec, ScriptCode script, int32* created_dir_id)
{
    UNEXPECTED_MANAGER();
}

OSErr C_FSpOpenDF(FSSpecPtr spec, SignedByte perms, int16* ref_num)
{
    UNEXPECTED_MANAGER();
}

OSErr C_GetCurrentProcess(ProcessSerialNumber* serial_number)
{
    UNEXPECTED_MANAGER();
}

OSErr C_GetProcessInformation(ProcessSerialNumber* serial_number, ProcessInfoPtr info)
{
    UNEXPECTED_MANAGER();
}

OSErr ROMlib_application_spec(uint32 creator, uint32 type, unsigned index, FSSpec* spec)
{
    UNEXPECTED_MANAGER();
}

int filetype_map_lookup_path(const char* extension, FileTypeMapping* mapping)
{
    UNEXPECTED_MANAGER();
}

OSErr FSClose(INTEGER reference_number)
{
    UNEXPECTED_MANAGER();
}

OSErr FSRead(INTEGER reference_number, LONGINT* count, Ptr buffer)
{
    UNEXPECTED_MANAGER();
}

OSErr FSWrite(INTEGER reference_number, LONGINT* count, Ptr buffer)
{
    UNEXPECTED_MANAGER();
}

OSErr GetEOF(INTEGER reference_number, LONGINT* eof)
{
    UNEXPECTED_MANAGER();
}

OSErrRET PBGetCatInfo(CInfoPBPtr parameters, BOOLEAN async)
{
    UNEXPECTED_MANAGER();
}

#define FILE_MANAGER_STUB(name, type)        \
    OSErrRET name(type block, BOOLEAN async) \
    {                                        \
        UNEXPECTED_MANAGER();                \
    }

FILE_MANAGER_STUB(PBAllocContig, ParmBlkPtr)
FILE_MANAGER_STUB(PBAllocate, ParmBlkPtr)
FILE_MANAGER_STUB(PBCatMove, CMovePBPtr)
FILE_MANAGER_STUB(PBClose, ParmBlkPtr)
FILE_MANAGER_STUB(PBDirCreate, HParmBlkPtr)
FILE_MANAGER_STUB(PBFlushFile, ParmBlkPtr)
FILE_MANAGER_STUB(PBGetEOF, ParmBlkPtr)
FILE_MANAGER_STUB(PBGetFCBInfo, FCBPBPtr)
FILE_MANAGER_STUB(PBGetFPos, ParmBlkPtr)
FILE_MANAGER_STUB(PBHCreate, HParmBlkPtr)
FILE_MANAGER_STUB(PBHDelete, HParmBlkPtr)
FILE_MANAGER_STUB(PBHGetVInfo, HParmBlkPtr)
FILE_MANAGER_STUB(PBHOpenRF, HParmBlkPtr)
FILE_MANAGER_STUB(PBHRename, HParmBlkPtr)
FILE_MANAGER_STUB(PBHRstFLock, HParmBlkPtr)
FILE_MANAGER_STUB(PBHSetFLock, HParmBlkPtr)
FILE_MANAGER_STUB(PBOpenDF, HParmBlkPtr)
FILE_MANAGER_STUB(PBRead, ParmBlkPtr)
FILE_MANAGER_STUB(PBSetCatInfo, CInfoPBPtr)
FILE_MANAGER_STUB(PBSetEOF, ParmBlkPtr)
FILE_MANAGER_STUB(PBSetFPos, ParmBlkPtr)
FILE_MANAGER_STUB(PBSetVInfo, HParmBlkPtr)
FILE_MANAGER_STUB(PBWrite, ParmBlkPtr)

#undef FILE_MANAGER_STUB

OSErr C_FSMakeFSSpec(int16 volume, int32 directory, Str255 name, FSSpecPtr spec)
{
    UNEXPECTED_MANAGER();
}

OSErr C_FindFolder(int16 volume, OSType type, Boolean create, int16* found_volume, int32* directory)
{
    UNEXPECTED_MANAGER();
}

OSErr GetVRefNum(INTEGER reference_number, INTEGER* volume)
{
    UNEXPECTED_MANAGER();
}

OSErr ROMlib_file_complete(void* block, BOOLEAN asynchronous, OSErr status)
{
    UNEXPECTED_MANAGER();
}

OSErr ROMlib_filename_from_fsspec(char** path, FSSpec* spec)
{
    UNEXPECTED_MANAGER();
}

OSErr ROMlib_host_fsspec(char* path, FSSpec* spec)
{
    UNEXPECTED_MANAGER();
}

int ROMlib_host_get_file_id(const char* path, ROMlib_host_file_id* id)
{
    UNEXPECTED_MANAGER();
}

char* ROMlib_host_locate_file_id(const char* path, const ROMlib_host_file_id* id)
{
    assert(!"Unexpected host path lookup during isolated service checks");
    return NULL;
}

HVCB* ROMlib_vcbbyvrn(short volume)
{
    assert(!"Unexpected volume lookup during isolated service checks");
    return NULL;
}

int ROMlib_win_volume_bytes(const char* path, uint64* total, uint64* free_bytes)
{
    UNEXPECTED_MANAGER();
}

OSErr SetEOF(INTEGER reference_number, LONGINT eof)
{
    UNEXPECTED_MANAGER();
}

OSErr SetFPos(INTEGER reference_number, INTEGER position_mode, LONGINT position_offset)
{
    UNEXPECTED_MANAGER();
}

INTEGER C_ResError(void)
{
    return -1;
}

INTEGER ROMlib_setreserr(INTEGER error)
{
    return error;
}

INTEGER ROMlib_open_data_resource_descriptor(INTEGER refnum)
{
    return -1;
}

OSErr ROMlib_create_data_resource_file(const FSSpec* spec)
{
    UNEXPECTED_MANAGER();
}

OSErr ROMlib_open_data_resource_file(const FSSpec* spec, SignedByte permission, INTEGER* reference)
{
    UNEXPECTED_MANAGER();
}

OSErr ROMlib_resource_chain_refnum(int top, INTEGER current, INTEGER* reference)
{
    UNEXPECTED_MANAGER();
}

OSErr ROMlib_initialize_resource_descriptor(INTEGER refnum)
{
    return -4;
}

OSErr ROMlib_detach_resource_file(INTEGER reference)
{
    UNEXPECTED_MANAGER();
}

OSErr ROMlib_insert_resource_file(INTEGER reference, INTEGER location)
{
    UNEXPECTED_MANAGER();
}

OSErr ROMlib_open_orphan_resource_file(const FSSpec* spec, SignedByte permission, INTEGER* reference)
{
    UNEXPECTED_MANAGER();
}

int ROMlib_resource_file_already_open(const FSSpec* spec, Boolean* in_chain, INTEGER* reference)
{
    if (in_chain)
        *in_chain = 0;
    if (reference)
        *reference = CW(-1);
    return 0;
}
