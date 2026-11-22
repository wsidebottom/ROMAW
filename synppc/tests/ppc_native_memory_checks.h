/*+===================================================================
  File:      ppc_native_memory_checks.h

  Summary:   Route existing hardware fixtures through emitted native memory roots.

  Classes:   None declared.

  Functions: Test adapter declarations for the 750 bus operations below.

  Origin:    Original ARDI Executor native PowerPC memory regression routing.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/
#ifndef ROMAW_PPC_NATIVE_MEMORY_CHECKS_H
#define ROMAW_PPC_NATIVE_MEMORY_CHECKS_H
#include "synppc/bus750.h"
extern "C" {
int native_test_read(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t, unsigned, unsigned, uint64_t*,
                     ROMlib_ppc_bus_result*);
int native_test_write(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t, unsigned, uint64_t,
                      ROMlib_ppc_bus_result*);
int native_test_reserve(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t, uint32_t*,
                        ROMlib_ppc_bus_result*);
int native_test_conditional(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t, uint32_t,
                            ROMlib_ppc_bus_result*);
int native_test_external(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t, int, uint32_t*,
                         ROMlib_ppc_bus_result*);
int native_test_translate(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t, unsigned,
                          ROMlib_ppc_bus_result*);
int native_test_cache(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t, unsigned,
                      ROMlib_ppc_bus_result*);
int native_test_sync(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, ROMlib_ppc_bus_result*);
int native_test_snoop(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t, unsigned, int, int,
                      ROMlib_ppc_bus_result*);
}
#define ROMlib_ppc_750_read native_test_read
#define ROMlib_ppc_750_write native_test_write
#define ROMlib_ppc_750_load_reserve native_test_reserve
#define ROMlib_ppc_750_store_conditional native_test_conditional
#define ROMlib_ppc_750_external native_test_external
#define ROMlib_ppc_750_translate native_test_translate
#define ROMlib_ppc_750_cache_control native_test_cache
#define ROMlib_ppc_750_host_sync native_test_sync
#define ROMlib_ppc_750_snoop native_test_snoop
#endif
