# Native Carbon compatibility library

This is a chronological implementation/disassembly audit. Coverage totals and
validation counts describe their individual checkpoints. Current setup and
coverage reporting are owned by [the package guide](../../carbonlib/README.md);
API contracts are organized in [the API notes](../../carbonlib/docs/api-notes.md).

## Selector 0x005A

The local CarbonLib 1.4 disassembly confirms the earlier investigation in
[myabcd-startup.md](myabcd-startup.md). In the `CarbonLib` PEF code section,
offsets `0xC98C..0xCA0C` contain the private system-window forwarding routine.
It builds ProcInfo `0x03F79FBE`, loads selector `0x005A` into r5, loads the
OSDispatch procedure from low memory `0x103C`, and calls
`InterfaceLib:CallUniversalProc` at `0xC9EC`. Four argument slots are written
at PPC stack offsets `0x38`, `0x3C`, `0x40` and `0x44`.

After removing the selector, the signature is a WindowPtr result and:

```
Rect*, StringPtr, Boolean, short, WindowPtr, Boolean, long, ProcPtr, RgnHandle
```

CarbonLib's wrapper establishes the transport ABI. The ProcessMgrLib analysis
in the startup investigation establishes the semantics: allocate an extended
kind-5 color window in the shared floating layer, retain its real process
owner and A5, and retain its event callback and borrowed mouse region. The
callback uses ProcInfo `0x3BE0` (window, EventRecord*, short part, Boolean*).
Selector `0x005B` disposes the window. The private API's original name remains
unconfirmed.

The current checkout already implements these operations in
`runtime/emustubs.c` and `windows/systemwindow.c`.
`synppc/tests/make_ppc_layer_probe.py` exercises creation through exactly this
Mixed Mode signature, ownership, separate layer lists, callbacks and disposal.
Replacing that service with a plain NewWindow call would discard required
behavior. The older notes describing 0x005A as MyABCD's current blocker are
historical: later checkpoints reached the application's interactive activities.

## Incremental native replacement

The user selected an incremental C/C++ static library, with real CarbonLib
fallback for missing APIs. The reference `CarbonLib` fragment exports 5,715
symbols; its companion fragments include CoreFoundation, navigation, text and
multimedia services. Resolving a symbol to a native implementation is not proof
of complete compatibility. Each group needs an established signature, real
behavior and regression coverage before it can replace the guest implementation.

Keep CarbonLib's real initialization and unsupported exports available. In
particular, its WaitNextEvent wrapper maintains Carbon timer/event bookkeeping;
do not replace it with a direct classic wait merely to avoid loading the library.
Do not publish generated success stubs or synthetic CoreFoundation data objects.
Apple binaries and generated disassemblies remain ignored local reference assets.

## First native group and validation

`carbonlib` builds `libromaw-carbon.a`; the Windows build links it
after the PPC bridge and stages the archive beside ROMAW. The registry
contains ten window accessors from the contiguous reference-code group
`0x74A10..0x74C2C`. Each entry records its reference offset and explicit Toolbox
marshalling signature. Unsupported functions, data exports and Carbon's event
wait wrapper retain their existing resolution behavior.

Validation for this group:

- `make -C carbonlib check OBJ="$PWD/system/obj/carbonlib"`: the actual
  Windows archive passes guest-pointer, byte-order, mono/color-port, state-handle
  extent, Boolean and layered-window lookup tests.
- `JOBS=10 make -C system`: Windows build and staging pass. Make dry runs
  verify that changing the C source rebuilds the archive and changing the archive
  relinks ROMAW. Generated-file and Windows help checks pass.
- `synppc/tests/run-romaw-tests.ps1 -Interpreter`: all thirty probes pass,
  including selector 0x005A ownership/callback handling, Carbon wait routing and
  the new native-accessor probe. Its synthetic CarbonLib exports a deliberately
  different GetWindowPort, plus a token implemented only in the PEF; the native
  accessor wins while the token returns the value set by the real PEF initializer.
- A Windows GDB launch of the local MyABCD reaches `window_port` in the new C
  archive through `ppc_service`. The trace also exercises native saved-window-state
  and port-bounds accessors. GDB stops the launch at this checkpoint; this is not
  a full interactive application test or a claim of complete CarbonLib support.

## MyABCD QuickDraw group

Inspection of MyABCD's PEF imports selected GetPortBounds, GetPortPixMap,
GetPortVisibleRegion, GetPortTextFont, GetPortTextSize, GetPixRowBytes and
GetQDGlobalsThePort. The group also supplies the related GetPortClipRegion and
SetQDError, bringing the native registry to nineteen exports. CarbonLib's reference
offsets are recorded in `carbonlib/src/exports.c`.

Null port arguments select the current A5-world port, region results are copied
into caller-owned handles, and text fields return signed 16-bit values. The
classic GetPixRowBytes fallback retains bit 14 and masks bit 15; using ROMAW's
narrower packed-pixel helper would change that contract. Errors use the same
native state as the existing QDError implementation. Successful accessors do not
clear a previous error. Missing current ports, null outputs, monochrome PixMap
queries and empty PixMap masters fail without dereferencing invalid storage.

The Windows archive fixture checks both port layouts, nonempty region copies,
output guards, error preservation and every 16-bit rowBytes value. The expanded
PPC Carbon Native Probe passes against the rebuilt/staged Windows ROMAW,
checking translated handle/pointer returns, current-port selection, rectangle
byte order, text fields, PixMap stride and region calls. Its PEF-only token still
requires and observes the fallback initializer. No fallback or Carbon event-wait
behavior was removed.

## QuickDraw pattern globals

MyABCD also imports GetQDGlobalsBlack. The next group adds that getter and its
White, Gray, LightGray and DarkGray counterparts, for twenty-four native exports
in total. Fourteen registry entries are direct CarbonLib imports of the local
MyABCD executable. These counts describe bindings, not overall API completeness.

The reference getters copy eight bytes from the active A5 world's QuickDraw
globals and return the destination pointer. They must not return borrowed global
storage, substitute fixed pattern bytes or cache one application's patterns.
The native implementations use ROMAW's existing A5-relative globals and leave
prior errors unchanged on success. A null destination reports paramErr.

The archive unit fixture switches between two A5 worlds with distinct mutable
pattern bytes, checks caller ownership and surrounding guards, and tests copying
onto the source itself. The new original-code PPC Carbon Pattern Probe checks
all five standard patterns, pointer returns, guards, repeat copies, error-state
preservation and the PEF-only initialized fallback token.

Final validation for both groups: WSL build and staging, archive unit tests, all
thirty-one staged PPC interpreter probes, generated-file checks and Windows help
checks pass. A Windows GDB MyABCD launch with the JIT enabled enters the native
PixMap accessor and subsequently reaches ppc_wait_next_event after application
startup. The black-pattern getter was not observed on that startup path; its
execution is covered by the focused probe. This is a startup checkpoint, not a
complete interactive MyABCD test.

## Dialog/control accessors and explicit window invalidation

The next two groups add ten exports, for thirty-four native bindings in total.
MyABCD directly imports GetDialogWindow, GetControlBounds and InvalWindowRect;
seventeen registry entries now match its direct CarbonLib imports.

The dialog group also implements GetDialogFromWindow, GetDialogTextEditHandle,
GetDialogDefaultItem and SetPortDialogPort. Reference conversions are identities,
the TextEdit handle remains borrowed, the default item is a signed guest word,
and control bounds follow the current master pointer. GetDialogCancelItem remains
on fallback: ROMAW's existing cancel-item setter does not maintain the
corresponding reference state, so exposing that record word would be misleading.

The update group supplies InvalWindowRect, InvalWindowRgn, ValidWindowRect and
ValidWindowRgn. It transforms local damage using the requested window's bitmap
origin and updates that window's global update region without changing the
current port. Rectangle bytes are captured before temporary allocation; region
inputs are cloned with checked HandToHand allocation before offsetting. A failed
clone never disposes the caller's region. The window is revalidated after that
allocation before accessing its update handle. Invalid windows return -5600;
invalid inputs return paramErr. Temporary allocation failures and region-operation
memory errors propagate, and temporary storage is released.

Unit tests cover mono/color coordinate origins, caller-region preservation,
current-port preservation, signed dialog fields, relocated control masters,
output guards, temporary allocation failure, moved rectangle storage and a
window disappearing during allocation. The original-code Carbon Dialog Probe
creates a real DLOG/DITL and control, while the Carbon Invalidation Probe compares
real global update regions, including subtraction of an interior rectangle.
Both require their PEF-only fallback initializer and token.

WSL build/staging, archive unit tests, all thirty-three staged PPC interpreter
probes, Windows help and generated-file checks pass. All thirty-four native
reference offsets match the local disassembly metadata. MyABCD with JIT enabled
reaches its application event wait without a launch failure or Windows exception;
these new APIs were not called before that startup checkpoint, so their execution
validation comes from the focused probes, not an interactive MyABCD claim.

Scoped source formatting and the PowerShell check pass. The repository-wide
check reports formatting drift in five concurrently edited Finder/storage files.
An unrelated Finder build error comparing wide filenames with narrow storage
directory constants was fixed with explicit filesystem-path conversion; that
small change remains with the concurrent working-file edits, outside the Carbon
commits.

## String conversion and atomic helpers

The next two groups add eight exports, bringing the archive to forty-two native
bindings. Twenty-four entries match MyABCD's direct CarbonLib imports. MyABCD
uses CopyPascalStringToC, CopyCStringToPascal, c2pstrcpy, IncrementAtomic,
DecrementAtomic, TestAndSet and TestAndClear; p2cstrcpy is the paired alias.

The reference Copy functions take source first; c2pstrcpy/p2cstrcpy reverse the
arguments and forward to them. All four have void signatures. Pascal payloads
are copied using their unsigned length byte, including embedded NULs. C input
is capped at 255 bytes. The native implementations scan before moving bytes,
support in-place and overlapping storage and never scan past the cap looking for
a terminator. A null source produces an empty result, as in the reference
fallback. A null destination is ignored safely.

CarbonLib code at 0xAF320/0xAF348 returns the old word from its reservation loop,
not the incremented/decremented value. Its explicit unaligned fallbacks also
return the old value. TestAndSet/TestAndClear at 0xAF5F4/0xAF654 mask the index to
three bits and number bits from the high bit of the addressed byte. The native
helpers preserve these contracts, use guest big-endian words and unsigned
wraparound, and serialize their own operations with one C atomic flag. This also
covers overlapping byte/word operations and unaligned words without typed
unaligned memory accesses. They perform no guest calls while holding that lock;
arbitrary writes outside these helpers are not made atomic by it.

The Windows archive fixture checks all 256 Pascal lengths, embedded NULs and
high bytes, 1,024 C-string lengths, truncation, both aliases, overlap and protected
page boundaries. Atomic checks cover signed boundaries, wraparound, four word
alignments and every byte/8-bit-index combination. Four Windows threads then
perform 16,000 increments and 16,000 decrements on the same unaligned counter,
checking that every returned previous value appears exactly once.

The original-code Carbon String and Atomic probes pass in both interpreter and
JIT modes, including guest pointer transport, alias argument order, in-place
conversion, truncation, previous-value returns, byte order, unaligned words,
MSB-first bit operations, output guards and real initialized PEF fallback. All
thirty-five interpreter probes, Windows help, generated-file and archive unit
checks pass after build/staging. All forty-two reference offsets match the
disassembly metadata. A Windows GDB MyABCD startup reaches its application event
wait after four calls to the new C-to-Pascal helper, with no launch failure or
Windows exception. IncrementAtomic was not hit before that checkpoint; the
focused probes cover its execution. This is not a full interactive app test.

Scoped formatting and the PowerShell check pass.

The next groups bring the archive to eighty-one exports: eighteen QuickDraw
record accessors, four A5-world globals and seventeen additional atomic helpers.
Twenty-five bindings match MyABCD's direct imports, including GetRegionBounds.
The audit verifies every native entry's name, export class and code offset
against the local CarbonLib 1.4 metadata. The remaining 5,634 main-fragment
exports are outside the archive; some already have native bridge delegates.
Companion fragments require separate implementation and coverage audits.

Record getters preserve guest layouts and caller-owned outputs. The CopyBits
BitMap pointer is the port's overlay at offset two, even for color ports; it
does not point at the PixMap record. Text-face and rowBytes returns are unsigned,
while spacing, pen modes and visibility retain their signed contracts. The
global getters read the current A5 world, including its random seed, 14-byte
screen BitMap and 68-byte arrow cursor. The seed setter writes guest byte order.

All 8/16/32-bit add, increment, decrement and bitwise atomic operations and
CompareAndSwap now share the same lock. They return the previous value; narrow
arithmetic returns are signed, and narrow logical returns are unsigned, as in
the [Apple SDK DriverSynchronization.h declarations](https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.4u.sdk/System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/CarbonCore.framework/Versions/A/Headers/DriverSynchronization.h).
The archive fixture checks wraparound, byte order, alignments and failed CAS
preservation. Four Windows threads perform 16,000 successful CAS updates with
uniqueness checks on the returned values, alongside the increment/decrement
contention tests.

The expanded Pattern and Atomic probes and new Records probe pass through both
the interpreter and JIT, including initialized real PEF fallback. All thirty-six
interpreter integration probes, archive unit tests, Windows help, generated-file
checks and PowerShell formatting pass after build/staging. MyABCD reaches its
application event wait under Windows GDB with no launch failure or Windows
exception. GetRegionBounds was not called before that checkpoint. This remains
a startup check, not a complete interactive workflow.

The optional coverage trace audit finds 78 other direct MyABCD imports called
through native services in that startup run. It reports these separately from
the archive's 25 direct imports; observing calls does not establish full API
behavior or eliminate fallback requirements.

The descriptor group adds AEGetDescData, AEGetDescDataSize and AEReplaceDescData,
bringing the archive to eighty-four exports and twenty-six direct MyABCD imports.
CarbonLib's 0x72454-0x7256c routines copy a bounded prefix of the descriptor's
handle payload, report its size (zero for typeNull), and replace it by disposing
and recreating the descriptor. The native adapters use ROMAW's existing
AppleEvent Manager for ownership. Replacement clears the disposed descriptor
before creation so an allocation failure leaves typeNull and a null handle.
Malformed negative capacities/sizes return paramErr before changing state.

Archive tests cover binary/truncated copies, guard bytes, zero-capacity outputs,
overlap, null/empty/purged handles and replacement allocation failure/order.
The original-code Descriptor probe exercises actual manager allocation and
replacement, changed type/size/payload, empty descriptors, output caps and PEF
initialization in interpreter and JIT modes. All thirty-seven interpreter probes
pass after rebuilding and staging. MyABCD again reaches its application event
wait without a launch failure or Windows exception. This does not establish
complete CarbonLib or interactive application compatibility.

The repository-wide source-format and header checks pass for the preceding
eighty-one-export group (1,356 source files and 1,503 headers checked).

