/*+===================================================================
  File:      native040-runner.c

  Summary:   Run emitted 68040 arithmetic, MMU, operand and function-code fixtures in one process.

  Classes:   None.

  Functions: main; imports eleven regression entry points.

  Origin:    Original ARDI Executor emitted 68040 regression adapter.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <stdio.h>
int native040_math_check(void);
int native040_mmu_check(void);
int native040_memory_check(void);
int native040_fc_check(void);
int native040_ram_check(void);
int native040_ea_check(void);
int native040_convert_check(void);
int native040_bus_check(void);
int native040_control_check(void);
int native040_registers_check(void);
int native040_flat_check(void);

int main(void)
{
    int status;
    if ((status = native040_math_check()) || (status = native040_mmu_check()) || (status = native040_memory_check()) ||
        (status = native040_fc_check()) || (status = native040_ram_check()) || (status = native040_ea_check()) ||
        (status = native040_convert_check()) || (status = native040_bus_check()) ||
        (status = native040_control_check()) || (status = native040_registers_check()) ||
        (status = native040_flat_check()))
        return status;
    puts("All eleven emitted 68040 kernel groups passed");
    return 0;
}
