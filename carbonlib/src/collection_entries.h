/*+===================================================================
  File:      collection_entries.h

  Summary:   Binds Collection Manager operations to the native Carbon registry.

  Classes:   None.

  Functions: Private frame adapters for Collection Manager entry points.

  Origin:    CarbonLib 1.4 export metadata and System 9.2.1 CollectionsLib.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_COLLECTION_ENTRIES_H
#define ROMAW_CARBON_COLLECTION_ENTRIES_H

#include "collection_manager.h"

#define CARBON_COLLECTION_ENTRY(name, operation)            \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call) \
    {                                                       \
        return ROMlib_carbon_collection(operation, call);   \
    }

CARBON_COLLECTION_ENTRY(NewCollection, collection_new)
CARBON_COLLECTION_ENTRY(DisposeCollection, collection_dispose)
CARBON_COLLECTION_ENTRY(CloneCollection, collection_clone)
CARBON_COLLECTION_ENTRY(CountCollectionOwners, collection_owners)
CARBON_COLLECTION_ENTRY(CopyCollection, collection_copy)
CARBON_COLLECTION_ENTRY(GetCollectionDefaultAttributes, collection_default_attrs)
CARBON_COLLECTION_ENTRY(SetCollectionDefaultAttributes, collection_set_default_attrs)
CARBON_COLLECTION_ENTRY(CountCollectionItems, collection_count)
CARBON_COLLECTION_ENTRY(AddCollectionItem, collection_add)
CARBON_COLLECTION_ENTRY(GetCollectionItem, collection_get)
CARBON_COLLECTION_ENTRY(RemoveCollectionItem, collection_remove)
CARBON_COLLECTION_ENTRY(SetCollectionItemInfo, collection_set_info)
CARBON_COLLECTION_ENTRY(GetCollectionItemInfo, collection_info)
CARBON_COLLECTION_ENTRY(ReplaceIndexedCollectionItem, collection_replace_index)
CARBON_COLLECTION_ENTRY(GetIndexedCollectionItem, collection_get_index)
CARBON_COLLECTION_ENTRY(RemoveIndexedCollectionItem, collection_remove_index)
CARBON_COLLECTION_ENTRY(SetIndexedCollectionItemInfo, collection_set_index_info)
CARBON_COLLECTION_ENTRY(GetIndexedCollectionItemInfo, collection_index_info)
CARBON_COLLECTION_ENTRY(CollectionTagExists, collection_tag_exists)
CARBON_COLLECTION_ENTRY(CountCollectionTags, collection_count_tags)
CARBON_COLLECTION_ENTRY(GetIndexedCollectionTag, collection_index_tag)
CARBON_COLLECTION_ENTRY(CountTaggedCollectionItems, collection_count_tagged)
CARBON_COLLECTION_ENTRY(GetTaggedCollectionItem, collection_get_tagged)
CARBON_COLLECTION_ENTRY(GetTaggedCollectionItemInfo, collection_tagged_info)
CARBON_COLLECTION_ENTRY(PurgeCollection, collection_purge)
CARBON_COLLECTION_ENTRY(PurgeCollectionTag, collection_purge_tag)
CARBON_COLLECTION_ENTRY(EmptyCollection, collection_empty)
CARBON_COLLECTION_ENTRY(GetCollectionExceptionProc, collection_exception)
CARBON_COLLECTION_ENTRY(SetCollectionExceptionProc, collection_set_exception)
CARBON_COLLECTION_ENTRY(AddCollectionItemHdl, collection_add_handle)
CARBON_COLLECTION_ENTRY(GetCollectionItemHdl, collection_get_handle)
CARBON_COLLECTION_ENTRY(ReplaceIndexedCollectionItemHdl, collection_replace_index_handle)
CARBON_COLLECTION_ENTRY(GetIndexedCollectionItemHdl, collection_get_index_handle)
CARBON_COLLECTION_ENTRY(RetainCollection, collection_retain)
CARBON_COLLECTION_ENTRY(ReleaseCollection, collection_release)
CARBON_COLLECTION_ENTRY(GetCollectionRetainCount, collection_owners)
CARBON_COLLECTION_ENTRY(FlattenCollection, collection_flatten)
CARBON_COLLECTION_ENTRY(FlattenPartialCollection, collection_flatten_partial)
CARBON_COLLECTION_ENTRY(UnflattenCollection, collection_unflatten)
CARBON_COLLECTION_ENTRY(FlattenCollectionToHdl, collection_flatten_handle)
CARBON_COLLECTION_ENTRY(UnflattenCollectionFromHdl, collection_unflatten_handle)
CARBON_COLLECTION_ENTRY(GetNewCollection, collection_get_new)

#undef CARBON_COLLECTION_ENTRY
#endif