The Pascal String Utilities group adds thirteen functions, bringing the archive
to ninety-seven exports and twenty-seven direct MyABCD imports. PLstrcmp is a
weak import. CarbonLib re-exports this entire family from StdCLib, so the registry
now distinguishes code offsets from imported-symbol indices and library names.
The coverage audit verifies each re-export's import name, library, class and
index against the local CarbonLib metadata; it reports 84 code exports and 13
re-exports separately. It does not count imported data or unresolved functions
as implemented APIs.

The native routines follow the local System 9 StdCLib disassembly at
0x10ea8-0x11330. The signed 16-bit counts and return types are also documented in
[Apple's Pascal String Utilities Reference](https://leopard-adc.pepas.com/documentation/Carbon/Reference/passtrings_reference/passtrings_ref.pdf).
Comparison subtracts unsigned payload bytes, then compares lengths for equal
prefixes. Embedded NUL bytes do not end a string. Character searches mask the
character to eight bits. PLpos takes the needle first and returns a one-based
payload position; PLstrstr takes the string first and returns a payload pointer.
Empty needles produce no match. Copies/appends cap payloads at 255 bytes and
leave nonpositive bounded calls unchanged. PLstrncpy retains the reference's
destination-length/prefix behavior instead of assuming it matches C strncpy.

Archive tests cover all byte pairs and Pascal lengths, signed count extremes,
output guards, prefix lengths, zero padding, binary searches and 255-byte caps.
Protected-page tests put each of the 256 possible string extents immediately
before inaccessible memory. The original-code Pascal probe tests all thirteen
functions through the PPC ABI and verifies initialized PEF fallback. Its optional
local `--reference` mode extracts the leaf routines into an ignored reference
fixture; no Apple executable bytes are included in maintained code or fixtures.
Both the original and differential fixtures pass in interpreter and JIT modes.
The differential fixture performs 54,101 assertions, including 792 bounded
copy/append cases over source/destination lengths and signed count boundaries.

All thirty-eight interpreter probes, archive tests, coverage-audit tests, Windows
help, generated-file and PowerShell checks pass after rebuilding and staging.
MyABCD reaches its application event wait under Windows GDB without a launch
failure or Windows exception; PLstrcmp is not called before that checkpoint.
The focused probes establish its execution. Full CarbonLib and interactive app
compatibility remain incomplete; 5,618 main-fragment exports are outside this
archive, including entries already supported by other native bridge delegates.
The repository-wide formatting check passes for all 1,361 supported source files
and 1,508 source headers.

The same Pascal implementations now serve direct StdCLib imports. The library
lookup accepts a CarbonLib function or an exact match to that function's recorded
re-export source; it does not expose other Carbon APIs through StdCLib or arbitrary
libraries. Unsupported functions and data keep their existing resolution paths.
Archive tests verify the library/class boundary across every registry entry.

The separate StdCLib Pascal probe adds the thirty-ninth integration case, and the
runner now requires native-service traces for every Pascal function in both
library variants. Both variants pass in interpreter and JIT modes, including the
54,101-check local-reference fixture through direct StdCLib imports. All
thirty-nine interpreter probes pass after rebuilding and staging; MyABCD still
reaches its application event wait without a launch failure or Windows exception.
The archive count stays at 97 unique exports: source-library aliases are not
counted as additional implementations.

The first CoreFoundation group adds six native re-exports: CFArrayGetCount,
CFGetTypeID, CFDataGetLength, CFDataGetBytePtr, CFDataGetMutableBytePtr and
CFDataGetBytes. The archive now contains 103 exports (84 CarbonLib code entries
and 19 re-exports), matching 28 direct MyABCD imports. CFArrayGetCount is a direct
strong import. The audit still lists 5,612 main-fragment exports outside the
archive; companion coverage and full behavioral compatibility remain incomplete.

The local CoreFoundationLib routines at 0x1bd6c, 0x1e590-0x1e5e8 and 0x53394
establish the object layout: array/data counts at offset 8, data's guest buffer
pointer at 0x14 and the runtime type tag in bits 8..15 of the header word at 4.
The native accessors read those actual fields, without creating allocator or
class state. CFDataGetBytes receives CFRange's location and length in separate
PPC argument words before the output pointer. Valid ranges preserve exact binary
bytes; negative, overflowing and out-of-bounds requests leave output unchanged.
Real constructors, mutation, class registration, callbacks and release retain
their existing PEF implementation and initialization ordering.

Unit tests check every 16-bit combination of type-tag/flag bits, count byte order,
unaligned records, guest pointer aliases, all 33,153 ranges of a 256-byte payload,
overlap, malformed ranges, guards and protected-page object extents. The two
original-code layout probes also verify both native import routes and initialized
fallback sentinel libraries. These layout fixtures do not establish real CF
object ownership or constructor compatibility.

The optional `synppc/tests/check-runtime.ps1 carbon-cf-real` validates actual local
CarbonLib/CoreFoundation objects in interpreter and JIT modes through each
library. Real fallback code creates immutable/mutable arrays and data, grows and
clears an array, copies an immutable payload, appends/resizes/shrinks mutable data
and releases the objects. Native reads observe those changes, native type tags
match the real runtime's array/data type IDs, and mutable buffer writes are
visible through native range copies. The runner requires the real CF initializer
and all six native service traces. All four real-object scenarios pass.

The real fixtures include a SIZE 0 resource with flags 0x58c0, as required by
CarbonLib's 0x60790-0x60854 application-capability check. The generic fixture's
SIZE -1 flags (0x80) correctly failed that check with -2821 before this metadata
was supplied. The initializer and its failure checks remain active.

All forty-one standard interpreter probes, focused layout JIT probes, archive
tests, coverage audit, real-object tests, Windows help, generated-file checks,
PowerShell and repository formatting checks pass after rebuilding/staging.
MyABCD's startup still reaches the application event wait. A manual run displayed
the Apple and Bee alphabet animations; later navigation failed with an unhandled
PowerPC host service at relocated PC 0x00f526d8. WindowsLib:IsValidWindowPtr was
the last traced service. No native CFArrayGetCount or CFGetTypeID call was
observed before that failure; diagnosing the navigation failure remains open.

Navigation is intermittent under GDB: a later run returned from alphabet
playback to the menu successfully. The last successful WindowsLib trace is
therefore insufficient to identify a missing API. Early PPC service rejections
now name the registered native entry at the failing PC, or report the encoded
service ID and current import count when no entry matches. Existing specific
callback/Toolbox errors remain intact. This changes diagnostics rather than
claiming the navigation failure is fixed; archive coverage remains 103 exports.

The two original-code Service Failure probes force a native memcpy span beyond
the 32-bit address limit and an invalid service ID. Both produce controlled exit
2 with the expected named/numbered diagnostic and no Windows exception. The
diagnostic runner supplies explicit memory zones and an execution mode so its
fixtures reach PPC execution, and accepts `-Jit` for the second CPU path. All
sixteen diagnostic cases pass in interpreter and JIT modes using the Windows
x64 GDB, alongside the 41 standard probes and four real CF object scenarios.

CFArrayGetValueAtIndex and CFArrayGetValues bring archive coverage to 105 exports:
84 code entries and 21 re-exports, matching 29 MyABCD imports. The audit still
lists 5,610 main-fragment exports outside the archive. Their CoreFoundationLib
import indices are 34 and 237. The local routines distinguish inline immutable,
fixed mutable, deque and CFStorage-backed arrays; custom inline callbacks add
20 bytes to the element offset. Deque left offsets are bytes, while CFStorage
nodes contain byte sizes and up to three children. Native readers preserve
opaque 32-bit element values, borrow ownership and leave existing lookup caches
unchanged. Creation, mutation, allocation and release continue through PEF.

Unit tests cover all deque left offsets, custom callback layouts, nested storage
branches, ranges and output guards. All four real-library scenarios pass with
20,000-element arrays, a range crossing an actual storage branch boundary,
reads after replacement/removal and callback counters verifying six retains
and six releases by fallback code, with none added by native reads. The 41
standard probes, focused JIT layouts and archive checks also pass after staging.

CFStringGetLength and CFStringGetCharactersPtr add two further re-exports
(CoreFoundationLib import indices 218 and 326). Coverage is now 107 exports,
including 23 re-exports; 5,608 main-fragment exports remain outside the archive.
The MyABCD direct-import count remains 29. Reference routines 0x38858 and
0x39228 distinguish inline/external buffers and optional Pascal length bytes.
External strings with length 0x00ffffff compute and cache their C-string length
in guest byte order. The UTF-16 accessor returns a borrowed original buffer only
when the Unicode storage bit is set. Neither API converts character encodings.

Unit checks exercise every flag byte, all Pascal lengths and lazy cache reuse.
Real fallback constructors supply byte, Pascal and UTF-16 strings, including
embedded NULs and surrogate pairs. Native reads observe real mutable-string
appends, and real CFRelease disposes the objects. All four interpreter/JIT and
CarbonLib/CoreFoundationLib real-object scenarios pass for these accessors.
All 41 standard probes pass in both interpreter and JIT modes. Archive tests,
coverage, generated-file and Windows help checks pass after rebuild/staging.
MyABCD reaches its application WaitNextEvent under Windows GDB; this startup
check does not establish that the intermittent later navigation failure is fixed.

The date group adds CFAbsoluteTimeGetCurrent, CFDateGetAbsoluteTime,
CFDateGetTimeIntervalSinceDate and CFDateCompare. Coverage is 111 exports
(84 code entries, 27 re-exports), matching 30 direct MyABCD imports; 5,604
main-fragment exports remain outside the archive. MyABCD imports the current-time
function. CoreFoundationLib import indices are 138, 61, 293 and 88 respectively.

Reference 0x232d8 reads unsigned GetDateTime seconds, converts through 2^52 and
adds -3061152000.0 (constants at data 0x910/0x918). Native code preserves that
whole-second behavior and the 1904-to-2001 epoch. Routines 0x23474-0x234b4 read
a big-endian IEEE double at object offset 8, subtract date values and compare
them, with unordered comparisons returning equal. No allocator or class state
is synthesized.

Registry result code 6 is a PPC-only double-result extension to the existing
three-bit signatures. The bridge still marshals integer/pointer arguments, then
uses a native double-return call and writes f1. It does not write an integer
result into r3. The resolver excludes this extension for 68K imports, preserving
their real-library resolution; native double arguments remain outside this ABI.

Unit fixtures cover unsigned seconds across the signed boundary, epoch and
32-bit rollover limits; date bits, signed zero, infinities, NaNs and unaligned
records; and interval/comparison results. Two original-code PPC date fixtures
check f1 bit patterns and bracket the native clock with classic clock reads.
The optional real-object runner's `-Group Dates` constructs dates with real
CFDateCreate, reads them natively and disposes them with real CFRelease.

The real date fixtures import GetDateTime through CarbonLib, as CarbonLib's
initializer rejects direct application imports from InterfaceLib with paramErr.
The initializers remain active. All four date scenarios and the four existing
CF object scenarios pass in interpreter/JIT modes through both libraries.
MyABCD reaches its application event wait under Windows GDB; the native clock
counter is still zero at that checkpoint, so this is a startup check rather
than evidence of the later app clock/navigation flow.
All 43 standard PPC probes pass in interpreter and JIT modes. Archive tests,
coverage audit, generated-file regeneration, Windows help, repository source
formatting and PowerShell checks pass after rebuilding and staging ROMAW.

With the date group staged, another GDB/UI run played Apple and Bee animations,
returned to the main menu and opened the games menu without reproducing the
intermittent service rejection. Short clicks often changed only the highlight;
a short drag inside Home returned to the menu. This does not establish the input
timing cause or close the earlier navigation failure.

The next GWorld reader group adds GetGWorld, GetGWorldDevice, GetGWorldPixMap,
GetPixBaseAddr and GetPixelsState at CarbonLib offsets 0xab0c4, 0xab3c8,
0xab508, 0xab304 and 0xab28c. The reference forwards to QDOffscreen selectors
5, 18, 23, 15 and 13. The archive reads ROMAW's existing canonical gw_info
records and live QuickDraw globals rather than building another object registry.
GWorld device/pixmap identity, lock/purge/keep-local flags and master-pointer
changes stay shared with the classic manager. Unlocked offscreen pixels are
indirect through handles; locked pixels and the main screen are direct pointers.
Ordinary color ports retain GetGWorldPixMap compatibility. No allocation,
locking, purge or disposal behavior is replaced by these readers.

Coverage is now 116 exports: 89 code entries and 27 re-exports, matching 34
MyABCD imports. There are still 5,599 main-fragment exports outside the archive,
plus the remaining companion-fragment scope. Unit fixtures cover moving/purged
buffers, direct screen storage, encoded output pointers and all flag bytes.
The original PPC GWorld fixture creates real 8-/16-/32-bit worlds, checks native
reads across nested locks and drives keep-local/purge/lock changes through
QDOffscreen selector 14 using the live classic dispatch vector. Its sentinel
CarbonLib supplies only initialized fallback, without an Apple binary.
After rebuilding/staging, MyABCD reaches its application WaitNextEvent under
Windows GDB with four calls to the new native GWorld readers before that
checkpoint. This confirms startup integration; it does not verify later game
playback or resolve the intermittent navigation failure.
All 44 standard PPC probes pass in interpreter and JIT modes, as do the four
real CF object and four real CFDate scenarios. Archive tests, coverage audit,
generated files, Windows help, repository formatting and PowerShell checks pass.

CFArrayApplyFunction adds the first callback-driven native CF operation.
CarbonLib re-exports CoreFoundationLib import 449; routine 0x1c328 reads each
selected element and calls the applier with the value followed by context.
The native implementation preserves those opaque guest words, borrows ownership
and reuses the existing storage readers for inline, deque and CFStorage arrays.
Callback failure stops the loop rather than resuming the guest caller normally.

The ROMAW backend validates descriptors/vectors and invokes PPC callbacks
through its existing reentrant stack pool and nesting limit. Its diagnostic
remains active across the native API return. The registry now has an explicit
PPC-only flag, honored for initial and dynamic resolution, so 68K callers of
this callback ABI retain real-library fallback. Ordinary entries retain their
previous architecture eligibility; floating-result restrictions remain active.
The coverage parser accepts the flag and rejects unparsed entries rather than
silently joining malformed metadata to a later export.

Coverage is 117 exports (89 code entries, 28 re-exports), with 5,598 main-fragment
exports outside the archive. MyABCD's direct-import count remains 34.
Unit fixtures exercise every valid range of each existing array storage fixture,
opaque contexts, invalid ranges and immediate stop after a callback failure.
An original PPC fixture recursively reenters native ApplyFunction and invokes
a native accessor inside the inner callback. Real CF arrays of all four storage
forms now check callback counts/checksums alongside ownership counters. The
two callback diagnostic fixtures exercise invalid descriptors and a valid
callback that calls an unsupported API, preserving controlled exit 2.
After rebuild/staging, all 45 standard probes and all 18 diagnostic probes pass
in interpreter and JIT modes. The four real CF object and four real CFDate
scenarios pass, along with archive, coverage, generated-file, help and formatting
checks. MyABCD still reaches its application event wait under Windows GDB;
this callback API is not a direct MyABCD import, and the later navigation fault
remains unresolved.

CFArrayBSearchValues adds CoreFoundationLib re-export 272 (0x110), using routine
0x1dc2c and its contiguous search helper at 0x1a124. Native code preserves signed
comparator results and opaque value/context words, stops after a failed callback
and keeps the existing 68K fallback for this PPC callback ABI. Contiguous ranges
shorter than four elements search backwards; larger ranges use the reference's
endpoint/power-of-two comparison sequence.

The CFStorage path has a verified CarbonLib 1.4 quirk: its initial pivot decision
combines the range location with the comparison result. Searching an actual
CFStorage array containing 0..19999 with range {1,7} for 1 returns 5, while range
location zero retains the usual insertion behavior. The local disassembly and
an interpreter run of the real library both establish this result. Native code
preserves it explicitly rather than silently replacing the storage branch with
a different binary-search algorithm.

The new `-Group Search` real-object fixtures obtain the original export through
a real CoreFoundationLib CFM connection and FindSymbol. They check its entry
instruction is not a native service stub, then call it directly on the same
objects used by the native import. Results, comparator call counts and argument
order hashes match in all four library/CPU combinations. This covers ascending
and descending arrays, duplicates, opaque words, every subrange of the short
arrays and selected ranges of 20,000-element CFStorage arrays. Constructors,
releases, initialization and CFM connection lifetime stay on their real paths.
Larger immutable arrays additionally check contiguous search lengths around
powers of two through 512, covering the helper's larger unrolled search paths.

Unit tests add comparator ranges and failure stopping across all existing array
layouts, all deque offsets and duplicate payloads. Two original-code search
fixtures use sentinel fallback without Apple binaries; two diagnostic fixtures
exercise an invalid comparator and a comparator calling an unsupported API.
Coverage is now 118 exports (89 code entries, 29 re-exports), with 5,597
main-fragment exports outside the archive and MyABCD's direct-import count still
34. Companion coverage and complete behavioral compatibility remain unfinished.
After rebuilding/staging, all 47 standard PPC probes and 20 diagnostic cases
pass in interpreter and JIT modes. All 12 real-library object/date/search
scenarios pass, along with archive, coverage, generated-file, Windows help,
source formatting and PowerShell checks. MyABCD reaches its application event
wait under Windows GDB; its intermittent later navigation failure remains open.

CFAllocatorGetDefault adds CoreFoundationLib re-export 130 (0x82), a direct
MyABCD import. The reference getter at 0x17344 reads the allocator at TOC+0x6f28,
falling back to the live system-default word at TOC+0x3164. Native code reads
that state without retaining or caching it, so real CFAllocatorSetDefault calls
remain visible. Allocator construction, mutation and initialization still use
the real library.

The bridge inspects the original linked function descriptor and its owning PEF
sections, including functions re-exported from a separate image. The known
CoreFoundationLib 1.4 code/data extents and offsets select the native state read;
other layouts invoke their original getter through the PPC callback backend.
Missing functions, invalid export classes, data-section entry points and
unaligned entry points produce controlled diagnostics. The generic section
query retains bounded arithmetic and does not require ROMAW headers.
This state-sharing implementation remains PPC-only; 68K callers retain fallback.

Coverage is 119 exports (89 code entries, 30 re-exports), with 5,596 main-fragment
exports outside the archive and 35 direct MyABCD imports supplied by it.
Companion APIs and complete behavioral compatibility remain unfinished.
The original-code allocator probes exercise initialization, live state changes,
translated results and a re-exported reference function. Real allocator tests
compare native results with the original getter before and after custom-default
changes, checking pointer identity and borrowed ownership. A GDB checkpoint
recorded 15 native state reads and zero delegated getter calls on the installed
library; the alternate-layout fixture exercises delegation separately.
After rebuild/staging, all 49 standard probes and 24 diagnostic cases pass in
both engines. All 16 real-library object/date/search/allocator scenarios pass,
as do archive, focused PEF bounds, coverage, generated-file, Windows help and
formatting checks. MyABCD reaches its application event wait with six native
allocator state reads under GDB. This is a startup checkpoint; its later
intermittent navigation failure remains unresolved.

CFAllocatorGetContext and CFAllocatorGetPreferredSizeForSize add re-exports
373 (0x175) and 133 (0x85), referencing CoreFoundationLib 0x1778c and 0x176f8.
Context retrieval writes version zero and copies eight live guest words from
object+16. Preferred-size requests call the descriptor at object+44 with the
size, hints and opaque info word. Nonpositive requests do not call it; the
result is clamped with signed comparisons against zero and the request. Failed
callbacks preserve their backend diagnostic and stop the caller
through the existing launch failure path. Null allocators use the live default.
These operations retain PPC-only eligibility while the 68K callback ABI stays
on real-library fallback.

Coverage is now 121 exports (89 code entries, 32 re-exports), with 5,594
main-fragment exports outside the archive. MyABCD's direct-import count remains
35. Archive tests cover unaligned context records/output guards, opaque words,
all combinations of signed size/result boundaries, missing callbacks, failure
stopping and default lookup. Original-code probes test PPC argument transport,
callback counts and signed overflow clamping. Real-library tests obtain and
verify original PPC exports before comparing context fields and preferred-size
results for explicit/default custom allocators, preserving borrowed ownership.
After rebuild/staging, all 49 standard probes and 26 diagnostic cases pass in
both engines, including invalid preferred-size descriptors and a callback that
calls an unsupported API. All 16 real-library scenarios pass, along with archive,
coverage, generated-file, Windows help and formatting checks. The previously
running full CPU/PEF suite also finished successfully. MyABCD reaches its event
wait with six native allocator-default reads; later navigation remains unverified.

CFAllocatorAllocate, CFAllocatorReallocate and CFAllocatorDeallocate add
CoreFoundationLib re-exports 359 (0x167), 58 (0x3a) and 356 (0x164). Their
reference code is 0x1752c, 0x175a0 and 0x17694. Native allocation forwards every
nonzero signed size to the required callback at object+32. Reallocation routes
null/positive requests there, nonnull/zero requests to deallocation at object+40,
and other requests to the optional callback at object+36. Null/zero reallocation
returns null. Null blocks and absent deallocation callbacks are ignored. Default
selection happens before those branches, matching the reference. Callback
arguments retain raw guest pointer/info words and hints; results translate once.
PPC callback errors stop the launch while preserving the original diagnostic.
The three APIs retain real 68K fallback and do not replace allocator creation,
class registration, default setters or object ownership.

Coverage is 124 exports (89 code entries, 35 re-exports), with 5,591 main-fragment
exports outside the archive. MyABCD's direct-import count remains 35. Archive
tests cover all callback-presence combinations, unaligned records, signed size
boundaries, default lookup, null returns and callback failures. Original-code
PPC probes record arguments and callback selection. The new `-Group Allocation`
real-library tests verify original PPC oracle exports, compare result/argument
records for explicit/default custom allocators, and grow a real system allocation
while checking its original bytes before disposal. No Apple executable bytes
are embedded in these fixtures.
After build/staging, all 51 standard probes and 32 diagnostic cases pass in
both engines. Invalid descriptors and unsupported calls inside each of the
three allocator callbacks produce controlled exit 2. All 20 real-library
scenarios pass, along with archive, coverage, generated-file, Windows help and
formatting checks. MyABCD reaches its event wait with six native allocator-
default reads under GDB. The later intermittent navigation failure remains open.

CFAllocatorCreate and CFAllocatorGetTypeID add CoreFoundationLib re-exports
194 (0xc2) and 482 (0x1e2), referencing code 0x173f4 and 0x1733c. A shared layout
check recognizes the installed 1.4 section extents and function offsets; other
layouts invoke the original exports. The type getter reads the live class word
at TOC+0x89c, without assigning a substitute class ID. Construction mirrors
0x53360's unsigned class-bound checks at TOC+0xa6c/0xa68 before setting header
type bits. Real class registration and library initialization remain required.

The constructor retains info first, then allocates 48 bytes through the selected
parent/default allocator or the input context's allocation callback when the
raw allocator word is kCFAllocatorUseContext (0x221). It stores the resolved
parent/sentinel, original version, retained info and live callback fields.
Allocation failure leaves info retained: both the local disassembly and the
original constructor's callback counts confirm this 1.4 behavior. Failed retain
callbacks stop before allocation and preserve their backend diagnostic.

Archive tests exercise unaligned records, class IDs/bounds, all parent modes,
retained/borrowed info, allocation failures and unknown-layout delegation. The
offline PPC class fixture contains only original instructions, with 1.4 section
extents and deliberately different reference export bodies; its real PEF
initializer publishes class 7 and default storage. This tests the native path
without Apple assets and does not replace any real application's class state.
The `-Group Creation` real-library tests obtain original constructor/type
exports, verify original PPC entry code, compare all 48 allocator bytes, and
dispose both native/original objects through real CFRelease. Reentrant storage
callbacks exercise native allocation/free inside construction/destruction and
check the reference's distinct UseContext disposal order.

Coverage is 126 exports (89 code entries, 37 re-exports), with 5,589 main-fragment
exports outside the archive and MyABCD's direct-import count still 35. Native
class registration, default mutation and general CF ownership remain unfinished.
After build/staging, all 53 standard probes and 36 diagnostic cases pass in both
engines. All 24 real-library scenarios pass, including the constructor's retained-
info allocation-failure behavior and real destruction of native-created objects.
Archive, coverage, generated-file, Windows help and formatting checks pass.
MyABCD reaches its event wait with one native constructor call and all 13 native
record writes, confirming C construction rather than whole-function delegation;
six allocator-default reads also use the native path. Later navigation remains
unverified and its intermittent failure remains open.

CFRetain, CFRelease, CFGetRetainCount and CFGetAllocator add CoreFoundationLib
re-exports 380 (0x17c), 458 (0x1ca), 167 (0xa7) and 287 (0x11f), with reference
code 0x533e4, 0x53488, 0x535f0 and 0x53844. CFRelease is a direct MyABCD import.
Known 1.4 layouts use C ownership operations; other layouts invoke the original
exports, and 68K callers retain their previous fallback.

The upper header halfword holds count bits and the overflow marker. Native code
preserves lower flags, maintains the real dictionary at TOC+0xa64 through its
original GetValue/SetValue/RemoveValue exports, and implements rollover/removal
at the reference's 32768-count boundaries. The reference lock helpers at
0x1a98c/0x1a990 are no-ops; these operations follow ROMAW's cooperative guest
execution rather than claiming independent host-thread synchronization.

Final release reads the live class table at TOC+0x65c4 and invokes its finalizer
before selecting system storage or a custom allocation prefix. Custom parent
references are released after deallocation. Allocator objects use their special
cleanup path, preserving UseContext free-before-info-release order and loading
needed values before storage can disappear. Callback failures stop further
cleanup and retain the active diagnostic. Class registration, dictionary
operations and other class finalizers remain shared real-library dependencies.

Archive tests cover all 65536 header prefixes, overflow values/failures,
unaligned records, allocator identity, lower-flag preservation, post-finalizer
flag changes and poisoned storage after deallocation. Offline PPC fixtures use
only original instructions and their own initialized class/map records. The
`-Group Ownership` real tests compare original PPC exports and native APIs on
separate actual CFData objects through 65540 retains and releases, checking both
headers and original getter results around the 32K/64K transitions. Custom-
allocator objects check prefixed storage, balanced callbacks and parent
references. Creation tests additionally exchange native/original release paths.

Coverage is 130 exports (89 code entries, 41 re-exports), with 5585 main-fragment
exports outside the archive and 36 direct MyABCD imports implemented. This does
not complete CoreFoundation registration, classes or CarbonLib compatibility.
After build/staging, all 55 standard probes and 40 diagnostic cases pass in both
engines. All 28 real-library scenarios pass, including the overflow stress tests
and reciprocal constructor/release interoperability. Archive, coverage,
generated-file, Windows help and formatting checks pass. MyABCD reaches its
event wait with 16 native CFRelease entries and three executions of the C final-
destruction path under GDB. Later activity/navigation compatibility remains
unverified; the intermittent navigation failure remains open.

CFArrayCreate and CFArrayGetTypeID add CoreFoundationLib re-exports 208 (0xd0)
and 123 (0x7b), referencing 0x1b8b4 and 0x1b5c4. CFArrayCreate is a direct
MyABCD import. Native immutable construction follows helper 0x1b5cc and runtime
instance allocation 0x53204, reading the registered class at TOC+0x8c0.
Instances use system storage or a retained custom-parent prefix, preserve the
type/no-prefix bits, and invoke registered class initialization before setting
array fields.

Callback classification compares the four function words against the live null
and type records at TOC+0x3248/0x3234, ignoring version. Custom records occupy 20
bytes after the immutable 12-byte header. Retain callbacks receive the original
allocator argument, including zero for default selection, and opaque element
words. Native code reads each live retain procedure and value at its iteration,
so callback mutations affect subsequent work as in the reference. Count is
published after successful retention. Unknown layouts and 68K imports retain
real-library fallback.

Negative counts, missing nonempty value buffers and unrepresentable allocation
sizes return null. Allocation failure also returns null before retention or
object access. This avoids the original outer constructor's unchecked access
to the null result of failed instance allocation; that low-memory write is not
reproduced in the real oracle tests. Callback errors retain their diagnostic and
stop further construction rather than publishing a successful array.

Archive tests cover all callback kinds, version-insensitive classification,
four allocator modes, unaligned records, live initialization/retain mutations,
prefix ownership, guards, failure paths and unknown-layout delegation. Offline
fixtures extend the existing original-code class module without Apple code.
The `-Group ArrayCreation` real tests compare complete immutable records and
callback hashes for default/system/custom/custom-default allocators, empty and
nonempty arrays, null/type/custom callbacks and type-record copies. They check
CF value/parent ownership, reciprocal original/native releases and allocation
failure before value retention. Real class registration and other methods stay
available through the real library.

Coverage is 132 exports (89 code entries, 43 re-exports), with 5583 main-fragment
exports outside the archive and 37 direct MyABCD imports implemented. Mutable
construction and complete array/CF/Carbon compatibility remain unfinished.
After build/staging, all 57 standard probes and 46 diagnostic cases pass in both
engines. All 32 real-library scenarios pass, including array ownership before
release and reciprocal native/original destruction. Archive, coverage,
generated-file, Windows help and formatting checks pass. MyABCD reaches its
event wait, but no native array constructor runs during the observed startup
or alphabet activity. Apple, Bee, Cat and Dog animations were observed before
navigation reproduced an unhandled host-service failure reported at
InterfaceLib:HLock. This remains an open runtime issue, not evidence of complete
application compatibility.

Follow-up MyABCD debugging identified the HLock failure after Escape leaves
fullscreen: a window-definition callback calls PtrToHand, then reads its output
as a guest Handle. The direct bridge binding passed that four-byte guest slot
to an implementation accepting a native Handle*, writing an eight-byte host
pointer on Windows x64. HandToHand had the same incompatible binding. Dedicated
adapters now use local native handles, convert to/from big-endian guest address
words, validate output extents and preserve the slot on failure. No reference
initializer or missing API is bypassed, and archive coverage remains 132.

The original PPC Handle Copy probe fails against the previous staged binary
at its output guards. With the adapters, interpreter and JIT checks pass for
four output alignments, exact copy contents, independent handles, lock/unlock,
nil handles, impossible allocations and invalid output slots. The two existing
ownership overflow stress probes have a 60-second completion limit; ordinary
probes retain 15 seconds. One stress run exceeded the old limit while other
validation was active, without a guest diagnostic.

In the rebuilt MyABCD, Escape successfully returns from fullscreen to its
window, and Home returns from both alphabet and numbers activities to the main
menu. Apple/Bee and One/Two animations were observed. GDB records the corrected
PtrToHand adapter during this transition. This verifies the previously failing
sequence and two activity paths, not all MyABCD or CarbonLib behavior.
Final validation passes all 58 standard probes and 46 diagnostic cases in each
engine, all four real array-construction scenarios, five CFM-68K probes, Windows
help and repository formatting checks. The diagnostic run uses x64 GDB for the
current Windows x64 build. No native CFArrayCreate call was logged in these
MyABCD activity paths; direct array interoperability remains fixture-tested.

CFStringGetCharacterAtIndex and CFStringGetCharacters add CoreFoundationLib
re-exports 398 (0x18e) and 184 (0xb8), with original entries at 0x3891c and
0x38a0c. Native readers follow inline/external storage and Pascal prefix flags.
UTF-16 remains in guest byte order; the scalar result is unsigned. Byte strings
use the current converter at TOC+0x6f5c. Missing conversion widens each byte,
while a callback's false low Boolean byte writes 0xfffe. Bulk reads load the
converter again for each non-ASCII byte, preserving live callback/input changes.
Errors stop subsequent work and preserve the callback diagnostic.

Length checks do not change the lazy byte-string cache. Negative indices and
invalid ranges return zero or leave output unchanged; normal readers do not
allocate, retain or release objects. Unrecognized profiles delegate the original
exports and 68K imports keep their real-library path. Archive tests cover byte
values, raw UTF-16, four record/output alignments, callback changes/failures,
guards, lazy cache preservation and foreign-layout delegation. The CFStrings
real-library group resolves original PPC reader descriptors and compares actual
immutable/mutable objects, MacRoman bytes including embedded NUL, surrogate and
0xffff units, and range boundaries around 32 characters.

The investigated CFStringCompare wrapper is deliberately not registered. Its
reference 0x3a880 calls CompareWithOptions 0x39fa8 over the first string's full
length. Differential testing of precomposed cafe-acute versus a combining-accent
string with option 0x10 exposed different results between a fresh callback stack
and the original caller's stack. The nonliteral engine calls decomposition at
0x4617c, through CFUniCharMapTo 0x492e0 mapping type 3, to 0x49a80. That last
routine is only blr in this local reference. An original PPC probe on the real
initialized library confirmed that U+00e9 returns 233 and leaves all 20 output
bytes untouched. The comparison engine then consumes an unwritten buffer.
Native comparison needs actual decomposition/collation work; forwarding this
engine from a C wrapper is not a verified implementation. Both comparison APIs
remain on their existing fallback paths, and their exports are not counted.

Coverage is 134 exports (89 code entries, 45 re-exports), leaving 5581 main
exports outside the archive. MyABCD's direct archive-import count remains 37;
the new readers also support the pending native comparison implementation.
After build/staging, archive checks, all 60 standard probes and 49 diagnostic
cases per engine, all 36 real-library scenarios, generated-file regeneration,
Windows help and formatting checks pass. The offline reader fixture explicitly
checks its own initializer's marker. MyABCD reaches its application event wait
with zero calls to either new reader logged during startup. Reader behavior is
verified by the fixtures; this startup check does not establish full interactive
MyABCD compatibility or native comparison behavior.

CFUniCharIsMemberOf now runs in C for the recognized CoreFoundationLib 1.4
profile (entry 0x461fc). Selectors 1-3 compute control/whitespace membership;
selectors 4-15 read the real initialized packed sets through their TOC pointers.
The reader checks 1024-code-unit summary bits, then binary-searches sorted
singletons and inclusive ranges. It leaves live records and lookup caches
unchanged. Unknown layouts call the original descriptor, preserving the unsigned
Boolean result and callback diagnostics. The existing library initializer still
runs before use; no Unicode property records are fabricated for actual apps.

This export exists only in CoreFoundationLib. The registry's COMPANION_ONLY flag
keeps it out of CarbonLib lookup and interprets its reference offset within the
named companion. Coverage audits require matching companion metadata and report
per-library totals. There are now 135 archive bindings: 134 main exports and one
companion-only export. Main coverage and MyABCD's 37 direct archive imports are
unchanged. Supplied metadata verifies 33/644 CoreFoundationLib exports and 13/279
StdCLib exports; re-exports overlap these counts. The remaining companion
fragments still need separate audits and implementation.

The real UniChar probe compares every UTF-16 code unit for selectors 0-15 with
the original PPC descriptor, in interpreter and JIT modes. Offline fixtures
verify genuine initialization, foreign-layout delegation and namespace isolation;
diagnostic fixtures check wrong-library imports and delegated callback failures.
Archive tests additionally exercise synthetic packed sets at four alignments,
empty/single/multiple records, summary-block boundaries and untouched storage.
This API supplies native character classification for subsequent comparison work;
it does not resolve the reference decomposition defect described above.

Validation after rebuilding/staging passes archive and coverage tests, all 62
standard probes and 51 diagnostic cases per engine, all 38 real-library
scenarios, generated-file regeneration, Windows help and repository formatting
checks. MyABCD reaches its application event wait with zero calls to the new
membership API during startup. That startup result checks regression behavior;
the exhaustive fixture establishes the new API's interoperability.

CFBundleGetFunctionPointerForName and CFBundleGetDataPointerForName now implement
their 1.4 lookup logic in C (CoreFoundationLib entries 0xf5b0/0xf7bc; CarbonLib
re-export indices 393/343). They check the live loaded byte, invoke the original
CFBundleLoadExecutable when needed, then require executable kind 1. Names pass
through the original CFStringGetPascalString with ASCII encoding and capacity
255. CFM resolution uses the same native connection/export helper as guest
FindSymbol. Function lookups require class 2 descriptors; data lookups require
class 1. Missing symbols, invalid connections and class mismatches retain the
reference CFLog messages and signed status/class arguments. Scratch storage is
system-only, disposed on failure as well as success, and leaves MemErr changes
from the actual operations intact while hiding scratch allocation/disposal errors.

Unrecognized profiles delegate the original entry, and 68K imports keep fallback.
The real loader, converter and logging functions remain dependencies of the
native implementation. Unit tests check their argument order, Boolean low bytes,
failure propagation and cleanup. Offline PPC fixtures check genuine initialization,
on-demand loading, shared initialized symbols, logging and foreign profiles.
The real-library Bundle group compares original/native calls using real CFStrings
and CFM connections, executing returned descriptors and reading initialized data.
It covers wrong classes, absent symbols, invalid connections, other executable
kinds, empty/254-/255-byte names and non-ASCII conversion failure. Its minimal
bundle records exercise the observed lookup fields; complete bundle creation and
executable loading still need implementation and separate validation.

Coverage is 136/5715 main exports (89 code, 47 re-exports), plus one companion-only
binding. Supplied metadata verifies 35/644 CoreFoundationLib and 13/279 StdCLib
exports, with overlapping re-exports. MyABCD has 39 direct archive imports;
274 strong and 132 weak imports remain outside the archive. Its startup reaches
the application event wait with zero new bundle-lookup calls recorded, so the
new lookup behavior is fixture-tested rather than demonstrated by that startup.

An export inventory using all 19 named data fragments in the CarbonLib container
and the separate StdCLib reference confirms the wider remaining scope below.
These counts overlap through re-exports and do not include compatibility supplied
by existing Toolbox bridge bindings or original PEF fallback. This inventories
metadata; it does not establish behavior for the unimplemented fragments or the
container's separate resource-code PEFs.

| Fragment | Reference exports | Archive exports |
| --- | ---: | ---: |
| CarbonLib | 5715 | 157 |
| CoreFoundationLib | 644 | 56 |
| StdCLib | 279 | 13 |
| Apple_Carbon_Multimedia | 2110 | 0 |
| Apple_Carbon_Networking | 172 | 0 |
| AppleHelpLib | 6 | 0 |
| ATSGlue.CL | 60 | 0 |
| ATSStreaming.CL | 9 | 0 |
| ATSUnicodeLib.CL | 133 | 0 |
| CarbonEventLib | 9 | 0 |
| CoreFoundationGlobalsLib1 | 17 | 0 |
| DataBrowserLib | 112 | 0 |
| DebugLib | 11 | 0 |
| DragLib | 43 | 0 |
| HTMLRenderingLib.CL | 71 | 0 |
| LayoutCore.CL | 15 | 0 |
| LocalesLib.CL | 15 | 0 |
| NavigationLib.CL | 45 | 0 |
| Textension_CL | 89 | 0 |
| UnicodeUtilitiesLib.CL | 9 | 0 |

After build/staging, archive/coverage checks, all 64 standard probes and 53
diagnostic cases per engine, all 42 real-library scenarios, five CFM-68K probes,
generated-file regeneration, Windows help and formatting checks pass. The
exhaustive UniChar interpreter regression exceeded its former 120-second cap
while progressing through selector 11 without mismatches. That million-pair
stress case now has a 240-second limit and passes in both engines; ordinary
real-library scenarios retain their 30-second limit.

CFStringGetPascalString now implements the 1.4 control flow in C (CoreFoundationLib
0x39274, CarbonLib re-export 328). It reads the shared inline/external layouts,
caches lazy external lengths and rejects source counts above 255. Byte copies
use the live eight-bit encoding at TOC+0x3a70, invoking the real lazy initializer
when needed and preserving the original encoding-group exceptions. Compatible
copies leave output unchanged when capacity is insufficient. Other conversions
call the original __CFStringEncodeByteStream with nine PPC words, including a
guest-addressed used-byte output. A partial conversion clears only the prefix;
callback errors preserve partial output and the active diagnostic. System
scratch storage preserves MemErr around allocation/disposal. Unknown profiles
delegate the original API and 68K imports keep fallback.

The original's 255 limit counts source units rather than encoded bytes. Real
PPC checks confirm that 255 U+0100 units encode to a complete 510-byte UTF-8
payload with sufficient capacity, return true and store prefix byte 254. Native
code retains this 1.4 behavior; it does not claim modern CF semantics for such
payloads. The CFPascal runner checks 960 source/encoding/capacity combinations
per engine/import library, comparing Boolean results and all 1032 buffer/guard
bytes. Sources include actual immutable/mutable strings, embedded NUL, MacRoman
bytes, UTF-16, paired/unpaired surrogates and empty/255-/256-unit boundaries.
Offline fixtures check real initialization, lazy caches, the ninth stack argument
and foreign profiles. Diagnostics cover lazy-encoding and byte-stream failures.

Native bundle lookups now call this C conversion API directly. Their real-object
interoperability group passes after migration. The offline bundle converter has
a deliberately foreign entry offset because its input is fixture Pascal bytes,
not real CFString records; that dependency consequently retains its original
fixture fallback. The general encoding engine and its lazy encoding initializer
remain real dependencies and must still be implemented.

Coverage is 137 main exports (89 code, 48 re-exports), plus one companion-only
binding. CoreFoundationLib coverage is 36/644; StdCLib remains 13/279. MyABCD's
39 direct archive imports are unchanged. Its application event wait is reached
with zero calls to the new conversion API during startup, so conversion behavior
is established by fixtures rather than that startup result. The separate PPC
runtime/PEF suite from the bundle group completed successfully; its CPU library
is unchanged by this string-only implementation.

Validation after build/staging passes archive and coverage checks, all 67
standard probes and 55 diagnostic cases per engine, all 46 real-library
scenarios, five CFM-68K probes, generated-file regeneration, Windows help and
formatting checks. No reference initializer is skipped. General encoding and
other unimplemented APIs continue through the original library.

Encoding migration now covers three paths within __CFStringEncodeByteStream:
UTF-16 output, NonLossyASCII escaping and the direct-UTF-16 UTF-8 cache/dispatch
path. CFStringGetPascalString calls this C router; the private engine export is
deliberately still unregistered. Unknown profiles, generic converters, UTF-8
calls without a used-byte output and small BOM-buffer cases keep their original
entry. UTF-8's codec callback also remains original code. This step adds no
completed exports and does not establish a fully native encoding subsystem.

NonLossyASCII uses 32-unit reads into released system scratch storage, copies
printable ASCII/tab/newline/return directly, doubles backslashes and emits octal
or live-table hexadecimal escapes. Each token fits completely before any bytes
are written. UTF-8 dispatch shares the real TOC+0x9ac cache and built-in converter
record, forwards six callback words and preserves unsigned flags and failure
diagnostics. UTF-16 copies guest-order units and accounts for a requested BOM.

New original-only PPC checks confirm the private 1.4 quirks preserved here:
UTF-16 reads from index zero despite a nonzero location; NonLossyASCII uses its
length as an inline-buffer end bound and stops at U+FFFF. Measurement counts BOM
bytes without writing a buffer. The checks resolve the original private export
through CFM, so future native registration cannot turn this oracle into itself.
Unit checks additionally cover exact token boundaries, output guards, allocation
failure, callback arguments/errors, cache persistence and untouched fallback
arguments. Diagnostic probes reach failures in the actual new six-word UTF-8
callback and NonLossyASCII byte-to-Unicode callback paths, with scratch cleanup.

The real Pascal conversion matrix still matches in both engines/import libraries,
including immutable/mutable strings and every guarded output byte. A separate
GDB run records 140 native UTF-16 branches, 120 NonLossyASCII scratch allocations
and 70 UTF-8 callback dispatches, proving the real matrix reaches the C paths.
The generic converter routines at 0x447c8/0x4576c/0x45d78 are not public exports;
their control flow, converter tables and external codec dependencies still need
native ports. Export coverage remains 137 main bindings plus one companion-only
binding, with 36/644 CoreFoundationLib exports and 13/279 StdCLib exports.

This group passes WSL build/staging, archive tests, all 67 standard probes and
57 diagnostic cases in each engine, eight targeted real-library scenarios
(Pascal conversion and bundle lookup), five CFM-68K probes, generated-file,
Windows help, coverage and formatting checks. The original-only private-engine
vectors run alongside each real Pascal matrix. Remaining encoding paths keep
fallback and are not counted as completed exports.

CFStringGetCStringPtr and CFStringGetPascalStringPtr now implement their 1.4
borrowed-storage rules in C (entries 0x38ff4/0x38d70; CarbonLib re-exports 70/263).
Both use the shared current-encoding and ASCII-compatibility checks, factored
with Pascal conversion without changing its initialization behavior. CString
lookup initializes encoding before checking its current termination flag, then
skips a Pascal prefix if present. Pascal lookup rejects missing/Unicode format
first, reads the current layout after initialization, and validates explicit
lengths against the prefix. Lazy external lengths retain their original cache.
Neither getter copies, allocates, retains or releases the borrowed storage.
Other profiles and 68K imports retain original fallback.

Archive tests cover all 256 low flag combinations at four alignments, immutable
and mutable layout forms, inconsistent/unknown explicit lengths, live buffer
changes, encoding-group exceptions, initialization ordering, callback mutations
and failures, and five foreign-profile dimensions. The StringPointers real group
compares 196 original/native pointer identities per engine/import library on
genuine byte, UTF-16, C-string and mutable objects. It requires each getter to
return actual borrowed storage for some cases, preventing an all-null result
from passing. Offline PPC probes separately verify precise returned addresses,
lazy-cache mutation, initialization and foreign-profile pointer marshalling.

Coverage is now 139 main exports (89 code, 50 re-exports), plus one companion-only
binding. CoreFoundationLib coverage is 38/644; StdCLib remains 13/279. MyABCD's
39 direct archive imports are unchanged. Generic encoding, the private engine
export and remaining codec dependencies are still incomplete.

After build/staging, archive checks, all 70 standard probes and 58 diagnostic
cases per engine, 12 targeted real-library scenarios (pointer getters, Pascal
conversion and bundle lookup), five CFM-68K probes, generated-file, Windows help,
coverage and formatting checks pass. MyABCD reaches its application event wait
with zero calls to either new getter during startup. Borrowed-pointer behavior
is established by the fixtures, not by that startup smoke check.

The partial encoding engine now uses the native borrowed-pointer getters for
warm byte storage. Exact-encoding copies include NUL and high bytes; other
ASCII-compatible encodings finish in C when the whole input is ASCII or its
prefix fills the output buffer. Measurement ignores output capacity. Cold
encoding caches and mixed tails with space remaining still delegate the whole
call before publishing output or counts. This ordering matters because the
original generic converter can fail without publishing its prefix counts.

Archive checks cover byte offsets, Pascal prefixes, null-output measurement,
zero/exact capacities, output guards and untouched mixed-tail/cold-cache
fallback. The 960-pair real Pascal matrix matches in both engines and both
import libraries; GDB separately records 130 entries into the new C copy branch.
Build/staging, 70 standard and 58 diagnostic cases per engine, 12 targeted real
scenarios, five CFM-68K probes, generated-file, help and formatting checks pass.
Coverage stays at 139 main exports plus one companion-only binding. The private
engine remains unregistered until generic converters and the remaining paths
are implemented.

CFStringGetCString now implements the public 1.4 entry at 0x395a0 (CarbonLib
re-export 303). Its shared copy helper retains Pascal conversion's source-count
limit while C strings accept longer sources. Both reserve one output byte;
C-string success appends NUL at the encoded-byte count rather than storing a
Pascal prefix. Incomplete conversion clears the first byte and preserves the
remaining partial payload; callback errors propagate before a terminator is
published. Compatible byte copies, source-layout decoding, lazy lengths and
encoding initialization run in C; general codecs retain the incremental engine
and real dependencies. Other profiles and 68K imports retain original fallback.

Archive checks include four source/output alignments, inline and external byte
storage, binary payloads, source lengths 0/1/255/256/511, exact capacities,
300-byte encoded output, partial conversion, allocation/callback failures,
MemErr preservation and five foreign-profile dimensions. The CFCString group
compares 960 original/native pairs per engine/import library, including mutable
Unicode strings and truncated UTF-8, UTF-16 and NonLossyASCII buffers. The existing
Pascal matrix is rerun because both APIs share the helper.

Coverage is 140 main exports (89 code, 51 re-exports), plus one companion-only
binding; CoreFoundationLib is 39/644 and StdCLib remains 13/279. MyABCD's direct
archive imports remain 39. Its startup reaches the application event wait with
zero calls to the new C-string API; the fixtures establish this API's behavior.

Validation passes WSL build/staging, archive checks, 73 standard probes and 60
diagnostic cases per engine, 16 targeted real-library scenarios (C-string and
Pascal conversion, bundle lookup and borrowed pointers), five CFM-68K probes,
generated-file, Windows help, coverage and both formatting checks. Generic
encoding and the remaining CarbonLib/companion exports remain incomplete.

The direct UTF-16-to-UTF-8 path now implements the built-in codec at 0x4d248 in
C, including its scalar emitter at 0x4d3c8. Native dispatch requires the original
descriptor at TOC+0x1f5c with the matching relocated code address and TOC. Other
cached callbacks and changed descriptors retain real callback dispatch and
failure diagnostics. The codec reads its live prefix-byte table at TOC+0x5934;
no Apple instruction bytes are compiled into the archive.

This 1.4 codec ignores flags, emits no BOM and measures whenever capacity is zero,
even if output is non-null. It accepts lone low surrogates and stops before a
lone high surrogate. If a valid pair cannot fit, it rolls back one source unit
rather than the whole pair. Unit vectors cover encoding boundaries, exact and
partial capacities, surrogate extrema, measurement, output guards, nonzero source
locations, a changed prefix table and descriptor fallback. Original-only private
PPC vectors independently pin pair rollback and zero-capacity behavior.

Both public conversion matrices now compare 1,200 original/native pairs per
engine/import library, adding paired and unpaired surrogate sources. The guarded
buffers and return values match; a separate GDB run records 110 calls into the C
codec during the Pascal matrix. This adds no completed exports: coverage remains
140 main bindings plus one companion-only binding, CoreFoundationLib 39/644 and
StdCLib 13/279. Byte-storage UTF-8, generic converter orchestration and remaining
codec dependencies still require native ports, and the private engine remains
unregistered.

For this codec group, WSL build/staging, archive tests, 73 standard and 60
diagnostic cases per engine, 16 targeted real-library scenarios, five CFM-68K
probes, generated-file, Windows help, coverage and formatting checks pass.
MyABCD reaches its application event wait with zero calls to this native codec
during startup; the real-object matrices establish the migrated path's behavior.

The generic byte-storage UTF-8 path now handles ASCII prefixes and 512-unit
decode/encode chunks in C. It uses the shared byte-to-Unicode converter and
built-in UTF-8 codec, maintains the original TOC+0x9c4 converter cache, and
accumulates converted units and encoded bytes across chunks. Partial conversion
stops at the original chunk boundary; a zero capacity without borrowed storage
measures, while the borrowed-prefix early return still reports zero. Null used
outputs remain supported. System scratch storage is released on success and
callback errors, preserving the caller's MemErr around allocation/disposal.

Cold eight-bit/preference caches and changed codec descriptors keep the complete
original call before any prefix writes. Other generic encodings also retain
fallback. Unit checks cover eight source lengths and ten capacities in output
and measurement modes, exact chunk boundaries, ASCII prefixes, partial tokens,
cache sharing, allocation failure, errors in both chunks and untouched fallback
arguments/output. The real conversion matrices now compare 1,440 original/native
pairs per engine/import library, adding 514/515/516/768-byte mixed sources.
GDB independently records 34 native byte-path allocations and 18 full 512-unit
reads during the C-string matrix. Export coverage remains unchanged; the private
engine and the remaining generic converter paths are still incomplete.

After each byte-decoder callback, the loop re-reads the UTF-8 cache and checks
the live descriptor before choosing C or guest dispatch. Reentrant replacement
therefore survives subsequent encoding rather than being overwritten. Unit
checks cover the six raw callback arguments, shared-cache mutation and cleanup
when the replacement callback fails.

Validation passes WSL build/staging, archive checks, 73 standard probes and 60
diagnostic cases per engine, 16 targeted real-library scenarios, five CFM-68K
probes, generated-file, Windows help, coverage and both formatting checks.
MyABCD reaches its application event wait without exercising this byte path in
startup. The conversion matrices and callback fixtures establish its behavior;
the full native CarbonLib replacement remains incomplete.

The built-in MacRoman byte-to-Unicode callback at 0x4ce50 now runs in C. A shared
descriptor check verifies TOC+0x1fb4, its relocated code address and its TOC before
using the live 128-word table at TOC+0x52f8. ASCII bytes retain direct widening;
high bytes copy guest-order Unicode words. Changed descriptors and other codecs
keep guest callback dispatch, including existing failure behavior. The same
decoder serves character accessors, UTF-16 output, NonLossyASCII and byte-storage
UTF-8 conversion. No export is newly registered or counted as complete.

Archive checks cover all byte values across inline/external storage, Pascal
prefixes and four alignments, with output guards and no calls to the mock guest
converter on the built-in path. They also verify live table mutation, overlapping
table/output storage, changed descriptor code/TOC and a custom callback switching
to the built-in decoder between bytes. Other callback replacement and error
checks remain in place.

All 20 targeted real-library scenarios pass, covering character accessors,
C-string/Pascal conversion, bundle lookup and borrowed pointers in both engines
and import libraries. A separate GDB run records 257 executions of the C table
mapping in the character-reader matrix. WSL build/staging, archive tests, 73
standard and 60 diagnostic cases per engine, five CFM-68K probes, generated-file,
Windows help, coverage and formatting checks pass. MyABCD reaches its application
event wait with zero executions of this new mapping during startup. Coverage
remains 140 main exports plus one companion-only binding; the full native
replacement remains incomplete.

The remaining built-in single-byte decoders now run in C: ASCII at 0x4ccb0,
ISO Latin-1 at 0x4cd08, Windows Latin-1 at 0x4cfc8 and NeXT Latin at 0x4d164.
They share the verified descriptor/code/TOC selection with MacRoman. ASCII
refuses high bytes; ISO Latin-1 widens them unchanged. Windows maps 0x80..0x9f
through TOC+0x548c and widens other bytes; NeXT maps high bytes through
TOC+0x56f4. Windows/NeXT reject exactly U+FFFD table entries, which character
readers publish as U+FFFE. MacRoman still accepts U+FFFD if present in its table.
Other callbacks and changed built-in descriptors retain real guest dispatch.

Unit checks cover all 256 byte values for each decoder, guarded ranges, table
refusals and changed descriptor code/TOC. The real CFStrings fixture runs all
normal initializers, temporarily selects each actual built-in callback for a
byte object, compares native and original readers, then restores the original
converter before further construction/release. It does not substitute codec
implementations or bypass initialization. GDB records 257 native executions of
each new decoder and 514 MacRoman executions in this expanded matrix.

WSL build/staging, archive tests, 73 standard and 60 diagnostic cases per engine,
all 20 targeted real-library scenarios, five CFM-68K probes, generated-file,
Windows help, coverage and formatting checks pass. MyABCD reaches its application
event wait without entering these decoders during startup. Coverage remains 140
main exports plus one companion-only binding, with CoreFoundationLib 39/644 and
StdCLib 13/279. Other encodings, generic converter paths and the full native
CarbonLib replacement remain incomplete.

CFStringGetBytes (0x38ac8; main re-export 378) and CFStringGetFastestEncoding
(0x3cfa8; main re-export 344) now implement their public control flow in C.
GetBytes copies compatible byte ranges with the requested capacity, measures
without an output buffer and stores guest-order used-byte counts. It preserves
raw loss/external words and reorders all nine arguments for the incremental
encoding engine. Other profiles and invalid native ranges retain the complete
original public call. FastestEncoding reads the storage bit, returning 0x100 for
wide strings; byte strings share the current encoding and its original cold
initializer, invoked once rather than the compatibility test's possible retry.

Archive tests cover binary payloads, inline/external and Pascal layouts, four
alignments, ranges/capacities, null outputs and optional used counts, raw engine
arguments, all format flags, initializer errors/retries and five foreign-profile
dimensions. PPC fixtures exercise the ninth argument at caller SP+56, both
library names and unmodified full-width foreign results. Separate diagnostics
check failures in actual encoding initialization and delegated engine callbacks.

The Encoding real group compares 1,900 extraction cases and seven fastest-query
results per engine/import library on seven genuine immutable/mutable sources.
Original CFM descriptors remain the oracle; all buffers, guards and used counts
match. GDB separately records 200 native copy paths, 1,700 engine paths and seven
native fastest queries. Coverage is 142 main exports (89 code, 53 re-exports),
plus one companion-only binding; CoreFoundationLib is 41/644 and StdCLib 13/279.
MyABCD's 39 direct archive imports remain unchanged. General encoders, private
initializers, the private engine export and the full native replacement remain
incomplete; these new public APIs preserve their real dependencies.

WSL build/staging, archive tests, all 76 standard probes and 62 diagnostic cases
per engine, 24 targeted real-library scenarios, five CFM-68K probes, generated-file,
Windows help, coverage and formatting checks pass. MyABCD reaches its application
event wait with zero calls to either new export during startup; the differential
fixtures establish their behavior rather than that startup smoke check.


The next batch implements seven encoding-query APIs together in `encodings.c`:
CFStringGetMaximumSizeForEncoding (0x35638),
CFStringConvertEncodingToNSStringEncoding (0x439a0),
CFStringConvertNSStringEncodingToEncoding (0x43af8),
CFStringConvertEncodingToWindowsCodepage (0x43bb8),
CFStringConvertWindowsCodepageToEncoding (0x43c88),
CFStringIsEncodingAvailable (0x43300), and
CFStringGetMostCompatibleMacStringEncoding (0x43da4).
All are CoreFoundationLib implementations with main-fragment re-exports. The
native algorithms preserve unsigned wraparound and reference quirks rather than
normalizing the original tables: the Windows reverse map is not sorted, the
forward Windows table overlaps it, and the generic group-0x800 size branch tests
the masked group. Mapping tables and converter definitions remain live guest
storage.

Availability and Mac compatibility share native definition/cache orchestration.
Original private helpers at 0x4de58, 0x4dda0 and 0x43270 retain external definition
discovery, external adapter creation and weak TEC base lookup. Calls use aligned
transition vectors into the verified original image through the normal callback
bridge. No Apple instructions are compiled into the archive, no initializer is
skipped, and foreign images delegate their original public exports. The built-in
converter path selects the actual initialized definition and adapter records;
the native layer does not substitute codec implementations.

EncodingQueries compares 1,958 results per engine/import library against original
CFM descriptors. GDB records native paths for 1,194 maximum-size, 199 CF-to-NS,
199 NS-to-CF, 199 CF-to-Windows, 151 Windows-to-CF, nine availability and seven
compatible-Mac queries. Unit tests exercise all 65,536 Windows code pages,
wraparound lengths, live-table mutations, shared-cache creation/hits and changed
profiles. Offline PPC tests separately exercise real callback-vector dispatch to
synthetic original-code dependencies and controlled failure propagation at each
external helper/cache stage. This does not establish real external TEC behavior.

Coverage is 149 main exports (89 code, 60 re-exports), plus one companion-only
binding; CoreFoundationLib is 48/644 and StdCLib remains 13/279. The main fragment
still has 5,566 pending archive bindings. MyABCD retains 39 direct native imports;
these seven exports are not direct app imports. Its startup reaches the
application event wait with zero calls to these new APIs. Full native Carbon
compatibility and external codec dependencies remain incomplete.

The seven-API batch passes archive checks, all 79 standard probes and 65
controlled-failure cases in each engine, 28 targeted real-library scenarios,
five CFM-68K probes, generated-file, Windows help, coverage and formatting checks.
ROMAW and the archive were compiled and staged. One JIT diagnostic run hit
GDB's internal DWARF `fde->cie` assertion; the affected case and the remaining
six diagnostic cases all passed on a targeted retry. No runtime change was
needed for that debugger failure.


The bundle batch implements eight public APIs in `bundles.c`: GetTypeID
(0xd7e8), CopyBundleURL (0xdb20), GetInfoDictionary (0xdd2c),
GetValueForInfoDictionaryKey (0xdedc), GetIdentifier (0xdf74),
IsExecutableLoaded (0xf184), LoadExecutable (0xf18c), and GetPlugIn (0xf8d0).
Their full names have the CFBundle prefix. All are main-fragment re-exports from
CoreFoundationLib. LoadExecutable is a direct MyABCD import; coverage now reaches
40 direct native imports, with 273 strong and 132 weak imports pending.

The C implementation preserves live class IDs, raw loaded bytes, borrowed plugin
pointers, URL ownership, lazy metadata caching and localized-value precedence.
CopyBundleURL reloads its field after retaining, as the original does. The load
state machine includes the original private CFM helper at 0xfe0c: retained bundle
URL first, executable URL second, connection word at +40, failure kind 6 and
successful loaded byte at +36. The public entry also discovers the executable
before checking the loaded state, marks a missing executable kind 7, maintains the
real unload queue, and preserves logging for unsupported bundle kinds.

Original private info loading (0x13adc), CFM resource/fragment discovery (0xfd2c),
pending-unload processing (0xf4d0) and lock helpers (0x1a98c/0x1a990) still execute
through aligned callback vectors in the verified image. Localized dictionaries,
URL discovery, dictionary operations, ownership and logging remain shared with
their original implementations. No fake connections or replacement bundle
objects are introduced. Foreign profiles and 68K callers retain original public
fallback. The earlier symbol fixture's tiny synthetic loader is explicitly
foreign because it does not implement URL/CFM discovery.

Archive tests cover every loaded/plugin flag byte, reentrant URL changes, lazy
metadata cache hits/retries, localized precedence, unload-queue removal, guarded
pending cleanup, both CFM attempts, missing/unsupported kinds and every callback
failure in the two-attempt path. Offline PPC fixtures separately verify actual
transition-vector dispatch, live original records, both import libraries and
foreign fallback, with six dependency-failure cases.

Coverage is 157 main exports (89 code, 68 re-exports), plus one companion-only
binding; CoreFoundationLib is 56/644 and StdCLib remains 13/279. There are 5,558
main archive bindings pending. These counts describe bindings, not complete
Carbon compatibility or complete replacement of the retained dependencies.


BundleState constructs a genuine directory URL and CFBundle through the original
constructors and reads an actual Info.plist. Its 24 comparisons per engine/import
library require non-null type, URL, info dictionary, identifier and metadata
values. Missing-executable loads return false and preserve kind 7. A flat probe
has no main bundle in the original runtime; the fixture therefore creates real
filesystem bundles rather than inventing a private record.

`check-runtime.ps1 carbon-bundle-copy` adds an actual original-PPC executable inside
the bundle and compares native versus original load entries in both engines.
All four runs reach GetDiskFragment and the existing unsupported-private-copy
diagnostic. GDB records one native CFM-helper execution for the C entry and zero
for the direct original PPC entry. It stops at the diagnostic, so these four
checks do not claim normal process completion or successful private copying.
The native helper's successful connection writes are checked with original
synthetic dependencies; real CFM private copies remain unimplemented at the
ROMAW bridge and are still required for full bundle-loading compatibility.

The Windows archive and ROMAW compile/stage successfully. All 82 standard
probes and 71 controlled-failure cases pass in each engine, alongside 32 targeted
real-library success scenarios, the four real private-copy diagnostic comparisons,
five CFM-68K probes, generated-file checks and Windows help. The interpreter
standard run resumed from the bundle batch after fixing the foreign fixture to
return valid guest pointer sentinels. MyABCD reaches its application event wait;
its startup does not invoke the newly bound bundle loader. Coverage/formatting
checks remain separate from the unresolved full-compatibility requirement.


Private CFM copies now have a native implementation in the ROMAW bridge.
This removes the dependency failure recorded in the preceding bundle checkpoint,
without redirecting private requests to shared globals. Disk, named and memory
requests instantiate fresh data and initialization; private instances stay hidden
from shared/name lookup while using normal shared imports. GetMemFragment now
finishes its selected image directly, so repeated private loads cannot accidentally
reselect another instance by name. Find requests do not instantiate memory images,
and unsupported option words return paramErr. Native-only libraries cannot create
PEF copies and return unimpErr for that request.

PEF section storage uses shared ownership. Independently prepared images may share
identical code sections that have no section relocations; data is always prepared
fresh from the container. Both source-first and copy-first unload orders retain
surviving code. Relocated or different code remains separately allocated. Logical
FSSpec matching ignores bytes after the Pascal name, so unused record padding
does not prevent disk-instance sharing.

CloseConnection terminates a private instance once, removes it from shutdown's
termination order, invalidates its ID, balances its shared dependency references
and unloads its image. Its peers retain shared code and their own data. Failed
termination remains a controlled runtime error. Shared connection caching and the
broader CFM closure model are not claimed complete by this change.

The original-code probes cover all three APIs together: private data mutations,
shared dependency counters, code identity, initializer connection IDs, named
invisibility, an independent shared instance, find behavior, close/termination,
new instances after close and invalid options. Six diagnostics cover initializer
and terminator failure in each API. The real bundle-copy fixture now expects
successful completion, verifies a real private connection in the bundle, invokes
its code and initialized data, and unloads through the original bundle API.
The former expected rejection remains documented above as historical evidence.
Native Carbon export coverage stays 157 main bindings plus one companion-only
binding; enabling a retained dependency does not add archive exports.


CFM preparation also distinguishes ordinary file I/O failures from fatal loader
or initializer failures. CarbonLib first probes the bundle directory; returning
its real file error allows the original fallback to the executable URL. Startup
imports retain their existing diagnostic path. The disk probe covers this
recoverable failure before a successful private load and checks that no connection
was published by the failed attempt.

Startup-profile fixtures no longer use the obsolete private-copy rejection as a
retry trigger. A synthetic CarbonLib private initializer now reads a marker in a
normal shared import and checks the requested system version. The suite verifies
bounded retries, saved-profile reuse, no retry after the event loop starts, and
controlled initializer/main/termination failures without saving failed profiles.


Validation for this dependency batch: full CPU/PEF checks, focused PEF ownership
checks, Windows build/staging, all 85 standard probes and 77 diagnostic cases per
engine, 32 targeted real-library interoperability scenarios, four successful real
private-copy bundle scenarios, seven startup-profile modes, five CFM-68K probes,
generated-file and Windows help checks pass. One parallel interpreter speech run
hit its timing assertion; the speech case and remaining interpreter cases passed
on a serial retry, without speech changes. After discarding unpublished metadata
from failed private file probes, the five focused loader/INIT probes per engine,
six private diagnostics per engine and four real bundle-copy scenarios were rerun
successfully. No failed probe publishes a connection.

MyABCD still reaches its application event wait at the diagnostic 9.2/8m/16m
profile, with no bundle-loader calls during that startup. The genuine executable
fixtures, not this startup smoke check, establish the new private loading paths.
Real CarbonLib initialization and missing-API fallback remain enabled.


Follow-up closure ownership: each reference/private prepare now creates a distinct
closure handle, and CFragInitBlock receives that handle separately from the root
connection ID. Each closure snapshots its dependency set and owns one reference
per member. Find requests return borrowed connection IDs; closing those IDs or
the launch closure is rejected without clearing the caller's input. The launch
closure pins application imports, and internal component/resource closures pin
their imports until context teardown.

Closing an owning handle decrements the whole snapshot, terminates zero-reference
members in reverse actual initialization order and unloads their images. Shared
members survive while another closure needs them. Reopening restores data from
the original disk or memory container and assigns fresh connection IDs. Existing
private CFM context/closure inspection reports the active snapshots. The original
CFBundle discovery and missing-API fallback remain enabled. Unsupported private
CFM interfaces and native-only private data instantiation remain incomplete;
these changes do not establish complete CFM or Carbon compatibility.

Original-code probes now distinguish closure IDs from initializer/borrowed
connection IDs for named, disk and memory loads. A separate closure fixture checks
two independent roots sharing an unpinned dependency, repeated reference loads,
active closure enumeration, final-release termination order and fresh reloads.


Closure batch validation: Windows build/staging, 86 standard probes and 77
controlled diagnostic cases per engine, eight real bundle interoperability
scenarios, four private-copy bundle scenarios, five CFM-68K probes, seven
startup-profile modes, generated-file/help/coverage checks and repository/Powershell
format checks pass. The interpreter's parallel speech run hit the existing
240-tick timing assertion; speech and all remaining cases passed on a serial
resume. MyABCD again reaches its event wait at the diagnostic 9.2/8m/16m profile;
no bundle loads occur during this startup smoke check. Native archive coverage
remains 157 main exports plus one companion-only export.


## Bundle creation and lifecycle

The next seven exports are implemented in carbonlib/src/bundles.c:
CFBundleCreate, CFBundleGetLocalInfoDictionary, CFBundleUnloadExecutable,
CFBundleGetVersionNumber, CFBundleGetDevelopmentRegion and both plural symbol
lookup APIs. The native constructor replaces the worker at 0xd7f0, including
ownership, canonical URLs, directory checking, instance fields and registration
order, rather than forwarding the public entry to the entire worker. Filesystem
probing, runtime allocation, global cache registration and plug-in setup remain
explicit initialized dependencies. Lazy localized metadata orchestrates real URL,
resource-data and property-list services. Main-bundle development-region resource
fallback retains its existing private dependency.

The reference version parser accepts 1..10 UTF-16 characters, one or two major
BCD digits, single minor/patch digits and d/a/b stages with up to three build
digits bounded to 255. It has observable quirks: "1" yields 0x10008000, "1."
yields 0x01008000, and a trailing separator or stage letter can be accepted.
The native parser preserves these results. String metadata is cached as a genuine
CFNumber in the info dictionary; invalid object types remove the numeric cache.
Development-region filtering removes empty/non-string entries and preserves the
main bundle's resource/default fallback. Plural lookup delegates individual
lookups to the existing native symbol implementation and preserves guest pointer
width/order, misses, wrong classes and output guards.

Real comparisons use separate dictionaries so native cache mutation cannot hide
a parser difference from the original getter. The genuine bundle constructor,
localized InfoPlist.strings and executable fixtures exercise actual object/file/
CFM dependencies. Three synthetic profiles check Carbon/Core imports and foreign
fallback; three controlled-failure cases check constructor, resource and unload
callback errors. Archive tests check dependency argument words, header/body
initialization, ownership, directory/allocation failures, main-bundle fallback,
foreign delegation and callback-buffer extents.

The native constructor's allocator is a raw guest word and its URL is a translated
pointer; signatures encode arguments in reverse order. The genuine-library test
caught and corrected an initial reversal that the cache-hit-only synthetic case
could not expose. Callback buffers are aligned to fit ROMAW's host-alias windows.

Unloading requires a distinction between bundle flags and CFM mappings. At
0xff18..0xff20 the local original worker passes its connection word directly to
InterfaceLib:CloseConnection; the
[public CFM declaration](https://dev.os9.ca/techpubs/mac/PPCSoftware/PPCSoftware-66.html)
takes a pointer to the connection ID. The native implementation reproduces the
reference call and ignored return, then clears the bundle fields. A GDB inspection
of three original/native load/unload pairs found all six mappings still active.
The fixture compares actual CountSymbols results and explicitly releases retained
mappings through CloseConnection(&id). These findings do not establish actual
unloading by CFBundleUnloadExecutable or complete CFM compatibility.

## Additional reference supplied by the user

[Retro68](https://github.com/autc04/Retro68/) supplies useful compiler, PEF and
symbol-reference material. Its
[import-library README](https://github.com/autc04/Retro68/blob/master/ImportLibraries/README.txt)
identifies the archives as generated export names from Universal Interfaces 3.4.1.
[MakeImport.cc](https://github.com/autc04/Retro68/blob/master/PEFTools/MakeImport.cc)
shows the PEF symbol-class/import glue construction. Its bundled
[PEFBinaryFormat.h](https://github.com/autc04/Retro68/blob/master/PEFTools/PEFBinaryFormat.h)
identifies itself as generated from ROMAW source. The project README states
that bundled Multiversal Interfaces omit Carbon; these files provide additional
reference evidence, not a native Carbon implementation or new coverage claims.
CarbonLib 1.4's local exports/disassembly remain the version-specific behavior
reference. No Retro68 code or import archive is incorporated in this batch.


Bundle lifecycle checkpoint: archive checks, Windows build/staging, 89 standard
probes and 80 controlled diagnostic cases per engine pass. After the final
callback-cache reload and alias-buffer alignment refinements, the archive checks,
eight focused bundle probes per engine, three lifecycle diagnostics per engine,
12 real bundle interoperability scenarios and four private-copy bundle scenarios
were rerun successfully. Five CFM-68K probes, seven startup-profile modes,
generated-file/help/coverage checks, repository formatting and PowerShell checks
also pass. MyABCD still reaches its event wait at 9.2/8m/16m; no native bundle
creation or load occurs during that startup, so it does not validate the new API
paths. Genuine bundle fixtures provide that evidence.

Coverage is now 164/5,715 main exports plus one companion-only export;
CoreFoundationLib is 63/644 and StdCLib remains 13/279. MyABCD has 41 direct
archive imports, with 272 strong and 132 weak CarbonLib imports still outside
that archive coverage. The main reference has 5,551 remaining bindings. Native
control flow coverage does not imply replacement of the original private workers
or complete behavioral compatibility.


## URL components and path append

Twelve more main exports have native C bodies in `carbonlib/src/urls.c`:
CFURLGetTypeID, CFURLCanBeDecomposed, CFURLGetString, CFURLGetBaseURL,
CFURLHasDirectoryPath, CFURLCopyScheme, CFURLCopyNetLocation, CFURLCopyPath,
CFURLCopyStrictPath, CFURLCopyResourceSpecifier, CFURLCopyLastPathComponent and
CFURLCreateCopyAppendingPathComponent. The last is a direct MyABCD import.
Their CoreFoundationLib 1.4 code offsets, in that order, are 0x61d8, 0x83e0,
0x8448, 0x84a8, 0x89d4, 0x85c0, 0x863c, 0x8848, 0x88b4, 0x8a6c,
0xaba8 and 0xae24. Profile checks require the known code/TOC extents and public
entry offset; other images delegate to the original public entry.

Native control flow reads guest-endian flags and packed ranges, preserves cached
parse mutations and base inheritance, returns owned component strings, writes
strict-path flags and implements the original last-range worker at 0xa9c0.
Append covers file-style formatting and generic escaped insertion, retaining the
original Windows doubled-backslash search constant. The parser at 0x61e0 and
file-to-URL conversion at 0x98c8 remain real private dependencies; string factories,
search/formatting, percent escaping/decoding, allocation and URL constructors also
remain real calls. The four append arguments retain reversed bridge signature
encoding: raw allocator, translated URL/component pointers and Boolean word.

Archive tests cover packed ranges, inherited fields, parse/cache updates, ownership,
all file styles, formatting combinations, escaped/root/empty components, guarded
strict outputs, allocation/callback failures and foreign-profile argument words.
Three synthetic PPC profiles establish native selection and foreign fallback;
three diagnostic modes exercise parser, converter and insertion errors.

The genuine URL fixture compares separate fresh native and original objects for
19 source forms: generic and opaque URLs, relative bases, percent escapes and
POSIX/HFS/Windows paths. Each scenario compares 190 getter pairs and 380 append
pairs (10 components, both directory flags), plus type/guard checks. Components
include reserved separators, spaces, percent text, empty strings and Unicode.
Each file style requires a successful append result. Empty URI, IPv6-literal URI
and malformed-percent URI constructors fail in the installed original 1.4 library;
they are explicit rejection checks, rather than being silently omitted or treated
as valid getter cases. CarbonLib and CoreFoundationLib scenarios pass under both
PPC engines. These results are version-specific and do not establish general URL
or full Carbon compatibility.

URL checkpoint validation: archive tests, Windows build/staging, 92 standard probes
and 83 diagnostics per engine pass. All four genuine URL scenarios, 12 related
bundle scenarios, four private-copy bundle scenarios, five CFM-68K probes, seven
startup-profile modes, generated-file/help/coverage and formatting checks pass.
MyABCD reaches its event wait at 9.2/8m/16m, with zero native URL append calls during
startup; no additional interactive workflow is claimed.

Coverage is now 176/5,715 main exports plus one companion-only export (177 total),
CoreFoundationLib 75/644 and StdCLib 13/279. MyABCD has 42/445 direct archive
imports, leaving 271 strong and 132 weak imports outside archive coverage.
There are 5,539 main bindings remaining; native entry coverage still includes
explicit real dependencies and must not be reported as full native compatibility.


## Decoded URL components, representations and FSRef control flow

The next group adds CFURLCopyHostName (0x8be0), CFURLCopyUserName (0x8de8),
CFURLCopyPassword (0x8ebc), CFURLCopyParameterString (0x901c),
CFURLCopyQueryString (0x9124), CFURLCopyFragment (0x91dc),
CFURLGetPortNumber (0x8cc0), CFURLCopyPathExtension (0xad14),
CFURLCreateFromFileSystemRepresentation (0xa880),
CFURLCreateFromFileSystemRepresentationRelativeToBase (0xa928),
CFURLCreateFromFSRef (0xc58c) and CFURLGetFSRef (0xc614).
The two reserved-pointer accessors at 0x9258/0x9260 are CoreFoundationLib-only
exports and are not counted toward main CarbonLib coverage.

Native code preserves authority inheritance barriers, separate parameter/query
inheritance masks, fragment non-inheritance, caller-selected percent escapes and
substring ownership. Inherited parameter/query results use the original caller's
allocator; authority recursion uses the base object's allocator. Port parsing uses
native decimal control flow equivalent to the 32-bit mode of 0x425e0: historical
whitespace, optional sign with following whitespace, full consumption and signed
32-bit saturation. It retains real character access and releases the component.
Path extension uses the native last component and backwards dot search, retaining
empty extensions and releasing the component. Reserved words are borrowed and
written in guest byte order without changing ownership.

Byte representations use the actual system encoding, HFS path style, supplied
byte extent/directory word and optional base. FSRef creation performs the real
capability check (0x4af8) and catalog lookup (0x4d70), then translates the
0xc50c worker's string/create/release control flow into C. The FSRef-to-path
string worker at 0x1832c remains real. FSRef extraction translates the 0xc3b4
worker's capability, absolute-copy, filesystem-path and release control flow;
its final path-to-FSRef worker at 0xc178 remains real. Like 1.4, filesystem-path
copying uses the original URL after obtaining an absolute copy. No private
filesystem state is fabricated and missing dependencies retain diagnostic errors.

Archive fixtures verify inheritance/allocator identities, ownership, percent-decoder
NULL results, numeric bounds/overflow and historical Unicode whitespace, reserved
word byte order, dot/root/empty extensions, explicit byte lengths with embedded NUL,
constructor allocation/final-result failures and supported FSRef success, errors,
output mutation and cleanup. Three synthetic PPC profiles exercise all entries and
foreign fallback; two diagnostic modes cover capability and byte-constructor callback
failures. Foreign Boolean fallback uses an explicit 0x123456ab word and checks
the 0xab result.

The genuine matrix uses 33 URL source forms, with 462 component/port pairs and
20 byte-constructor pairs per scenario, plus reserved identity and FSRef gate/output
checks. Both libraries pass in both PPC engines. ROMAW's real filesystem
capability gate currently rejects FSRef operations. Genuine tests therefore prove
that gate and unchanged output records, not actual supported FSRef filesystem
operation; the archive tests provide the supported-worker protocol evidence.
The preceding URL matrix has 19 forms (190 getter and 380 append pairs); the
previous prose count of 20 was corrected to match the fixture.

Coverage is 188/5,715 main exports plus three companion-only exports (191 total),
CoreFoundationLib 89/644 and StdCLib 13/279. MyABCD has 43/445 direct archive
imports, with 270 strong and 132 weak imports remaining outside archive coverage.
There are 5,527 main bindings left, plus the explicit real dependencies of migrated
entries. MyABCD reaches its event wait at 9.2/8m/16m with no native FSRef creation
calls during startup. No additional interactive workflow is claimed.

URL component checkpoint validation: archive tests, Windows build/staging, all
95 standard probes and 85 controlled diagnostics per engine pass. Four genuine
URL component scenarios, four preceding URL scenarios and four bundle lifecycle
scenarios pass, along with generated-file, help, coverage, repository formatting
and PowerShell checks. The final archive rerun also checks distinct inheritance
allocator identities and cleanup when filesystem constructors return NULL.


## URL creation, shared initialization and path mutation

Eight more main entries have C bodies: CFURLCreateWithString (0x7ac0),
CFURLCreateWithBytes (0x78bc), CFURLCreateData (0x79e4),
CFURLCreateWithFileSystemPath (0x9e10),
CFURLCreateWithFileSystemPathRelativeToBase (0x9f90),
CFURLCreateCopyDeletingLastPathComponent (0xb1e0),
CFURLCreateCopyAppendingPathExtension (0xb704) and
CFURLCreateCopyDeletingPathExtension (0xb928). _CFURLAlloc (0x6ac8) and
_CFURLInitWithString (0x7084) add two companion-only entries. The private 0x6b18
file-record initialization control flow is also C and is shared by these bodies.

Allocation requests 20 extra bytes from the real runtime using the live URL type
ID, preserving the eight-byte header and clearing the body. Initialization copies
the string with the object's allocator, ORs the style bits and owns an absolute
copy of the selected base. String initialization selects an absolute URL when the
first colon precedes the last fragment marker (or no marker exists), matching the
reference; private initialization does not perform public constructor validation.
String/byte construction retains the real validator at 0x6bac. Non-UTF-8 URL byte
construction re-encodes through 0x755c, whose encoding/escape control remains real.
External representation retains UTF-8 directly or uses that worker with the
original whitespace table before creating actual CFData. All normal temporary
ownership and explicit callback errors are preserved.

Filesystem constructors determine absolute paths for all three styles, adjust one
trailing separator, preserve root/doubled separators as directories and populate
the POSIX safe-string flag with the original ASCII set. Current-directory discovery
at 0x9d84 and absolute-base copying remain real. The installed Windows-style code
checks an apostrophe at index two after the drive colon; C retains this quirk.
Native filesystem allocation failure returns NULL before record initialization;
1.4's disassembly lacks that NULL check. Archive fixtures establish this checked
failure path; genuine comparisons do not force the original allocation crash.

Path mutations use the native packed last-component helper. Extension insertion
can convert file-style URLs to generic URLs, applies percent escaping for generic
extensions, preserves directory/base behavior and releases temporary strings.
Deletion retains the original URL when no extension exists. Parent deletion handles
root, dot and dot-dot paths, preserving the original short-path rewrites rather
than applying a new normalization algorithm. For example, `a/./` becomes
`a/./../` in the corresponding reference path. String editing and escaping remain
explicit dependencies. Existing older C groups that deliberately call saved
original constructor descriptors retain those paths; migrating every internal
callback is separate from registering the new native entries.

Archive tests check body/header guards, allocation failure, base/fragment rules,
public validation, re-encoding argument words, whitespace control, explicit byte
extents, path styles/directory flags/safe characters, current-directory ownership,
normal failure cleanup, mutations and foreign argument marshalling. Three synthetic
PPC profiles cover main/companion binding and foreign fallback; three diagnostics
exercise runtime allocation, validator and re-encoding callback errors.

The genuine fixture obtains original descriptors through CFM and uses them to
construct mutation sources and shared bases, preventing a native constructor defect
from affecting both sides. It compares raw flags and stored strings before CFEqual
can parse/cache the URLs, as well as final semantic equality and owned results.
Per scenario it covers 33 source forms with 363 mutation pairs, seven initializer
pairs, seven string-constructor pairs, 32 filesystem-constructor pairs, nine byte
constructor pairs and 114 external representation pairs (532 pairs total), plus
zeroed body and ownership checks. UTF-8, MacRoman and Windows Latin-1 are included;
nonempty ASCII byte construction and external representations must succeed in
both native and original calls. Both libraries pass in both PPC engines.

Coverage is 196/5,715 main entries plus five companion-only entries (201 total),
CoreFoundationLib 99/644 and StdCLib 13/279. MyABCD remains 43/445 direct archive
imports with 270 strong and 132 weak imports outside that coverage. There are
5,519 remaining main bindings and real dependencies within migrated groups.
MyABCD reaches its event wait at 9.2/8m/16m with zero native string URL constructor
calls on startup; no additional interactive workflow is established.

URL creation checkpoint validation: archive tests and Windows build/staging pass,
along with 98 standard probes and 88 controlled diagnostics per engine. The final
four creation scenarios include raw representation and positive byte/data checks;
twelve related URL/component/bundle lifecycle scenarios also pass. Generated-file,
help, coverage, full repository formatting and PowerShell checks pass. The final
archive rerun follows test-source formatting; production code was unchanged after
the successful build and genuine comparisons.


## URL resolution, filesystem representations and native validation

Three main entries are now C: CFURLCopyAbsoluteURL (0x7f6c),
CFURLCopyFileSystemPath (0xa4bc) and CFURLGetFileSystemRepresentation (0xa78c).
Native shared control flow includes the 0xa50c filesystem worker, 0x95c4 POSIX
conversion, 0x9b20 same-style absolute helper and the 0x6bac URI validator.
The current-directory filesystem constructor now shares its existing C worker
with resolution. No export count is added for a private helper or for replacing
the validator inside an already-registered entry.

Absolute copying retains a URL without a base, handles matching file styles,
file-to-generic conversion and the POSIX fast path, then reconstructs scheme,
authority, path and inherited/resource/query/fragment components as the reference
does. Explicit decoded query/fragment components still invoke formatting when
decoding returns NULL, preserving those reference branches. An absent required
merged path cannot publish a truncated successful URL. Parser/file conversion
at 0x61e0/0x98c8, generic path resolution at 0x7d68 and merging at 0x9be8 remain
real. Native filesystem conversion handles matching-style retention, POSIX decoding,
directory separator trimming, optional base conversion/merge and ownership;
HFS/Windows workers at 0xbf64/0x96ac remain real. Saved-descriptor calls in older
groups also remain until callback routing is separately migrated.

The native validator removes the 0x6bac callback from the native string/byte
constructors and their mutation paths. The original increments its cursor before
rejecting an ordinary character or a repeated fragment marker; its final cursor
comparison accepts that character if it was last. A second '#' is therefore
accepted at the very end but rejected earlier. Invalid/incomplete percent triplets
explicitly force failure and do not inherit that behavior. C retains these rules,
including UTF-16 units outside ASCII, rather than imposing a different URI grammar.
Public empty-string construction remains rejected before validation.

Filesystem byte conversion uses HFS style, the real system encoding, external
representation mode, explicit byte capacity and big-endian used-byte output in
its nine-word encoding callback. It reserves a terminator and preserves partial
writes on ordinary conversion failure. Native NULL/nonpositive-capacity handling
returns failure. For a nonempty path and capacity one it now releases the path
and returns failure before encoding: the worker treats encoder capacity zero as
a sizing query, while 1.4's wrapper compares the returned character count and
writes a terminator at the reported byte count without checking capacity.

The first genuine comparison exposed this difference on
`file:///D/Source/a%20b/`: GDB recorded 12 characters converted and 12 bytes
reported for capacity one. The original wrapper returned success and placed its
terminator at byte 12. The dedicated oracle observation supplies 128 actual
backing bytes, confirms that declared-capacity violation and keeps the final
backing guard intact; the native call must fail with all 128 bytes untouched.
Capacities 8 and 128 are compared normally, with partial bytes and guards checked.
The final used-byte bounds check also rejects an oversized dependency report.
This intentional checked behavior is not described as identical to the original
wrapper's overflow. Foreign-image fallback retains the original entry.

Archive tests cover 131,072 BMP position cases plus percent/fragment boundaries,
retention, same-style and POSIX-fast resolution, scheme/authority reconstruction,
missing merge output, filesystem conversion/trim/ownership, partial writes,
capacity guards, malformed dependency counts and foreign raw argument words.
Three synthetic profiles test native/companion routing and foreign fallback;
their private validator deliberately rejects valid text, proving that native
construction no longer calls it. Existing creation diagnostics now fail the
character-read dependency during native validation. Three additional diagnostics
cover encoder, merge and HFS conversion errors without hiding callback failures.

The genuine matrix builds sources and bases using original CFM descriptors.
It covers 35 URL forms, including POSIX/HFS/Windows same-style bases, cross-style
and generic resolution, the POSIX fast path, query/fragment inheritance and invalid
UTF-8 percent-decoding results. Per scenario, 280 normal result pairs compare
absolute raw flags/stored strings before semantic equality, filesystem strings
and byte conversion results/partial output; 539 additional native/original
constructor pairs check validator decisions and accepted raw representations.
The validator inputs include every byte-sized unit plus selected higher/surrogate
units at interior/final positions and 15 percent/fragment cases. The capacity-one
observation is a separate intentional difference. Both libraries pass under both
PPC engines; these are version-specific fixtures, not complete URL compatibility.

Coverage is 199/5,715 main entries plus five companion-only entries (204 total),
CoreFoundationLib 102/644 and StdCLib 13/279. MyABCD remains 43/445 direct archive
imports, with 270 strong and 132 weak imports outside archive coverage. There are
5,516 main bindings remaining, along with real dependencies of migrated entries.
MyABCD reaches its event wait at 9.2/8m/16m with no native absolute resolution on
startup; no additional interactive workflow is claimed.

URL resolution checkpoint validation: archive checks and Windows build/staging
pass, as do 101 standard probes and 91 controlled diagnostics per engine after
the capacity-one fix. All four expanded resolution scenarios and sixteen related
creation/component/accessor/bundle lifecycle scenarios pass. The final strengthened
three ABI profiles were rerun under both engines, requiring exact native runtime
allocation/retained-string results and distinct foreign markers. Generated-file,
help, coverage, repository formatting and PowerShell checks pass; the final changed
generator was also checked with Black and the source-header validator. MyABCD's
GDB smoke reaches its application event wait without a PPC error.

## Native string operations

The next group moves eight public entry bodies into `cfstrings.c`:

| Export | CoreFoundationLib 1.4 offset |
| --- | --- |
| CFStringCompare | 0x3a880 |
| CFStringCompareWithOptions | 0x39fa8 |
| __CFStringMakeConstantString | 0x38110 |
| CFStringCreateCopy | 0x37d08 |
| CFStringHasPrefix | 0x3b5ac |
| CFStringHasSuffix | 0x3b61c |
| CFStringFind | 0x3b4e0 |
| CFStringGetIntValue | 0x3cfe0 |

The exact-profile check retains the established 0x5dee0 code / 0x72a8 TOC
extents. A different profile calls its original descriptor. The eight entries
remain PPC-only, preserving the existing 68K fallback. `CFStringFind` explicitly
marshals its caller-provided result pointer in r3 before the ordinary arguments;
the generic bridge does not pretend its eight-byte CFRange is a scalar return.

Comparison reads 32-unit chunks and preserves the reference's U+FFFF sentinel,
ASCII folding and first-unit Unicode folding through worker 0x49358. Localized
options invoke worker 0x398b0 with the actual locale/default state. The source
includes bounded one-sided decomposition control flow, but this is **not** a
working canonical-equivalence implementation for this CarbonLib binary:
0x4617c calls 0x492e0 with mapping kind 3, which dispatches to 0x49a80. That
entry is only `blr`. It returns its input character without filling the caller's
ten-unit output, so the original comparison can read uninitialized/out-of-bounds
stack memory using a count such as 233 for U+00E9. The C body refuses counts
above ten through a controlled callback diagnostic. Genuine tests deliberately
avoid that unsafe original path; synthetic fixtures and archive tests verify
the oversized-result rejection. They do exercise ASCII nonliteral comparison,
literal/case-folded comparisons and localized options on Unicode strings.

Constant strings use the real dictionary in TOC 0x98c, copying the actual key
callback record from 0x3a98 and using the value callbacks at 0x33f0. Both lookups,
canonical borrowed ownership, factory retain/release and the Unicode-key copy
are preserved. The original lock/unlock functions are empty; no thread-safety
claim is added. Non-ASCII literals retain the original escaped warning through
the real message table, formatting and CFLog; genuine probes exercise it. Failed
dictionary/string/key allocation returns NULL safely rather than continuing with
an absent object. Runtime objects and dictionary operations remain real workers.

Copying retains an immutable string when its allocator matches, otherwise
preserves inline/external, Pascal/explicit length and eight-bit/UTF16 storage
selection before invoking the original ten-word immutable funnel. Prefix/suffix
construct the original anchored/backward flags and delegate the existing search
worker. Find copies the resulting range, or writes (-1,0) on a miss. Integer
conversion implements historical Unicode whitespace, optional sign (including
post-sign whitespace), decimal prefixes and signed 32-bit saturation; it does
not require full-string consumption.

Archive tests cover callback failure at each step, output alignment and guards,
borrowed/retained ownership, cached external lengths, allocator/copy selection,
constant cache hits/races/allocation failures/warnings and foreign-profile
argument forwarding. Original PPC profiles distinguish the C comparison/query
results from foreign markers and check the hidden result ABI. Real-object
fixtures obtain un-intercepted descriptors through CFM and compare raw scalar,
range/guard and canonical identity results in both namespaces and engines.

Coverage is 207/5,715 main entries plus five companion-only entries (212 total),
CoreFoundationLib 110/644 and StdCLib 13/279. MyABCD has 45/445 direct archive
imports, with 268 strong and 132 weak imports outside archive coverage. There
are 5,508 main bindings remaining, plus the real dependencies of migrated APIs.
These counts describe registered native entry bodies, not independent or full
Carbon compatibility.

String-group checkpoint validation: archive checks and Windows build/staging
pass. All 104 standard probes and 92 controlled diagnostics pass in each engine,
including exact native/foreign string profiles and the oversized-result guard.
Twenty real-object scenarios pass across StringOps, CFStrings, StringPointers,
CFCString and URLCreation. Each StringOps scenario contains 967 scalar, range,
guard, copy and constant-identity assertions. Generated-file, Windows help,
coverage, repository formatting and PowerShell checks pass. MyABCD reaches its
application event wait at 9.2/8m/16m without a PPC error: three native constant
calls and zero native compare calls were observed. This is startup validation;
no further interactive workflow or canonical-equivalence support is claimed.

## Classic accessor batch: 185 entry bodies

The implementation batch size is now at least 100 APIs before group validation
and commit. This batch adds 137 exported LMGet/LMSet bodies in `lowmem.c` and 48
classic record bodies in `records.c`: 29 List, ten Control, six Menu and three
Dialog accessors. These are direct C implementations with no Apple worker calls.
They preserve the main CarbonLib disassembly's public code offsets; registry
entries are PPC-only so existing 68K imports keep their PEF fallback. Missing
APIs still resolve through the real library. The ABI reference facts for every
entry are in `carbonlib/tests/classic_access_cases.json`.

The low-memory family spans reference code 0xa7230..0xa77ac. Scalar loads preserve
`lha` sign extension and unsigned `lbz` values; stores narrow only the written
byte/word. Pointer-valued globals are raw guest words, not translated host
pointers. The indexed level-two dispatch table sign-extends its 16-bit index
before the wrapping 32-bit address calculation. Address-returning accessors expose
the real inline globals; fixed-size setters copy precisely 32, 16 or eight bytes
according to the reference. RGB accessors retain the ordered four-byte/two-byte
operations, including overlapping source and destination. Unaligned globals use
`memcpy`, avoiding host C alignment assumptions.

List accessors follow the current guest master pointer on each invocation and
preserve signed list/selection flag bytes, packed cell/Point records and opaque
callback/data words. Only setters with a null-handle early return in the original
gain that behavior. Control popup accessors follow both handle levels and retain
the original absent-data checks. Menu title retrieval copies its actual Pascal
extent, preserves the 0x0501 sentinel rule and returns the caller's guest output
pointer. Dialog focus converts the zero-based edit field to a signed 16-bit item
number, including the 0x7fff wrap and negative/no-dialog results. GetMenuID's
private metadata path and unsupported control properties remain outside this
batch rather than being replaced with direct-field approximations.

The raw PPC word signatures deliberately distinguish a guest pointer value from
a host pointer. Actual memory accesses go through `ROMlib_carbon_guest_memory`,
which checks the complete span with the existing PPC memory mapper, including
host aliases and low-memory trap-vector views. Invalid pointers, overflowed spans
and invalid indexed addresses retain a controlled launch diagnostic. Once an
access fails, later accesses on the failed launch stop. Ordered record/RGB copies
validate each operation separately so earlier valid writes remain visible if a
later operation faults. The native fixture checks this partial-write behavior.

The isolated archive fixture covers all 185 entries over twelve scalar patterns,
live/relocated/guest-zero master pointers, high-bit opaque pointers, signed
fields, exact write extents, nil checks, overlapping copies and malformed spans.
Independent static assertions tie the layouts to ROMAW's classic List,
Control, Menu and Dialog structures. Its expected bytes are constructed without
using the implementation's endian helpers, and the complete guest-memory image
is checked for unintended writes. Test tables are generated under `obj/carbonlib`
from the maintained JSON facts, with dependencies on both data and generator.

The standalone PPC fixture resolves all 185 native entries, checks live getters
against direct PPC loads, exercises setters with current values and validates
packed application-owned records. It confirms original PEF initialization via a
fallback token. The optional `ClassicAccess` real-library group resolves original
CarbonLib descriptors with CFM and compares every API in both engines, saving
native register/output bytes before calling the original. It writes only current
values to live low-memory globals; malformed values stay in isolated memory.

The genuine probe keeps a real CarbonLib code export in the launch dependency
graph and imports CFM through CarbonLib. Direct InterfaceLib application imports
were rejected by CarbonLib's initializer; using its Carbon exports permits normal
initialization and comparison. No initializer is skipped or forced to succeed.

Coverage at the classic-accessor checkpoint was 392/5,715 main exports plus five companion-only exports (397 total),
including 274 main code exports and 118 re-exports. CoreFoundation remains
110/644 and StdCLib 13/279. There are 5,323 main bindings remaining, plus the
dependencies and behavior limits of earlier native groups. MyABCD's direct
archive-import coverage remains 45/445: 268 strong and 132 weak imports are
outside the archive. This shared classic-API expansion does not establish an
additional MyABCD workflow or complete Carbon compatibility.

Classic-accessor checkpoint validation: native archive checks and Windows
build/staging pass. All 105 standard probes and 97 controlled diagnostics pass
per engine, including invalid handle/master/output/copy/index cases. The two
genuine 185-API scenarios pass 1,659 assertions each, explicitly checking original
PPC descriptors rather than native-service oracle entries. Eight related genuine
string-operation/URL-creation scenarios also pass. Generated-file checks, new
test-table data/generator invalidation checks, Windows help, coverage auditing,
repository formatting and PowerShell checks pass. MyABCD reaches its application
event wait at 9.2/8m/16m without a PPC error (three native constant calls, zero
native compare calls). This remains startup validation, not a new interactive
application milestone.

## CoreFoundation collection/value cohort: 102 entry bodies

The next batch migrates public control flow for 16 Array, 11 Data, six Set,
16 CharacterSet, seven Number, two Boolean and 40 String entries, plus CFEqual,
CFHash, CFCopyDescription and CFDateGetTypeID. The manifest under
`carbonlib/plans/cf-values-batch.json` records main-fragment import
ordinals, companion code offsets, explicit raw PPC signatures, native functions
and the corresponding Universal Interfaces 3.4.2 declaration headers. The
argument-word audit covers every declaration, expanding CFRange into two words.

All 102 entries are PPC-only and require the initialized real CoreFoundation
runtime. Unknown code/data profiles invoke the saved original descriptor and
preserve its full return register. Missing API names retain normal PEF lookup.
The backend's bounded guest-memory view checks complete extents and preserves
the first diagnostic. A separate failure reporter lets the archive reject bad
capacity/range/worker results without fabricating success or private state.

Native Data code constructs immutable/fixed/variable instances, retains no-copy
deallocators and implements replacement, append/delete and length changes.
Variable growth preserves the reference's strict next-power-of-two behavior;
failed reallocation preserves old storage. Fixed-capacity writes reject ranges
outside capacity before writing. Array code classifies null/type/custom callback
tables, retains new values before publishing them, releases replaced values,
dispatches equality/comparator callbacks and handles inline/deque/CFStorage
transitions. The sort adapter is loaded from the live TOC slot, rather than
passing the slot's address as a descriptor. Generic CFStorage, deque rebalance,
range release and sort workers remain real dependencies.

Set queries use native bucket enumeration, count and callback/hash control flow
with the original generic lookup worker. Generic equality/hash/description
dispatch reads the live class table and singleton/type getters read live globals.
No class registrations or singleton pointers are synthesized. Character-set
constructors and mutations share the registered original class; native mutations
materialize compatible 8192-byte BMP bitmaps. Membership understands predefined,
range, sorted-list, bitmap and compact representations. Unicode predefined tables
and conversion of original compact/predefined objects to bitmaps remain real
workers. Empty mutable bitmap inversion allocates storage before complementing
it; its safe oracle is an original full-range constructor, since the original
empty-inversion path dereferences null bitmap storage.

Number constructors preserve the 14 public type aliases and canonical storage
mapping in the live image. GetValue handles optional output, canonical exactness
and the reference's same-storage narrowing result; actual conversion still uses
the real PowerPC worker. Compare retains full signed 64-bit integer precision
unless a floating operand selects double comparison. CarbonLib 1.4's separate
greater/less branches return equality for unordered values and both signs of
zero. The newer SDK's documented NaN ordering differs; the versioned native
implementation follows the directly compared 1.4 behavior.

String construction maps the public parameter/ownership combinations into the
original immutable allocation/decoding funnel. Mutable allocation and external
buffer records are initialized in C. Resizing, replacement and encoded append
workers remain real dependencies. C code controls substring/copy, append/insert/
delete/replace, search/result arrays, splitting/joining, line bounds, padding,
trimming, encoding caches and case conversion. Search uses the native comparison
entry with versioned case/nonliteral flags. Unicode mapping/classification,
converter registries and TextCommon/TEC imports remain real dependencies.

The existing bounded nonlocalized decomposition failure applies to nonliteral
search as well as comparison. The original case loop can write an expansion
past its last UTF16 character while retaining the old string length. Native code
bounds the worker's temporary result and diagnoses an expansion exceeding the
remaining string before the copy; general expanding case conversion remains
unfinished. Callback/allocator failure coverage and dependence on real workers
also limit compatibility claims. These are migrated public entry bodies, not
102 independent replacements for every worker or input behavior.

The isolated archive fixture checks all 102 foreign-profile signatures over
successful, failed-callback and missing-reference paths, then exercises native
live globals, fixed Data overlap/zero-fill/guards and Array search/swap/removal.
Portable PPC fixtures check every foreign-profile entry in both namespaces and
exercise invalid output extents, fixed capacity and case-mapping extent errors.
The optional `Values` real group resolves original descriptors with CFM, checks
their symbol class and opcode, and executes every new native entry in both
namespaces and engines. Coverage includes real type callbacks, deque/storage
growth/shrinkage, the number conversion matrix, predefined and mutable character
sets, Unicode strings, external buffers, search/line ranges and encoding aliases.
No initializer is skipped and generated fixtures contain original test code.

Current binding coverage is 494/5,715 main exports plus five companion-only
exports (499 total): 274 main code exports and 220 re-exports. CoreFoundation is
212/644 and StdCLib remains 13/279. The remaining main count is 5,221, alongside
retained private workers and behavior limits. MyABCD remains 45/445 direct archive
imports, with 268 strong and 132 weak imports outside the archive. Shared-library
coverage alone does not establish an additional MyABCD interactive workflow.

This batch is committed as a paused checkpoint. Before the pause, the archive
unit checks passed; each engine passed 107 standard probes and 100 controlled
diagnostics, and all four Values namespace/engine scenarios passed 2,964
assertions each. Those results precede the repository directory regrouping.
Final repository-wide formatting checks, rebuild/staging, generated-file checks
and a new MyABCD startup smoke check remain pending.
