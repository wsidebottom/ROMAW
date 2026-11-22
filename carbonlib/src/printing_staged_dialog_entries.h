/*+===================================================================
  File:      printing_staged_dialog_entries.h

  Summary:   Printing Manager staged dialog validation and driver worker metadata.

  Classes:   None.

  Functions: Declares and instantiates native staged dialog entry points.

  Origin:    CarbonLib 1.4 Printing Manager disassembly and Universal Interfaces 3.4.2 PMApplication.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

CARBON_PRINT_STAGED_DIALOG(PMSessionPageSetupDialogInit, 0x941d8, 0x82dc4, staged_setup_init)
CARBON_PRINT_STAGED_DIALOG(PMSessionPrintDialogInit, 0x942f4, 0x83084, staged_job_init)
CARBON_PRINT_STAGED_DIALOG(PMSessionPrintDialogMain, 0x94450, 0x83218, staged_job_main)
CARBON_PRINT_STAGED_DIALOG(PMSessionPageSetupDialogMain, 0x9459c, 0x82eec, staged_setup_main)
CARBON_PRINT_STAGED_DIALOG(PMPrintDialogInit, 0x947dc, 0x83084, staged_legacy_job_init)
