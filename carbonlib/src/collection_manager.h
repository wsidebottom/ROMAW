/*+===================================================================
  File:      collection_manager.h

  Summary:   Defines native frame adapters and operation IDs for the Collection Manager.

  Classes:   None.

  Functions: Declares the native Collection Manager dispatcher.

  Origin:    System 9.2.1 Collections and CollectionsLib fragments and
             Universal Interfaces 3.4.2 Collections.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_COLLECTION_MANAGER_H
#define ROMAW_CARBON_COLLECTION_MANAGER_H

#include "carbonlib.h"

enum carbon_collection_operation {
    collection_new,
    collection_dispose,
    collection_clone,
    collection_owners,
    collection_copy,
    collection_default_attrs,
    collection_set_default_attrs,
    collection_count,
    collection_add,
    collection_get,
    collection_remove,
    collection_set_info,
    collection_info,
    collection_replace_index,
    collection_get_index,
    collection_remove_index,
    collection_set_index_info,
    collection_index_info,
    collection_tag_exists,
    collection_count_tags,
    collection_index_tag,
    collection_count_tagged,
    collection_get_tagged,
    collection_tagged_info,
    collection_purge,
    collection_purge_tag,
    collection_empty,
    collection_exception,
    collection_set_exception,
    collection_add_handle,
    collection_get_handle,
    collection_replace_index_handle,
    collection_get_index_handle,
    collection_retain,
    collection_release,
    collection_flatten,
    collection_flatten_partial,
    collection_unflatten,
    collection_flatten_handle,
    collection_unflatten_handle,
    collection_get_new
};

int ROMlib_carbon_collection(enum carbon_collection_operation operation, ROMlib_carbon_ppc_frame* call);

#endif
