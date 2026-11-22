/*+===================================================================
  File:      printing_legacy_entries.h

  Summary:   Printing Manager operations on the default session.

  Classes:   None.

  Functions: Declares and instantiates the compatibility entry points.

  Origin:    CarbonLib 1.4 default-session wrapper disassembly.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

CARBON_PRINT_LEGACY(PMSetIdleProc, 0x90ba0, PMSessionSetIdleProc, 1)
CARBON_PRINT_LEGACY(PMError, 0x90c24, PMSessionError, 0)
CARBON_PRINT_LEGACY(PMSetError, 0x90c98, PMSessionSetError, 1)
CARBON_PRINT_LEGACY(PMGeneral, 0x90d1c, PMSessionGeneral, 1)
CARBON_PRINT_LEGACY(PMDefaultPrintSettings, 0x90f84, PMSessionDefaultPrintSettings, 1)
CARBON_PRINT_LEGACY(PMValidatePrintSettings, 0x9229c, PMSessionValidatePrintSettings, 2)
CARBON_PRINT_LEGACY(PMDefaultPageFormat, 0x924ac, PMSessionDefaultPageFormat, 1)
CARBON_PRINT_LEGACY(PMValidatePageFormat, 0x933c0, PMSessionValidatePageFormat, 2)
CARBON_PRINT_LEGACY(PMEnableColorSync, 0x93714, PMSessionEnableColorSync, 0)
CARBON_PRINT_LEGACY(PMDisableColorSync, 0x93788, PMSessionDisableColorSync, 0)
CARBON_PRINT_LEGACY(PMPostScriptHandle, 0x93bd8, PMSessionPostScriptHandle, 1)
CARBON_PRINT_LEGACY(PMPostScriptData, 0x93c5c, PMSessionPostScriptData, 2)
CARBON_PRINT_LEGACY(PMPostScriptBegin, 0x93b0c, PMSessionPostScriptBegin, 0)
CARBON_PRINT_LEGACY(PMPostScriptEnd, 0x93b80, PMSessionPostScriptEnd, 0)
CARBON_PRINT_LEGACY(PMPostScriptFile, 0x93cf0, PMSessionPostScriptFile, 1)
CARBON_PRINT_LEGACY(PMPageSetupDialog, 0x946b8, PMSessionPageSetupDialog, 2)
CARBON_PRINT_LEGACY(PMPrintDialog, 0x94718, PMSessionPrintDialog, 3)
CARBON_PRINT_LEGACY(PMPageSetupDialogInit, 0x9477c, PMSessionPageSetupDialogInit, 2)
CARBON_PRINT_LEGACY(PMPrintDialogInitWithPageFormat, 0x94920, PMSessionPrintDialogInit, 3)
CARBON_PRINT_LEGACY(PMPrintDialogMain, 0x94984, PMSessionPrintDialogMain, 4)
CARBON_PRINT_LEGACY(PMPageSetupDialogMain, 0x949ec, PMSessionPageSetupDialogMain, 3)
CARBON_PRINT_LEGACY(PMMakeOldPrintRecord, 0x90da0, PMSessionMakeOldPrintRecord, 3)
CARBON_PRINT_LEGACY(PMConvertOldPrintRecord, 0x936c0, PMSessionConvertOldPrintRecord, 3)
