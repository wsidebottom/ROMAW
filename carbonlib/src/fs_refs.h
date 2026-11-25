/*+===================================================================
  File:      fs_refs.h

  Summary:   Declares the Carbon File Manager archive entry points and their selectors.

  Classes:   None.

  Functions: ROMlib_carbon_fs_ref, ROMlib_carbon_fs_ref_parameter_block, ROMlib_carbon_fs_alias,
             ROMlib_carbon_fs_resource.

  Origin:    Universal Interfaces 3.4.2 Files.h and the CarbonLib 1.4 File Manager.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_FS_REFS_H
#define ROMAW_CARBON_FS_REFS_H

#include "file_manager.h"

enum carbon_fs_operation {
    carbon_fs_extended_volume = 0x12,
    carbon_fs_get_volume = 0x1d,
    carbon_fs_set_volume = 0x1e,
    carbon_fs_read_fork = 0x51,
    carbon_fs_write_fork = 0x52,
    carbon_fs_get_position = 0x53,
    carbon_fs_set_position = 0x54,
    carbon_fs_get_size = 0x55,
    carbon_fs_set_size = 0x56,
    carbon_fs_allocate_fork = 0x57,
    carbon_fs_flush_fork = 0x58,
    carbon_fs_close_fork = 0x59,
    carbon_fs_fork_info = 0x5a,
    carbon_fs_close_iterator = 0x5b,
    carbon_fs_bulk_catalog = 0x5c,
    carbon_fs_search_catalog = 0x5d,
    carbon_fs_make_ref = 0x6e,
    carbon_fs_create_file = 0x70,
    carbon_fs_create_directory = 0x71,
    carbon_fs_delete_object = 0x72,
    carbon_fs_move_object = 0x73,
    carbon_fs_rename_object = 0x74,
    carbon_fs_get_catalog = 0x76,
    carbon_fs_set_catalog = 0x77,
    carbon_fs_open_iterator = 0x78,
    carbon_fs_open_fork = 0x79,
    carbon_fs_make_ref_unicode = 0x7a,
    carbon_fs_compare_refs = 0x7c,
    carbon_fs_create_fork = 0x7d,
    carbon_fs_delete_fork = 0x7e,
    carbon_fs_iterate_forks = 0x7f,
    carbon_fs_make_ref_from_spec = 0x100,
    carbon_fs_find_folder,
    carbon_fs_find_folder_extended,
    carbon_fs_data_name,
    carbon_fs_resource_name,
    carbon_fs_get_volume_ref,
    carbon_fs_flush_volume,
    carbon_fs_cat_move,
    carbon_fs_path_make_ref,
    carbon_fs_ref_make_path,
};

int ROMlib_carbon_fs_ref(ROMlib_carbon_ppc_frame* call, enum carbon_fs_operation operation);
int ROMlib_carbon_fs_ref_parameter_block(ROMlib_carbon_ppc_frame* call, enum carbon_fs_operation operation,
                                         int asynchronous);

enum carbon_fs_alias_operation {
    fs_alias_new,
    fs_alias_minimal,
    fs_alias_info,
    fs_alias_resolve,
    fs_alias_resolve_mount,
    fs_alias_resolve_file,
    fs_alias_resolve_file_mount,
    fs_alias_follow,
    fs_alias_update,
};

int ROMlib_carbon_fs_alias(ROMlib_carbon_ppc_frame* call, enum carbon_fs_alias_operation operation);

enum carbon_fs_resource_operation {
    fs_resource_create,
    fs_resource_create_named,
    fs_resource_open,
    fs_resource_open_named,
    fs_resource_is_open,
    fs_resource_spec_is_open,
    fs_resource_create_data_spec,
    fs_resource_open_data_spec,
    fs_resource_top,
    fs_resource_next,
    fs_resource_detach,
    fs_resource_insert,
    fs_resource_open_orphan,
};

int ROMlib_carbon_fs_resource(ROMlib_carbon_ppc_frame* call, enum carbon_fs_resource_operation operation);

#endif
