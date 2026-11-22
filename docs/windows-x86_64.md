# Windows x64 ABI notes

[The build guide](../README.windows-build) owns toolchain, dependency prefix,
profile and staging instructions. Windows x64 is the supported ROMAW host;
standalone CPU harnesses can validate other documented host widths.

## Guest and host pointers

ROMAW uses `platform/arch/x86_64`. Macintosh records retain four-byte pointer
fields. Explicit guest-pointer accessors cover low-memory globals, handle master
pointers, QuickDraw and the PPC bridge; native callbacks and address arithmetic
use pointer-sized integers. Windows `unsigned long` is still 32 bits, so host
addresses use `uintptr_t` or an equivalent pointer-sized type.

Heap master-pointer offsets are signed, including masters below their zone.
CPU fixtures keep the complete host allocation address in `ROMlib_offset`.
Win64 memory windows reserve host-pointer aliases near the top of guest address
space, retaining them when only the direct-memory bound changes. Translation
block successors use guest addresses rather than narrowed host pointers.
Linux generators must match the target pointer width.

## Execution and validation

Syn68k's threaded interpreter keeps computed-goto labels within one C function
to preserve x64 stack/callback conventions; switch dispatch is also available. The active native translator
is x64; the retired IA-32 translator cannot execute in a 64-bit process.
See [syn68k](../syn68k/README.md) for dispatch selection and
[JIT/AOT](../syn68k/docs/aot-x64.md) for native execution.

Synppc preserves full-width native addresses while passing classic 32-bit guest
arguments. Its supported hosts are Windows x64 and Linux x64 regression builds;
the IA-32 backend has been retired. See
[translation status](../synppc/docs/translation-status.md) for current build
requirements and historical validation checkpoints.

Focused fixtures cover pointer round trips above 4 GB, packed guest ABI layouts,
callback widths, heap relocation and SDL sound playback/stop/restart. Use
[the regression suite guide](../system/tests/README.md) to select current checks.
These checks do not establish full Macintosh, physical CPU/MMU or Carbon support.

File-picker behavior and printer dependencies are described in
[the runtime guide](runtime-guide.md#file-dialogs-and-printing).
