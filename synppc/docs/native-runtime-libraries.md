# Native runtime library services

The ROMAW adapter builds native C implementations into
`libsynppc-romaw.a`. The portable `libsynppc.a` does not acquire ROMAW
header dependencies. No Apple executable bytes are embedded in these services.

## Memory Manager

`src/romaw/memory-manager.c` owns per-launch heaps and their pointer blocks.
The adapter supplies actual Macintosh-zone allocation, disposal, memory access
and free-space queries. Heap kinds zero and one select the System and application
zones; temporary kind two uses the System partition, matching ROMAW's existing
temporary-memory implementation. Changing the current Toolbox zone does not
change a block's owning heap or the zone used by reallocation.

The native import set is:

| Operation | Entries |
| --- | --- |
| Heap lifetime | `MMNewHeap`, `MMDisposeHeap`, `MMGetDefaultHeap`, `MMSetDefaultHeap` |
| Pointer allocation | `MMAllocate`, `MMAllocateIn`, `MMAllocateClear`, `MMAllocateClearIn`, `MMReallocate`, `MMFree` |
| Queries | `MMBlockSize`, `MMGetHeap`, `MMGetFreeSpace`, `MMSystemFreeSpace`, `MMConfiguration` |

Heap references are opaque guest allocations, with ownership metadata maintained
on the host. They do not imitate an Apple private C++ vtable or boot structure.
The minimum/maximum chunk parameters are policy hints: this implementation uses
individual Macintosh blocks rather than Apple's cached chunk allocator.
Handle-based APIs and direct private-class access are outside this native set.

Failed growth preserves the old pointer and its contents. Cleared allocations
clear the requested guest bytes. Free-space outputs are validated together before
either is written and retain guest byte order. Unknown/stale non-null free and
heap references fail explicitly. Heap disposal and process teardown release the
owned blocks exactly once.

The bridge also imports the existing C temporary-memory implementations:
`TempFreeMem`, `TempMaxMem`, `TempNewHandle`, `TempHLock`, `TempHUnlock` and
`TempDisposeHandle`.

## SOM allocation variables

The native SOM subset exposes `SOMMalloc`, `SOMCalloc`, `SOMRealloc` and `SOMFree`
as writable data exports containing guest transition-vector pointers. Their
initial values call ROMAW's checked guest C allocators. They are not immutable
function exports and do not expose host function addresses.

When an original SOM image is available, its actual data exports and initializer
retain ownership instead. Native allocator variables do not replace a supplied
runtime's shared state. This allocation subset does not implement the SOM class,
method-dispatch or object-model APIs; those still need their real fallback.

## CFM notifications

`FragAddNotifyProc` and `FragRemoveNotifyProc` register actual PPC callbacks in the
current execution context. The private ABI was checked against the local CFM
reference: a callback receives connection ID, closure ID and event code, with no
registration refCon. Events are create (0), attach (1), detach (2), release (3)
and initialization complete (4).

Ordinary owning-closure load/close paths invoke the corresponding callbacks.
Duplicate registrations are retained and removal removes one matching entry.
Registrations added during an event do not receive that same in-progress event;
removed callbacks are not invoked later in that dispatch. Registration storage
is bounded and allocation failure is reported. Closing the last reference to
an actively executing callback's owner is rejected before reference counts
change. Unloaded callback owners lose their registrations. Private instances that share PIC
code retain separate callback ownership through their transition vectors and
TOCs; closing one instance does not remove a peer's registrations.

The original machine-code reporting fixtures exercise allocator data indirection
and writes, real library initialization, closure IDs, all five notification events,
duplicate removal, close/reload, private PIC-owner isolation and absence of
callbacks after removal:

```powershell
./synppc/tests/run-romaw-tests.ps1 -Group 'PPC Native Memory Probe' -Interpreter
./synppc/tests/run-romaw-tests.ps1 -Group 'PPC Native Memory Probe' -Jit
./synppc/tests/run-romaw-tests.ps1 -Group 'PPC Native Libraries Probe' -Interpreter
./synppc/tests/run-romaw-tests.ps1 -Group 'PPC Native Libraries Probe' -Jit
```

`native_memory_manager_test.c` additionally checks bounded outputs, malformed
references, failed reallocation, heap disposal and complete teardown using an
isolated allocation backend. These checks establish the listed contracts;
they do not establish complete Memory Manager, SOM, CFM or Finder compatibility.

## Appearance view font

`AppearanceLib:GetViewsFont` adapts its required Pascal-name and big-endian
16-bit size outputs to the existing native `GetThemeFont` views role. Script
resolution, font settings, original-library probing and guest Toolbox patches
remain owned by that service. A null name or size output reports `paramErr`
without modifying either buffer. This frame adapter is restricted to PPC callers.

`PPC Appearance Font Probe` verifies default/explicit system scripts, configured
font-size changes, unaligned output guards and required-output failures in both
interpreter and JIT modes. It does not establish full Appearance compatibility.

## Classic Appearance drawing states

The PPC Appearance adapter implements `GetThemeDrawingState`,
`SetThemeDrawingState`, `DisposeThemeDrawingState` and
`NormalizeThemeDrawingState` through actual native QuickDraw operations. Saved
state includes pen position, dimensions, mode/pattern, foreground/background
colors, background patterns, text mode and pattern origin. It leaves the current
port, clipping and font selection under the application's control. Color pixel
patterns retain the original contract's borrowed references. No Apple private
boot state or executable bytes are synthesized.

Opaque tokens and their host metadata belong to the launch context. Allocation
is bounded to 512 outstanding states; failed capture clears a valid output slot.
Invalid/null tokens fail rather than freeing arbitrary memory. Restore can retain
or dispose the token, and teardown releases outstanding state metadata and guest
allocations. The Classic adapter does not add new Carbon export bodies.

`PPC Appearance State Probe` checks monochrome and offscreen color ports, actual
restored pixels for simple and color background patterns, pen/text/origin state,
unchanged font selection, pointer-sized output guards, invalid arguments,
capacity/release/reuse and disposal/teardown in interpreter and JIT modes.
