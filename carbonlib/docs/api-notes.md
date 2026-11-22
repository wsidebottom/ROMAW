# Carbon implementation contracts and validation notes

This reference records API groups as they were implemented, including ABI,
ownership, reference-runtime dependencies and focused validation. Later groups
can extend earlier fallback boundaries. For current build and coverage reporting,
use [the package guide](../README.md); cumulative export totals are intentionally
not repeated here. The chronological disassembly/migration evidence remains in
[the Carbon audit](../../synppc/docs/carbon-native.md).

Paths such as `src/` and `plans/` are relative to `carbonlib/`; `synppc/`,
`carbonlib/tests/` and `tools/` commands are relative to the workspace root.
`-Group` examples refer to `synppc/tests/check-runtime.ps1 carbon-cf-real` unless
another runner is explicitly named. These notes do not establish complete Carbon
compatibility or independent implementations of retained real workers.

## Window and QuickDraw accessors

Implemented: GetWindowPort, IsWindowHilited, IsWindowUpdatePending,
GetWindowStandardState, GetWindowUserState, SetWindowStandardState,
SetWindowUserState, SetPortWindowPort, GetWindowPortBounds and GetWindowFromPort.
Rectangles retain guest byte order; pointer arguments/results pass through the
existing bridge translation. Window lookup includes application and floating
layers. State handles shorter than WStateData are ignored safely.

The WindowsLib metadata calls share the companion's window records for the
modified flag, gadget drop threshold and owning process. The visual class
uses the live window definition and Appearance features. The double-click
action uses the companion's global value and preference store. A different
WindowsLib layout keeps the saved dispatch. Without a saved WindowsLib,
the modified flag and gadget threshold live in ROMAW's auxiliary window
record. The owner is the current emulated process, visual classes use the
classic dialog/document distinction, and the double-click action is stored
in ROMAW's WindowManager preferences.

The MyABCD QuickDraw group adds GetPortBounds, GetPortPixMap,
GetPortVisibleRegion, GetPortClipRegion, GetPortTextFont, GetPortTextSize,
GetPixRowBytes, GetQDGlobalsThePort and SetQDError. A null port selects the current
port. Region getters copy into the caller's handle. Invalid arguments report
paramErr through ROMAW's existing QDError state; successful getters preserve
that state. GetPixRowBytes retains bit 14, matching CarbonLib's classic fallback.
`src/exports.c` contains the registry; implementations are grouped by subsystem.

The GWorld group adds GetGWorld, GetGWorldDevice, GetGWorldPixMap, GetPixBaseAddr
and GetPixelsState. These read the canonical records maintained by ROMAW's
existing QDOffscreen manager. Unlocked offscreen buffers contain handles; locked
buffers and screen pixels contain direct pointers. Reads follow moving master
pointers and preserve ownership and lock counts. GWorld allocation, mutation and
disposal continue through their existing native or real-library resolution.

The pattern group adds GetQDGlobalsBlack, GetQDGlobalsWhite, GetQDGlobalsGray,
GetQDGlobalsLightGray and GetQDGlobalsDarkGray. MyABCD imports the black-pattern
getter. These functions copy eight bytes from the current A5 world's live
QuickDraw globals into caller storage and return the caller's pointer. They do
not cache patterns or return borrowed global storage.

The dialog/control accessor group adds GetDialogWindow, GetDialogFromWindow,
GetDialogTextEditHandle, GetDialogDefaultItem, SetPortDialogPort and
GetControlBounds. Dialog/window conversions preserve the reference pointer;
TextEdit handles remain borrowed; control bounds are read from the current
master pointer so moving a control does not leave cached data. Cancel-item
accessors remain on fallback pending a complete native cancel-item contract.

The update-region group adds InvalWindowRect, InvalWindowRgn, ValidWindowRect
and ValidWindowRgn. These APIs transform local damage into the requested window's
global update region without changing the current port. Region inputs are copied
before offsetting; rectangle inputs are captured before allocation can move guest
storage. Invalid windows return errInvalidWindowPtr, invalid inputs return
paramErr and temporary allocation failures return memFullErr.

## AppleEvent descriptor storage

Descriptor flattening writes the Classic big-endian `dle2` header and
payload. A size-only call returns the required length. Short buffers leave
their bytes intact. Unflattening reconstructs guest-owned handles for
`dle2` descriptors and legacy `dle1` scalar values. Legacy `dle1`
AppleEvent conversion continues through saved CarbonLib. Record inspection
checks both Classic packed records and the live AppleEvent Manager layout
used by ROMAW.

The AEStream family builds scalar descriptors, lists, records and events
with the live AppleEvent Manager. Nested scopes transfer descriptors on
successful close, including custom record types. Opening an event consumes
the input only after the stream is ready, and closing publishes the optional
keyword list in the event's `optk` attribute. Streams allocated by saved
CarbonLib continue through that implementation.

