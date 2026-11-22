# Private cooperative guest contexts

ROMAW exposes Gestalt `fctx = 2` and a private trap `ABFE`. This is a
ROMAW extension, not the classic Thread Manager or a complete application
scheduler. It does not switch host threads.

A normal C ABI trap veneer has a return address, a save-context address and a
load-context address on the guest stack. A zero save or load address omits that
half of the exchange. Equal nonzero addresses capture without switching.
Buffers must be even, mapped for 512 bytes, and either identical or disjoint.
Malformed buffers return `paramErr` in D0 without publishing a partial save.
A load argument of `0xffffffff` (feature level 2) captures a template without
binding a native interpreter frame. Its continuation PC is zero; the caller
must supply a new entry PC and stack before loading it. This permits creating
a worker in an event callback without ever resuming that callback's host frame.

Success returns zero when the saved continuation resumes. A call from a
different native interpreter frame returns one without switching or saving;
the caller must retain its current application state and retry at a safe
boundary. New launches establish this association at their first A-line trap,
before entering a native Toolbox callback. The injected GDB test explicitly
starts its own isolated interpreter family and exits afterward.

The buffer layout is private to the running process:

| Offset | Meaning |
| --- | --- |
| 0 | Big-endian `FCTX` signature |
| 4 | ABI version 1 |
| 8 | Guest continuation PC |
| 12 | SR as a big-endian longword |
| 16 | Sixteen big-endian registers, D0 through A7 |
| 80, 84, 88 | USP, MSP, ISP |
| 92, 96 | Optional stack lower and upper bounds; both zero for normal stack |
| 104 | Opaque architectural FPU state, not a portable disk format |

Restores validate the PC, SP and optional stack bounds before changing CPU
state. They clear the synthetic JSR cache. Host interpreter pointers, pending
interrupts, MMU/control-register state and native interpreter jump buffers stay
owned by ROMAW. The application must switch only at safe guest boundaries;
a saved continuation does not preserve an intervening host C callback stack.
New launches reset the optional stack bounds. StackSpace describes the active
alternate stack when one is installed.

The caller owns stacks and context buffers and must keep code, class-return
stacks, error scopes and application globals valid across suspension. The ABI
does not itself make an application reentrant or protect shared file paths.

Validation:

- `bash system/tests/check-runtime.sh cooperative-context` exercises the production
  exchanger under ASan/UBSan, including alternating contexts, FPU/flags, invalid
  pointers, overlap, bounds, and preservation of interpreter-only fields.
- `bash system/tests/build-cooperative-context-probe.sh` builds an original
  m68k C probe and its ABI-only trap veneer.
- `system/tests/cooperative_context_m68k.gdb` records the Windows guest probe:
  two routines alternate stacks 400 times, check results and normal completion,
  and reject a real nested interpreter switch. It expects
  `ARDI_CONTEXT_TEST_ROOT`, the built probe and an isolated classic Finder at
  `build/cooperative-context-profile/.romaw/Apps/Finder`. The former
  PowerShell wrapper is retired; this is a debugger fixture requiring that
  explicit local setup, not a standalone current suite command.

These checks validate the primitive. They do not establish that Finder's
concurrent file operations or the general Thread Manager work.

Interpreter nesting is checked using syn68k's `emulation_depth` as well as the
optional setjmp-frame pointer. Switch-dispatch builds leave that pointer null,
including inside callbacks, so pointer comparison alone cannot guard the
native callback stack. The launch's first trap records the canonical depth;
requests at another depth return 1 without saving or switching. The native
fixture tests equal null pointers at different depths, and the Windows m68k
probe verifies rejection from a real nested `interpret_code` call before
running its worker-switch scenarios.
