/*+===================================================================
  File:      native040-memory.h

  Summary:   Share invocation-local physical RAM binding and store-span checks among closed native roots.

  Classes:   None.

  Functions: Build-time native040_bind_memory, native040_read, native040_write and native040_span declarations.

  Origin:    Original ARDI Executor direct 68040 lowering.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYN68K_NATIVE040_MEMORY_H
#define SYN68K_NATIVE040_MEMORY_H
#include "native040.h"

typedef struct {
    Syn68kMMU040Memory memory;
    Syn68kMMU040Memory* original;
    const Syn68kNative040RAM* ram;
    Syn68kMMU040Cache cache;
    Syn68kCache040Bus cache_bus;
} NativeMemory;

Syn68kMMU040Memory* native040_bind_memory(Syn68kNative040MMU* frame, NativeMemory* memory);
void native040_read(Syn68kNative040MMU* frame);
void native040_write(Syn68kNative040MMU* frame);
void native040_span(Syn68kNative040MMU* frame);
#endif
