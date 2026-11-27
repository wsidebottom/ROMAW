/*+===================================================================
  File:      native040-runner.c

  Summary:   Run all emitted 68040 kernel regression groups in one process.

  Classes:   None.

  Functions: main

  Origin:    ROMAW grouped native 68040 regression runner.

  Copyright and Legal notices: Applicable license: GPL-2.0-or-later; see SPDX below.
===================================================================+*/

/*+===================================================================
  File:      native040-runner.c

  Summary:   Run emitted 68040 arithmetic, MMU, operand and function-code fixtures in one process.

  Classes:   None.

  Functions: main; imports twenty-one regression entry points.

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
int native040_cache_check(void);
int native040_cache_ram_check(void);
int native040_cache_traffic_check(void);
int native040_mmu_cache_check(void);
int native040_mmu_cache_op_check(void);

int native040_cache_move16_check(void);
int native040_mmu_move16_check(void);
int native040_pagecross_check(void);
int native040_read_prefix_check(void);
int native040_cache_snoop_check(void);
int native040_cache_buffer_check(void);

int main(void)
{
    int status;
    if ((status = native040_math_check()) || (status = native040_mmu_check()) || (status = native040_memory_check()) ||
        (status = native040_fc_check()) || (status = native040_ram_check()) || (status = native040_ea_check()) ||
        (status = native040_convert_check()) || (status = native040_bus_check()) ||
        (status = native040_control_check()) || (status = native040_registers_check()) ||
        (status = native040_flat_check()) || (status = native040_cache_check()) ||
        (status = native040_cache_ram_check()) || (status = native040_cache_traffic_check()) ||
        (status = native040_mmu_cache_check()) || (status = native040_mmu_cache_op_check()) ||
        (status = native040_cache_move16_check()) || (status = native040_mmu_move16_check()) ||
        (status = native040_pagecross_check()) || (status = native040_read_prefix_check()) ||
        (status = native040_cache_snoop_check()) || (status = native040_cache_buffer_check()))
        return status;
    puts("All twenty-two emitted 68040 kernel groups passed");
    return 0;
}
