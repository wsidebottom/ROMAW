/*+===================================================================
  File:      fs_ref_entries.h

  Summary:   Maps the Carbon File Manager's FSRef and volume wrappers to native operations.

  Classes:   None.

  Functions: Private PowerPC frame wrappers for the export registry.

  Origin:    CarbonLib 1.4 and the System 9.2.1 InterfaceLib overlay.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_FS_REF_ENTRIES_H
#define ROMAW_CARBON_FS_REF_ENTRIES_H

#include "fs_refs.h"

#define CARBON_FS(name, operation)                             \
    static int carbon_fs_##name(ROMlib_carbon_ppc_frame* call) \
    {                                                          \
        return ROMlib_carbon_fs_ref(call, operation);          \
    }

#define CARBON_FS_PAIR(name, operation)                                  \
    static int carbon_fs_##name##Sync(ROMlib_carbon_ppc_frame* call)     \
    {                                                                    \
        return ROMlib_carbon_fs_ref_parameter_block(call, operation, 0); \
    }                                                                    \
    static int carbon_fs_##name##Async(ROMlib_carbon_ppc_frame* call)    \
    {                                                                    \
        return ROMlib_carbon_fs_ref_parameter_block(call, operation, 1); \
    }

CARBON_FS(FSpMakeFSRef, carbon_fs_make_ref_from_spec)
CARBON_FS(FSPathMakeRef, carbon_fs_path_make_ref)
CARBON_FS(FSRefMakePath, carbon_fs_ref_make_path)
CARBON_FS(FSGetCatalogInfo, carbon_fs_get_catalog)
CARBON_FS(FSCompareFSRefs, carbon_fs_compare_refs)
CARBON_FS(FSFindFolder, carbon_fs_find_folder)
CARBON_FS(FSGetVolumeInfo, carbon_fs_get_volume)
CARBON_FS(FSSetVolumeInfo, carbon_fs_set_volume)

CARBON_FS_PAIR(PBMakeFSRef, carbon_fs_make_ref)
CARBON_FS_PAIR(PBGetCatalogInfo, carbon_fs_get_catalog)
CARBON_FS_PAIR(PBCompareFSRefs, carbon_fs_compare_refs)
CARBON_FS_PAIR(PBGetVolumeInfo, carbon_fs_get_volume)
CARBON_FS_PAIR(PBSetVolumeInfo, carbon_fs_set_volume)

CARBON_FS(FSMakeFSRefUnicode, carbon_fs_make_ref_unicode)
CARBON_FS(FSCreateFileUnicode, carbon_fs_create_file)
CARBON_FS(FSCreateDirectoryUnicode, carbon_fs_create_directory)
CARBON_FS(FSDeleteObject, carbon_fs_delete_object)
CARBON_FS(FSMoveObject, carbon_fs_move_object)
CARBON_FS(FSRenameUnicode, carbon_fs_rename_object)
CARBON_FS(FSSetCatalogInfo, carbon_fs_set_catalog)
CARBON_FS(FSGetDataForkName, carbon_fs_data_name)
CARBON_FS(FSGetResourceForkName, carbon_fs_resource_name)
CARBON_FS(FSOpenFork, carbon_fs_open_fork)
CARBON_FS(FSReadFork, carbon_fs_read_fork)
CARBON_FS(FSWriteFork, carbon_fs_write_fork)
CARBON_FS(FSGetForkPosition, carbon_fs_get_position)
CARBON_FS(FSSetForkPosition, carbon_fs_set_position)
CARBON_FS(FSGetForkSize, carbon_fs_get_size)
CARBON_FS(FSSetForkSize, carbon_fs_set_size)
CARBON_FS(FSAllocateFork, carbon_fs_allocate_fork)
CARBON_FS(FSFlushFork, carbon_fs_flush_fork)
CARBON_FS(FSCloseFork, carbon_fs_close_fork)

CARBON_FS_PAIR(PBMakeFSRefUnicode, carbon_fs_make_ref_unicode)
CARBON_FS_PAIR(PBCreateFileUnicode, carbon_fs_create_file)
CARBON_FS_PAIR(PBCreateDirectoryUnicode, carbon_fs_create_directory)
CARBON_FS_PAIR(PBDeleteObject, carbon_fs_delete_object)
CARBON_FS_PAIR(PBMoveObject, carbon_fs_move_object)
CARBON_FS_PAIR(PBRenameUnicode, carbon_fs_rename_object)
CARBON_FS_PAIR(PBSetCatalogInfo, carbon_fs_set_catalog)
CARBON_FS_PAIR(PBOpenFork, carbon_fs_open_fork)
CARBON_FS_PAIR(PBReadFork, carbon_fs_read_fork)
CARBON_FS_PAIR(PBWriteFork, carbon_fs_write_fork)
CARBON_FS_PAIR(PBGetForkPosition, carbon_fs_get_position)
CARBON_FS_PAIR(PBSetForkPosition, carbon_fs_set_position)
CARBON_FS_PAIR(PBGetForkSize, carbon_fs_get_size)
CARBON_FS_PAIR(PBSetForkSize, carbon_fs_set_size)
CARBON_FS_PAIR(PBAllocateFork, carbon_fs_allocate_fork)
CARBON_FS_PAIR(PBFlushFork, carbon_fs_flush_fork)
CARBON_FS_PAIR(PBCloseFork, carbon_fs_close_fork)

CARBON_FS(FSGetForkCBInfo, carbon_fs_fork_info)
CARBON_FS(FSCreateFork, carbon_fs_create_fork)
CARBON_FS(FSDeleteFork, carbon_fs_delete_fork)
CARBON_FS(FSIterateForks, carbon_fs_iterate_forks)
CARBON_FS(FSOpenIterator, carbon_fs_open_iterator)
CARBON_FS(FSCloseIterator, carbon_fs_close_iterator)
CARBON_FS(FSGetCatalogInfoBulk, carbon_fs_bulk_catalog)
CARBON_FS(FSFindFolderExtended, carbon_fs_find_folder_extended)
CARBON_FS(GetVRefNum, carbon_fs_get_volume_ref)
CARBON_FS(FlushVol, carbon_fs_flush_volume)
CARBON_FS(CatMove, carbon_fs_cat_move)

CARBON_FS_PAIR(PBGetForkCBInfo, carbon_fs_fork_info)
CARBON_FS_PAIR(PBCreateFork, carbon_fs_create_fork)
CARBON_FS_PAIR(PBDeleteFork, carbon_fs_delete_fork)
CARBON_FS_PAIR(PBIterateForks, carbon_fs_iterate_forks)
CARBON_FS_PAIR(PBOpenIterator, carbon_fs_open_iterator)
CARBON_FS_PAIR(PBCloseIterator, carbon_fs_close_iterator)
CARBON_FS_PAIR(PBGetCatalogInfoBulk, carbon_fs_bulk_catalog)
CARBON_FS_PAIR(PBXGetVolInfo, carbon_fs_extended_volume)

#undef CARBON_FS_PAIR
#undef CARBON_FS

#define CARBON_FS_ALIAS(name, operation)                    \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call) \
    {                                                       \
        return ROMlib_carbon_fs_alias(call, operation);     \
    }

CARBON_FS_ALIAS(FSNewAlias, fs_alias_new)
CARBON_FS_ALIAS(FSNewAliasMinimal, fs_alias_minimal)
CARBON_FS_ALIAS(FSIsAliasFile, fs_alias_info)
CARBON_FS_ALIAS(FSResolveAlias, fs_alias_resolve)
CARBON_FS_ALIAS(FSResolveAliasWithMountFlags, fs_alias_resolve_mount)
CARBON_FS_ALIAS(FSResolveAliasFile, fs_alias_resolve_file)
CARBON_FS_ALIAS(FSResolveAliasFileWithMountFlags, fs_alias_resolve_file_mount)
CARBON_FS_ALIAS(FSFollowFinderAlias, fs_alias_follow)
CARBON_FS_ALIAS(FSUpdateAlias, fs_alias_update)
#undef CARBON_FS_ALIAS

#define CARBON_FS_RESOURCE(name, operation)                 \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call) \
    {                                                       \
        return ROMlib_carbon_fs_resource(call, operation);  \
    }

CARBON_FS_RESOURCE(FSCreateResFile, fs_resource_create)
CARBON_FS_RESOURCE(FSCreateResourceFile, fs_resource_create_named)
CARBON_FS_RESOURCE(FSOpenResFile, fs_resource_open)
CARBON_FS_RESOURCE(FSOpenResourceFile, fs_resource_open_named)
CARBON_FS_RESOURCE(FSResourceFileAlreadyOpen, fs_resource_is_open)
CARBON_FS_RESOURCE(FSpResourceFileAlreadyOpen, fs_resource_spec_is_open)
CARBON_FS_RESOURCE(FSpCreateDataForkAsResourceFile, fs_resource_create_data_spec)
CARBON_FS_RESOURCE(FSpOpenDataForkAsResourceFile, fs_resource_open_data_spec)
CARBON_FS_RESOURCE(GetTopResourceFile, fs_resource_top)
CARBON_FS_RESOURCE(GetNextResourceFile, fs_resource_next)
CARBON_FS_RESOURCE(DetachResourceFile, fs_resource_detach)
CARBON_FS_RESOURCE(InsertResourceFile, fs_resource_insert)
CARBON_FS_RESOURCE(FSpOpenOrphanResFile, fs_resource_open_orphan)
#undef CARBON_FS_RESOURCE

#endif
