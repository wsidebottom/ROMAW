/*+===================================================================
  File:      printing_dialog_entries.h

  Summary:   Printing dialog field access and CarbonLib 1.4 ABI data.

  Classes:   None.

  Functions: Declares and instantiates printing dialog accessors when included
             with the corresponding macro.

  Origin:    CarbonLib 1.4 Printing Manager code and Universal Interfaces
             3.4.2 PMCore.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

CARBON_PRINT_DIALOG(PMGetDialogPtr, 0x94a50, 0, dialog_reference)
CARBON_PRINT_DIALOG(PMGetModalFilterProc, 0x94b1c, 0xaa, dialog_read_word)
CARBON_PRINT_DIALOG(PMSetModalFilterProc, 0x94be8, 0xaa, dialog_write_word)
CARBON_PRINT_DIALOG(PMGetItemProc, 0x94cb4, 0xae, dialog_read_word)
CARBON_PRINT_DIALOG(PMSetItemProc, 0x94d80, 0xae, dialog_write_word)
CARBON_PRINT_DIALOG(PMGetDialogAccepted, 0x94f10, 0xb6, dialog_read_byte)
CARBON_PRINT_DIALOG(PMSetDialogAccepted, 0x94e4c, 0xb6, dialog_write_byte)
CARBON_PRINT_DIALOG(PMGetDialogDone, 0x94fdc, 0xb7, dialog_read_byte)
CARBON_PRINT_DIALOG(PMSetDialogDone, 0x950a8, 0xb7, dialog_write_byte)
