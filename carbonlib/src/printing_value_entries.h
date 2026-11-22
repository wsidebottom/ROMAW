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
CARBON_PRINT_VALUE(PMTicketGetTypedItem, 0x7e980, get_ticket_typed_item)
CARBON_PRINT_VALUE(PMTicketSetRetainCount, 0x80354, set_ticket_retain_count)
CARBON_PRINT_VALUE(PMTicketLock, 0x810b8, lock_print_ticket)
CARBON_PRINT_VALUE(PMTicketUnlock, 0x8110c, unlock_print_ticket)
