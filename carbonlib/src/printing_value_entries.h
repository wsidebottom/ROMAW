/*+===================================================================
  File:      printing_value_entries.h

  Summary:   CarbonLib 1.4 Printing Manager and Core Foundation conversions.

  Classes:   None.

  Functions: Declares and instantiates print value conversion entry points.

  Origin:    CarbonLib 1.4 Printing Manager conversion disassembly.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

CARBON_PRINT_VALUE(PMCreateCFStringFromCString, 0x9542c, from_cstring)
CARBON_PRINT_VALUE(PMCreateCFStringFromPString, 0x95488, from_pascal_string)
CARBON_PRINT_VALUE(PMCreateCFNumberFromDouble, 0x9562c, from_double)
CARBON_PRINT_VALUE(PMCreateCFNumberFromSInt32, 0x9568c, from_int32)
CARBON_PRINT_VALUE(PMCreateCFArrayFromPMRect, 0x95780, from_page_rect)
CARBON_PRINT_VALUE(PMCreateCFArrayFromPMResolution, 0x95980, from_resolution)
CARBON_PRINT_VALUE(PMCreatePMRectFromCFArray, 0x95908, to_page_rect)
CARBON_PRINT_VALUE(PMCreatePMResolutionFromCFArray, 0x95a80, to_resolution)
CARBON_PRINT_VALUE(PMCreateCFStringFromCFIndex, 0x954e0, from_index)
CARBON_PRINT_VALUE(PMCreateCFArray, 0x956ec, from_array)
CARBON_PRINT_VALUE(PMCreateCStringFromCFString, 0x95568, to_cstring)
CARBON_PRINT_VALUE(PMCFTypeIsStringOrArrayOfStrings, 0x95fe4, is_string_or_array)
CARBON_PRINT_VALUE(PMCFPrefsCopyMutableArray, 0x95af8, copy_preference_array)
CARBON_PRINT_VALUE(PMGetJobName, 0x915cc, get_pascal_job_name)
CARBON_PRINT_VALUE(PMSetJobName, 0x91cd4, set_pascal_job_name)
CARBON_PRINT_VALUE(PMReadCFDataFromFile, 0x95cc0, read_print_data)
CARBON_PRINT_VALUE(PMWriteCFDataToFile, 0x95bc4, write_print_data)
CARBON_PRINT_VALUE(PMReadCFPropertyListFromFile, 0x95f0c, read_print_plist)
CARBON_PRINT_VALUE(PMWriteCFPropertyListToFile, 0x95e34, write_print_plist)
CARBON_PRINT_VALUE(CreateSubTicketKey, 0x7ebe4, create_subticket_key)
CARBON_PRINT_VALUE(FindSubDict, 0x7f1e0, find_subticket)
CARBON_PRINT_VALUE(FindAllSubTickets, 0x7f4e4, find_all_subtickets)
CARBON_PRINT_VALUE(SearchLeafForItem, 0x7f278, search_ticket_leaf)
CARBON_PRINT_VALUE(SearchNodeForItem, 0x7f358, search_ticket_node)
CARBON_PRINT_VALUE(FindEnumTicketType, 0x7eef8, find_ticket_type)
CARBON_PRINT_VALUE(ValidItem, 0x7e0a4, validate_ticket_item)
CARBON_PRINT_VALUE(AddItem, 0x7e674, add_ticket_item)
CARBON_PRINT_VALUE(UpdateItem, 0x7e870, update_ticket_item)
CARBON_PRINT_VALUE(BuildItemDictionary, 0x7e158, build_ticket_item_dictionary)
CARBON_PRINT_VALUE(GetItemArrayAndDict, 0x7e318, get_ticket_item_array)
CARBON_PRINT_VALUE(GetLockedBitState, 0x7e3d8, get_ticket_item_lock)
CARBON_PRINT_VALUE(SetLockedBitState, 0x7e498, set_ticket_item_lock)
CARBON_PRINT_VALUE(LockOrUnlockItem, 0x7e59c, replace_ticket_item_lock)
CARBON_PRINT_VALUE(PMTicketGetTypedItem, 0x7e980, get_ticket_typed_item)
CARBON_PRINT_VALUE(PMSetProfile, 0x92070, set_print_profile)
CARBON_PRINT_VALUE(RemoveMetaData, 0x7eb00, remove_ticket_metadata)
CARBON_PRINT_VALUE(PMMutexCreate, 0x97648, create_print_mutex)
CARBON_PRINT_VALUE(PMMutexRetain, 0x9772c, retain_print_mutex)
CARBON_PRINT_VALUE(PMMutexRelease, 0x977b8, release_print_mutex)
CARBON_PRINT_VALUE(PMMutexLock, 0x97854, lock_print_mutex)
CARBON_PRINT_VALUE(PMMutexTryLock, 0x978e0, try_print_mutex)
CARBON_PRINT_VALUE(PMMutexUnlock, 0x9798c, unlock_print_mutex)
CARBON_PRINT_VALUE(PMTicketSetRetainCount, 0x80354, set_ticket_retain_count)
CARBON_PRINT_VALUE(PMTicketLock, 0x810b8, lock_print_ticket)
CARBON_PRINT_VALUE(PMTicketUnlock, 0x8110c, unlock_print_ticket)
