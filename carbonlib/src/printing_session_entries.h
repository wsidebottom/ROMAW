/*+===================================================================
  File:      printing_session_entries.h

  Summary:   Printing session validation order and CarbonLib 1.4 ABI data.

  Classes:   None.

  Functions: Declares and instantiates the session operations when included
             with the corresponding macro.

  Origin:    CarbonLib 1.4 Printing Manager code and Universal Interfaces
             3.4.2 PMCore.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

CARBON_PRINT_SESSION(PMSessionDefaultPrintSettings, 0x8e35c, 0x8681c, 2, 0, 0, session_open, session_settings, 1)
CARBON_PRINT_SESSION(PMSessionDefaultPageFormat, 0x8e470, 0x878ac, 2, 0, 0, session_open, session_page_format, 1)
CARBON_PRINT_SESSION(PMSessionValidatePrintSettings, 0x8e584, 0x8690c, 3, 0, 4, session_open, session_settings, 1)
CARBON_PRINT_SESSION(PMSessionValidatePageFormat, 0x8e69c, 0x87a50, 3, 0, 4, session_open, session_page_format, 1)
CARBON_PRINT_SESSION(PMSessionSetIdleProc, 0x8e7b4, 0x847cc, 2, 0, 0, session_idle, no_session_ticket, 0)
CARBON_PRINT_SESSION(PMSessionSetDataInSession, 0x8ed54, 0x897c8, 3, 6, 0, session_preserve_error, no_session_ticket, 0)
CARBON_PRINT_SESSION(PMSessionGetDataFromSession, 0x8ee7c, 0x89894, 3, 6, 0, session_preserve_error, no_session_ticket,
                     0)
CARBON_PRINT_SESSION(PMSessionError, 0x8efa4, 0x84d00, 1, 0, 0, session_any, no_session_ticket, 0)
CARBON_PRINT_SESSION(PMSessionSetError, 0x8f050, 0x84d24, 2, 0, 0, session_open, no_session_ticket, 1)
CARBON_PRINT_SESSION(PMSessionGeneral, 0x8f11c, 0x847d8, 2, 0, 2, session_open, no_session_ticket, 1)
CARBON_PRINT_SESSION(PMSessionEnableColorSync, 0x8f490, 0x85144, 1, 0, 0, session_in_page, no_session_ticket, 1)
CARBON_PRINT_SESSION(PMSessionDisableColorSync, 0x8f560, 0x85170, 1, 0, 0, session_in_page, no_session_ticket, 1)
CARBON_PRINT_SESSION(PMSessionPostScriptHandle, 0x8f7b8, 0x85360, 2, 2, 0, session_any, no_session_ticket, 1)
CARBON_PRINT_SESSION(PMSessionPostScriptData, 0x8f894, 0x853c8, 3, 6, 0, session_any, no_session_ticket, 1)
CARBON_PRINT_SESSION(PMSessionPostScriptFile, 0x8fab4, 0x85448, 2, 2, 0, session_any, no_session_ticket, 1)
CARBON_PRINT_SESSION(PMSessionGetDocumentFormatGeneration, 0x8fb90, 0x857e4, 2, 2, 0, session_any, no_session_ticket, 1)
CARBON_PRINT_SESSION(PMSessionGetDocumentFormatSupported, 0x8fdc8, 0x85a44, 3, 2, 0, session_any, no_session_ticket, 1)
CARBON_PRINT_SESSION(PMSessionIsDocumentFormatSupported, 0x8fe9c, 0x85b24, 3, 4, 0, session_any, no_session_ticket, 1)
CARBON_PRINT_SESSION(PMSessionGetGraphicsContext, 0x8ff70, 0x85c24, 3, 0, 0, session_in_page, no_session_ticket, 1)
CARBON_PRINT_SESSION(PMSessionGetCurrentPrinter, 0x90048, 0x0, 2, 2, 0, session_any, no_session_ticket, 1)
