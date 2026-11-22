/*+===================================================================
  File:      native040-checks.h

  Summary:   Route existing MMU fixtures through published native code.

  Classes:   None.

  Functions: Renamed regression declarations from mmu040-memory.h.

  Origin:    Original ARDI Executor emitted 68040 regression adapter.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef SYN68K_NATIVE040_CHECKS_H
#define SYN68K_NATIVE040_CHECKS_H
#define syn68k_mmu040_translate syn68k_test_native040_translate
#define syn68k_mmu040_ptest syn68k_test_native040_ptest
#define syn68k_mmu040_flush syn68k_test_native040_flush
#define syn68k_mmu040_control syn68k_test_native040_control
#define syn68k_mmu040_reset syn68k_test_native040_reset
#define syn68k_mmu040_read syn68k_test_native040_read
#define syn68k_mmu040_write syn68k_test_native040_write
#define syn68k_mmu040_write_rmw syn68k_test_native040_write_rmw
#include "mmu040-memory.h"
#endif
