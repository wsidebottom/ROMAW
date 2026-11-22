# Resplendence Classic startup

Automatic application partitions use the preferred byte count from the application's
`SIZE` resource (ID 0, falling back to -1). Explicit command-line and application
preferences retain precedence. Missing or invalid resources use the runtime fallback;
24-bit address limits and the declared minimum remain enforced. Resplendence's preferred
partition is 66048 KB and is allocated without a fixed per-application override.

Startup also requires nested Mixed Mode descriptors, classic international and queue
services, and cooperative PPC threads. ROMAW implements the cooperative threads on
the Windows UI thread with separate guest stacks and saved interpreter/JIT contexts.
Creation, yielding, state changes, critical sections, results and disposal are exercised
by the PPC thread probe in interpreter and JIT modes. The native scheduler fixture checks
cancellation, cleanup, allocation failures and floating-point context preservation.

This is a subset of the [Thread Manager API](https://leopard-adc.pepas.com/documentation/Carbon/Reference/Thread_Manager/index.html).
Preemptive threads and premade pools are not implemented. CFM-68K thread creation is
unsupported, and switching through nested 68K/native callback continuations is rejected.
The implementation contains no Apple executable code and requires no installed extension.

The startup investigation also found deletion markers hiding bundled System resources
in the local editable resource override. The original files were backed up, those markers
were cleared, and the other resource overrides were retained. Version-resource updates
now tolerate missing, unloaded or ungrowable resources without dereferencing them.

File opening now bridges StyledLineBreak, minimal full-path aliases, standard alerts
and Desktop Manager comment queries. GetProcessInformation accepts records at least
as large as the classic structure, including REALbasic's extended 130-byte buffer.
MathLib supplies bounded num2dec, dec2str, str2dec and dec2num adapters with the packed
42-byte decimal layout and the PPC floating-point argument slots.

Resource-view construction exposed two QuickDraw compatibility failures. A missing
saved clipping region now leaves the current clip unchanged instead of dereferencing
NULL. GetFontInfo passes the metrics-only null text pointer to custom measurement
callbacks. Portable pattern blits calculate negative row offsets with signed pointer
arithmetic; the former unsigned multiplication moved Y = -1 nearly 4 GB beyond the
bitmap on x64. Appearance clients can draw list-box frames and controls in the current
graphics port, including offscreen drawing.

Windows validation with the staged executable and automatic SIZE allocation opened
Resplendence's own application file, listed 578 resources and 6329 KB of data, entered
the Resources catalog, opened the three-entry ALRT list, and selected its second row.
No resource contents were modified or saved. Full editing/saving workflows and general
Thread Manager compatibility have not been established. The list headers still have
rendering defects; explicit larger-system-zone debug launches also showed an incorrect
green dialog palette, while the normal automatic-memory launch used white dialogs.

Targeted PPC file, callback, standard-alert, decimal and Appearance probes passed in
interpreter and JIT modes. Decimal conversion passed Linux ASan/UBSan, a native Windows
x64 fixture and 4000 binary64 round trips. Native QuickDraw fixtures checked font
callbacks, null/valid clipping copies and negative/zero/positive pattern origins with
guards. The Windows build/staging, generated-file checks, changed-source formatting,
file-header checks and PowerShell formatting checks passed.

A subsequent captured stop came from a NewGWorld request with pixel depth 24.
Unsupported depths now return cDepthErr (-157) before allocating any world/device;
missing required output/bounds pointers return paramErr. Default-depth requests also
validate the selected device depth. The Appearance probe checks eight invalid depths,
including 24, followed by successful 8/16/32-bit allocations in both PPC engines.
The rebuilt executable is staged. The user resumed the Open dialog during the manual
retest, so opening a resource editor after this correction remains unverified.

A later stackOverflowException was captured inside a PPC scrollbar tracking callback.
The suspended application stack pointer was 19270480, while the callback ran on a
separate reserved stack at 7794608. Synchronous native Toolbox callbacks now retain
the suspended caller's stack/backchain. Guest execution clears the native-caller
reference while running and restores it on return; callbacks without a suspended
PPC caller continue to use reserved stacks. The strengthened callback fixture fails
on the previous build and passes in interpreter/JIT modes, including native notification
and file completions under heap pressure. International, grow-zone, queue, line-break,
modal-filter and cooperative-thread probes also passed in both engines.

The subsequent address-zero fault was captured as an unresolved InterfaceLib:SameProcess
import while opening a resource type. It now uses the existing Process Manager routine,
with required-pointer checks. PPC probes cover a real serial, kCurrentProcess, an unknown
serial, null arguments and the one-byte Boolean output without overwriting its guard.

The green dialog background was independently reproduced by an alert probe. The default
wctb 0 resource supplied five ColorSpec entries but declared a final index of five,
causing the window renderer to read a sixth entry from the following heap block. Its
index-zero garbage entry replaced white with RGB 0000:4000:008c (palette index 232).
The resource now declares final index four. Window color-table reads also require the
header to fit and limit enumeration to complete entries inside GetHandleSize, protecting
existing malformed overrides. The alert probe deliberately supplies the legacy malformed
table and checks the background pixel remains white in both PPC engines.

The next captured resource-editor stop was an unresolved AppearanceLib:GetControlFeatures
import from STRuntime, querying a tab control (native kind 128). The bridge now exposes
the Control Manager query. Native controls report implemented data/embedding features;
tabs do not claim unimplemented focus/idle messages. Custom CDEFs are queried only after
the Appearance new-message handshake. The strengthened Appearance probe checks tab,
bevel and root controls, required arguments, big-endian UInt32 output and its guard.
It passed against the rebuilt Windows executable in interpreter and JIT modes. This
resolves the captured missing import; subsequent resource-editor operations still need
interactive validation.

The next live stop exposed a regression in that query: a classic checkbox CDEF
asserted on the Appearance test-new-messages handshake (message 27). The classic
button/checkbox/radio definition now explicitly declines that handshake with zero,
so GetControlFeatures returns errMsgNotSupported (-30580) and clears its output.
The Appearance fixture now includes a classic checkbox and verifies this result
in interpreter and JIT modes; both passed after rebuilding. Windows GDB captured
the original assertion through C_cdef0 and ROMlib_get_control_features.

A later STRuntime stop was an unresolved InterfaceLib:GetNextProcess import.
The PPC bridge now exposes the existing Process Manager enumeration routine;
its required serial-number pointer is validated before dereferencing. The PPC
file/process fixture checks enumeration from kNoProcess, the live process serial,
end-of-list clearing/procNotFound, output guards, unknown serial preservation and
null arguments. Both interpreter and JIT modes passed after the Windows rebuild.
The missing import was captured under Windows GDB; further application interaction
remains an ongoing compatibility test.

The next STRuntime stop was an unresolved InterfaceLib:PBDTGetAPPLSync import.
Sync/async PPC entry points now call the existing Desktop Manager application
lookup rather than substituting a canned result. Null parameter blocks return
paramErr. The file/process probe checks invalid desktop references and guest-endian
ioResult in both forms, plus null blocks, in interpreter/JIT modes; both passed.
The Desktop DB/DF ASan/UBSan tests also passed. The Windows rebuild is staged;
subsequent real application lookup/editor interactions remain to be tested.

Compared Preferences with the running SheepShaver instance: the Document Window
popups showed empty outlines in ROMAW instead of "0 - Default" and "All Formats".
The live menu list contained populated menus 20/21, while the popup private records
held null menu handles. Popup controls now bind installed dynamic menus, including
menus installed after control creation, retain live additions and preserve borrowed
menu ownership on disposal. Appearance popup creation no longer requires a MENU
resource. The popup window-font branch also sets text size with TextSize.
A dedicated PPC popup fixture passed in both engines, covering early/late binding,
arrow pixels, additions, selection values and disposal; the existing Appearance
probe also passed. The live rebuilt Preferences displayed both labels and arrows.
Scrollbar-divider and tab-switch redraw defects are being investigated separately.

The delayed grow-icon refresh now intersects the window clip with the resize
corner. It no longer redraws full-width/full-height scrollbar divider lines when
the application's original grow request used a corner-only clip. Appearance
pixel checks verify the grow glyph returns after erasure while pixels outside
the corner remain white; both PPC engines passed.

Tab switching also exposed saved-region corruption: SetOrigin translated global
SaveVisRgn even when the current port was an offscreen port rather than the window
being updated. BeginUpdate now records its port; only that port's origin changes
translate the saved visible region, and EndUpdate clears the association. A PPC
fixture fails on the previous build with a displaced visible-region coordinate
of 100, and passes in both engines with the fix. It also verifies origin changes
on the update window itself still round-trip correctly. The earlier pane-clearing
trial was removed; ordinary application redraw handles the content.

The staged Windows build was checked interactively through General, Document Window,
Fonts, Advanced and back to General, then resized and switched through Fonts and
Document Window again. No stale controls/labels or unwanted scrollbar dividers
remained in that sequence. Both dropdown labels/arrows remained visible. No resource
contents or application preference values were saved during this check. General
resource editing/saving compatibility remains unverified.

The next captured STRuntime stop was an unresolved InterfaceLib:PortionLine call.
The PPC bridge now exposes it, and the previous constant-result placeholder is
replaced by Roman space weighting using the current space glyph width, space count
and horizontal scaling. Empty/no-space runs and invalid scales return zero; guest
spans are bounded before scanning and oversized Fixed results saturate. This covers
Roman spacing, not general intercharacter-script justification. API reference:
https://dev.os9.ca/techpubs/mac/Text/Text-170.html and the common parameter discussion
at https://dev.os9.ca/techpubs/mac/Text/Text-147.html.

The existing callback/text probe now checks PortionLine for all four run positions,
scaling, empty/no-space/null/negative input, saturation and an oversized guest span.
It passed in both PPC engines. All existing callback modes also passed after moving
the fixture's expanded import table away from its callback records. The Windows
build, generated-file, formatting and header checks passed. The specific real-app
operation following this newly resolved import remains to be retested.

A subsequent STRuntime stop was NewTSMDocument with the classic 'tsvc' interface.
The captured process was not TSM-registered. Native PPC Text Services entry points
now use per-process registration and document state: creation, activation,
deactivation, deletion, input-window policy, active-document lookup and teardown.
The Roman creation result tsmScriptHasNoIMErr (-2517) still supplies a valid opaque
document Handle. Required argument spans and interface types are checked, failed
creation clears the output, and launch teardown disposes remaining records. Native
TSMEvent leaves events untouched for ordinary application processing. Existing 68K
active-document/input-window queries share the same state; Mixed Mode no longer
rejects every non-null document query. The obsolete diagnostic expecting selector
0x1d to be unsupported was replaced by positive coverage.

The TSM fixture checks registration errors, unsupported types, output guards,
refcon byte order, multiple IDs, active-document replacement, input-window policy,
Mixed Mode access to the live IDs, deletion, event preservation and launch cleanup.
It passed in interpreter/JIT modes, as did the existing component/68K query probe
and the former non-null-query diagnostic now exercised as a successful call. The
process-client fixture, Windows build, generated-file, header/format and PowerShell
checks passed. This supplies Roman document bookkeeping and fallback, not Unicode
text services or input-method component hosting; general 68K document-creation
selectors remain incomplete. The real-app operation following the captured import
still needs interactive validation. Apple API reference:
https://dev.os9.ca/techpubs/mac/Text/Text-431.html.

The next STRuntime stop was an unresolved InterfaceLib:GetSysDirection import.
PPC GetSysDirection/SetSysDirection now read/write the existing signed Macintosh
word at 0x0BAC (TESysJust), rather than keeping a second direction value. This
matches the Script Manager low-memory accessor contract; it does not implement
general bidirectional layout. Reference:
https://leopard-adc.pepas.com/documentation/mac/Text/Text-408.html.

The callback/text fixture checks signed/nonzero values, raw low-memory reads,
preservation of the adjacent word and restoration of the initial direction. All
five callback modes passed in interpreter and JIT configurations after expanding
the fixture import table. The Windows build, generated-file and source format/header
checks passed. The app operation following this newly resolved call still needs
interactive testing.

The next STRuntime stop was InterfaceLib:PixelToChar, called through Mixed Mode
with an empty text run. PixelToChar and CharToPixel are now exposed to PPC code.
Their shared Roman caret measurements replace unbounded stack allocation and the
empty-run division by zero, validate guest spans and offsets, preserve big-endian
outputs, distinguish leading/trailing edges, and distribute slop over spaces.
Large runs use wide per-glyph advances to avoid the legacy renderer's fixed-point
overflow; CharToPixel clamps its signed 16-bit result. Contextual scripts and
general bidirectional caret layout remain incomplete. API references:
https://dev.os9.ca/techpubs/mac/Text/Text-172.html and
https://dev.os9.ca/techpubs/mac/Text/Text-173.html.

The callback/text fixture covers empty and invalid runs, leading/trailing edges,
remaining width, output guards/byte order, clamped offsets, space slop, large runs,
and the ninth packed Point argument on the PPC stack. All five callback modes
passed in interpreter and JIT configurations. The Windows build, generated-file,
formatting and header checks passed. The real-app operation following this newly
resolved import remains to be tested interactively.

A subsequent STRuntime stop was the unresolved InterfaceLib:VisibleLength import,
called with a 52-byte host path. PPC now reaches the existing Roman trailing-space
implementation. Invalid lengths and out-of-range guest spans return zero without
reading memory; input text remains unchanged. Other script systems' directional
and multibyte whitespace handling remains incomplete. API reference:
https://dev.os9.ca/techpubs/mac/Text/Text-169.html.

Review also found that the legacy Pixel2Char wrapper supplied a null required
remaining-width output to the newly validated PixelToChar. It now supplies local
storage. Both legacy caret wrappers use defined signed fixed-point conversion,
including negative inputs, and are exposed to PPC callers. The existing callback
fixture checks VisibleLength trimming, empty/all-space/invalid/oversized input and
input preservation, plus legacy caret offsets and edge outputs. All five callback
modes passed in interpreter and JIT configurations. Windows build and pinned
format/header checks passed. Interactive follow-up remains necessary.

The next STRuntime stop was InterfaceLib:DrawJustified, with a 52-byte run and
zero slop, reached through a Mixed Mode callback. PPC now reaches the shared
QuickDraw drawing implementation. Roman justification temporarily adds scaled
per-space slop to the current port's spExtra and restores the original field on
return; the previous implementation ignored slop. Invalid/empty runs and invalid
scale factors leave the pen untouched. Drawing uses the current port's font,
style and text mode. Non-Roman contextual/bidirectional justification remains
incomplete. API reference: https://dev.os9.ca/techpubs/mac/Text/Text-160.html.

The existing callback/text fixture checks positive and negative slop at horizontal
scales 1 and 2, nonzero existing space-extra preservation, pen advancement and
invalid/empty inputs. All five callback modes passed in interpreter and JIT
configurations. Windows build and pinned format/header checks passed. These
checks exercise the drawing path and pen/state contracts; they do not establish
pixel-perfect rendering or complete the real-app interaction workflow.

A later stop in a loaded routine resource was AppearanceLib:SetImageWellContentInfo.
The captured control was null and its content type was zero. Native Control
Manager creation previously lacked the image-well CDEF (176/177). Image wells now
have native creation, recessed framing, selected state, hit testing, autotracking,
and data-access feature reporting. Resource icon suites, color icons and pictures,
and borrowed handle forms of those contents can be drawn. Content descriptors
remain six-byte big-endian records in private auxiliary control storage; borrowed
image handles are never disposed with the control. Unsupported content types
return unimpErr rather than claiming success. IconRef content and transforms
remain incomplete. Reference:
https://leopard-adc.pepas.com/documentation/Carbon/Reference/Control_Manager/Reference/reference.html.

PPC GetImageWellContentInfo/SetImageWellContentInfo and the 'cont' GetControlData/
SetControlData paths share that state and validate descriptor spans. Null or
nonexistent controls return paramErr; unrelated control kinds reject the tag.
The Appearance fixture now checks resource-picture pixels, empty-content clearing,
borrowed-picture pixels and lifetime after control disposal, content round trips,
unsupported types, malformed sizes, null arguments and data-access features.
The expanded import table was moved clear of the callback descriptor. Interpreter
and JIT checks passed, along with the Windows build and format/header checks.
Interactive real-app validation is still required; this does not establish full
Appearance Manager control or rendering compatibility.

Paint Settings subsequently raised REALbasic's NilObjectException rather than an
unresolved-import fault. The focused service trace showed a NewGWorld request at
depth 24 immediately before the exception; its output world pointer remained null.
The earlier unsupported-depth assumption was too restrictive for this application:
it did not retry with depth 32 before dereferencing the failed picture object.

ROMAW now accepts depth 24 as a compatibility request for full RGB color using
its existing 32-bit QuickDraw pixel storage. PixMaps report pixelSize 32,
pixelType RGBDirect, three eight-bit components and a four-byte pixel stride;
packed three-byte pixels are not introduced. UpdateGWorld normalizes the same
request before comparing formats, so an unchanged 24-bit request does not
needlessly reallocate a 32-bit world.

The Appearance fixture now treats 24 as supported, verifies the actual RGB pixel
metadata and row capacity, and exercises foreground/background text rendering in
the allocated world. It passed under interpreter and JIT. The classic entry-point
GDB fixture checks allocation output guards, RGB metadata, a no-op depth-24 update
and resize behavior. Its runner now passes explicit memory options to prevent
startup's automatic partition relaunch from escaping the debugger. Windows build,
standalone GWorld output, format/header and PowerShell checks passed. Real-app
Paint Settings still needs interactive verification after this allocation fix.

The next captured STRuntime fault was unresolved InterfaceLib:InitCRM. PPC now
reuses ROMAW's existing Communications Resource Manager queue implementation:
InitCRM, CRMGetCRMVersion, CRMGetHeader, CRMInstall, CRMRemove and CRMSearch.
Initialization remains idempotent and supplies the same COM1/COM2 descriptors as
the classic dispatch path. CRMSearch now compares IDs without signed overflow and
filters by the requested device type; null install/search/remove arguments are
handled without dereferencing them. This exposes queue management and enumeration,
not the full Communications Toolbox, resource helpers or connection tools.

The callback fixture's new CRM mode checks repeated initialization, the live queue
header, serial enumeration, guest record installation, mixed-device-type searches,
maximum-ID termination, removal/restoration of the original queue and null inputs.
It passed under interpreter and JIT, as did all five existing callback/text modes.
Serial allocation rollback/retry checks passed at all ten initialization allocation
points. Windows build and pinned source/PowerShell checks passed. The real-app
operation following InitCRM still needs interactive validation.

The next null weak-import call came from RBOTLib, not STRuntime. Its caller was
OTInetClientLib:OTInetGetInterfaceInfo with the default-interface selector (-1).
The owning PEF import table contained a run of unresolved Open Transport imports,
so adjacent resolved-import matching alone could not
identify it; the owning PEF code section and TOC slot identified the function.

ROMAW has no active Open Transport TCP/IP provider. A native query fallback now
returns kOTNotFoundErr (-3201) for default or nonnegative interface selectors,
kEINVALErr (-3221) for other negative selectors, and paramErr for a null record.
It leaves the result record unchanged and prefers a real installed PEF export
when one exists. This is graceful unavailable-provider handling, not implemented
Open Transport networking or a fabricated successful interface. References:
https://leopard-adc.pepas.com/documentation/mac/NetworkingOT/NetworkingWOT-407.html
and https://leopard-adc.pepas.com/documentation/Carbon/Reference/Open_Transport/Reference/reference.html.

The callback fixture's OT mode checks signed result codes, default/indexed and
invalid selectors, null output and preservation of all 552 result bytes. It and
all six existing modes passed in interpreter and JIT configurations. The Windows
build, pinned source/header checks and PowerShell checks passed. The real-app
operation after the newly handled query still needs interactive verification.

A subsequent loaded routine called the unresolved AppearanceLib:
SetBevelButtonTextAlignment entry, with centered alignment and zero offset.
The native bevel control now stores text alignment and signed horizontal offset,
exposes the PPC setter, and shares its state with the two-byte 'tali'/'toff'
control-data tags. Existing bevels retain centered text by default. Invalid
alignment values and malformed sizes do not change the stored state.

The renderer applies left, center, right and system-direction alignment. Positive
offsets inset left/right text from the corresponding edge; centered text ignores
the offset. Drawing is clipped to the original text area so signed offsets cannot
paint across neighboring controls. This extends text alignment, not the separate
bevel graphic-alignment API. Reference:
https://leopard-adc.pepas.com/documentation/Carbon/Reference/Control_Manager/Reference/reference.html.

The Appearance fixture checks centered defaults, signed alignment/offset round
trips, invalid values/sizes, tag updates preserving the other field, and actual
glyph pixel positions. It verifies positive offsets move opposite edges inward,
centered text ignores offsets, and system alignment follows both directions while
restoring the original direction afterward. These checks passed in interpreter
and JIT configurations, along with Windows build and source format/header checks.
The specific real-app operation after the new setter still needs interaction.

The next routine-resource stop was AppearanceLib:SetBevelButtonContentInfo,
clearing a bevel's graphic (content type zero). Bevel buttons and image wells now
share validated six-byte content storage and rendering for resource/borrowed icon
suites, color icons and pictures. PPC GetBevelButtonContentInfo and
SetBevelButtonContentInfo expose that state, and generic 'cont' data access accepts
both native control kinds while typed accessors reject the wrong kind.

Bevel creation initializes the descriptor from the resource content type and ID.
Clearing it removes the graphic, rather than leaving the old cached color icon.
Resource color icons are loaded for drawing and disposed afterward; picture
resources stay owned by the Resource Manager, and caller-supplied handles are
borrowed. Bevel layout uses the picture/icon bounds with the existing text
placement rules. IconRef and additional newer content types remain unsupported.

The Appearance fixture adds resource-picture drawing, typed and generic content
round trips, clearing, wrong-kind/null arguments, and borrowed-picture survival
after replacement and control disposal. These passed in interpreter and JIT modes,
as did the existing image-well and text-alignment tests. Windows build,
generated-file, format and header checks passed. Interactive verification of the
real-app action following this setter remains necessary.

The next missing AppearanceLib call was SetBevelButtonGraphicAlignment with
system-direction alignment (-1) and zero horizontal/vertical offsets. Native
bevels now store graphic alignment and a signed Point offset, expose the setter,
and share state with the 'gali' (two-byte) and 'goff' (four-byte Point) data tags.
The renderer supports center, four edges, four corners and system direction.
Centered graphics ignore offsets; other alignments apply offsets from their
aligned edges and clip drawing to the graphic area reserved beside any text.

The pixel regression found a separate DrawPicture issue: before seeing a clip
opcode, it installed an infinite clip instead of honoring the caller. Pictures
without such an opcode could paint outside a button's graphic area. DrawPicture
now copies the incoming clip at the start; subsequent picture clip records retain
the existing intersection behavior.

The Appearance fixture checks all nine alignments, both system directions,
positive/negative offsets, centered-offset behavior, Point byte order, state tags,
invalid values/sizes and a pixel outside the reserved graphic area. The latter
failed before the DrawPicture correction and passed afterward. Interpreter and
JIT checks passed, including the existing content/text/image-well scenarios.
Windows build, protected-page PICT-header checks, generated-file regeneration,
formatting and header checks passed. Real-app interaction after the new setter
still needs verification. This extends native bevel layout rather than claiming
complete Appearance Manager or general bidirectional rendering compatibility.

The next loaded routine called the unresolved SetBevelButtonMenuValue API with
item 1. Native bevels now retain a separate menu-selection word and expose the
PPC setter/getter plus the read-only 'mval' control-data tag. Changing this value
does not overwrite the button's pressed/toggle value, and changing the pressed
value does not overwrite menu selection. Negative classic item values, null
arguments and unrelated control kinds return errors. This supplies selection
bookkeeping; attached-menu presentation and tracking remain incomplete.

The Appearance fixture checks selection values 0, 7 and 32767, big-endian getter
outputs with an adjacent guard word, the two-byte data tag/size, independence from
pressed state and invalid calls. Interpreter and JIT runs passed, including the
existing alignment, content, image-well and picture-clipping cases. Windows build,
formatting and header checks passed. The real-app operation after the newly bound
setter still needs interactive verification.

The next stop was AppearanceLib:HandleControlClick on a native bevel button
(control kind 32), with a packed local Point and null action/modifiers. The PPC
binding now validates membership in a live window's control list, rejects missed
or disabled controls, and delegates native tracking to TrackControl. The existing
Mixed Mode action path and the -1 default-action sentinel are preserved. The
explicit hit test avoids TrackControl's historical global-coordinate fallback
for a missed local point. Modifier-aware custom CDEF tracking is still incomplete.

The Appearance fixture places a bevel around the current mouse and queues a
mouse-up, checking successful tracking with null/default actions without manual
input. It also checks null controls, missed points and disabled controls.

Interpreter and JIT Appearance runs passed, as did the Windows build, formatting
and source-header checks. The real-app operation after this binding still needs
interactive verification.

Resplendence next stopped on STRuntime's unresolved InterfaceLib:GetSysFont.
The shared Script Manager implementation and PPC native import now return the
current big-endian SysFontFam low-memory word as a signed Macintosh INTEGER,
matching [Inside Macintosh's system/application font documentation](https://leopard-adc.pepas.com/documentation/mac/Text/Text-207.html).
The Appearance fixture compares the initial low-memory value, then checks 0, 3,
-32768 and -1 and restores the original word. This checks live state and signed
return marshalling instead of assuming the default font ID is always zero.

Interpreter and JIT Appearance checks, the Windows build, formatting and source-
header checks passed. Further real-app interaction remains to be tested.

STRuntime next called unresolved InterfaceLib:NewAlias with a null source and a
real target FSSpec. The PPC import now uses the existing shared Alias Manager
implementation and its three-pointer calling convention. The PPC file fixture
creates absolute and relative aliases of its application, resolves them back to
the original volume/directory/Pascal name, and verifies four-byte Handle outputs
and one-byte changed flags with adjacent guards. Invalid target/output calls are
also checked. No additional alias format or storage implementation was added.

The file/alias fixture passed under both PPC interpreter and JIT. Alias output-
safety, Windows build, formatting and source-header checks passed. Further
real-app interaction after this binding remains to be tested.

The next unresolved STRuntime import was InterfaceLib:AEPutPtr, inserting an
'alis' payload into an AppleEvent list. The PPC binding now uses the existing
AppleEvent implementation with its list/index/type/data/size argument layout.
The AppleEvent fixture appends and replaces a TEXT element, changes its original
input bytes, and checks the independently stored data through AEGetNthPtr,
including returned type/size and an adjacent output guard. An invalid insertion
index must leave the item count unchanged. Existing descriptor-lifetime and
handler-identity checks remain in the same fixture.

Interpreter and JIT AppleEvent checks, the Windows build, formatting and source-
header checks passed. Further real-app interaction remains to be tested.

The waiting cursor while opening Finder's ALRT list was accompanied by repeated
opens of a temporary resource fork under Y:/System 9 Folder, followed by
GetResource/GetResAttrs/ReleaseResource/CloseResFile for each listed item.
Debugger samples repeatedly caught sidecar-header reads and host fstat calls
inside ROMlib_FORKOFFSET. The list eventually appeared and the cursor cleared;
this was slow network resource work rather than another unresolved import.

Host-side fork offsets now have an FCB-lifetime cache without changing guest
FCB layouts or caching resource payloads. File/volume/descriptor/fork identity
must match. Close, explicit EOF/metadata refresh and descriptor updates invalidate
the cache; descriptor writes invalidate all entries so aliases using other host
handles cannot retain a relocated fork offset. Failed lookups are not cached.
PPC service tracing now also checks the trace debug category before formatting
and writing a message for every Toolbox call.

The sidecar test checks 1024 lookups using one host metadata query, preservation
of the file mark, explicit invalidation, fork relocation and file identity
changes. It and the Windows build passed. PPC file/alias, AppleEvent and Appearance
fixtures passed with interpreter and JIT, as did formatting/header checks.
The original responsive debug session remains open because the user resumed
interaction. The new build is in build/resplendence-debug/fork-stage; its effect
on the complete network ALRT-list workflow still needs interactive verification.
