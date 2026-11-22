# CPU implementation status

ROMAW uses syn68k for 68K guests and synppc for PowerPC guests. Neither is a
complete physical-machine model. Decoder/native coverage, architectural
conformance and application compatibility are separate claims; use the detailed
contracts and dated validation checkpoints below.

| Area | Current reference |
| --- | --- |
| 68040 instructions, exceptions and FPU/FPSP | [68040 status](../syn68k/docs/68040-status.md) |
| 68040 mapped operands, exception frames and continuation limits | [MMU contract](../syn68k/docs/mmu040.md) |
| Win64 68K native execution and persistent cache | [JIT/AOT](../syn68k/docs/aot-x64.md) |
| 750 supervisor state, MMU, caches, bus and exceptions | [750 architectural core](../synppc/docs/750-status.md) |
| PPC inline lowering, emitted kernels and persistent cache | [Translation status](../synppc/docs/translation-status.md) |
| PPC flat application execution and embedding | [synppc](../synppc/README.md) |
| Carbon bindings and real-library dependencies | [Native Carbon](../carbonlib/README.md) |

Windows x64 is the supported ROMAW host. Standalone CPU validation on other
host widths or Linux does not establish an application port. Guest addresses and
Macintosh pointer fields remain 32 bits; host pointers retain their full width.
See [the Windows ABI notes](windows-x86_64.md).

## Integration boundaries

The built-in Finder runs as Windows host code after shared Toolbox initialization.
Its entry point and UI-thread checks are internal; it has no external native-app
SDK or DLL launch interface. Classic apps use the guest CPU runtimes.

Syn68k supports switch/threaded interpretation, native x64 translation and an
opt-in persistent cache. FPU operations retain software precision and Motorola
FPSP support. The bounded MMU interfaces have mapped stepping and continuation
work, but do not enable paging throughout ROMAW's Toolbox/host-pointer paths.
The owning MMU document records the remaining restart and integration limits.

Synppc separates the flat application adapter from the experimental `-ppc-system`
architectural path. Its interpreter, JIT and AOT share guest state contracts;
checked fault/device/embedding boundaries remain part of native execution.
603/G5 hardware captures are evidence for those machines and the defined fields
compared, not substitutes for the 750 specification. General Carbon compatibility
is not established by passing CPU tests.

## Host-port policy

The old native PowerPC/68K host backends, native CFM/InterfaceLib/Mixed Mode
implementations, SVGAlib, pre-SDL Win32 renderer and dummy-sound stub have been
removed. Historic SPARC, MIPS, i860, VAX and Alpha host selections are rejected.
PowerPC guest execution, classic Toolbox interfaces and external reference Macs
remain supported in their documented roles. Vendored architecture detection
remains upstream-owned.

## External references and JIT choices

The Basilisk II/UAE reference reviewed is `kanjitalk755/macemu` commit
`f532be3d3d70b70f272b52050ce0889a4025d624`:

- [FPU implementation](https://github.com/kanjitalk755/macemu/blob/f532be3d3d70b70f272b52050ce0889a4025d624/BasiliskII/src/uae_cpu/fpu/fpu_uae.cpp)
- [JIT support](https://github.com/kanjitalk755/macemu/blob/f532be3d3d70b70f272b52050ce0889a4025d624/BasiliskII/src/uae_cpu/compiler/compemu_support.cpp)
- [JIT interface/cache definitions](https://github.com/kanjitalk755/macemu/blob/f532be3d3d70b70f272b52050ce0889a4025d624/BasiliskII/src/uae_cpu/compiler/compemu.h)

These files carry GPL-2.0-or-later notices. No Basilisk II code has been copied
into this batch. Its JIT is tied to UAE registers, memory and dispatch, and the
reviewed backend supports IA-32/x86-64 rather than all host architectures.
Its cache invalidation and interpreter fallback are useful references, but it
is not a replacement library that can be linked into Syn68k unchanged.

MAME's previously adapted FPU helpers retain their existing pinned provenance in
[the MAME assessment](../syn68k/third_party/mame/README.md). Additional reference
files were reviewed at MAME commit `1936178ff4550226e74205acd64ff27554300b99`:

- [68040 FPU](https://github.com/mamedev/mame/blob/1936178ff4550226e74205acd64ff27554300b99/src/devices/cpu/m68000/m68kfpu.cpp)
- [PowerPC recompiler](https://github.com/mamedev/mame/blob/1936178ff4550226e74205acd64ff27554300b99/src/devices/cpu/powerpc/ppcdrc.cpp)

Those files carry BSD-3-Clause notices. MAME's recompiler uses its UML/codegen,
device and memory infrastructure. Adapting it would require importing and
maintaining those dependencies or translating its design into this runtime.
Motorola manuals and FPSP remain the architectural reference; agreement between
emulators alone is insufficient evidence of complete processor accuracy.
