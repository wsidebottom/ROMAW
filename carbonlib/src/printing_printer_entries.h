/*+===================================================================
  File:      printing_printer_entries.h

  Summary:   CarbonLib 1.4 printer query workers and argument counts.

  Classes:   None.

  Functions: Declares and instantiates the printer queries with the
             corresponding declaration macro.

  Origin:    CarbonLib 1.4 Printing Manager code and Universal Interfaces
             3.4.2 PMCore.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

CARBON_PRINT_PRINTER(PMPrinterGetDescriptionURL, 0x9010c, 0x840b4, 3, 1)
CARBON_PRINT_PRINTER(PMPrinterGetPrinterResolution, 0x90254, 0x83c10, 3, 1)
CARBON_PRINT_PRINTER(PMPrinterGetPrinterResolutionCount, 0x9039c, 0x83db4, 2, 1)
CARBON_PRINT_PRINTER(PMPrinterGetIndexedPrinterResolution, 0x904d4, 0x83e10, 3, 1)
CARBON_PRINT_PRINTER(PMPrinterGetLanguageInfo, 0x9061c, 0x83f28, 2, 1)
CARBON_PRINT_PRINTER(PMPrinterGetDriverReleaseInfo, 0x90754, 0x83fdc, 2, 1)
CARBON_PRINT_PRINTER(PMPrinterGetDriverCreator, 0x9088c, 0x83920, 2, 0)
CARBON_PRINT_DEFAULT_PRINTER(PMGetPrinterResolution, 0x93454, PMPrinterGetPrinterResolution, 2, 0)
CARBON_PRINT_DEFAULT_PRINTER(PMGetPrinterResolutionCount, 0x93528, PMPrinterGetPrinterResolutionCount, 1, 0)
CARBON_PRINT_DEFAULT_PRINTER(PMGetIndexedPrinterResolution, 0x935ec, PMPrinterGetIndexedPrinterResolution, 2, 0)
CARBON_PRINT_DEFAULT_PRINTER(PMGetLanguageInfo, 0x938b0, PMPrinterGetLanguageInfo, 1, 1)
CARBON_PRINT_DEFAULT_PRINTER(PMGetDriverReleaseInfo, 0x93984, PMPrinterGetDriverReleaseInfo, 1, 0)
CARBON_PRINT_DEFAULT_PRINTER(PMGetDriverCreator, 0x93a48, PMPrinterGetDriverCreator, 1, 0)
