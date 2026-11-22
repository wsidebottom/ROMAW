/*+===================================================================
  File:      printing_lifecycle_entries.h

  Summary:   Lists Carbon Printing Manager document, page, dialog, and format entries.

  Classes:   None.

  Functions: Native PowerPC frame entry declarations and registration rows.

  Origin:    CarbonLib 1.4 and Universal Interfaces 3.4.2 PMCore.h and PMApplication.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

CARBON_PRINT_LIFECYCLE(PMSessionBeginDocumentNoDialog, 0x8e86c, begin_document, print_idle)
CARBON_PRINT_LIFECYCLE(PMSessionEndDocumentNoDialog, 0x8e9f8, end_document, print_document)
CARBON_PRINT_LIFECYCLE(PMSessionBeginPageNoDialog, 0x8eae8, begin_page, print_document)
CARBON_PRINT_LIFECYCLE(PMSessionEndPageNoDialog, 0x8ec64, end_page, print_page)
CARBON_PRINT_LIFECYCLE(PMSessionPostScriptBegin, 0x8f630, begin_postscript, print_page)
CARBON_PRINT_LIFECYCLE(PMSessionPostScriptEnd, 0x8f6f4, end_postscript, print_page)
CARBON_PRINT_FORMAT(PMSessionSetDocumentFormatGeneration, 0x8fc6c)
CARBON_PRINT_LIFECYCLE(PMSessionPageSetupDialog, 0x93f44, page_setup_dialog, print_idle)
CARBON_PRINT_LIFECYCLE(PMSessionPrintDialog, 0x94068, job_dialog, print_idle)
CARBON_PRINT_LIFECYCLE(PMSessionBeginDocument, 0x9516c, begin_document, print_idle)
CARBON_PRINT_LIFECYCLE(PMSessionEndDocument, 0x95190, end_document, print_document)
CARBON_PRINT_LIFECYCLE(PMSessionBeginPage, 0x951b4, begin_page, print_document)
CARBON_PRINT_LIFECYCLE(PMSessionEndPage, 0x951d8, end_page, print_page)
