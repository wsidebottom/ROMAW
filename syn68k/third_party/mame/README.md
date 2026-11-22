# MAME references and adapted code

Revision: `8dcfb5c90f3d3ae7845bdfee96c8f9eb09614842`.

The exception-priority and division/writeback helpers in
`runtime/include/fpu_exception.h` are adapted from `m68kfpu.cpp`, copyright
Karl Stenerud and R. Belmont, BSD-3-Clause.
Its notice is retained in the header and `LICENSE`. The Windows build stages
the notice alongside Executor in `licenses/mame-fpu.txt`.

Sources reviewed:

- [FPU and pending exceptions](https://github.com/mamedev/mame/blob/8dcfb5c90f3d3ae7845bdfee96c8f9eb09614842/src/devices/cpu/m68000/m68kfpu.cpp)
- [68040 page-table translation and PTEST](https://github.com/mamedev/mame/blob/8dcfb5c90f3d3ae7845bdfee96c8f9eb09614842/src/devices/cpu/m68000/m68kmmu.h)
- [CHK operand ordering](https://github.com/mamedev/mame/blob/8dcfb5c90f3d3ae7845bdfee96c8f9eb09614842/src/devices/cpu/m68000/m68k_in.lst)

MAME's 68040 implementation is C++ and integrates with its device, memory,
exception and timing framework. Its FPU uses SoftFloat 3E. It is useful for
selected adaptations and cross-checks, rather than a direct replacement for
Syn68k's generated integer core. At this revision, its deferred FPU exception
code explicitly covers division while other operations remain incomplete.

Syn68k retains its own bounded EA access and Motorola frame implementation.
Division's pending behavior follows the same model: post from arithmetic,
report before the next F-line instruction, suspend on FSAVE, and recover from
the saved frame on FRESTORE. Division also adapts the scaled exponent/range
calculation and recovery of 67-bit writeback data for E3 busy frames. Both are
checked against an independent exact-integer rational oracle. This does not
establish complete busy-frame or FPSP compatibility. The reviewed MMU code
has not yet been integrated.

CHK's word/long handlers were reviewed for operand ordering: both fetch the
bound before checking a negative Dn. Syn68k's bounded decoder and regression
fixtures are original code. Architecturally undefined flags retain Syn68k's
chosen behavior rather than copying MAME's undocumented flag choices.

Additional interrupt-entry reference: MAME commit
`1936178ff4550226e74205acd64ff27554300b99`,
[m68kcpu.cpp](https://github.com/mamedev/mame/blob/1936178ff4550226e74205acd64ff27554300b99/src/devices/cpu/m68000/m68kcpu.cpp),
`m68ki_exception_interrupt` (BSD-3-Clause, Karl Stenerud). Its master-mode entry
preserves a real MSP frame, then selects ISP and saves a format-1 frame retaining
the saved M bit. Syn68k's original short-frame helper and tests use that return
contract. No MAME interrupt implementation was copied. MC68040UM section 8.4.2
and its RTE flow support this bank selection; page 8-15's prose describes a
different saved SR. Physical-hardware validation of those bits remains open.