The six AEBuild entry points parse scalar values, typed data, lists and
records, and can construct or update a live AppleEvent. PPC register
arguments and stack arguments use Classic CFM word order. The explicit
`va_list` entry points read the guest parameter area. A failed native
parameter build leaves the original event intact. Numeric coercions outside
the native integer and Boolean paths use saved CarbonLib. [Apple Technical
Note TN2045](https://leopard-adc.pepas.com/technotes/tn/tn2045.html)

`AEPrintDescToHandle` returns a guest Memory Manager handle whose size is
the length of the printed text. It emits rebuildable syntax for descriptors,
lists, and records, using hexadecimal data when quoting cannot preserve the
bytes. AppleEvent output includes its class, ID, parameters and attributes
for inspection. The printer uses compact formatting and keeps saved
dispatch for descriptor layouts it cannot decode.

`AEGetArray` and `AEPutArray` support packed and word-aligned data,
handles, descriptors, and keyword/descriptor pairs with ROMAW's expanded
list storage. Extraction returns complete items that fit the buffer.
Replacement publishes the new list after allocation and copying succeed.
Original factored list storage continues through saved dispatch when present.
[Apple Event Manager reference](https://leopard-adc.pepas.com/documentation/Carbon/Reference/Apple_Event_Manager/Reference/reference.html#//apple_ref/c/func/AEGetArray)

## Byte strings and atomic helpers

The string group adds CopyPascalStringToC, CopyCStringToPascal, c2pstrcpy and
p2cstrcpy. These are byte copies, not encoding conversion: Pascal payloads retain
embedded NUL bytes, C input is capped at 255 bytes, and in-place/overlapping buffers
are supported. The Copy APIs take source first; the lowercase aliases take
destination first. All four have void signatures.

The atomic group adds IncrementAtomic, DecrementAtomic, TestAndSet and
TestAndClear. Word operations use guest big-endian order, wrap unsigned arithmetic
and return the previous value. Bit operations use MSB-first byte numbering and
the low three bits of the bit index, as in CarbonLib's PPC implementation. One
native atomic lock serializes these helpers, including overlapping or unaligned
words, without invoking guest code while held. This does not make arbitrary
non-helper writes by other code atomic.

The full reference atomic family is now present: AddAtomic, 8-/16-bit add and
increment/decrement, all three widths of BitAnd/BitOr/BitXorAtomic, and
CompareAndSwap. Signed arithmetic results and unsigned logical results retain
their different extension rules. All return the previous value, except the
Boolean compare-and-swap result. One shared lock coordinates all widths.

The remaining record accessors cover text face/mode, pen mode/visibility/size/
location, spacing, fractional pen position, GrafProcs, the CopyBits BitMap
overlay, PixMap bounds/depth, region bounds and foreground/background/operation/
highlight colors. GetRegionBounds is a direct MyABCD import. Byte-order-sensitive
records are copied unchanged; unsigned text-face results retain all eight bits.

The remaining QuickDraw globals expose the active A5 world's random seed,
screen BitMap and arrow cursor. The seed setter writes guest byte order, while
the 14-byte BitMap and 68-byte Cursor are copied into caller-owned buffers.

AEGetDescData, AEGetDescDataSize and AEReplaceDescData expose descriptor data
through the existing AppleEvent Manager. Copies truncate to the caller's cap,
preserve binary bytes and leave output untouched for empty descriptors.
Replacement disposes the old payload and creates the new descriptor; allocation
failure leaves a valid empty descriptor.

## CoreFoundation accessors and callbacks

CFArrayGetCount, CFGetTypeID and the four CFData accessors operate on real
CarbonLib 1.4/CoreFoundationLib guest objects. Counts and runtime type tags are
read in guest byte order; CFData buffer addresses are translated guest pointers.
CFDataGetBytes transports CFRange as two PPC words and copies the requested
valid range. These getters share objects with the original constructors and
mutators, including changes to the same objects between native reads. The native
ownership and allocator groups below also operate on these shared objects.
CFArrayGetValueAtIndex and CFArrayGetValues also read immutable, fixed mutable,
deque and CFStorage-backed arrays. Elements retain their opaque guest bits;
reads borrow values without invoking ownership callbacks or changing storage caches.
CFStringGetLength and CFStringGetCharactersPtr handle the real string layouts,
including inline/external storage, Pascal length bytes and lazy C-string length
caching. UTF-16 pointers borrow the original guest buffer; byte strings return
NULL rather than allocating a converted buffer.
These ten APIs are available through CarbonLib and their exact CoreFoundationLib
source imports.

CFArrayApplyFunction invokes the supplied PPC callback once per selected
element, preserving opaque element/context words and borrowed ownership across
all four storage forms. Its backend validates callback descriptors and uses the
existing nested callback stacks. A failed callback stops enumeration and keeps
the original launch diagnostic. This API is marked PPC-only in the registry;
68K imports keep real-library fallback for their callback calling convention.

CFArrayBSearchValues uses the same PPC callback backend for signed comparator
results, opaque values and contexts. It follows the reference's insertion and
comparison sequence across all storage forms, including a verified CarbonLib
1.4 CFStorage quirk for nonzero range locations. This callback ABI also retains
68K fallback.

CFAbsoluteTimeGetCurrent, CFDateGetAbsoluteTime, CFDateGetTimeIntervalSinceDate
and CFDateCompare add the native date group. The current-time call converts
ROMAW's unsigned Macintosh seconds to the 2001 epoch, retaining CarbonLib
1.4's whole-second resolution. Date readers decode the real object's IEEE value
without changing ownership. PPC double results use f1 through an explicit
signature extension; ordinary integer/pointer arguments keep their existing
marshalling. Native double arguments and 68K double results are not provided by
this extension; those imports retain real-library fallback.

The thirteen Pascal String Utilities implement length, copy/append (including
signed bounded forms), bytewise comparison, character/set/substring searches,
span and Pascal position lookup. These are CarbonLib re-exports from StdCLib;
the registry records their actual import indices and source library separately
from CarbonLib code offsets. PLstrcmp is an optional MyABCD import. Embedded NUL
and high bytes are payload, search results point into that payload, and empty
substring searches return no match in the local reference. PLstrncpy preserves
a longer destination's length when its requested prefix is fully supplied;
short sources shorten the result and zero-fill through the requested cap.

Matching CarbonLib function imports use this registry. Implemented re-exports
also serve imports from their exact recorded source library: the thirteen
Pascal functions work through both CarbonLib and StdCLib. Other libraries do
not acquire matching names from the registry. Unsupported functions and all
data symbols retain the existing resolution path, including real PEF
fallback and its initialization/dependency ordering. Carbon's WaitNextEvent
wrapper is deliberately retained for timer bookkeeping. No missing function is
replaced with a generated success stub.

## Validation fixtures

The Windows fixture links the actual archive against a small host fixture and
checks null handling, mono/color port offsets, guest pointers, byte order,
state-handle bounds, Boolean normalization, layer lookup, export classes, region
copies, signed font/size fields, shared errors and every 16-bit rowBytes value.
Pattern tests switch between two A5 worlds, use distinct mutable patterns and
check output guards and copy ownership. PPC integration and fallback checks live
in `synppc/tests`.

String tests cover every Pascal length, long/non-ASCII C strings, embedded NULs,
overlap and protected-page caps. Atomic tests cover signed boundaries, wraparound,
all byte/bit-index combinations, unaligned words and contended Windows threads
with a uniqueness check on every returned previous value.

Pascal tests cover all byte values, every string length, signed count boundaries,
255-byte truncation, prefix preservation, binary searches and protected-page
read bounds. The default PPC Pascal probe contains original code only. For an
additional local differential run, pass `--reference <System/pef/data-StdCLib>`
to `synppc/tests/make_ppc_carbon_pascal_probe.py`. It reads the ignored local
reference and creates an ignored oracle fixture; reference code is not embedded
in the generator or the native archive.

CoreFoundation unit tests cover runtime tag bits, unaligned records, pointer
translation, every range of a 256-byte payload, malformed ranges and protected
record extents. The ordinary PPC layout probes use original sentinel libraries
and verify fallback initialization without requiring Apple binaries. Run
`synppc/tests/check-runtime.ps1 carbon-cf-real` for the additional local tests using
the user's installed CarbonLib assets: real constructors, mutable array/data
changes, native accessors and real release, through both import libraries in
interpreter and JIT modes. Their test applications declare Carbon's required
SIZE capability flags; initializers execute normally.
Pass `-Group Dates` to the same runner for real CFDate construction/release,
native double results, comparisons and current-time checks in all four library/
CPU combinations. The original-code date probes exercise the same PPC ABI
without installed Apple libraries.
The original callback probe also reenters native ApplyFunction from a PPC
callback, then calls a native accessor from the inner callback. Diagnostic
fixtures reject an invalid descriptor and propagate an unsupported API called
inside a valid callback. Real-array tests check callback counts/checksums and
confirm that enumeration adds no retain/release calls.
Pass `-Group Search` to the real-object runner for differential search tests.
They obtain the original CoreFoundationLib export using GetSharedLibrary and
FindSymbol, verify that its entry is original PPC code, then compare results,
comparator counts and argument-order hashes against the native import. Tests
cover ascending/descending data, duplicates, opaque words, all storage forms,
empty/subranges and 20,000-element arrays.
Additional contiguous arrays exercise search lengths through 512 elements.

## Allocator and reference ownership

CFAllocatorGetDefault reads the live default allocator from the installed
CoreFoundationLib 1.4 TOC, sharing state with allocator creation and real setters.
The bridge checks the original export's descriptor and PEF section metadata;
unrecognized layouts call the original getter through the PPC callback backend.
No allocator objects or initialization state are fabricated. This PPC-only
entry retains the real implementation for 68K callers. Pass `-Group Allocator`
to the real-object runner to check default changes, pointer identity and borrowed
ownership against the original getter in both engines and import libraries.
CFAllocatorGetContext copies the eight context words in guest byte order and
sets the output version to zero. CFAllocatorGetPreferredSizeForSize invokes
the optional PPC callback only for positive requests, preserving opaque info
and hints; its result is clamped against zero and the requested size using
signed CFIndex comparisons. Both resolve a null allocator through the live
default getter. They borrow references and share constructor, setter and release
state. The allocator runner compares both operations with original PPC
exports on actual custom allocators, including default selection and ownership.
The allocation group adds CFAllocatorAllocate, CFAllocatorReallocate and
CFAllocatorDeallocate. These invoke the real allocator object's PPC callbacks
with raw guest block/info words and translated pointer results. Allocation
forwards negative sizes; reallocation selects allocation, deallocation or the
optional reallocation callback according to pointer/size, preserving null
results and missing optional callbacks. A null allocator uses the live default.
Pass `-Group Allocation` to compare callback records and results with original
PPC exports and check real system allocation, growth, byte preservation and
disposal. Default setters and unsupported APIs retain fallback.
CFAllocatorCreate and CFAllocatorGetTypeID now use the real initialized class
state for the recognized 1.4 layout; other layouts delegate their original
exports. Creation retains info, obtains 48 bytes from the selected parent or
`kCFAllocatorUseContext`, initializes the class/header and copies the live
context. The first argument retains its raw guest representation for the
`0x221` sentinel. Failed allocation leaves info retained, matching the verified
1.4 behavior. Run `-Group Creation` for constructor-record comparisons and real
CFRelease disposal, including callback order and the allocation-failure path.
Class registration, default setters, other class finalizers and overflow
dictionary operations still use the real library; successful construction does
not establish a fully native CF runtime.

CFRetain, CFRelease, CFGetRetainCount and CFGetAllocator now implement the 1.4
ownership rules in C. Header counts and the real overflow dictionary stay
shared with original PPC calls. Release dispatches registered finalizers,
handles allocator-object disposal order, frees system/prefixed storage and
releases custom parents. Allocator lookup returns borrowed references. Unknown
layouts and 68K callers retain fallback. `-Group Ownership` compares native and
original operations through 65,540 retains/releases, crossing both overflow
boundaries, and checks real destruction and custom-parent ownership. Dictionary
helpers and other class finalizers remain real dependencies. The creation
runner also cross-checks original release of native-created objects and native
release of original-created objects.

## Array construction

CFArrayCreate and CFArrayGetTypeID add immutable array construction using live
class state. Null, standard and custom callback records retain their different
layouts; classification compares function fields and ignores the version word.
Custom allocators own a retained four-byte prefix. Class initialization runs
before array fields are set, and retain callbacks receive the caller's original
allocator word. Callback records and input values stay live across callbacks.
Allocation failure returns null before element retention; invalid counts and
size overflow fail without allocating. Other layouts and 68K callers retain
fallback. `-Group ArrayCreation` compares original/native records, callback
hashes, all allocator modes and reciprocal release paths on real objects.
Mutable array constructors, other array operations and class registration remain
on their existing native or real-library paths.

## String and Unicode readers

CFStringGetCharacterAtIndex and CFStringGetCharacters read inline/external
UTF-16 and byte strings, including Pascal prefixes. Byte conversion uses the
live CoreFoundation converter. Verified built-in MacRoman, ASCII, ISO Latin-1,
Windows Latin-1 and NeXT descriptors now decode in C; changed and other
descriptors retain callback dispatch. MacRoman accepts every live table word;
Windows/NeXT reject U+FFFD table entries, while ASCII refuses high bytes.
Converter absence widens bytes, and a false conversion
result emits 0xFFFE. Character results preserve all 16 bits, ranges use two PPC
words, and character reads leave the lazy length cache unchanged. Unknown layouts
and 68K callers retain fallback. `-Group CFStrings` compares native and original
readers on actual immutable/mutable strings, all MacRoman bytes, embedded NULs,
surrogates, 0xFFFF and ranges spanning 32-character boundaries. CFStringCompare
and its comparison engine remain on fallback; the migration notes describe the
reference's incomplete decomposition helper, which prevents treating a wrapper
around that engine as a complete native comparison implementation.

CFUniCharIsMemberOf implements the three algorithmic character sets and twelve
packed sets shared with the initialized CoreFoundationLib 1.4. Packed reads use
summary bits and binary searches over singleton/range records without allocation
or cache writes. Unrecognized profiles delegate the original export. This symbol
belongs only to CoreFoundationLib; CarbonLib imports cannot resolve it through
the native registry. `-Group UniChar` compares every UTF-16 code unit across all
16 selectors with the original PPC implementation, in both engines. Offline
fixtures check initialization, namespace isolation and foreign-profile fallback,
including callback failure propagation.

## String pointers and encoding

CFStringGetCStringPtr and CFStringGetPascalStringPtr borrow the real byte storage
without copying, allocating or retaining it. Encoding checks share the live
eight-bit encoding and ASCII-family rules. CString lookup checks the current
termination flag after encoding initialization; Pascal lookup rejects its format
first and verifies explicit lengths, updating the original lazy cache as needed.
Other layouts and 68K imports retain fallback. `-Group StringPointers` compares
196 original/native pointer results per engine/import library on real immutable
and mutable strings, with both non-null and null results required. Archive tests
also cover all low flag bytes at four alignments, length mismatches, callback
mutations/failures, borrowed-buffer changes and foreign profiles.

CFStringGetCString and CFStringGetPascalString implement the 1.4 source-layout,
capacity and encoding selection rules. Compatible byte strings copy in C, including binary bytes and
Pascal prefixes; lazy external lengths share their original cache. Other paths
use the incremental encoding engine, passing all nine PPC words and a
guest used-byte output. UTF-16 output and NonLossyASCII escaping run in C;
UTF-8 strings with direct UTF-16 storage use native converter-cache/dispatch
logic and a C implementation of the verified built-in codec. Changed callbacks
and descriptors retain real callback dispatch; other cases retain the original engine.
With the eight-bit encoding cache already initialized, the engine also copies
byte strings in C when the requested encoding matches, the entire input is
ASCII-compatible, or its ASCII prefix fills the destination. Exact-encoding
copies retain embedded NUL and high bytes. With warm encoding/preference caches,
mixed UTF-8 tails decode in 512-unit chunks and use the C codec, preserving the
generic converter cache and partial results. Built-in single-byte decoders now
use native mapping/refusal rules and live tables; other byte-to-Unicode callbacks remain
dependencies. Cold caches and mixed tails for other encodings delegate
the complete call before writing output or counts.
Incomplete conversion clears only the Pascal prefix,
retaining partial payloads. As in this reference, the 255 limit applies to source
units; successful longer encoded payloads store the low byte of their byte count.
Unknown profiles and 68K callers retain fallback. The engine export remains
unregistered: generic converter dispatch, remaining cases, other codec callbacks and
the lazy eight-bit-encoding initializer still require native implementation.

C-string conversion reserves one byte for termination and accepts source lengths
above 255. Direct copies preserve binary payloads and append NUL; complete
conversions terminate at the engine's encoded-byte count. Incomplete conversions
clear the first output byte while preserving partial payloads. Callback failures
propagate without publishing a terminator. `-Group CFCString` compares Boolean
results and full guarded buffers against the original PPC implementation.
The built-in UTF-8 codec keeps its live prefix table and legacy surrogate rules:
lone low surrogates are encoded, lone high surrogates stop conversion, and a pair
that cannot fit rolls back one unit. Zero capacity measures without writing,
even with an output pointer. Neither BOM nor other callback flags change this
codec's output. The C-string and Pascal matrices each compare 1,440 pairs per
engine/import library, including surrogate pairs and unpaired units.

`-Group CFPascal` compares return values and entire guarded buffers for 1,440
source/encoding/capacity combinations per engine/import library, using real
immutable/mutable strings, MacRoman bytes, UTF-16, surrogates, embedded NULs,
empty/255-/256-unit inputs, five encodings and negative/zero/short/large capacities.
The native bundle lookups now call this C API directly; system scratch storage
preserves memory errors and is released after conversion or callback failure.

CFStringGetBytes implements the 1.4 range/capacity rules: compatible byte strings
copy in C, measurements ignore capacity, and converted character counts remain
separate from encoded-byte counts. Other encodings use the incremental engine
with all nine words, including the optional used-byte output from the PPC stack.
CFStringGetFastestEncoding returns Unicode for wide storage and otherwise reads
the shared byte encoding, invoking its real initializer only when cold. Unknown
profiles and 68K callers keep fallback. `-Group Encoding` compares 1,900 extraction
cases and seven encoding queries per engine/import library against original PPC
descriptors, including ranges, binary bytes, BOM/loss flags and optional outputs.
General converters and encoding initialization remain real-library dependencies.

## Bundle symbols and lifecycle

CFBundleGetFunctionPointerForName and CFBundleGetDataPointerForName implement
the 1.4 CFM lookup path in C. They load the executable on demand, require the CFM
executable kind, convert names to ASCII Pascal strings with the original
255-byte limit, resolve the existing connection and require descriptor/data
symbol classes respectively. Missing or wrong-class symbols use the original
logging functions. Loading, encoding and logging remain real-library
dependencies; other profiles and 68K imports retain fallback. Temporary system
storage is released on all paths and preserves the memory-error state.

`-Group Bundle` compares results with original PPC exports using real CFStrings
and initialized CFM images, checking callable descriptors, borrowed data,
wrong-class/missing symbols, invalid connections, executable-kind filtering and
ASCII name boundaries. Its bundle records deliberately isolate the lookup fields;
this does not verify complete bundle construction or loading. Offline fixtures
and archive tests additionally check on-demand load, exact callback arguments,
foreign-profile delegation, allocation failure and dependency failures.

GWorld tests check mutable canonical records, pixel-buffer relocation/purge,
screen-pointer handling, all flag-byte combinations and encoded port/device
outputs. The PPC fixture allocates 8-, 16- and 32-bit worlds, reads their actual
devices/pixmaps, exercises nested locks and drives state changes through classic
QDOffscreen dispatch before native reads. It retains initialized PEF fallback.

See [the selector and migration findings](../../synppc/docs/carbon-native.md)
for OSDispatch 0x005A, which the current runtime already implements.

The seven encoding-query APIs now implement maximum encoded size, both NSString
identifier conversions, both Windows code-page conversions, encoding availability
and the most compatible Mac encoding in C (`src/encodings.c`). They preserve the
reference's unsigned wraparound and legacy mapping quirks, including overlapping
live tables and its original binary interval lookup. Availability and compatible
Mac lookup share the initialized converter definitions and raw-key dictionary.
External definition discovery, external converter adapters and weak TEC base
lookup still execute the original private dependencies in the verified image;
no replacement converter state or success stubs are invented. Foreign image
profiles and 68K imports retain the original public export.

`-Group EncodingQueries` compares 1,958 original/native results per engine/import
library across all seven APIs. Offline PPC fixtures also exercise private callback
vectors, shared cache creation/hits, initializer execution, foreign fallback and
controlled failures in discovery, adapter and dictionary callbacks. Archive tests
check all 65,536 Windows code pages against independently specified mappings.
Real external TEC discovery is retained as a dependency, not claimed as a native
implementation by this group.

The bundle state group adds CFBundleGetTypeID, CFBundleCopyBundleURL,
CFBundleGetInfoDictionary, CFBundleGetValueForInfoDictionaryKey,
CFBundleGetIdentifier, CFBundleIsExecutableLoaded, CFBundleLoadExecutable and
CFBundleGetPlugIn. These share the original initialized objects and caches.
Metadata values prefer localized dictionaries; identifier lookup uses the live
CFBundleIdentifier constant. CopyBundleURL retains and then reloads the URL,
matching the reference even if the ownership callback changes the bundle.

LoadExecutable implements the original state machine and its private CFM helper
in C: discover/release the executable URL, remove already-loaded bundles from
the unload queue, perform pending unload cleanup, then try bundle and executable
URLs in order. Failed discovery writes kind 6, absent executables write kind 7,
and successful fragment discovery writes the real connection and loaded flag.
The resource/PEF discovery helper, pending-unload worker and locks still execute
original dependencies; they are not counted as fully replaced subsystems.
Lazy info-dictionary loading and localized-resource loading also retain their
original dependencies. Foreign profiles and 68K imports retain public fallback.

`-Group BundleState` compares 24 native/original results per engine/import library
on a genuine filesystem bundle, requiring populated type, URL and metadata.
The original constructor loads its Info.plist and creates the actual object.
Missing-executable loads retain kind 7 and return false. The separate optional
`check-runtime.ps1 carbon-bundle-copy` includes original PPC executable fixtures and
checks successful native and original CFM loading in both engines, invokes the
loaded function/data exports and unloads through the original bundle API.
ROMAW now supplies private connections for disk, named and memory fragments.
Each copy gets fresh data and initialization, retains normal shared imports,
stays out of named lookup, and terminates/releases independently at close.
Identical PIC code without section relocations shares ref-counted storage;
instance-specific or changed code keeps independent storage. Native-only
libraries without a real PEF instance return unimpErr for private-copy requests.
The original resource/fragment discovery worker and real-library fallback remain
in use; this does not establish complete CFM or Carbon compatibility.

The bundle lifecycle group adds CFBundleCreate, CFBundleGetLocalInfoDictionary,
CFBundleUnloadExecutable, CFBundleGetVersionNumber, CFBundleGetDevelopmentRegion,
CFBundleGetFunctionPointersForNames and CFBundleGetDataPointersForNames. The C
constructor implements the worker behind the public forwarding entry: cache
lookup/retain, canonical filesystem URL creation, directory validation, runtime
instance initialization, info loading, cache registration and plug-in setup.
Filesystem detection, instance allocation, cache registration and plug-in workers
still use real initialized dependencies. Localized metadata reads actual resource
data and property lists; borrowed dictionaries are cached in the original object.

Version and region getters validate real metadata types, update the same mutable
info dictionary and preserve 1.4's finite version grammar and BCD quirks. Bulk
lookup writes guest-order four-byte pointers through the native single-symbol
lookups, retaining missing/wrong-class results and executable loading behavior.
Foreign profiles and 68K imports retain public fallback.

Unload matches the reference's pending-queue, plug-in, CFM-close-attempt and cache
cleanup flow. A cleared loaded flag does not establish image release: the local
1.4 code passes the stored connection word directly to CloseConnection, while
its documented API takes a pointer to the ID. Both original and native paths
leave the mappings live in the verified ROMAW scenarios. The real lifecycle
probe checks actual CFM mapping status and explicitly closes any retained mappings
with the documented pointer argument. Complete fragment unloading is not claimed.

The optional `-Group BundleLifecycle` compares separate native/original bundles,
actual localized InfoPlist.strings, 78 version inputs, metadata type/cache changes,
CFM function/data symbol arrays, output guards, repeated unload and fresh loads.
It also exercises missing paths, regular files and ordinary directories. Archive
fixtures cover callback failures, allocation/ownership, main-bundle region fallback
and foreign profile delegation; synthetic PPC fixtures cover the public ABI and
controlled dependency failures. MyABCD directly imports the new constructor.

## URL access and mutation

The URL group adds CFURLGetTypeID, CFURLCanBeDecomposed, CFURLGetString,
CFURLGetBaseURL, CFURLHasDirectoryPath, CFURLCopyScheme, CFURLCopyNetLocation,
CFURLCopyPath, CFURLCopyStrictPath, CFURLCopyResourceSpecifier,
CFURLCopyLastPathComponent and CFURLCreateCopyAppendingPathComponent. The last
is a direct MyABCD import. Native C handles packed component ranges, inheritance,
strict-path output, last-component selection and both file-style and generic URL
append control flow. Parsing, file-to-URL conversion, string operations, escaping
and URL construction still use real library dependencies. Unrecognized image
profiles and unsupported calling paths retain public-library fallback. Genuine
CarbonLib/CoreFoundationLib comparisons cover generic, relative, POSIX, HFS and
Windows URLs in both PPC engines; this does not establish complete URL support.

The next URL group adds decoded host/user/password, parameter/query/fragment,
port number, path extension, filesystem byte-string constructors and FSRef
conversion control flow. Two reserved-pointer accessors are companion-only.
Port parsing reproduces the historical whitespace/sign rules, consumes the full
component and saturates signed 32-bit overflow. Component inheritance and allocator
ownership follow the original workers. FSRef capability, catalog lookup and path
conversion still use explicit real dependencies. ROMAW currently reports no
modern FSRef capability: genuine probes verify rejection without output mutation;
supported conversion paths are verified with archive fixtures. Full FSRef filesystem
operation is not established. Foreign images and missing APIs retain fallback.

URL creation/mutation now covers CFURLCreateWithString, CFURLCreateWithBytes,
CFURLCreateData, both filesystem-path constructors, deleting the last path
component and appending/deleting path extensions. The companion-only _CFURLAlloc
and _CFURLInitWithString entries share native record allocation/initialization
with these constructors. Their C bodies preserve allocator ownership, base URL
selection, directory separators, the POSIX safe-string flag and the original
parent-path rewrites. Runtime object allocation, re-encoding,
string operations and current-directory discovery remain real dependencies.
Older native callers that explicitly invoke saved original constructor descriptors
still use those reference paths; this batch does not replace all callback routing.
Genuine comparisons verify raw flags/stored strings before semantic equality,
with successful byte construction and external representation checks.

URL resolution adds CFURLCopyAbsoluteURL, CFURLCopyFileSystemPath and
CFURLGetFileSystemRepresentation. Shared filesystem conversion control flow,
POSIX percent-decoding/trimming and same-style absolute construction are C;
merge, parser, file-to-URL and HFS/Windows conversion workers remain explicit
real dependencies. The constructors/mutations now use native URI validation,
including the reference's final-character and repeated-fragment boundary behavior.
Archive tests cover every BMP code unit at interior/final positions; genuine
fixtures compare 539 validator inputs and 35 resolution sources in both engines.
Filesystem byte output rejects nonempty capacity-one buffers before the original
encoder can treat zero output capacity as a sizing query. CarbonLib 1.4 incorrectly
returns success and writes its terminator beyond the declared extent in that case;
a bounded reference fixture records the difference. Normal-size output and partial
writes are compared directly, including guard bytes. Foreign profiles retain fallback.

String operations add CFStringCompare, CFStringCompareWithOptions,
__CFStringMakeConstantString, CFStringCreateCopy, CFStringHasPrefix,
CFStringHasSuffix, CFStringFind and CFStringGetIntValue. Native C handles chunked
literal/case comparison, the shared constant dictionary, immutable identity/copy
selection, anchored searches, PPC hidden CFRange results and saturated decimal
prefix parsing. Unicode folding, localized comparison, object factories, dictionary
operations and the search worker remain explicit real dependencies. Foreign
reference profiles and missing APIs retain the real fallback.

CarbonLib 1.4's nonlocalized Unicode decomposition worker at 0x49a80 is an empty
stub. Its caller can mistake the unchanged character for a count and read beyond
its ten-unit scratch buffer. Native comparison rejects oversized helper results
with a controlled diagnostic. Nonlocalized canonical equivalence requiring that
worker remains unsupported; the real-object comparison suite does not execute
the unsafe original path. Safe literal, case-folded, localized and ASCII nonliteral
paths are compared with original descriptors in both PPC engines and namespaces.

## Classic record accessors

The classic accessor batch adds 185 independent C entry bodies: all 137 exported
LMGet/LMSet routines, 29 List accessors, ten Control accessors, six Menu accessors
and three Dialog accessors. Low-memory operations share ROMAW's existing
storage, including pointer-valued globals, packed Points, indexed dispatch tables,
fixed-size string/scratch buffers and RGB fields. Record accessors follow current
guest master pointers and preserve the reference's signed fields, optional-handle
checks, menu-title sentinel and ordered overlapping copies. Guest pointer values
remain raw 32-bit words; memory is checked and translated only at dereference.

These entries are PPC-only; 68K and missing APIs retain their existing fallback.
The native fixture tests malformed values and bounds using isolated guest memory.
The PPC fixture checks live low-memory setters with their current values and
uses application-owned records. Its optional real mode compares all 185 routines
with original CarbonLib descriptors after normal library initialization, recording
native output before the original can replace it. This batch expands shared API
coverage; application imports require their own workflow validation.

The next 102-entry cohort covers 16 Array, 11 Data, six Set, 16 CharacterSet,
seven Number, two Boolean, 40 String, three generic object operations and
CFDateGetTypeID. The manifest is `plans/cf-values-batch.json`; it records every
public signature, reference code offset and main-fragment import ordinal.
Native C handles collection/value control flow, mutation, callback dispatch,
number construction/exactness/comparison, encoding caches, string searches,
line bounds, padding/trimming and case conversion. Constructors and mutations
share live original classes, allocator ownership, singletons and caches.
Character-set mutations use the compatible Unicode BMP bitmap representation.

Runtime allocation, generic hash lookup, CFStorage/deque operations, sorting,
immutable string allocation/decoding, mutable string resizing/replacement,
number conversion and Unicode/encoding tables remain explicit real dependencies.
Unregistered APIs and foreign reference profiles keep real-library fallback.
Native entry counts measure migration progress rather than complete independent
API implementations; retained workers remain dependencies.

The optional `Values` real-library group checks all 102 native entries against
un-intercepted original PPC descriptors in both namespaces and engines. It
includes deque/storage growth and shrinkage, type callbacks, all 14 number-type
aliases, narrowing/exactness, signed zero, infinities/NaNs, bitmap/set operations,
Unicode construction, external buffers, search ranges and encoding aliases.
The standalone foreign-profile fixtures exercise every entry without running
native object code. Three portable diagnostics cover invalid guest output,
fixed-array capacity and Unicode mapping extents. Generated fixtures contain
original test code and require no redistributed Apple executable bytes.

Universal Interfaces 3.4.2 headers are available locally under
`Y:/SDKs/UniversalInterfaces3.4.2/Universal/Interfaces/CIncludes` (also under
UniversalHeaders3.4.2). Use them for declarations and contracts alongside the
versioned disassembly. Their newer CFNumber NaN ordering contract differs from
the observed CarbonLib 1.4 result; the native comparison preserves 1.4 behavior.
The existing nonlocalized decomposition limit also applies to nonliteral search.
The 1.4 case-mapping loop can expand beyond its last UTF16 character without
updating string length. Native code reports that unsupported extent before
writing outside the original string; general expanding case conversion remains
unfinished. These checks establish the tested scenarios, not full Carbon or
MyABCD workflow compatibility.

The 102-entry checkpoint was revalidated after the directory regrouping against
`windows-binaries/bin/carbon-validation/romaw.exe`: archive fixtures, four
real Values scenarios, 118 standard PPC probes and 101 controlled diagnostics per
engine, the CPU core suite, generated-file dependencies, formatting and help
checks passed. MyABCD reached its JIT application event wait with nine calls into
this cohort in the final run (ten in an earlier independent run). This remains
a startup smoke check. The separate candidate was staged while the normal
executable was in use; these results do not describe unrelated later changes to
the default executable. The file probe also exposed truncated host alias paths;
resolution now recovers through the stored parent catalog ID.

## Collection and runtime cohort

The following 100-entry cohort is recorded in `plans/cf-collections-batch.json`:
47 Set/Bag/Dictionary entries, 16 Tree entries, 15 Preferences entries, three
PropertyList entries, six UUID entries, nine private Storage entries and four
private Runtime entries. There are 86 additional main-CarbonLib re-exports and
14 companion-only entries. Public argument-word signatures were checked against
86 Universal Interfaces declarations, including UUID's hidden result pointer
and by-value four-word payload. The remaining 14 use the versioned private or
older disassembly ABI.

Native C now handles collection lookup dispatch, mutation and ownership order,
tree links and contexts, runtime registration/allocation, storage cache/range
operations, UUID text conversion, typed preference values and bulk operations,
and property-list dictionary/scalar copying. Original hash factories/search/growth,
balanced storage nodes, UUID interning/entropy, preference domain/cache/files and
suite ordering, XML parsing/serialization and recursive array-copy workers remain
dependencies. Public helper calls through saved original descriptors also remain.
These are native entry bodies, not an independent replacement for those workers.

`CFTreeSetContext` and `CFTreeSortChildren` are bare-return functions in the local
1.4 reference and remain on fallback, outside this count. The installed sibling
parent-link convention is preserved. Native bounds reject immutable hash mutations,
fixed-capacity overflow and already-parented appends; the original assertion helper
at 0x1a994 is itself a bare return. Runtime class IDs must fit the eight-bit object
header, storage loops must progress and native dictionary-copy recursion is bounded
at 512 levels. Short UUID text returns NULL without using missing uninitialized
bytes; the unsafe original malformed-input path is excluded from the oracle.

The `Collections` group of `synppc/tests/check-runtime.ps1 carbon-cf-real` compares
all 100 entries through CoreFoundationLib and the 86 public entries through
CarbonLib, in both engines, with real initializers and distinct original descriptors.
It includes collection growth/callbacks, storage-node splits and collapse, class
initializers, UUID ABI/text, property-list values and preference writes checked
with the original reader before the other writer runs. Preference IDs are unique,
short classic filenames; written keys are removed and synchronized afterward.
Separate foreign-profile fixtures check every entry's exact fallback result.

Collection checkpoint validation passed: archive fixtures, 120 standard probes
and 107 controlled diagnostics per engine, four genuine Collections scenarios
(1,651 assertions per CoreFoundationLib scenario), four Values regression scenarios,
generated-file/dependency, coverage, formatting and Windows help checks. MyABCD
reached its application event wait in both engines with 24 calls into this new
cohort during startup. This does not establish additional interactive activities
or complete independent Carbon compatibility. The validated candidate is preserved
under `windows-binaries/bin/carbon-collections` and copied byte-for-byte to the
normal `windows-binaries/bin/ROMAW.exe`, with the current static archive staged
beside it. All 35 archive object text sections match the tested candidate after
final header formatting. The normal staged executable also passed help and the
MyABCD JIT event-wait check with 24 new-cohort calls.

## Framework cohort

The 106-entry framework cohort in `plans/cf-frameworks-batch.json` adds 29 Bundle,
20 PlugIn, 25 XML, one allocator and 31 private preference entry bodies. The
registrations are enabled by default after validation. Entry bodies can still invoke real workers.

Native code now handles preference-domain callback dispatch, application records,
cache ownership and search ordering, XML node allocation, parser state and client
callbacks, bundle entry flow, plug-in instance records and default allocator
ownership. Filesystem/cache-key construction, preference representation merging,
plug-in registry/loading, XML scanner/additional-info/recursive serialization and
saved original public helpers remain dependencies. The real fragment must stay
installed until these workers are separately replaced. CarbonLib 1.4 timezone
exports mostly return placeholders; they were excluded from this cohort rather
than counted as functional timezone support.

`carbonlib/tests/check-frameworks.ps1 -ROMAW <ROMAW.exe>` generates one
combined `PPC Core Framework Real Probe` application and reuses it in both engines.
Its six sections contain 1,077 named assertions covering all 106 new entries,
including generated original PEF library fixtures, resource forks, localized
resources, preference cleanup, callback output and allocator restoration.
`checks.json` names assertions, `coverage.json` lists exercised/pending entries,
and `validation.json` records both engine results and the executable hash.
`-ReferenceOnly` validates fixture expectations against real fallback without
claiming native entry coverage. Generated supporting library fragments are not
separate regression applications and contain no Apple instructions.

The shared Windows harness covers all 106 foreign-profile argument/result ABIs,
callback failures, missing references and isolated guest-memory/overlap/extent
cases. Both original-fallback and candidate-native runs passed the full combined
probe in both engines. Regressions reused two further apps for the earlier Values
and Collections cohorts (2,964 and 1,651 assertions per engine). MyABCD reached its
application event wait in both engines with three calls into this framework
cohort; this remains a startup smoke check.

The cohort tests exposed and corrected both-direction current-user/any-user domain
aliasing in the installed compatibility mode. Executable URL checks resolve the
reference's initially relative and subsequently cached absolute URLs before
comparing paths. Public argument words were checked against 73 SDK declarations;
33 older/private declarations were audited from the disassembly. The validated
isolated candidate is under `windows-binaries/bin/carbon-frameworks`. The current
checkout was rebuilt and the verified executable/archive were staged normally,
with a copy retained under `bin/carbon-frameworks-current`. Normal-path help and
both-engine MyABCD event-wait checks passed with three cohort calls.

The earlier character-set regression exposed a host-alias boundary bug during the
current-checkout checks: an 8 KB native stack snapshot crossed into a different
128-KiB mapping. Bitmap conversion now uses checked guest allocation. Small fixed
buffers passed into PPC workers are aligned to a power-of-two extent that fits
one alias slot. The shared harness checks snapshot ownership, self operations,
allocator failure and callback failure; the real Values app passes in both
engines. This required no PPC opcode implementation.

Use `carbonlib/tests/check-corefoundation.ps1` for the routine grouped regression
pass. One JIT launch of the real-library application reports Values, Collections,
Frameworks, ClassicAccess and Toolbox checks, including the 185 classic accessors
and the MathLib and Math64Lib comparisons.
`validation.json` includes launch attempts, assertion counts by subsystem, entry
coverage, per-engine results and executable hashes. Pass `-Detailed` for generator
output and assertion counts for each section. If a guest assertion stops the app,
the report records the completed groups and the active group.

Use `-Full` at integration checkpoints to run the real-library app and the
synthetic fallback app in the selected engine. The fallback app checks Values
and Collections through both CarbonLib and CoreFoundationLib and runs even if
the real-library app fails. The exhaustive PowerPC integration suite also runs
these four fallback scenarios in one app.

Use `-Group Values,Collections` to select groups, `-Group Fallback` to run the
synthetic fallback app on its own, `-Engine Both` to repeat selected groups under
both engines and `-ROMAW <path>` to select a staged candidate. Explicit group
and engine selections also apply with `-Full`. Use `-ReferenceOnly` when validating
expectations against the reference runtime.
`check-frameworks.ps1` runs just the Frameworks group. `make -C carbonlib check`
covers isolated memory, ABI and callback cases in one host executable. Add
routine assertions to the grouped apps and use individual launches when a test
requires process isolation. Use MyABCD itself for its startup smoke check.
`synppc/tests/check-runtime.ps1 carbon-cf-real -Group All` runs the installed-library
comparisons for both library namespaces in one JIT launch. Pass `-Engine Both`
to also run the interpreter, `-Detailed` to print each check, or
`-Group @('Dates', 'Objects')` for a focused run.

The PPC call-frame adapter removes the eleven-integer-argument restriction for
entries marked `ROMLIB_CARBON_PPC_FRAME`. It transports r3..r10, raw f1..f13 bits,
FPSCR and the original caller stack. Checked argument reads use the classic
r1+24 parameter area and only touch the requested word. Saved-original frame
calls reuse that caller area, preserving floating inputs, hidden aggregate
arguments, stack arguments and varargs homes. They require a suspended PPC
caller; 68K retains real fallback. This is infrastructure for remaining date,
UUID and formatted-string work, not an increase in implemented export coverage.

The existing CFDateGetAbsoluteTime entry now validates the 1.4 profile before
reading its payload and forwards foreign profiles through the complete frame.
It copies IEEE bits directly, preserving signed zero and NaN payloads, validates
the eight-byte guest payload, and retains the earlier native null-to-zero result.
The Fallback app checks all seventeen argument words and thirteen FP inputs
before returning a GPR sentinel, an FP result and a changed FPSCR rounding mode.
The preceding executable fails this added scenario; the native frame candidate
passes it in both engines. The shared host harness covers 32 argument
positions, truncated/overflowing stack reads, FP bit preservation, missing
references, callback failures and register-output rollback. Collections also
checks the frame getter on real initialized dates against saved originals.

## CoreFoundation services

The regular archive registers the 100 CoreFoundation services listed in
`plans/cf-services-batch.json`. Of these, 52 are main CarbonLib exports and
48 are accessible only through the companion fragment.

Run `make -C carbonlib check` for the shared Windows host harness and
`carbonlib/tests/check-corefoundation.ps1` for the grouped guest apps.
The Frameworks app verifies native entry traces and compares results with the
installed CoreFoundation fragment on the PowerPC interpreter and JIT.
Its scenarios cover sorting callbacks, calendars, strings, value construction,
runtime records, bundle services, file and URL resources, property lists,
character-set bitmaps and diagnostic output. Use `-Group Frameworks` to run
that app alone. MyABCD called the native `CFStringCreateWithFormat` and reached
its application event wait with both CPU engines in the staged build.
The host harness checks all 100 foreign-profile call frames,
callback failures, missing references and 63 native calendar vectors.

The manifest records the remaining real-library workers per entry, including
XML/OpenStep parsing, encoders, selected bundle and resource operations, and
timezone and floating-point exception paths. The loader continues to resolve
unimplemented APIs through the installed fragment.

The Toolbox app covers 227 CarbonLib 1.4 wrappers using ordinary Pascal
`CallUniversalProc`. The native entries load the current low-memory Toolbox
vector for each call, preserve the Pascal argument widths and apply the
wrapper's return conversion. The Mixed Mode Manager dispatches to the installed
68K routine or a replacement PPC descriptor. The app substitutes two targets
per API and checks their arguments, results, and invocation order against
saved original CarbonLib exports. It restores the vectors before exercising
installed region, graphics and resource traps.

MyABCD called 20 of the new wrappers
41 times on each engine before its application event wait. The loader retains real PEF fallback.

The same Toolbox app covers another 162 CarbonLib wrappers that supply a
selector to a D0-dispatched or stack-dispatched Pascal trap. Tests exercise
replacement descriptors that receive the selector and multi-record descriptors
that match it and omit it from the callback. Installed-trap tests include
process identity, allocation and release of temporary handles, OffscreenVersion
and font preferences. The native wrappers share ROMAW's Mixed Mode and
68K manager implementations, including their diagnostics for unsupported
selectors.

Both CPU engines reached its application event wait
after 154 calls to 25 of the new wrappers, including 54 calls each to
TempFreeMem and TempNewHandle. Other application flows and individual
manager features require their own behavioral checks.

Use `carbonlib/tests/check-corefoundation.ps1 -Group Toolbox` for a Toolbox
wrapper batch. That one guest app exercises the batch in both engines and
reports its checks by subsystem. The two default PowerPC integration apps cover
cross-group regressions. Run a diagnostic only when investigating a failure path.
`synppc/tests/run-diagnostic-tests.ps1 -Both` runs the OSDispatch diagnostic
with one generated fixture shared by both engines. Select a different fixture
with `-Application` or request the exhaustive negative suite with `-All`.

The Toolbox app exercises 203 UPP constructors and 204 UPP disposers. The
constructors pass each export's CarbonLib 1.4 ProcInfo and PowerPC ISA to
ROMAW's Mixed Mode descriptor allocator. The disposers release descriptors
through the same allocation path. The tests compare complete descriptor
records, check null callbacks, dispatch an Apple Event handler, and verify
heap recovery when either binding allocates and the opposite binding disposes.
The constructors and disposers add 15 weak callback imports for MyABCD.

Another 168 UPP invokers use the same Mixed Mode Pascal dispatch as the
Toolbox vectors. They select the callback from the argument slot after the
ProcInfo parameters and convert its result to the wrapper's declared width.
The Toolbox app checks every invoker with a PowerPC callback and exercises a
raw 68K Apple Event handler on both entry paths.

The next batch adds 51 live trap wrappers and completes the CarbonLib 1.4
UPP constructor and disposer exports. Forty-seven entries use register-based
Mixed Mode dispatch. The Toolbox app replaces their OS or Toolbox vectors
with 68K code that records the dispatched registers and returns a known value.
It also allocates, locks and disposes real guest handles and pointers across
the native and reference paths. For 24 borrowed PowerPC UPP families the app
checks caller ownership, transition-vector dispatch, integer arguments and
the floating argument in f1. NewComponentFunctionUPP takes its ProcInfo
from the caller.

The trap and borrowed UPP batch added 17 strong imports and four weak imports
for MyABCD.

## Math and classic Toolbox re-exports

The archive also supplies 132 MathLib re-exports. They marshal scalar and
predicate results through the PPC registers, translate the guest rounding
mode for host libm calls, and write big-endian auxiliary results. IEEE
classification and sign operations inspect the guest representation, including
both limbs of the long-double sign operations. MathLib's PowerPC environment
APIs operate on the guest FPSCR, including saving and restoring selected
exception causes.

The double-double routines use binary128 intermediate operations from
ROMAW's SoftFloat archive. They preserve tails below binary128 precision
when rounding, scaling, decomposing, or computing a square root. The
conversion functions read and write the original packed ten-byte extended80
records or two binary64 limbs in guest byte order.

Long-double transcendental functions evaluate at binary128 precision with
statically linked quadmath. The functions that use binary64 arithmetic in
the reference round the input limbs to binary64 and clear the low result
limb. `remquol` reads the quotient pointer from the PPC stack parameter area.

Run `carbonlib/tests/check-corefoundation.ps1 -Group Toolbox` to check Toolbox
and MathLib in one app. Add `-ToolboxMath` to run only the math sections when
diagnosing a failure. The generator uses the local
System 9 reference under `references/disasm/system9` to load the saved MathLib
and MathLibGlobals fragments. The math checks sample finite arithmetic
within a numerical tolerance, compare specific IEEE values exactly, and
check PowerPC exception state, low-limb rounding, and pointer outputs on
both engines.

The archive supplies all 47 Math64Lib re-exports. The original PPC ABI passes
wide integer results through a hidden guest pointer and wide floating results
through f1 and f2. The native routines preserve signed and unsigned comparisons,
the seven-bit shift count, division by zero, optional remainder outputs, and
truncation and saturation of double-double inputs. The combined Toolbox app
loads the Math64Lib resource from the System 9 reference and verifies both
bindings, including aliased division outputs and values that need both floating
limbs. Its 18,828 checks passed on each PPC engine.

The classic File Manager group implements 89 parameter-block entry points and
16 convenience calls. The live register trap receives the parameter block in
A0, the OS trap word in D1, and the HFSDispatch selector in D0 when applicable.
The convenience calls build aligned temporary parameter blocks and copy their
results in guest byte order. The combined Toolbox app verifies every new entry
and performs file and directory operations inside its fixture directory. It
also checks a short read and an asynchronous completion through installed
traps. The full Toolbox app passed 19,181 checks on each engine.

This group adds 18 direct MyABCD imports. Before the application event wait,
MyABCD made 29 calls through six of these entries on each engine.

The archive implements 174 more InterfaceLib re-exports through live Toolbox
vectors, including 38 Apple Event Manager entries. Mixed Mode reads all PPC
arguments from the native frame and supplies the dispatch selector separately.
The Toolbox app checks every entry with replacement PPC callbacks, including
the ninth call argument. Installed Apple Event tests cover descriptor lists,
keyed parameters, and no-reply delivery to a handler in the current process.
Deleting an item now preserves the following descriptor bytes and reports
invalid indexes. The Toolbox app passed 20,842 checks per engine.

The batch added 50 MyABCD imports, six of which saw 14 calls per engine before
the application event wait.

The Toolbox manager group adds 124 InterfaceLib re-exports. Its entries use
live traps for lists, icons, the color picker, palettes, the Script Manager,
TextEdit, sound, windows, menus, dialogs, and pictures. The existing Toolbox
app checks argument and result transport for every entry, including the
ninth argument plus a dispatch selector. It also exercises installed list,
icon, color, palette, Roman script, and sound state operations. The Toolbox
app passed 22,288 assertions per engine, and the fallback app passed 71 per
engine.

The FSSpec group adds twelve CarbonLib entry points. `FSMakeFSSpec` uses
ROMAW's existing path resolution and preserves overlapping filename input.
The other entries build parameter blocks for the live File Manager traps.
The Toolbox app checks missing leaves, Finder metadata and script fields,
fork opens, locking, renaming, catalog moves, and cleanup. It passed 22,318
assertions per engine with these checks included.

MyABCD reached its application event wait on both the interpreter and JIT.
Each startup made six calls to the new `GetScriptManagerVariable` entry and
six calls across `FSMakeFSSpec`, `FSpOpenDF`, and `FSpGetFInfo`.

The classic utilities group adds 25 APIs covering memory moves and resident
memory requests, low-memory event and timing state, Roman script variables,
fixed-point conversions, and offscreen graphics. The Toolbox app checks
register and selector transport, overlapping copies, fixed-point rounding and
saturation, and GWorld allocation and resizing. It passed 22,438 assertions
per engine. The fallback app passed 71 assertions per engine on the same
ROMAW build.

MyABCD reached its event wait on both engines. Each startup made 28 calls
across eight entries from the classic utilities group.

The QuickDraw geometry group adds 27 APIs. It follows the System 9 NQD point
and rectangle routines, including packed Points, signed coordinate rounding,
the QuickDraw error state, and the shared random seed. The Toolbox app checks
input and output aliasing, monochrome and color backing origins, pattern
origin storage, and region coordinate round trips. It passed 22,497 assertions
per engine, and the fallback app passed 71 per engine.

MyABCD used the native `SectRect` on each engine before reaching its event wait.

The application manager group adds 49 APIs covering classic menu, window,
dialog, resource, folder, and handle operations. Its menu mutations work on
the installed Menu Manager's handles, including when a Pascal title or item
string grows. `GetIndString` bounds each entry against the loaded resource
and clears the output for truncated input. The Toolbox app checks live
menu edits, window lifecycle, handle locking, and a private resource file
alongside argument transport. It passed 22,905 assertions per engine, and
the fallback app passed 71 per engine.

MyABCD made 65 calls across eleven entries from the application manager group
on each startup.

The Classic Collection Manager covers 42 CarbonLib APIs, including all 39
CollectionsLib exports. Guest handles and flattened streams use the System 9
Collections layout. Full and partial flattening transfer persistent items with
the original callback chunk sizes. Unflattening clears the collection on stream
errors and checks directory and payload bounds. `GetNewCollection` reads bounded
`cltn` resources through the installed Resource Manager. The existing Toolbox app
checks native and saved streams in both directions, callback chunking, attribute
filters, and malformed handles and resources. It passed 23,035 assertions per
engine, and the existing fallback app passed 71 per engine.

MyABCD reached its event wait on both engines with this group installed.

The Menu Manager group adds 18 CarbonLib implementations for item enabling and
application and system property storage. Three menu bar visibility APIs now use
the shared Menu Manager from the archive. Classic and Carbon item state agree
across the 31-item mask boundary, including after insertion and deletion. Menu
drawing, selection and keyboard shortcuts use the shared state. Properties
retain their attributes on replacement and move with their items. The existing
Toolbox app checks big-endian sizes, bounded reads, property namespaces, long
menu item state and desktop region restoration.

The Control and Window Manager group covers 36 more CarbonLib exports. It
uses live classic controls for activation, control IDs, command IDs and the
installed getters and setters. It also connects Carbon keyboard focus to
dialog edit items and to focus-aware control procedures. User panes can register
idle, activation and focus callbacks using control data tags. The native data
calls also support size queries, classic Appearance control data and CDEF data
messages. Window bounds and regions cover hidden content, structure, update and
global port geometry. Specialized window regions use the saved WDEF path.

The ATSUI group adds 18 native archive entries for style creation and disposal,
style attributes, feature and variation queries, layout refcons, text location,
transient matching, soft breaks and font fallback settings. The wrappers operate
on the CarbonLib 1.4 layouts. They use the saved glyph-engine allocator, base
constructor and tag-specific workers where they share state with the real ATSUI
companion. The real library also handles unrecognized companion profiles, cold
startup, shaped-cache invalidation and explicit fallback font lists.

Use `-ToolboxATSUI` with `carbonlib/tests/check-corefoundation.ps1` to run
ATSUI in place of Toolbox in the shared real-library app. Use `-Group ATSUI`
for a focused run of that app. When explicitly requested together, ATSUI and
Toolbox run in separate instances of the reporting app. The ATSUI font
fragments initialize at process startup and change the menu drawing path
exercised by Toolbox. The app explicitly activates the bundled Helvetica face so the
saved and native implementations can operate on the same styles and layouts.
Each engine passed 108 assertions, including 70 ATSUI scenarios. Automatic
activation and resource ID conflict resolution for font suitcases remain open.

The next group adds 20 ATSUI implementations for text layout creation and
copying, pointer and handle attachment, run styles, layout and line controls,
soft breaks, and FOND font lookup. Native and saved operations share the same
layout records. The grouped checks exercise movable text handles, copied
layouts, explicit line overrides and font ID lookup. Glyph generation,
measurement, highlighting and drawing continue through the real library.

The archive also supplies 10 PowerPC FixMath wide operations through both
CarbonLib and InterfaceLib. These support the original ATS font scaler.
The Toolbox app checks full-width signed products, carry and borrow,
arithmetic and rounded shifts, division with optional remainders, overflow
and square roots. The reference is AppleÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢s [QuickDraw GX Mathematics chapter](https://dev.os9.ca/techpubs/mac/pdf/QD_GX_Environment-Utilities/Math_Functions.pdf).

The next 79 exports cover Component Manager, classic memory and time services,
TextEdit, Dialog Manager and QuickDraw. The 34 component entries share
ROMAW's registration and instance state. Default selection changes
enumeration and default opens. The registry honors duplicate suppression and
registration after existing components. Component callbacks receive packed
parameters in Pascal stack order, and component information accepts either a
registration or an open instance. [Apple Component Manager reference](https://developer.apple.com/documentation/coreservices/carbon_core/component_manager)

The other entries cover guest TextEdit hooks and scrap fields, 64-bit clock
output, calendar and heap operations, descriptor initialization, and live
Toolbox vectors. The Toolbox section of the shared app checks vector argument
and result transport, real handle and clock operations, component search
order, callback payloads, and registration from the app's resource file.
The two reporting apps passed 61,908 assertions on the interpreter and JIT.
MyABCD reached its event wait on both engines.

Fourteen ATSUI layout entries handle cache invalidation, text edits, line
controls, cursor navigation, highlighting and line breaking. The native
entries validate ranges, manage layout locks and resolve pen coordinates.
The saved workers continue to calculate glyph positions and update layout
storage. The existing ATSUI profile includes differential cases for these
entries and needs no additional regression application.

Twenty ATSUI font catalogue entries enumerate installed font IDs, map them back
to FOND families, copy and search names, and query features, tracking and
variations. Table queries use FontObjects' reported length. Catalogue enumeration
and name searches filter fonts through the original scaler registry. The shared
ATSUI fixture compares status and output bytes with the saved catalogue.

Seven ATSUI glyph entries resolve style indexes, duplicate and release style
attribute groups, query baselines, curve type and glyph information, and load
glyph vectors.
The native code owns range and output validation, style index insertion and
curve selection. The original renderer still shapes nonempty vectors. The
shared ATSUI regression includes cross-implementation style group disposal.

The glyph metrics queries use the saved scaler and glyph cache, then convert
their fixed-point results to the caller's ideal or screen metric records in
native code. Screen metrics honor the original antialiasing override and size
threshold. The glyph bounds entry validates the layout range and dispatches
the retained geometry worker. The shared ATSUI fixture compares status and
output bytes across both implementations.

Character conversion expands scaler glyph IDs into guest glyph-vector records,
retains character byte offsets and style indexes, and retries the symbol-font
private-use mapping. Curve path queries copy contours out of the original
glyph cache and convert their coordinates from fixed point to guest floats.
The shared ATSUI fixture compares both record formats with the saved APIs.

Quadratic and cubic outline queries walk cached paths in native code. They
resolve implied points between adjacent off-curve controls, apply the glyph
bearing, and dispatch the caller's path callbacks with guest float records.
The cubic query converts quadratic control points before invoking its cubic
callback. The shared fixture compares callback counts, coordinates and status.

Style archive copy and paste preserve the movable handle state. Copy uses the
saved version-zero encoder. Paste walks the serialized attribute, feature and
variation records within the handle extent before applying them. The shared
fixture round-trips archives across both implementations. The native decoder
advances past both halves of each feature pair so subsequent variation
records start at their encoded offset.

Continuous attribute queries handle empty ranges and single-run styles in
native code. Font matching skips empty ranges and validates the run boundaries
before entering the retained font matcher. Multi-run attribute merging still
uses the saved worker.

Glyph information drawing rebuilds the vector in native code, preserves glyph
order and style indexes, and passes the converted origin to the saved
rasterizer. The offscreen drawing checks reuse the shared ATSUI app.

The four type-selection entries share ROMAW's Roman search record, prefix
collation and item callback with the classic Script Manager. CharacterType
classifies ASCII Roman characters in native code and uses the saved library
for other characters and scripts. The Toolbox profile includes the PPC item
callback, the one-based result, a record-size guard and character classification.

The ATSUI renderer group adds native entry points for ATSUDrawText,
ATSUGetTextHighlight, ATSUMeasureText and ATSUMeasureTextImage. They validate
layouts, character ranges and highlight regions, and resolve the current-pen
sentinel before calling the CarbonLib 1.4 workers. The original ATSUI workers
still shape and rasterize glyphs. Other ATSUI profiles retain the original
public entry points.

The 32-bit heap now rounds physical handle storage to a quadword and preserves
the padding when moving handles. FMSharedCaches uses those bytes when reading
short TrueType fragments. The renderer shares font antialias preferences
through FontDispatch and reads the live A5 QuickDraw globals. The native NQD
RSect companion intersects packed rectangles and reads variadic pointers
from both registers and the guest stack.

The existing ATSUI regression app compares measurement outputs, highlight
bounds and counts of changed offscreen pixels against the saved entry points.
The three grouped applications passed 62,716 assertions across the interpreter
and JIT. MyABCD reached its first event wait in both engines.

The File Manager now handles PBMakeFSSpecSync and PBMakeFSSpecAsync through
HFSDispatch selector 0x1b. The asynchronous form writes ioResult and calls
the caller's completion routine. The private CFM FragFindOwnerOfPC service
looks up an address in the active PEF images and returns its section index and
extent. Existing Toolbox tests check successful and failed owner lookups and
exercise both file-spec entry points in the same reporting app.

The real Printing Manager completed basic settings and page-format round
trips. PMCreateSession looks up the name in the System file's STR -8192.
ROMAW's resource names Printer, while the original System 9 resource names
LaserWriter 8. The PowerPC bridge now routes PrOpen, PrClose, PrError,
PrSetError, PrintDefault, PrValidate and PrGeneral through the live PrGlue
vector. With the real driver installed as Printer in Extensions and the System
Folder of an isolated profile, the real library creates and releases a session.
The native Printing Manager group supplies 100 PowerPC entry bodies for print
settings and page formats, session and printer queries, packed dialog fields
and Core Foundation conversions. The wrappers share the initialized CarbonLib
1.4 tickets and driver workers. Other library layouts and unsupported printing
APIs use saved public entry points. The StdCLib bridge bounds strcat and strncat
against guest memory before changing the destination.

Run `check-corefoundation.ps1 -Group Printing -ProfileDirectory <profile>` to
compare native and saved entry points in the existing reporting app. Supply an
isolated profile with CarbonLib and the printer resource named by the System's
STR -8192 in Extensions and the System Folder. The checks use ticket data,
session state and printer queries without sending a job to a printer.

Include `Printing` in the `-Group` selection to run it in the same app as the
routine groups when the profile has the required printer driver. The
Toolbox group checks the live PrGlue selectors and PrJobMerge's job-settings
transfer. The Printing group compares the native entries and saved CarbonLib
through both canceled dialogs, document and page transitions, PostScript
sections, and graphics context lookup using a guest printer fixture.

Thirteen more native Printing Manager entries control the document and page
lifecycle, modal Page Setup and Print dialogs, PostScript sections, and document
format generation. The native control flow shares the installed driver workers
and ticket storage with saved-library calls. Other library profiles retain the
saved public entry points.

The ticket wrappers convert scale between percentages and the stored factor.
The unadjusted paper setters normalize the rectangle origin and preserve its
width and height. Legacy Pascal job names and Core Foundation conversions
share the initialized print tickets, allocators and preferences domains.
Legacy document, page and dialog calls use the same session transitions as
their session-aware counterparts. The session data accessors share the live
dictionary and preserve the driver's recorded error. The default printer
queries and old print-record conversions use the current driver and ticket
state, including the optional null session in the conversion APIs.
Staged Page Setup and Print dialogs validate the print tickets and use the
live classic driver selectors. The grouped fixture checks the returned
dialog record, cancellation, and callback descriptor delivery.
Print data and XML property-list file calls use Classic File Manager paths
and return Core Foundation objects. The grouped fixture confines its file
operations to its application directory.
Raw print ticket operations share the live dictionary lock flag, use Core
Foundation retain counts, and verify the returned item's type after the
existing ticket search.

The two reporting apps passed 62,370 assertions across both engines. MyABCD
reached its first event wait under the interpreter and JIT.

## File Manager

The File Manager group adds 102 entries for FSRef creation and comparison,
catalog and volume information, Unicode catalog changes, flat catalog
iteration, fork I/O, and the corresponding synchronous and asynchronous
parameter blocks. MyABCD imports four of the new entries: FSpMakeFSRef,
FSGetCatalogInfo, FSGetVolumeInfo, and FSFindFolder. Saved CarbonLib File Manager
wrappers can call the same native parameter-block handlers. Windows references
use host file IDs, when available, to find files again after a rename or move.

The shared Toolbox app checks copied references, Finder metadata, composed
MacRoman names, fork data and short reads, volume and catalog records, flat
iteration, and asynchronous completion. Unicode names and fork I/O use the
existing classic catalog and fork limits. The broad HFS Plus Gestalt capability
remains disabled while additional filesystem operations retain fallback.
Toolbox and Printing passed 24,847 assertions per engine in the same app.
MyABCD reached its first event wait under the interpreter and JIT.

The Carbon Event Manager adds 40 native entries for event records, parameters,
queue membership and ownership, event targets, handler chains, timers and
event clocks. The entries share CarbonLib 1.4 event objects and use its
private allocator, parameter-list, dispatch and platform workers. Other
CarbonLib layouts retain saved public entry points. Event loop pumping and
modal waits still use the saved implementation.

The shared Toolbox app compares native and saved event records, checks parameter
truncation and independent copies, posts and removes events, dispatches through
handler chains, and exercises timer delivery through the saved timer scheduler.
It passed 24,377 assertions per engine. MyABCD reached its first event wait in
both engines. The whole saved event-loop test timed out, so the timer fixture
exercises its scheduler directly.

## Text conversion

The archive adds 18 TextCommon and UnicodeConverter entries for encoding IDs,
converter creation, disposal and reset, fallback registration, offset mapping
and Pascal string conversion. Native converter records share the installed
UnicodeConverter and TextCommon tables. The saved mapping and codec workers
continue to load resources and perform table-dependent byte conversion.
The default MacRoman mapping returned an unsupported-encoding error from both
implementations in the test profile. The grouped app verified explicit
MacRoman mappings and encoding-based Windows Thai converters.

Ten Carbon Scrap Manager entries share the saved flavor list, scrap generation
and promise keeper with the classic Scrap Manager. The grouped Toolbox app
checks stale references, bounded reads, flavor metadata and a promise fulfilled
by a saved-library callback. Its fixtures use sender-only flavors.

Textension's scrap availability uses its own private-scrap and public-flavor
generation caches. Its conversion entries retain Textension's virtual workers
and return either the worker error or the fragment's conversion status. The
grouped Toolbox fixture substitutes guest methods to check these paths without
changing the Windows clipboard.

Native `TXNInitTextension` uses CarbonLib's lazy fragment loader, then initializes
the known Textension fragment's run list and global state. It keeps the saved
allocation, script settings and format registration workers. The grouped Toolbox
fixture checks first initialization, repeated initialization through both entry
paths and subsequent document creation.

`TXNNewObject` handles its output parameters, allocation, frame geometry and
activation in C. The saved Textension constructors and layout workers still
create the editor and surface. The grouped Toolbox app checks required and
optional output behavior, the stacked frame context and scroll bar geometry.
`TXNTerminateTextension` preserves CarbonLib's lazy loading and installed
termination hook. The installed Textension 1.3.2 hook returns immediately and
leaves live documents intact, which the same fixture checks before continuing.

Native `TXNSetData` handles plain data in the active storage encoding and empty
replacements. It builds the range and data records in C and uses the shared
Textension edit worker. Cross-encoding, file-offset translation and embedded
media replacements use the saved entry. The same Toolbox app checks invalid
ranges, empty edits, explicit deletion and saved-library readback.

`TXNSetFontDefaults` shares the original per-script font table and style
worker. The native entry updates typing attributes for empty documents and
restores the selection and editor mode after updating a populated document.
The grouped Toolbox app checks both paths through native and saved entries.

`TXNSetTypeAttributes` handles range translation and normalizes font names,
sizes, faces and colors in C. It sends the resulting records through the
original Textension edit engine. The Toolbox fixture compares range styling,
point-size rounding and invalid-attribute results through both entry paths.

`TXNPsuedoUnicodeKeyDown` prepares the Unicode key event and translation record
in C and uses the saved translation and edit methods. The Toolbox fixture
checks legacy-mode rejection and the supplied and synthesized event paths with
a guest translation method.

`TXNCopy` reads the active selection and sends it to Textension's private
scrap writer. `TXNCut` and `TXNPaste` use the same undo and edit workers as
`TXNClear`, with the original paste filter. The Toolbox fixture checks copy
dispatch through a guest scrap writer and read-only cut and paste behavior.

The focused Textension group also exercises encoded search hits and misses,
the PPC search callback, file insertion, and short and long CFString input.
`TXNDrawCFStringTextBox` uses the allocated character buffer directly for long
strings. The saved routine dereferences that allocation as a handle, so the
long-string case exercises the native branch and the short cases compare both.

The four `TypeSelect` exports share ROMAW's classic type-selection record
and invoke the supplied item callback through the PPC bridge. `CharacterType`
handles Roman ASCII in the archive and uses the saved entry for other input.
The focused group checks prefix matching, the callback result and adjacent
guest memory.

Fifteen Text Encoding Converter exports use native catalogue, Internet-name,
converter context and serial streaming code. Installed encoding plugins and
the saved private codec worker still perform table-dependent conversion. The
grouped Toolbox app compares bounded catalogue output, live converter handles,
and UTF-8 to UTF-16 streaming with the saved fragment.

The Unicode-to-text streaming wrapper now handles context state, mapped
offsets and fallback dispatch in C for `ConvertFromUnicodeToText` and
`ConvertFromUnicodeToPString`. It uses the saved mapping and codec workers.
The archive also releases Unicode-to-text run contexts. The grouped tests
compare exact byte counts, continuation, short buffers, custom fallback
results, and the saved converter's results for UTF-8 input. The installed
reference returned an unmappable-sequence error for the tested UTF-8
accented input with the explicit MacRoman mapping.

The archive can also change the mapping on an existing Unicode converter and
find safe truncation points for text and Unicode input. Mapping changes load
replacement tables before releasing the old ones. Reverse truncation reads the
saved mapping tables through a snapshot of the converter context. The grouped
tests cover rejected updates, odd UTF-16 bounds, and a stateful table with a
two-byte character split across the requested boundary.

Twelve more Text Encoding Converter entries enumerate subencodings, direct
destinations, and mail and web encodings, and manage sniffer contexts. They use
the installed plugin tables and callbacks. The Toolbox app checks bounded
catalogue output against the saved fragment. Its sniffer cases combine an
unsupported candidate with a test plugin that exercises context reset, sample
scoring and result ordering.

Four additional exports create, stream and flush one-to-many converters and
report the installed TEC version. The archive shares converter handles and the
saved encoding worker with the real fragment. The grouped app checks
Unicode-to-Thai conversion, run counts and the version record against that
fragment. The loader runs the real initializer to populate the plug-in registry
when CarbonLib binds every TEC import to a native entry.

The Window Manager uses native code for properties, persistence attributes, and
default and cancel button selection. These APIs share the saved CarbonLib
collections and control dispatch. The Toolbox app reads and mutates the same
window from both implementations, including removing properties and clearing
designated buttons. The Control Manager uses the same operations for its own
properties, allocating each child collection in the owning control's zone.
`CheckMenuItem` updates the mark through the installed trap and invalidates
the item through Carbon's menu dispatcher.

Menu command IDs travel with their items when the menu changes. Native command
queries search the supplied menu or installed menu bar in item order and
descend into submenus. Enable, mark and property APIs act on the first
matching item and use the same state as the corresponding item APIs.

The menu CFString APIs copy, set, append and insert text using the saved menu
properties and the classic Pascal text used for display. When the reference
run converter is unavailable, characters representable in the system
encoding can still be stored with their CFString. Item encoding follows
its item through insertions and deletions, alongside the command ID.

Window CFString operations share CarbonLib's regular and alternate title
properties. The regular title also updates the Pascal title used by the
classic Window Manager. Setting that title through CarbonLib's classic
`SetWTitle` clears the stored CFString so subsequent copies see the new text.

The window class and attribute APIs derive classic window metadata and cache
it in the shared Carbon record. The same record holds owner count and
activation scope. Window class changes apply the installed class
attributes and regroup the window through CarbonLib.

Class traversal uses CarbonLib's shared window list and preserves the
installed fragment's visibility rules. Attribute changes update the shared
record and classic window definition, using the saved drawing workers. The
optional newer WindowsLib backend still uses the saved CarbonLib path.

## Carbon event handling

The native Carbon `WaitNextEvent` uses the saved event manager's wait depth
and its cached InterfaceLib entry. This preserves classic event trap patches
and the state shared with Carbon's private event loop. Other CarbonLib
versions keep their own wait wrapper.

Carbon's native `Gestalt` preserves the InterfaceLib CPU and architecture
responses. It applies the installed CarbonLib's menu and folder capability
flags and uses its saved worker for ATSUI version and feature queries.

The Carbon archive now owns ROMAW's cooperative Thread Manager. CarbonLib
and ThreadsLib imports share the same scheduler and premade stack pool.
Pool queries count available stacks, and disposing a thread can return its
stack to the pool. The disposal result goes to the output registered by
`NewThread`. Pool support covers cooperative PowerPC threads. Task references
are shared by threads in the same ROMAW launch. Task-scoped readiness
requests take effect at the next scheduler selection. Stack-space queries use
the live application stack or the saved stack pointer of a suspended thread.
PowerPC callbacks can choose the next cooperative thread and observe
switches, termination, and debugger creation and disposal notifications.

`ReadLocation` and `WriteLocation` use the live extended-PRAM traps. ROMAW
stores the twelve-byte record in its system parameter file after the
existing system preferences. `ReadDateTime` and `SetDateTime` use the live
clock traps. On Windows the clock write adjusts the guest clock for the
current ROMAW process.

The Carbon Sound Input manager enumerates Windows waveform devices and
captures bounded mono or stereo PCM into Macintosh buffers. It supports
synchronous capture, asynchronous completion, pause and resume, abort status,
sample settings, and byte/duration conversions. The bundled `-sdlaudio test`
mode provides the grouped regression with a deterministic input device.
Capture-to-file, unbounded recordings, and interrupt-time sample callbacks
still require further implementation.

The time-base conversions and arithmetic share ROMAW's 25 MHz PowerPC
clock. They preserve the System 9 duration precision switch, signed-wide
forever value, and operand order for clamped subtraction. The existing Toolbox
app exercises both scalar and hidden-pointer results, carry, underflow and
duration boundaries.

## Guest globals and runtime state

The archive supplies guest data for the CoreFoundation epoch constants and the
MathLib constants `pi` and `_FE_DFL_ENV`. CarbonLib and companion imports share
guest addresses within a PPC launch. Values regressions compare the epoch
constants against the installed CoreFoundation PEF, which still provides
object-valued globals and callback tables.

QuickDraw port accessors now share ROMAW's GrafPort state for geometry,
region copies, recording queries, pen settings, drawing procedures, and
patterns. The cursor setter updates the per-application QuickDraw globals.
The Carbon port lock APIs use the same nesting count as LockPixels for
movable GWorld storage. Screen ports already have stable pixel storage.
The Toolbox app exercises the setters, handle ownership, and nested locking.

The native Appearance cursor calls select QuickDraw cursor resources and
apply watch and spinner frame indices from the System 9 tables. Color
cursors, theme-specific installation options, and missing resources use the
saved Appearance fragment when available. The Toolbox app tests the cursor
bytes delivered to the live setter without moving the host cursor.

The native Font Manager enumerates visible FOND families and size-zero legacy
FONT/NFNT resources. It shares classic family name lookup, detects changes to
resource maps during enumeration, and preserves the Resource Manager's load
and error state. Filtered or nondefault-scope queries use the saved Font
Manager. FOND instance iteration reads associations for bitmap and sfnt
faces, shares stable identifiers across bitmap sizes, and supplies native
format, generation, container, and bounded sfnt table queries. Foreign font
identifiers keep their original Font Manager resolution. The unfiltered
catalog iterator yields each resource-backed face once and shares generation
tracking with family and font queries. Font activation and family text
encoding remain on their existing resolution paths.

Window modality queries and changes share CarbonLib's window properties with
the installed library. The Toolbox app checks the stored modality, unavailable
window, and system-modal flag across both implementations.

The application-modal APIs manage the shared modal record, temporary window
ownership, and exit status. The native loop checks cancellation between
bounded waits so a timer can end it with an empty event queue. ReceiveNextEvent
uses the installed wait worker and selects matching events in the native
queue code. Queue flushing updates the same priority lists and releases their
event references. Comparator searches share the installed iterator bookkeeping
so callbacks can work with the live queue. The ROMAW bridge supplies
WindowsLib floating-window activation and honors the shared owner count
during disposal.

The international comparison, date and resource calls use ROMAW's Pack6
implementation. Pascal and explicit-length strings share the same collation
workers, including the installed Roman locale and supported sorting
resources. Date and time formatting share the classic locale resources across
32-bit and 64-bit timestamps.

Sound Converter streams decode MACE 3:1 and 6:1 and convert mono or stereo
8-bit offset-binary and 16-bit PCM. The converter retains MACE predictor and
sample-rate phase across buffers, and supports overlapping input and output.
Callback-driven fills use the same conversion state and retain whole PCM frames
that do not fit in the caller's buffer. A later fill or EndConversion drains
those frames. The grouped Toolbox app checks callback invocation, short fills,
leftover data and output guards. Unsupported formats and foreign converter
references use the installed SoundLib when available. Converter info selectors
still use their existing resolution paths.

AEInteractWithUser uses the live Pack8 vector. Native Apple Event dispatch
tracks the current handler's event, and queued sends to the current process
carry the sender's interaction preference. A handler can interact when the
preference permits it and the guest is in the foreground. ROMAW's guest
Process Manager exposes a single foreground application.

IdleUpdate, UpdateSystemActivity and GetLastActivity share the guest Power
Manager's tick bookkeeping through patchable OS vectors. IdleUpdate returns
the low-memory Ticks value and records overall activity. Input events record
user activity, and explicit updates record their selected activity type.
These calls track guest activity timestamps. Portable sleep, display dimming
and hardware power management still use their existing resolution paths.

The seven public CFM re-exports use native Carbon argument handling and the
shared ROMAW PEF loader. Unnamed disk and memory fragments can be opened
without requesting a main address. Malformed arguments return Macintosh
errors to the caller, and a failed named lookup can report the missing
fragment in the Pascal error string. A connection to an installed PEF still
finds that fragment's actual exports, including its real CarbonLib fallbacks.

ExitToShell follows the live Toolbox vector. A patch can return to its
caller. Reaching the original vector stops the PPC application through
ROMAW's normal fragment teardown. The grouped Toolbox app exercises both
the patched calls and default termination.

CarbonLib and CoreFoundationLib imports share per-launch native callback
records for arrays, sets, bags and dictionaries. The records expose PPC
descriptors for CF retain, release, description, equality, hashing and
string copying. Explicit lookup on a real CFM connection still returns the
original fragment's record. The grouped collection checks exercise both
records, including ownership and copying a mutable string.

Constructors for the CoreFoundation 1.4 layout use the original table
identities for standard callbacks so native and saved collections compare
equal. Inline arrays and small deques release ranges in C. Large
CFStorage-backed arrays use the original release traversal. Custom callbacks
run through Mixed Mode.

The verified CoreFoundation 1.4 system allocator uses native storage workers
for allocation, reallocation, deallocation and preferred size. CarbonLib
and CoreFoundationLib imports share the live allocator word and singleton.
ROMAW installs the workers in the original transition vectors after
CoreFoundation initializes, preserving allocator and descriptor addresses.
The workers use the original Memory Manager imports and tagged pointer and
locked-handle layouts. The grouped Frameworks checks invoke the workers
directly and exchange allocations with saved PPC routines. Custom allocator
callbacks and other CoreFoundation profiles keep their existing dispatch.

Navigation Services option initialization and reply cleanup now run natively.
The option routines match the reference write extents and preserve reserved
bytes. Reply cleanup releases legacy Apple Event descriptors, locked file
translation handles and Carbon save-name strings. Legacy `NavGetFile`,
`NavChooseFile`, and `NavPutFile` can use Standard File for single-file modal
dialogs with type popups and translation disabled. The native open path
passes up to four types. A successful call returns an Apple Event list with one
packed FSSpec. Save replies also provide the replacement flag and script code.
Options requiring the full Navigation Services interface use the existing
PEF path. The grouped Toolbox app supplies deterministic Standard File replies
to verify selection, cancellation, and disposal.

The Carbon create/run APIs also support app-modal open, choose-file and save
dialogs with those options. A dialog keeps its own type list and filename,
records the user's action, and creates an independent reply on request. The
Carbon save reply contains the destination directory and a separate retained
filename. Modal and sheet options outside this implementation remain backed
by NavigationLib.CL. The grouped fixture tests the dialog lifecycle with
scripted Standard File replies and a scripted directory lookup.

URL Access can stream a local `file:` URL into guest buffers using `URLOpen`,
`URLIdle`, and the buffer and state APIs. The grouped Toolbox checks cover
a multi-buffer transfer, progress callbacks, release, completion and abort.
An installed URLAccessLib still owns the reference when available. The native
local path supports an open with no destination file and zero flags.

The Internet Config calls use ROMAW's Component Manager to find a registered
`PREF`/`ICAp` component and send its classic selector and parameter block.
Startup checks the component version and supports the older configuration
message. Startup errors close the instance, and shutdown closes it even when
the final component message reports an error. When no native registry match
exists, startup can call the saved CarbonLib routine. Preference data and URL
handling come from the component. The grouped Toolbox checks exercise all 40
entry points against a PPC fixture in the same application.

The Mixed Mode invokers now read callbacks and arguments from the PPC
parameter area when a signature exceeds the eight argument registers. The
native archive adds 13 Pascal invokers and 11 C-stack or register-based
invokers. The grouped Toolbox checks compare the saved and native entry
points with PPC callbacks, and exercise raw 68K C and register callbacks.
The remaining special-convention TextEdit hooks use their existing fallback.

The FSRef Alias Manager calls use the classic AliasHandle format and live
Alias Manager dispatch. Native calls resolve FSRef identities to catalog
records and convert resolved targets back to FSRefs. The grouped Toolbox
checks cover creating and updating aliases, identifying files and folders,
and resolving a Finder alias with an actual resource fork. Foreign FSRefs and
nonzero mount flags retain the saved CarbonLib path.

FSRef Resource Manager calls can create and open resource maps in the standard
resource fork or in the data fork. The classic and FSRef open-file queries
distinguish a fork opened through the File Manager from a map in the Resource
Manager chain. Foreign FSRefs and other named forks keep the saved CarbonLib
path. The classic FSSpec data-fork APIs create a map in an empty or missing file
and open it through the same bounded reader. The grouped Toolbox checks cover both
fork formats, persistence, writable data-map reuse, and resource-chain traversal.
Detached maps preserve their file references and loaded resource handles.
Explicit update and close operations still work on them. Orphan resource-fork
opens preload into the system heap and stay outside the active chain until
an application inserts them at one of the public Resource Manager locations.

The classic Toolbox and Appearance re-exports share ROMAW's existing PPC
argument marshaller and C implementations. The grouped Toolbox fixture calls
these entries through CarbonLib and checks event queueing, menu shortcuts,
dialog item lists, control embedding, image content, and mixed-width outputs.
Appearance rendering and styling follow ROMAW's classic controls and menus.

Data Browser version-zero callback tables and typed item-data accessors use the
CarbonLib 1.4 packed layout, including the item type tag and the text getter
and setter's retain behavior. The grouped Toolbox checks cover field widths,
date overflow and invalid draw-state transitions. Live control operations and
setters that acquire menu or icon references continue through DataBrowserLib.

The Component Manager sends native messages for 73 scripting APIs, 13 sound
component APIs and nine Text Services APIs. The adapters pack Pascal parameters
and carry the result from the registered component. Foreign references and
patched dispatch can use the saved fragment where available. The application
still supplies or loads the corresponding component implementation.

The fixed-point multiply, divide and ratio routines preserve CarbonLib's
rounding and saturation. Decimal conversion preserves Pascal string lengths,
the original parser's 32-bit wraparound and the bytes beyond an output string.

ATSUI style cloning, comparison, attribute enumeration and edits now run
through native public entry points. Individual font feature and variation
changes use the existing style engine to manage storage. Layouts can copy
control settings and retarget a borrowed text pointer without changing the
selected text range. The grouped ATSUI checks cover both native and saved
style records.

## ColorSync and later Toolbox groups

The native ColorSync array converters cover RGB to gray, RGB to and from HSV
and HLS, and XYZ to and from fixed-point XYZ, Yxy, Lab and Luv. They preserve
packed guest strides, untouched output fields, and the original out-of-range
status. Conversion supports in-place arrays with either record width. The
eight HSV, HLS, Lab and Luv converters matched the original PPC fragment's
status and output bytes on 8,192 inputs each. The grouped Toolbox app checks
representative two-record arrays, clipping and untouched fields.

The ColorSync reference is the extension in `Software Installers/ColorSync`
on the supplied Mac OS 9.2.1 disc. The two forks were decoded from
`ColorSync Installer Tome` using the [rusty-backup Tome reader](https://github.com/danifunker/rusty-backup/blob/99b3b9ff1566b6e340a4ea1759f016da77cfacff/crates/arc-mac/src/tome.rs).
Their SHA-256 digests are
`5725a3a2ba6caa24b4c8b1cf5bd25df96ded54a447de3f399bd83c37bc7dc7c9` (data)
and `33d5957c39a1580b88c68cdf10128d563d9f3675dd7b2133c69e57e6aab70f58` (resource).
The original forks and their disassembly are local build assets.

The Alias Manager resolves file and folder aliases on mounted volumes for both
the classic FSSpec and Carbon FSRef paths. The documented no-UI option uses the
same resolver. Saved ColorSync calls the InterfaceLib entry point when opening
its temporary profile. The grouped Toolbox app compares saved and native
`CMUpdateProfile` on handle-backed and buffer-backed profiles and reopens the
result with the saved reader.

ColorSync profile descriptions use the ICC v2 `desc` tag in the live profile
directory. The native readers report full ASCII and Unicode lengths on
truncation, strip a UTF-16BE BOM, and use ASCII when a script description is
absent. The writer stages the encoded tag through the existing edit store.
Script codes for the current keyboard and system use the active Script Manager.

ColorSync profile copies to movable handles and bounded buffers serialize the
live tag directory, including pending edits. The native opener builds the
profile and directory handles from the serialized ICC v2 record. The native
flattening path sends the same ICC bytes
through the application's mixed-mode spool callback in 4 KiB chunks and
propagates its errors. Other copy destinations retain saved ColorSync.
The native unflattening path reads the ICC length from the stream and stages
the complete profile before updating the caller's movable handle or bounded
buffer. A truncated spool preserves the destination.

The native constructor creates an empty ICC v2 profile. The final close frees
its directory and owned in-memory edits while preserving the caller's backing
handle or buffer. File- and procedure-backed lifecycles use saved ColorSync.

Multiprocessing reports the guest's single installed and scheduled processor.
The block copy, clear and code publication calls use the same Toolbox and
ROMAW paths as the corresponding System 9 `MPLibrary` entry points. The
grouped Toolbox checks exercise overlapping copies, byte guards and changes
to executable guest memory. MP task and synchronization services continue
through the fragment fallback.

The TextEdit hook invokers use the Mixed Mode register conventions documented
in `TextEdit.h` and the argument widths in the CarbonLib 1.4 wrappers. The
grouped Toolbox checks compare PowerPC argument forwarding with the saved
wrappers and exercise 68K register inputs, condition codes and output pointers.

Navigation Services checks the guest System and Appearance Manager versions
and reads the version from the system resource map. The ColorSync version
query reports the 3.0.4 reference ABI, and the folder query finds or creates
the ColorSync Profiles directory through the Folder Manager.

The Control Manager keeps a full 32-bit value, minimum, maximum and view size
for each live control. Range setters clamp the value and mirror its sign and
low fifteen bits into the classic control record. Changes made through the
classic record are visible to subsequent Carbon calls. Native sliders and
scrollbars use the wide range for tracking and drawing, and scrollbars use the
view size to calculate the thumb length.

Embedded controls retain their sibling order across enumeration, reparenting
and disposal. Disposing a container also disposes its embedded descendants.
The geometry APIs return the current rectangle and baseline for classic
controls and dispatch the extended sizing and region messages for CDEFs that
advertise support. Classic scrollbar region queries use the live thumb bounds.

Window retain, clone, release and retain-count calls share the owner count
used by Carbon `DisposeWindow`. They use CarbonLib 1.4's property record when
the reference is loaded and keep the count with the Window Manager's auxiliary
record when it is absent. The grouped regression mixes native and saved calls
on the same window and checks that the final release closes it.

Navigation Services load and unload share CarbonLib 1.4's load count and symbol
table with the saved dialog implementations. Native code manages the load
lifecycle and uses the companion's private connection and symbol-table workers.

The classic utility entries cover Pascal string comparison, Roman case and
diacritic conversion, handle-based text replacement, clock and system
parameter access, palette resizing and animation, and TextEdit scrap transfers.
The string comparison and manager calls read the active Toolbox trap vectors.

Classic TextEdit activation, selection clicks, keystrokes, idle, disposal,
and auto-scroll flags use the live Toolbox vectors. When a record enables
inline text services, these wrappers use the installed CarbonLib entry.
Other TextEdit feature flags also use that entry when it is available.

## Data Browser and custom controls

## Data Browser

The native Data Browser property layer validates controls and required
arguments, narrows scalar fields, and packs the records sent to the control
definition. The reference control definition still owns item storage,
rendering, and selection.
The grouped regression compares the native and saved calls through the same
68K control definition, including argument bytes and output boundaries.

Native Data Browser action wrappers send the original CDEF messages for item
and container changes, traversal, editing, and column operations. Selection
queries read the original item-state bit. The live control definition still
handles storage and rendering.

Native `CreateCustomControl` allocates a normal control, applies the standard
initialization collection items, and dispatches initialization and subsequent
messages to the supplied control definition UPP. It disposes controls whose
initialization fails and propagates the full callback status. The grouped
regression exercises creation, subsequent messages and disposal through a
PowerPC definition.

Native `CreateDataBrowserControl` creates the initialization collection
and the live control, using the control definition supplied by the real
DataBrowserLib. The grouped regression creates a list-view browser with
a registered test icon and queries its style through the same CDEF.
`SetDataBrowserItemDataMenuRef` writes the guest menu field and retains its
reference through the native Menu Manager entry point.

## Menu ownership and attributes

The native menu ownership APIs read and update the same Collection Manager
records as CarbonLib 1.4. `DisposeMenu` and `ReleaseMenu` preserve a retained
menu until its last owner releases it. Final disposal uses the live
Toolbox vector so the installed CarbonLib patch cleans up menu-definition
and tracking state before the handle can be reused. The menu attribute accessors use the
same records and pass changes with drawing side effects to the saved
implementation. Keyboard modifiers, key glyphs and reference constants use
ROMAW's existing per-item storage.

`CreateNewMenu` allocates through the live Toolbox vector and applies
Carbon menu attributes. `InvalidateMenuSize` clears both cached dimensions.
The indentation and command-key wrappers validate item indices and expose
their packed fields through ROMAW's menu item system properties. They
also call the saved Menu Manager to update its records and invalidate layout.
The standard MDEF measures and draws item indentation and honors per-menu
font settings and mark-column exclusion. The font API retains CarbonLib's
shared font records and its optional Menu Manager override. Changing a font
or mark-column setting invalidates the cached menu dimensions.
The item attribute APIs share the saved Menu Manager's attribute updates.
ROMAW's native menu records retain explicit item and icon flags, and
the standard MDEF dims a disabled icon independently of its label.
Menu invalidation checks the live menu-bar flags and preserves the saved
tracking, pixel-cache, keyboard-cache, and enabling state.
The menu tracking accessor reads bounded legacy save records and uses the
live scroll coordinates for the topmost menu. The native redraw wrapper
recalculates invalid geometry, groups contiguous invalid items, and sends
the original menu-definition draw message with the saved tracking data.

Item font and icon handle accessors use the live MenuDispatch vector.
The standard MDEF uses per-item fonts and measures and draws attached
classic icons, small icons, color icons, and icon suites. The caller keeps
ownership of attached handles.

## Remaining MyABCD weak imports

The CarbonLib 1.4 CoreFoundation fragment returns null from
`CFTimeZoneCopyDefault` and `CFTimeZoneCopySystem`, zero from
`CFTimeZoneGetSecondsFromGMT`, and false from `CFTimeZoneIsDaylightSavingTime`.
Universal Interfaces 3.4.2 documents CFTimeZone as unavailable on classic
CarbonLib. These four weak imports keep their reference behavior and remain
outside the native coverage count. The other weak import,
`LSGetApplicationForInfo`, is absent from the local 1.4 export catalog.

The native theme font APIs resolve Carbon role aliases and read the Script
Manager's packed font and size selectors. They use live Font Manager and
QuickDraw traps and consult the saved Appearance font settings when available.

`SetControlColorProc` installs or clears a per-control color UPP.
`SetUpControlBackground` and `SetUpControlTextColor` pass the original message,
depth and color flag to that UPP. Declined requests continue through the
control hierarchy and the default window colors.

Native Icon Services stores icon family data independently of the caller's
handle and tracks owners across lookup, acquisition, unregister and release.
System lookup reads the `'isrv'` mapping when available and searches the
embedded System and the active System Folder's `System Resources` companion.
The original sound icon is resource `-20790` in that companion. Guest icon
family conversion supports chunk selection and copies raw element data.
Classic indexed icon families convert to and from ROMAW icon suites.
IconRef plotting, hit testing and region conversion use the suite renderer.
The Data Browser item setter retains its IconRef. Classic 16, 32, 48 and
128-pixel RGB family elements convert between unpacked xRGB handles and the
interleaved or run-length encoded ICNS representation. A malformed packet
leaves the caller's output handle intact. Classic RGB families use their
eight-bit alpha or one-bit mask for hit testing and region conversion.
Normal drawing composites RGB pixels over the active QuickDraw port with
its clipping and visibility regions. Indexed drawing also supports the
classic transforms. Newer icon formats and RGB transforms remain incomplete.

The system allocator binding still depends on the live CoreFoundation runtime
for object identity.
