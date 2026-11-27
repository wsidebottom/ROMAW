# 68040 compatibility work

Syn68k has an experimental MC68040 FPU execution path in addition to its
existing integer core. This is not yet a complete MC68040 implementation.
ROMAW's FPU capability reporting remains unchanged until instruction,
exception and saved-state compatibility are validated together.

## Descriptor placement audit follow-up (2026-11-27)

[MC68040UMAD](https://www.nxp.com/docs/en/reference-manual/MC68040UMAD.pdf),
item 13, prohibits copyback mappings for table/page descriptors. The earlier
descriptor-push fixture inherited a copyback TTR and manually injected dirty
descriptor cache lines. Correcting its U/M bits did not establish a permitted
descriptor placement. Its 74,496 cases validate the engine's injected-state
continuation and rejection guards; they do not establish architectural behavior
for a supported software configuration. The six source-miss unsupported-delivery
observations used that same injected configuration. They do not justify changing
MOVE16's architectural source/destination ordering.

The new write-through descriptor matrix passes 201,603 cases in optimized and
separate cold/warm AOT processes. It constructs tables through WT memory writes,
optionally warms clean descriptor cache lines, and checks ancestor U updates,
all permitted U/M combinations, protected U-only updates, both page sizes, all
five MOVE16 forms, privilege, normal trace and source cache modes/hits. Native
memory variants cover callback-backed descriptors and directly lowered RAM
table reads, writes and locked ORs, with no checked semantic-helper calls.

For write-protected destinations, a guest handler drains captured PD, advances
the stacked PC and postincrements, and returns through RTE. Changing peripheral
source data makes an unintended second read observable; cleanup consumes the
first sample without retouching the source. Two persistent instruction-view
bodies are emitted cold and both load warm with zero compilation. The matrix is
registered in the MMU and AOT groups.

That regression/documentation follow-up left the 179 CPU inputs, four archives
and all 66 staged/current binary hashes identical to the accepted
buffered-victim bundle; the later prefetch implementation below changes four CPU
files. The new audit is
`build/68040-wt-descriptor-output/source-audit.json`. Dirty-descriptor injected
states remain engine stress tests and need separate hardware evidence before
being used as a required architectural ordering case. Physical prefetch,
further restart/delivery integration and the remaining FPU audits remain open.

## Latest validated bundle: sequential instruction prefetch (2026-11-27)

Commits f05d59ad and 78008a66 retain sequential opcode and extension words
across budget stops. Checked and emitted logical reads share the retained
words and faults. The guest regression covers a changed backing FPU command
and a long displacement assembled from a queued word and a demand read.

Grouped cache, FPU, MMU, RTE, flat MOVE16 and cold/warm AOT checks passed.
The warm sequential fixture reused one compiled body with zero compilation.
Help, Browser and 68K Speedometer startup passed. The audited 66-file ROMAW
bundle is promoted to build/68040-current/bin. The preceding bundle is
retained at build/68040-pre-sequential3-bin-20261127.

CPU build ID: 53fc8e00e3761dfaee3e1d2fb0f50c9a38129f17de0e17f4a16b0ec9ddbf7536
Archive SHA256: 9201f5d5384709b8a357610caa1fc7e5826b21516d4d170a482c3d5b6ea02bab
Audit: build/68040-sequential3-output/68040-sequential3-source-audit.json

Further bus scheduling and restart integration, and the
remaining FPU busy frame and special-value audits are open.

## Previous validated bundle: conditional branch prefetch (2026-11-27)

Commit 835e379f retains the selected Bcc or DBcc word or access fault across
budget stops. Accepted exception entry flushes it. The 51-case regression
covers byte, word and long displacements, DBcc, guest format-7/RTE recovery,
interrupt entry, checked execution and both native memory bindings.

Grouped cache, FPU, MMU, RTE, flat MOVE16 and cold/warm AOT checks passed.
The warm branch fixture reused 16 compiled bodies with zero compilation.
Help, Browser and 68K Speedometer startup passed. The audited 66-file ROMAW
bundle is promoted to build/68040-current/bin. The preceding bundle is
retained at build/68040-pre-branch-prefetch-bin-20261127.

CPU build ID: 55b5586b13e5edff0896e0e00e72b446806ed8b0de27ed48cd8f753ba05e465c
Archive SHA256: bfddb5e61839ea3a10733c91466e30441d1fae5f40bf6aabfb1644c386b4090b
Audit: build/68040-branch-output/68040-branch-source-audit.json

Sequential and extension-word prefetch, further restart integration, and the
remaining FPU busy frame and special-value audits are open.

## Previous validated bundle: nonresident exception prefetch (2026-11-27)

Source checkpoint `4cd04dfd` adds the MC68040UMAD item 15 translation
boundary before literal ILLEGAL, A-line, CHK-family and newly unimplemented FPU
exceptions at the end of a page. A nonresident following page produces format-7
access error at the original opcode, with FA at that next page. New FPU pending
frames are not posted before this check; existing pending frames retain their
preinstruction priority. CHK-owned flags are deferred until its exception wins.

The 8,187-case matrix passes optimized, grouped MMU and cold/warm AOT checks.
It covers missing root/pointer/leaf entries, 4K/8K pages, all CPU/native-memory
combinations, privilege/trace, cache absent/disabled/enabled, guest physical table
repair/PFLUSHA/RTE, callback changes and already-pending FPU frames. Resident,
middle-of-page and paging-disabled controls retain their original behavior.
The exact fixture rejects the preceding accepted archive. Wrapped translation
helpers prove checked execution remains checked and native execution uses the
closed translation root. Cold AOT emits 19 bodies with 1,544 intra-process
hits; warm execution emits none and records 1,563 hits.

All 22 native groups pass on both x64 ABIs, each verifying 47 closed roots and
no imports. The independently generated Windows image matches the CPU archive.
Full cache, FPU, MMU, flat MOVE16, saved-state/FPSP and cold/warm AOT groups pass.
FPU validation retains 1,714,422 reported cases. Mapped stream recovery retains
8,107,168 cases and AOT recovery retains 3,618,920 per process, with zero warm
compilation and 1,262,191 warm hits. The WT descriptor matrix retains 201,603
cases per process; earlier injected dirty-descriptor cases remain stress tests.

The rebuilt 66-file bundle at `build/68040-prefetch2-stage/bin` is promoted
to `build/68040-current/bin`. Its predecessor is retained at
`build/68040-pre-prefetch2-bin-20261127`. The audit at
`build/68040-prefetch2-output/68040-prefetch2-source-audit.json` verifies
179 CPU inputs, 169 fixtures and four identical archives. Four CPU files changed.
Application base is `4cd04dfdeadaf26af3e6cd227ebd6ce82625a46b`.
CPU build ID is `4aa1e942069540daaaba0a69f5cc6930abff95bcd0ccc4fca01108107be7fdaf`;
archive SHA256 is `4db0b44d424666314182b97b119254ade54daaf3fef8aaacc91048b893e9fdc0`.
Help, Browser and 68K Speedometer startup pass with
30,954/77,160 native entries and null checkpoints; Speedometer makes one
FPU query. These are startup smokes, not broad application compatibility tests.

The goal remains active. This checks the nonresident translation part of the
exception boundary; it does not fetch the physical next instruction word or
implement a general pending-prefetch queue. Other instruction-access priorities,
further locked/active-stack/double-fault continuations and remaining FPU
busy/FPSP, encoding and special-value audits remain open. Full 68040 and
ROMAW-wide paged Toolbox compatibility remain unproven.

## Previous validated bundle: buffered-victim arbitration (2026-11-27)

Source checkpoints `2a0ed97f` and `db140772` expose a dirty replacement
buffer during a board-controlled grant after data fill and before writeback.
Snoops may supply, sink or invalidate that buffer. The eventual push uses the
updated dirty selection and bytes, or is cancelled after invalidation. Ordinary
recursive cache accesses remain rejected. The acquired CPU read is retained;
a copyback store does not overwrite a master's subsequently sunk bytes.

The reference matrix passes 80,736 cases in optimized Windows and ASan/UBSan
builds. All 22 emitted kernel groups pass on both Windows and Linux x64 ABIs,
including 238,752 buffer cases per ABI. Both images verify 47 closed roots with
no imports, and the independently generated Windows image matches the CPU
archive. The exact buffer fixture rejects the preceding accepted checked and
emitted archive.

A mapped 64-case matrix covers FPU reads/stores, normal and failed pushes,
checked/native CPU and memory combinations, guest PD cleanup/RTE and all trace
settings. Captured PD retains snoop-modified bytes; the handler preserves the CT
trace PC. Its cold AOT process emits one body and its warm process loads it with
zero compilation. Full cache, FPU, MMU, flat MOVE16, saved-state/FPSP and cold/warm
AOT groups pass. The existing mapped AOT recovery retains 3,618,920 cases per
process and 1,262,191 warm hits with zero compilation.

The 66-file bundle at `build/68040-victim-stage/bin` is promoted to
`build/68040-current/bin`, with its prior bundle retained at
`build/68040-pre-victim-bin-20261127`. The audit at
`build/68040-victim-output/68040-victim-source-audit.json` verifies 179 CPU
inputs, 167 fixtures and four identical archives. Application base is
`2a0ed97fc4d2a88d461d080ee536efa1e7adc1da`; CPU build ID is
`d3adceb90f85bb207ca475dc162a406aa65ca155d2a82df1b10484f720646e79`; archive SHA256 is
`d1d75ae29cb9058427efed126bee33e335802a5ef0a6c9379b695ea559aa78cc`.
Help, Browser and 68K Speedometer startup pass,
with 30,944/77,161 native entries and null checkpoints;
Speedometer makes one FPU query. These are startup smokes, not broad application
compatibility tests.

The goal remains active. Further locked/active-stack/double-fault continuations,
physical instruction prefetch,
partial external transactions, and remaining FPU busy/FPSP, encoding and
special-value audits remain open. This grant boundary does not implement
preemption within a line burst or pin-level timing. Full 68040 and ROMAW-wide
paged Toolbox compatibility remain unproven.

## Previous validated bundle: physical snoop lowering (2026-11-27)

Source checkpoint `d2397f65` adds accepted external-master snoops to the
physical cache and mapped-memory bindings, with packet and typed native roots.
Both physical cache banks obey their snoop state transitions, including
instruction-line invalidation on enabled external writes and SC=2 reads.
Scalar dirty sink writes latch input before any bank changes. MMU, CPU
registers and the CPU store-attempt flag are unaffected; disabled caches
continue to participate. Read misses and nonintervening hits leave memory
response to the board.

The 1,045,880-case state/SC/TT/dirty-mask/offset/alias/ownership matrix passes
in reference/sanitized runs and through emitted packet and bus entries on
Windows and Linux x64. All 21 native groups pass on each ABI, with 47 verified
roots and no imports. The CPU archive's emitted image matches the independently
generated Windows image. Both checked and emitted earlier candidates fail the
final metadata-alias regression at the expected input-latching assertion.

A mapped 24-case regression checks CPU and memory dispatch combinations,
dirty source/sink and clean invalidation with caches disabled, and external
code writes for all SC encodings. Changing a cached MOVEQ into a two-word FABS
checks opcode family, instruction length, instruction-cache visibility and
FPU execution. Its cold/warm AOT runs pass, with no warm compilation.
Inhibited external writes wait for guest CINVL to change the instruction view;
enabled writes invalidate it.

Grouped cache/reference, FPU, MMU, flat MOVE16 and full cold/warm AOT checks
pass. The FPU group retains 1,714,422 reported cases.
MMU stream recovery retains 8,107,168 cases; mapped AOT recovery passes
3,618,920 per process with 1,262,191 warm hits and zero warm compilation.
Basic saved-state and 146 Motorola FPSP cases pass in both dispatch modes
at two load addresses.

ROMAW uses application base `caa0da54aa5cf3219e333e2480ef7a4a76008941` with the tested
CPU archive. The 66-file bundle at `build/68040-snoop3-stage/bin` is promoted
to `build/68040-current/bin`; the previous bundle is retained at
`build/68040-pre-snoop3-bin-20261127`.
`build/68040-snoop3-output/68040-snoop3-source-audit.json` matches
179 CPU inputs, 165 fixtures and four identical CPU archives.
CPU build ID is `0671b6be564aeca21de5a2b15c561dea4ecaf452ea6c803df0148e8f86e47a7e`;
archive SHA256 is `b743876fa2a3227b489abb1d4e5c33faee0082d8d667a808d8bf1742cd5a21ee`.
Help, Browser and 68K Speedometer startup pass. Browser/Speedometer reach
30,940/77,153 native entries with null checkpoints and no Windows exception;
Speedometer issues one FPU query.

The broader goal remains active. Valid source-miss compound descriptor pushes,
further locked/active-stack/double-fault continuations, physical instruction
prefetch, in-flight victim-buffer/partial external transaction integration,
and remaining FPU busy/FPSP, encoding and special-value audits remain open.
The snoop API does not claim pin-level arbitration/termination timing.
Full 68040 and ROMAW-wide paged Toolbox compatibility remain unproven.

## Valid descriptor regression follow-up (2026-11-27)

The original descriptor-push fixture used U=0,M=1, which UM 3.2.1 prohibits.
That setup did not by itself prove behavior for a defined descriptor state.
The fixture now uses valid U=0,M=0,WP=1, whose locked U update is specified in
Table 3-1. Guest cleanup recovers PD, clears WP, executes RTE and retries;
the final unprotected write uses an unlocked descriptor U/M update.

The 70,176 fault/guard cases now use that valid setup. An additional 4,320
normal-write controls cover all valid U/M combinations, source misses and
clean/dirty hits, both page sizes, both cacheable modes, all five forms,
user/supervisor and all trace settings. They assert no spurious push and
correct RAM/cache descriptor updates. The combined 74,496-case matrix passes
at both optimization levels and in cold/warm AOT runs, with two cold bodies
and zero compilation/two hits warm. Three shared instruction-view sentinels
also pass; the neighboring 96,333-case MOVE16 suite remains green. The final
fixture rejects the previous CPU archive.

The production CPU inputs and all four archives are unchanged. Both staged
and current 66-file bundles match the validated rebuild below byte for byte;
no executable rebuild is needed for this regression-only correction.
`build/68040-move16-valid-descriptor-output/source-audit.json` records the
follow-up evidence and the inherited full grouped validation.

A separate valid-descriptor observation still finds unsupported delivery after
an uncached source read and a destination descriptor push fault in all three
execution modes. That remains open; cached-source replay must not be extended
to it without establishing safe bus sequencing and the appropriate frame.

## MOVE16 descriptor pushes (2026-11-27)

Source checkpoint `7d562350` certifies the compound fault where MOVE16 obtains
its source from cache before a destination descriptor U update encounters a
dirty line push failure. The format-7 PD holds the pushed descriptor line;
WB2 marks the pending MOVE16. Guest cleanup and RTE retry the original
instruction without draining WB2 as scalar data or repeating a source bus read.
Address increments wait for successful completion, while live callback changes
to other registers, CCR and FPU null state are retained.

The new regression passes 70,176 cases in the MMU group and in each cold/warm
AOT process. It covers all forms, both page sizes, both cacheable modes, source
and destination offsets, user/supervisor, trace modes, clean/dirty source hits,
partial dirty masks and every push beat. It executes guest PD cleanup, RTE
and retry, and rejects 58 malformed record variants across 96 contexts without
changing CPU, memory, MMU, cache or bus state. The exact fixture fails against
the previous accepted archive.

Grouped cache/reference, FPU, MMU, flat MOVE16 and full cold/warm AOT checks
pass. Basic saved-state and Motorola FPSP checks also pass. Existing FPU,
stream recovery and native-image coverage are retained; the emitted memory
image and its native runner branch are unchanged.

ROMAW uses application base `b9f4e437fe5b699e4f18e2d2aa858830b6475d15` and the
tested CPU archive. Its 66-file bundle at
`build/68040-move16-descriptor-stage/bin` is promoted to
`build/68040-current/bin`. The prior bundle is preserved at
`build/68040-pre-move16-descriptor-bin-20261127`.
The audit at
`build/68040-move16-descriptor-output/68040-move16-descriptor-source-audit.json`
matches 179 CPU inputs, 163 fixtures and four identical archives.
CPU build ID is `5ddecec4e3d53f946662aa9c256128f01d64a54b15f69bcbd84cc032a013ee8c`;
archive SHA256 is `914890e28ca053b3153215efb43571ef721cba79ef99a929b261aec18c163c4a`.
Help, Browser and 68K Speedometer startup pass. Browser/Speedometer report
30,947/77,155 native entries with null checkpoints and no Windows exception;
Speedometer makes one FPU query.

Remaining work includes source-miss/cache-inhibited compound descriptor pushes,
further locked push and active-stack/double-fault continuations, physical
instruction prefetch, snoop integration, and FPU busy/FPSP saved-state,
encoding and special-value audits. Full 68040 and paged Toolbox compatibility
remain unproven.

## Move-out stage retirement (2026-11-27)

FRESTORE now resolves the older E3 writeback before retiring a quiet E1
move-out stage. The completed writeback can no longer reactivate a frame
without a pending exception. The following FPU instruction executes normally,
and an idle FSAVE writes only its four-byte header. This resolves the combined
E3/E1 zero-vector case recorded at the preceding checkpoint.

The new 368,640-case matrix covers checked/native CPU and memory, both page
sizes, postincrement, all rounding/precision settings, eight formats, every
destination FPR, E1/E3/combined stages and previous null/idle states. It checks
quiet completion, enabled SNAN, nonmaskable integer SNAN, older enabled INEX,
nonmaskable overflow and packed-conversion priority. Registers, FPCR, FPSR,
FPIAR, the next FNOP and FSAVE tail bytes are verified. Its exact fixture
rejects the preceding CPU archive. Cold AOT emits four bodies; warm replay
loads all four with zero compilation.

Grouped cache/reference, FPU, MMU, flat MOVE16 and full cold/warm AOT checks
pass. The native FPU runner reports 1,714,422 cases. Basic saved-state and
146 Motorola FPSP cases pass in both dispatch modes at two FPSP load addresses.
MMU recovery retains 8,107,168 stream cases. Mapped AOT recovery passes
3,618,920 cases per process, with 1,262,191 hits and zero compilations warm.
The emitted memory image and native runner branch match the prior proof:
twenty kernel groups and 45 verified roots per Windows/Linux ABI remain
covered by that exact-source evidence.

ROMAW was rebuilt from application base `8c9bcdfb53a21f5c2b2de059d18c1d009f420e25`,
including the latest committed application changes at snapshot creation.
Its 66-file bundle at
`build/68040-moveout-resume-stage/bin` is promoted to
`build/68040-current/bin`; the preceding bundle is preserved at
`build/68040-pre-moveout-resume-bin-20261127`.
The audit at
`build/68040-moveout-resume-output/68040-moveout-resume-source-audit.json`
matches 179 CPU inputs, 162 fixtures and four identical CPU archives.
CPU build ID is `402e35679eba26a4db9e7efb703fd0441bf8a1658ee9fbf76e7cdc4b65c8e4a6`;
archive SHA256 is
`7c46b6740da30f5fe16367d9cf9684311bed4de956a5aa259f85dd058c26299d`.
Help returns zero with 7,669 bytes. Browser and 68K Speedometer reach their
startup loops after 30,951 and
77,160 native entries, with null checkpoints and no Windows
exception. Speedometer issues one FPU query. These remain startup checks.

The broader goal remains active: compound descriptor/locked cache-push
continuations, further active-stack/double-fault variants, physical instruction
prefetch, snoop integration and remaining FPU busy/FPSP saved-state, encoding
and special-value audits still require work. Full 68040 and ROMAW-wide paged
Toolbox compatibility are not established.

## Completed FPSP frames (2026-11-26)

Restoring an FPSP-completed E1 frame with no enabled exception now leaves the
FPU idle. Its existing result and control state remain live; the following FPU
instruction no longer dispatches a zero pending vector as vector 1. Frames
without both S/X completion markers still request unimplemented-instruction
handling. Enabled signaling-NaN exceptions remain pending. This resolves the
zero-vector issue recorded at the preceding checkpoint.

The new 55,296-case matrix covers checked/native CPU and memory, both page
sizes, short/busy frames, postincrement, all defined rounding and precision
settings, exception masks, FSIN/FINT/FMOVECR commands, all S/X combinations
and previous null/idle/pending states. It checks all FPRs and controls,
the next FNOP and a four-byte idle FSAVE with an untouched tail. Its exact
fixture rejects the preceding CPU archive. Cold AOT emits four bodies; warm
replay restores all four with zero compilation.

Grouped cache/reference, FPU, MMU, flat MOVE16 and complete cold/warm AOT
checks pass. The native FPU runner reports 1,345,782 cases, including the
137,088 captured-frame cases. Basic saved-state checks and 146 Motorola FPSP
cases pass in both dispatch modes, with FPSP at two load addresses. MMU
recovery retains 8,107,168 stream cases; mapped AOT recovery passes 3,618,920
cases per process, with 1,262,191 hits and zero compilations warm. The closed
memory image and its runner branch are byte-identical to the prior proof:
twenty kernel groups and 45 verified roots per Windows/Linux ABI remain
covered by that exact-source evidence.

ROMAW was rebuilt with the tested archive. Its 74-file bundle at
`build/68040-fpsp-completion-stage/bin` is promoted to `build/68040-current/bin`;
the preceding bundle is preserved at
`build/68040-pre-fpsp-completion-bin-20261126`. The audit at
`build/68040-fpsp-completion-output/68040-fpsp-completion-source-audit.json`
matches 179 CPU inputs, 161 fixtures and four identical archives. CPU build ID
is `91f756a14e91e721842f5cde6c910df6437778db6294ee75d019ae4c572b1507`;
archive SHA256 is
`2af39c2582820f4eb13aa458c953c0f8559dc2694871b1c4d77b7bf3362bfa6b`.
Help returns zero with 7,669 bytes. Browser and 68K Speedometer reach their
startup loops after 30,953 and
77,158 native entries, with null checkpoints and no Windows
exception. Speedometer issues one FPU query. These remain startup checks.

The broader goal remains active: compound descriptor/locked cache-push
continuations, further active-stack/double-fault variants, physical instruction
prefetch, snoop integration and remaining FPU busy/FPSP saved-state, encoding
and special-value audits still require work. A separate combined E3-writeback/
E1-FMOVE-out probe still reproduces a zero pending vector after writeback;
that continuation remains open. Full 68040 and ROMAW-wide paged
Toolbox compatibility are not established.

## Captured FRESTORE frames (2026-11-26)

FRESTORE acquires the header once and decodes resumed operands and the saved
instruction PC from the captured frame image. It no longer resamples WBTEMP,
ETEMP, FPTEMP or FPIAR after committing restored state. Changing or failing
callbacks cannot substitute a second operand or introduce a late read fault.
Null and idle frames retain their direct path.

The new fixture passes 137,088 cases across checked/native CPU and memory,
both page sizes, short and busy frames, combined E1/E3 stages, rounding and
precision settings, postincrement, volatile callbacks and genuine body faults.
Within that total, 1,152 cases run guest descriptor repair or physical-fault
return, RTE and retry. Literal register results supplement immutable-image
comparisons. The exact fixture rejects the previous archive and a header-only
repair, establishing that operand capture is required too.

Grouped cache/reference, FPU, MMU, flat MOVE16 and full cold/warm AOT checks
pass. The FPU runner reports 1,290,486 cases; MMU recovery retains 8,107,168
stream cases. Basic saved-state checks and 146 Motorola FPSP cases also pass
in both dispatch modes, with FPSP tested at two load addresses. The new AOT
fixture emits three bodies cold and restores all
three with zero compilation warm. Existing mapped recovery warm replay retains
1,262,191 hits without compilation. The closed memory image is byte-identical
to the previous checkpoint: its twenty Windows/Linux kernel groups and 45
verified roots per ABI remain covered by that source-matched evidence.

ROMAW was rebuilt with the exact tested CPU archive. Its 74-file bundle at
`build/68040-frestore-capture-stage/bin` is promoted to `build/68040-current/bin`;
the previous bundle is preserved at
`build/68040-pre-frestore-capture-bin-20261126`. The audit at
`build/68040-frestore-capture-output/68040-frestore-capture-source-audit.json`
matches 179 CPU inputs, 160 fixtures and four identical CPU archives. CPU build
ID is `3eba57521957a82ba7a919a41fd64802773d033b8e16dd54ddd90b5ddebfc294`;
archive SHA256 is
`cb71803606789731f6d577806a7ce32ed196b80ec2ce0fe5328820f6088860ff`.
Help returns zero with 7,669 bytes. Browser and 68K Speedometer reach their
startup loops after 30,950 and 77,160 native entries, respectively, with null
checkpoints and no Windows exception. Speedometer issues one FPU query.

The broader goal remains active. Compound descriptor/locked cache-push
continuations, further active-stack/double-fault variants, physical instruction
prefetch, snoop integration and FPU busy/FPSP saved-state, encoding and
special-value audits still require work. A separate probe reproduces a zero
pending-vector state after restoring an FPSP-completed frame with no enabled
exception; its subsequent vector-1 dispatch remains under investigation.
These regressions do not establish full 68040 or paged Toolbox compatibility.

## Partial cached-read diagnostics (2026-11-26)

Cached reads now retain the successful operand prefix on a later physical,
fill or victim-push fault. The MMU appends that prefix to its earlier completed
chunks. Only bytes counted by `completed` appear in the diagnostic value;
incomplete internal fills and poisoned failed callback data are excluded.
Page-crossing replay reports the current pass's prefix. Failed public reads
leave their output unchanged, and write diagnostics retain the original
width-limited operand.

The new independent matrix passes 2,342 cases per checked/emitted pass. It
covers physical widths, natural beats and every line offset, cache-hit
prefixes, fill/push failures, both memory interfaces and applicable function
codes. Its exact fixture rejects the previous kernel in four checked/native
Windows/Linux runs. The existing 5,060-case page-crossing matrix now verifies
the diagnostic prefix at faults in both passes. All twenty emitted-kernel
groups pass on both ABIs with 45 verified roots and no runtime imports.

Grouped cache/reference, FPU, MMU, flat MOVE16 and full cold/warm AOT checks
pass. Coverage retains 8,107,168 recovery-stream cases, 36,097 unaligned locked
cases and the 7,232-case RTE matrix from `9cd945c2`. Each mapped AOT recovery
process passes 3,618,920 cases; warm replay records 1,262,191 hits with zero
compilations. The RTE warm fixture also restores its body without compilation.

ROMAW was rebuilt with the exact tested CPU archive. Its 74-file bundle at
`build/68040-read-prefix-stage/bin` is promoted to `build/68040-current/bin`;
the previous bundle is retained at `build/68040-pre-read-prefix-bin-20261126`.
The audit at `build/68040-read-prefix-output/68040-read-prefix-source-audit.json`
verifies 179 CPU inputs, 159 fixtures and four identical CPU archives. CPU
build ID is `5b85299b75c425b2c6891dd981455cf45c4be074628701714d39663785de4df3`;
archive SHA256 is
`28961dbb9c4cb76cba101af7035cb91b2b1933e4586fe9617c013661b85c233b`.
Help returns zero with 7,669 bytes. Browser and 68K Speedometer reach their
startup loops after 30,943 and 77,158 native entries, respectively,
with null native checkpoints and no Windows exception. Speedometer issues
one FPU capability query. These checks establish startup behavior.

The broader goal remains active: compound descriptor and locked cache-push
continuations, further active-stack/double-fault variants, physical instruction
prefetch, snoop integration and FPU busy/FPSP saved-state, encoding and
special-value audits still require work. ROMAW-wide paged pointers and full
68040 compatibility are not established by these regressions. Earlier
checkpoint details follow.

## RTE user-stack faults (2026-11-26)

RTE now delivers an access fault after a format-1 frame has committed a user
SR and selected USP. The completed prefix and callback effects remain live.
Frame-read certificates require the function code to match the live S bit;
RTE cache-push certificates accept the corresponding user-data access. Fault
entry chooses ISP or MSP using the committed M bit. Returning to the RTE PC
in user mode still takes privilege vector 8, without reading the user frame.

The 7,232-case regression covers header fields and partially read longwords,
both page sizes, every CCR value, stack banks, ATC/physical/cache-push faults,
guest descriptor repair and PD cleanup. Checked/native CPU and memory
combinations pass. Its grouped cold/warm AOT runs compile one body cold and
restore it with zero compilations warm. The runner creates its dedicated
cache directory, and the fixture asserts that warm replay compiles nothing.

Grouped cache/reference, FPU, MMU, flat MOVE16 and full cold/warm AOT checks
pass. The nineteen emitted-kernel groups pass on Windows and Linux x64,
with 45 verified roots and no runtime imports. The full stream fixture passes
8,107,168 cases. Each mapped AOT recovery process passes 3,618,920 cases;
warm recovery reports 1,262,191 hits and zero compilations.

ROMAW is rebuilt and staged with the exact tested CPU archive. The 74-file
bundle at `build/68040-rte-user-stage/bin` is promoted to
`build/68040-current/bin`; the previous bundle is retained at
`build/68040-pre-rte-user-bin-20261126`. The audit at
`build/68040-rte-user-output/68040-rte-user-source-audit.json` verifies
179 CPU inputs, 158 fixtures and four identical CPU archives. CPU build ID is
`d433e25b599022826808741a12d3c2dcea025c5a5ead7f7dbdd6a37d42c49948`;
archive SHA256 is
`9d6bacc7ede7c1cb94a73d3ccf3e1a9466f20d49c3ae21f4899714a7299cbe61`.
Help returns zero with 7,669 bytes. Browser and 68K Speedometer reach their
startup loops after 30,952 and 77,157 native entries, respectively, with null
native checkpoints and no Windows exception. Speedometer issues one FPU
capability query. These checks cover startup rather than complete workflows.

This checkpoint follows page-crossing data replay in `e85de5f0` and unaligned
locked-store writeback in `e9e98a07`. The sections below retain earlier
checkpoint details. Remaining work includes cached partial-read diagnostics,
compound descriptor and locked cache-push continuations, further active-stack
and double-fault cases, physical instruction-prefetch behavior, snoop
integration, and FPU busy/FPSP saved-state, encoding and special-value audits.
The overall FPU/MMU work remains active; ROMAW-wide paged pointers and full
68040 compatibility are not established by these regressions.

## CAS/CAS2 termination and compare-register aliases (2026-11-26)

Mapped compare/exchange now follows MC68040UM 7.4.5 on comparison failure:
CAS writes back its read value, while CAS2 writes back only its second read
operand. Compare-register updates and subtraction flags remain visible. The
successful CAS2 first write uses private LOCKED_CONTINUE traffic; all its beats
retain LOCK without LOCKE. The final write, including the mismatch writeback,
asserts LOCKE only on its final physical chunk. The caller's cache binding is
not mutated. Both the mapped and generated legacy decoders now honor the PRM's
shared-compare-register rule: operand 1 takes precedence if Dc1 and Dc2 are the
same register. Ordinary flat bus policy is unchanged.

The bus rule follows the processor-specific
[MC68040UM section 7.4.5](https://www.nxp.com/docs/en/reference-manual/MC68040UM.pdf).
Its explicit second-operand CAS2 writeback differs from the destination-1 note
in the generic
[M68000 Programmer's Reference Manual CAS2 entry](https://www.nxp.com/docs/en/reference-manual/M68000PRM.pdf).
The model selects the processor manual for that bus detail and the PRM for the
unambiguous compare-register alias rule. This is a documented specification
choice, not a hardware measurement.

Aligned failures of those terminating writes use the existing retired-store
certificate. Physical and ATC faults preserve the captured old value on a
mismatch, or the captured update on success; a successful CAS2 first-write
fault also captures its independent second store. The guest repairs descriptors
when needed, drains the slots through original-function-code MOVES and returns
with RTE. Compare-register results, flags, address updates, full FPU state and
pending T0/T1 trace survive without another operand read. Checked and emitted
memory paths share the sequencing; this does not add a native integer opcode
emitter.

The 31,035-case fixture covers byte/word/long CAS and word/long CAS2, every line
offset, all cache modes, DE settings, empty/clean/dirty tags, both privilege
levels and comparison outcomes. It checks physical beat widths and order,
original-value mismatch writes, cache push/invalidation, final-only LOCKE,
register/update/address aliases. Identical-address updates check deterministic
runtime behavior; the PRM leaves overlapping memory updates undefined. Aligned
physical, protected-page and invalid-page write failures cover both successful
CAS2 write positions and its mismatch termination. Guest handlers preserve DFC
and their scratch registers; the fixture checks cleanup data and excludes
operand rereads. Link guards exclude original C MMU/cache transfer functions during
native initial execution. The final fixture rejects the previous CPU archive
because its failed CAS emits no terminating write. It separately rejects the
initial CAS update at the corrected shared-register result. A separate 384-case
matrix checks the ISA alias rule through flat execution, the shared component
and the MMU, using both interpreter and x64 JIT entry paths.

Ordinary and cold/warm standalone runs pass; warm replay restores eight images
without compilation. The final CPU archive retains the byte-identical emitted
MMU/cache image from the eighteen-group Windows/Linux checkpoint, with all 45
verified roots and their unchanged source inputs. The grouped cache/reference,
FPU, MMU, flat MOVE16 and full AOT gates pass. Reported coverage includes
1,153,398 FPU cases, 81,056 operand/differential cases plus the 384-case variant
with x64 JIT flat entry, 8,107,168 stream/recovery cases, 101,224 control-flow
cases and both 50,453-case flat MOVE16 runs. Full cold/warm AOT passes 3,618,920
mapped recovery cases per process; warm replay reports 1,262,191 hits and zero
compilations. The new 31,035-case fixture and existing MOVE16, allocation,
cache-op, push/fill, store and halt fixtures also pass cold/warm replay. Stable
identity, malformed-artifact fallback and publication checks pass.

ROMAW was rebuilt from application base `dc2e152e` with the exact tested CPU
archive. The 74-file bundle at `build/68040-cas-final-stage/bin` is promoted to
`build/68040-current/bin`; the previous MOVE16 bundle is retained at
`build/68040-pre-cas-bin-20261126`. The complete audit at
`build/68040-cas-final-output/68040-cas-final-source-audit.json` verifies 179 CPU
inputs, 155 fixture files, four identical CPU archives and all staged files.
It also records the retained negative-oracle archives and the unchanged emitted
kernel's earlier eighteen-group proof. CPU build ID is
`528e56be405522011198065b1ba44c8cacb0e28e6768ada68c854c136b079b77`;
archive SHA256 is
`bab6a114fb368fa3c38a7f762a0a53f5e31bf82b66b7127027fbecc319399fea`.
Help returns zero with 7,669 bytes. Browser reaches WaitNextEvent after 30,951
native entries; 68K Speedometer reaches its startup loop after 77,160 with one
FPU capability query. Both have null native checkpoints and no Windows
exception. These remain startup checks, not full interactive FPU workflows.

Further work includes unaligned locked-store fault certification, snoop and
compound bus continuations, and FPU busy/FPSP saved-state/restart compatibility.
The bus owner still supplies atomicity across the complete locked sequence.
Pin timing and ROMAW-wide guest paging are not established by this checkpoint.

## MOVE16 physical line integration (2026-11-26)

Mapped MOVE16 now uses a separate nonallocating physical line entry. Each read
and write half translates once, latches physical address and storage attributes,
and starts a four-longword wrapped burst at EA[3:2], ignoring EA[1:0]. A callback
changing a PTE and flushing the ATC affects the next translation, including the
separate destination half; it cannot remap the remaining physical beats.

Cacheable source hits use cached data. Inhibited dirty source hits push then
invalidate before the operand read. Destination hits invalidate without pushing;
misses never allocate. DE=0 bypasses existing tags. Replacement and the data-miss
bypass latch follow the modeled cache policy. Read output publishes only after
the complete half succeeds; writes snapshot all input before descriptor or data
callbacks. TT=1 operands remain distinct from TT/TM=0 cache pushes. The cache
active guard spans the complete half and rejects same-object callback reentry.
The documented same-register postincrement write-protection erratum discards a
dirty cached source while retaining its captured PD data for guest recovery.

The emitted image adds a typed line-bus root and a line packet on its cache
entry. Normal MMU walks, history changes, cache selection and RAM transfers run
inside that closed image on Windows and Linux x64; callbacks and fault-frame
handling remain checked. The packet ABI changed, so the CPU archive and ROMAW
were rebuilt together. MOVE16's shared integer instruction dispatcher remains
C; these tests establish emitted memory/cache execution, not a new native
integer opcode emitter.

Physical write faults retain all PD0-3 words and retire normal address updates
once. WB1S is now zero, as required by MC68040UM 8.4.6.3; SSW.TT=1 identifies the
pending line. ATC write faults instead retain WB2's line tag and original
PC/registers. Read aborts retain the original source address and no pending
write. Line metadata admission checks physical progress, cycle identity,
attributes and every captured byte. Dirty inhibited source-push faults retain
the physical push frame; guest physical MOVES cleanup and RTE retry complete
the original instruction. Pending T1 follows completed cleanup once.

Regression matrices cover 10,344 physical line cases, 802 MMU/latch/RAM cases
and 96,333 mapped CPU/cache/frame/guest-cleanup/trace cases. Both reference and
emitted physical fixtures cover partial failures and callback input mutation;
the mapped fixture checks all five forms, independent critical words, cache
states/modes, user/supervisor functions, source-push failures, malformed records
and the write-protection erratum. Link guards reject original C MMU/cache line
calls during native initial execution. The guest-code AOT sentinel shares the
mapped bus; MOVE16 memory work uses the emitted kernel in both JIT and AOT.

All eighteen emitted kernel groups pass on Windows x64 and Linux x64 with
sanitized boundaries. Both generated images verify 45 roots without live
imports or writable globals. The grouped cache/reference, FPU, MMU, flat MOVE16
and full cold/warm AOT gates pass: 1,153,398 reported FPU cases, 8,107,168 stream/
recovery cases, 101,224 control-flow cases and two 50,453-case flat MOVE16 runs.
The final handlers use the guaranteed FA field for physical MOVE16 cleanup,
independently of invalid WB1S. After those two test-offset refinements, affected
stream groups pass again in both execution modes, and all 3,618,920 mapped
recovery cases pass cold and warm; warm replay has 1,262,191 hits and zero
compilations. The 96,333-case mapped line fixture passes ordinary/cold/warm;
cold replay emits three images with two hits, and warm replay has five hits
with zero compilations. Other cache/recovery and malformed-artifact gates pass.

ROMAW was rebuilt from application base `dc2e152e` with the exact tested CPU
archive. The 74-file bundle at `build/68040-move16-stage/bin` is promoted to
`build/68040-current/bin`; the previous bundle is retained at
`build/68040-pre-move16-bin-20261126`. The complete audit at
`build/68040-move16-output/68040-move16-source-audit.json` verifies 179 CPU inputs,
154 fixture files, four identical CPU archives and all staged files. It records
the exact two test-offset changes after the full gate and their focused and
cold/warm revalidation. CPU build ID is
`ac6f97b622b45d64a2db58d7cdc0100575566db6c74ef74cffb5f2baabd028d1`;
archive SHA256 is
`e7a26c477424136ba41ab41f031b00747b8cd42079d6782e42cd8182dd44e51d`.
Help returns zero with 7,669 bytes. Browser reaches WaitNextEvent after 30,951
native entries; 68K Speedometer reaches its startup loop after 77,160 with one
FPU capability query. Both have null native checkpoints and no Windows
exception. These are startup checks, not complete interactive workflows.

Remaining work includes snoop integration, further compound and locked
continuations, full CAS/CAS2 sequencing and FPU busy/FPSP saved-state/restart
work. Destination descriptor-push failures after externally consumed MOVE16
source data still need continuation certification. This does not establish
silicon bus timing, ROMAW-wide paged pointers, Mac OS boot or complete
interactive FPU application compatibility. Capability advertisement remains
unchanged.

## Copyback allocation-fault recovery (2026-11-26)

A failed copyback write allocation now retains its original logical store in
format-7 WB2, with a captured following operand in WB3. WB1 is invalid: no
physical operand write has been issued while the DMU is waiting for the refill.
The SSW describes the execution unit's original write/size/FC, FA is the original
logical request and WB2/3 data remains register aligned. The refill bus address
and read beat remain separate cycle diagnostics. Frame routing is derived from
the writeback-stage ownership in MC68040UM 2.2/8.4.6.5 and table 8-6 note a;
these regressions
establish guest recovery through the model, not silicon timing.

The shared status validator now admits only valid ordinary-copyback data fill
snapshots, checking original logical progress and the physical wrapped beat.
It rejects unneeded trailing beats as fault metadata. The cache already ignores
such trailing fill failures after supplying the requested bytes, aborts allocation
and preserves normal store fallback; requested-byte failures preserve dirty
victims and leave clean victims invalid. Normal MMU/cache execution stays in the
unchanged closed emitted image. Checked frame helpers remain fault-only paths.

The 45,753-case matrix covers byte/word/long scalars at every line offset,
requested and unneeded beats, empty/clean/dirty victims, user/supervisor FCs,
T0/T1, logical aliases, noncontiguous page crossings and 32-bit address wrap.
Real guest MOVES drains WB2/WB3 before RTE, retaining completed scalar CCR/EA and
input reads. MOVEM, FPU S/D/X moves, register/control lists, null and captured
52/100-byte FSAVE exercise early replay, CM, final retirement and captured tails.
Enabled INEX2 conversion retains its saved state and reaches vector 49 before
trace after cleanup. Twenty-five malformed metadata variants per mode fail
before effects. Link guards prove that native modes bypass the original C MMU
translator/cache transfer functions during initial execution; checked mode
confirms the guards are active. The pre-fix CPU archive fails this fixture. MOVE16 writeback admission explicitly
rejects ordinary allocation fills: MOVE16's actual nonallocating transaction
model remains unfinished, and an allocation read must not masquerade as a
physical MOVE16 write.

The final fixture passes against the fresh CPU archive in ordinary, cold AOT
and fresh-process warm AOT runs. Cold AOT emits seven images with 373 hits;
warm replay reports 380 hits and zero compilations. A separate pre-guard build
fails the MOVE16 allocation-admission check. The final emitted native memory/cache
image remains byte-identical to the previously validated sixteen-group
Windows/Linux x64 image.

The final grouped cache/reference, FPU, MMU, flat MOVE16 and full AOT gates
pass. Validation includes 1,153,398 reported native FPU cases, 8,107,168 mapped
stream/recovery cases, 101,224 control-flow cases and both 50,453-case flat
MOVE16 runs. Full cold/warm AOT passes 3,618,920 recovery cases per process;
warm replay reports 1,262,191 hits and zero compilations. Existing cache-op,
push/fill, store-push and halt matrices pass cold/warm replay alongside the new
allocation matrix. Stable identity, malformed-cache fallback and publication
checks also pass.

ROMAW was rebuilt from application base `dc2e152e` with the exact tested CPU
archive, staged at `build/68040-allocate-stage/bin`, and promoted to
`build/68040-current/bin`. The previous bundle is retained at
`build/68040-pre-allocate-bin-20261126`. The full audit at
`build/68040-allocate-output/68040-allocate-source-audit.json` verifies 179 CPU
inputs, 22 fixtures, four identical CPU archives and all 74 staged files.
CPU build ID is
`be67b0f8f9a85c95c782419b6ef7e701b6e5e068d3ec3b74ee13dffc9e4820b4`;
archive SHA256 is
`9e97115b92abc227a43b265910083070fb9779e52337fb1949f8f6c9660a6210`.
Help returns zero with 7,669 bytes. Browser reaches WaitNextEvent after 30,874
native entries; 68K Speedometer reaches its startup loop after 77,157 with one
FPU capability query. Both have null native checkpoints and no Windows
exception. These remain startup checks rather than complete interactive workflows.

MOVE16/snoop transactions, additional compound/bus continuations, full CAS/CAS2
sequencing and the remaining FPU saved-state/restart work still need completion.

## Descriptor-lockup and double-fault halts (2026-11-26)

Mapped instruction execution now latches a distinct descriptor-lockup halt
when a table search hits a disabled data-cache tag. It reports HALTED with the
failed access snapshot before constructing a guest access-fault frame, applying
store-retirement certificates or scheduling trace. Descriptor lockups during
exception stack/vector accesses also halt and retain completed bus effects.
Double-fault delivery keeps its DOUBLE_FAULT result and latches a separate
halt reason. Subsequent run and exception-entry calls are inert despite IRQ7,
STOP, PC changes or descriptor/cache repairs. Zero-budget run calls retain
BUDGET without releasing the halt. Invalid halt values fail before effects.

The embedding owns hardware reset and reinitializes the mapped CPU and its
architectural state together; the new regression resets the same CPU instance.
This is a halt/reinitialization contract, not a new reset-vector or pin model.
Existing STOP wakeup and ordinary recoverable exception behavior are preserved.
The new 8,154-case matrix covers all descriptor levels, four cache ways, clean
and dirty tags, privilege/master banks, trace, instruction fetch, scalar/FPU
reads and writes, MOVEM, PTEST, FSAVE/FRESTORE, RTE, exception stack/vector
lockups, double faults and normal enabled-cache/miss controls. Full snapshots
prove that halted calls do not change CPU/MMU/cache/RAM state or callbacks,
compile native code or execute native entries. The pre-fix archive fails the
same fixture. Cold AOT emits four images with 861 hits; fresh-process warm
replay has 865 hits and zero compilations.

The rebuilt native memory/cache image is byte-identical to the preceding
validated image, retaining its sixteen-group Windows/Linux x64 kernel proof;
this increment changes only CPU runner/delivery state and its public header.
The grouped cache/reference, FPU, MMU, flat MOVE16 and full AOT gates all pass.
They include 1,153,398 reported native FPU cases, 8,107,168 mapped stream/recovery
cases, 101,224 control-flow cases and two 50,453-case flat MOVE16 runs. Full AOT
passes 3,618,920 recovery cases per process, with 1,262,191 hits and zero
compilations on warm replay; the existing cache-op/push/store and new halt
matrices also pass cold/warm replay. Source identity, malformed-cache fallback
and publication checks pass.

ROMAW was rebuilt from application base `dc2e152e` using the exact tested CPU
archive. The 74-file `build/68040-halt-stage/bin` bundle is promoted to
`build/68040-current/bin`; its predecessor is retained at
`build/68040-pre-halt-bin-20261126`. The full audit at
`build/68040-halt-output/68040-halt-source-audit.json` verifies 179 CPU inputs,
21 fixtures, four identical CPU archives and all staged hashes. CPU build ID is
`260074b0e506f9d59e1ba7467758f7fc5cab64ead2677d186761e234bcfd4f55`;
archive SHA256 is
`532ba38d8852bd8fc36a3eb9ec001c839e29bd04d54016b4560b5400593a763d`.
Help returns zero with 7,669 bytes. Browser reaches WaitNextEvent after 30,954
native entries; 68K Speedometer reaches its startup loop after 77,165 with one
FPU capability query. Both have null native checkpoints and no Windows
exception. These are startup checks, not complete interactive workflows.

Write-allocate fills, additional compound continuations, MOVE16/snoop, full
CAS/CAS2 sequencing and the remaining FPU saved-state/restart work remain.

## Store-generated cache-push recovery (2026-11-26)

Store retirement now separates the failed physical cache push from its original
logical operand. A complete copyback store already owns its cache bytes and
needs no duplicate operand writeback. An incomplete store retains its full
logical scalar in WB2. A captured tail uses WB3 when the current operand is
pending, otherwise WB2. Both slots preserve original size, FC and register-aligned
data; FA/WB1A and PD0-3 remain physical, and WB1 is invalid for a push.

The shared logical-operand status API feeds CPU and FPU fault-offer validation.
Final scalar, FPU/list/control and FSAVE stores retain completed flags/EA/FPU
work and resume at the following instruction without repeating input reads.
Early certified FPU stores instead reexecute at the original PC, retaining
captured state and any pending operand. MOVEM keeps CM/EA with its documented
list replay. CT/CP/CM and their EAs survive the physical-push frame overlay;
FSAVE's captured tail remains valid after accepted saved-frame cleanup.

The 27,795-case matrix passes checked/native callback/permanent-RAM modes,
every byte offset and scalar width, copyback/inhibited mappings, user/supervisor
FCs, single/line pushes, failed beats, CCRs, trace, MOVEM read/write, FPU moves,
register/control lists and 52/100-byte FSAVE early/late failures. A callback
input source confirms retirement never repeats its reads. Real guest MOVES
drains physical PD first and logical WB2/WB3 next, preserving live EA fields.
Enabled-inexact output cases retain the pending FPU frame and dispatch vector
49 before trace. An adversarial U-clear/M-set page descriptor exercises a locked
U-only descriptor-push failure with physical TM=0/LK and a distinct virtual
store address, then repairs history through guest writeback. UM 3.2.1 prohibits
that incoherent resident descriptor state; this checks deterministic recovery
without establishing hardware behavior for it. Invalid progress/retirement
metadata is rejected before effects.
Cold AOT emits eight images with 1,272 hits; fresh-process warm replay reports
1,280 hits and zero compilations.

Grouped validation passes the cache/reference and emitted-kernel suites on
Windows/Linux x64 (sixteen kernel groups per ABI), all 1,153,398 reported native
FPU cases, 8,107,168 mapped stream/recovery cases, 101,224 mapped control-flow
cases and both 50,453-case flat MOVE16 runs. Full cold/warm AOT passes
3,618,920 recovery cases per process; warm replay has 1,262,191 hits and zero
compilations. Existing cache-op and read-fill/push recovery matrices also pass.
The isolated publication fixture uses fail-fast stubs for the new fault-status
APIs; it never executes fault continuations.

ROMAW was rebuilt from application base `dc2e152e` with the exact tested CPU
archive, staged in `build/68040-cache-store-stage/bin`, and promoted to
`build/68040-current/bin`. The prior bundle is retained at
`build/68040-pre-cache-store-bin-20261126`. The source audit at
`build/68040-cache-store-output/68040-cache-store-source-audit.json` verifies
179 CPU inputs, twenty fixtures, four identical CPU archives and all 74 staged
files. CPU build ID is
`c1fc14ee212978afd5e5f843dee211a879c9c2f4411a8dce6abd952b072953ba`;
archive SHA256 is
`f8a2c10b7247800f256cfcc59c83f4bd893c3b436c11604e0c9d1460b5d19c35`.
Help returns zero with 7,669 bytes. Browser reaches WaitNextEvent after 30,942
native entries; the 68K Speedometer startup reaches its event loop after
77,159, with one FPU capability query. Both checkpoints are null and report no
Windows exception. These are startup checks, not full interactive workflows.

Write-allocate fill faults, additional compound/bus continuations,
descriptor-lockup halt, MOVE16/snoop integration, full CAS/CAS2 sequencing and
the remaining FPU saved-state/restart work still require completion.

## Physical push frames and read-fill recovery (2026-11-25)

Mapped execution now certifies unchanged unretired CPUSH/PTEST and ordinary
read/fetch cache-push aborts. Format 7 retains the physical initial push address
in FA/WB1A and all four memory-aligned PD words, including single-word pushes.
SSW uses the push size and locked-cycle state, with TM=0 and invalid WB1/2/3.
No hidden continuation flag or automatic physical cleanup is added. Guest
MOVES drains the captured data before RTE reexecutes the original instruction;
completed cache invalidations remain complete. Trace follows successful replay.

Read-fill faults retain the original logical operand address/size with RW set,
including user data and instruction fetches. Existing read certification restores
owned speculative An updates while retaining physical callback/history effects.
MOVEM read fills use CM/EA with earlier register loads retained. RTE fill
and push faults restart at the live privileged SP, retaining completed
throwaway frames and callback effects without restoring an earlier SP. Host callback changes deny rollback
without discarding the changed state. Invalid physical snapshots or conflicting
retirement metadata are rejected before touching CPU state or the bus.

The new 14,559-case matrix passes checked and native callback/permanent-RAM
modes, real guest MOVES cleanup/RTE, maintenance scopes, dirty masks, every
line beat, ISP/MSP, trace, user data reads, MOVEM/RTE fills, instruction-fetch
and locked descriptor-push faults, malformed snapshots and callback mutations.
Cold AOT emits seven images with 571 hits; a fresh warm process reports 578
hits and zero compilations. Second-needed-beat and cross-line read faults retain
the original operand FA/size without publishing a successful prefix. RTE push
cases retain the live SP after throwaway frames, including callback mutations. The descriptor cleanup guest executes PFLUSHA
after draining its captured old PTE, allowing a fresh walk to publish U history.

The corrected archive passes 1,153,398 reported FPU-native cases, 8,107,168
MMU stream/recovery cases, 101,224 flow/RTE cases and 100,906 flat MOVE16
cases. All sixteen emitted kernel groups pass on both ABIs. Grouped AOT
passes 3,618,920 recovery cases per run with 73 cold emissions and 1,262,191
warm hits; the existing 37,404-case cache-op replay and the new 14,559-case
frame replay also pass with zero warm compilation. Build identity and malformed
cache fallback pass. The audit at
`build/68040-cache-frame-output/68040-cache-frame-source-audit.json` matches
179 CPU inputs, eighteen fixtures and all 74 staged files.

ROMAW is rebuilt from application base `dc2e152e` with the tested CPU overlay,
and promoted from `build/68040-cache-frame-stage/bin` to
`build/68040-current/bin`. The previous bundle is preserved at
`build/68040-pre-cache-frame-bin-20261125`. Help exits zero with 7,669 bytes.
Browser/Speedometer startups reach WaitNextEvent with 30,949/77,157 native
entries, nil checkpoints, no win_exception and one Speedometer FPU query.
These remain startup checks rather than a complete interactive FPU workflow.

Committed-store and compound cache-push continuations, write-allocate fills,
descriptor-lockup halt, MOVE16/snoop integration, full CAS/CAS2 sequencing and
the remaining FPU busy-frame and mapped restart work remain required. This is
an incremental integration checkpoint, not full 68040 completion.

## Mapped CINV/CPUSH and interruptible native maintenance (2026-11-25)

Mapped CINV/CPUSH now decodes all cache selections and line/page/all scopes.
An is a physical address even with translation enabled, and TC selects 4K/8K
pages. Disabled cache banks still participate. Scope zero is illegal before
privilege checking; valid user operations take privilege violation. A valid
supervisor operation with no board cache binding reports unsupported execution.
General registers and CCRs are preserved. Both T0 and T1 trace follow completion.
The flat application's existing cache-op compatibility path is unchanged.

The closed native image adds a memory-bound maintenance root. Normal selection,
push/invalidation and live IRQ sampling run inside that image. Interrupts are
sampled before maintenance and after each selected line completes. Completed
pushes/invalidations survive interruption, while the PC remains at the original
instruction and trace waits for replay. The pending interrupt is consumed only
by accepted CPU exception entry. A failed push wins over an IRQ raised by the
failed physical callback and retains the complete captured line independently
of its invalidated cache tag.

The maintenance reference matrix passes 1,648 cases on Windows and Linux
ASan/UBSan. All sixteen emitted kernel groups pass on both ABIs, including
3,240 maintenance cases per ABI with original host maintenance helpers rejected
by link-time guards. The mapped CPU matrix passes 37,404 opcode, physical-scope,
privilege, trace, interruption/replay and push-fault cases across checked,
native callback and native permanent-RAM configurations. Its cold AOT run emits
256 records with 258 hits; fresh-process replay loads 514 with zero compilation.

Grouped validation also passes 1,153,398 reported FPU-native cases,
8,107,168 MMU stream/recovery cases, 101,224 flow/RTE cases and 100,906 flat
MOVE16 cases. Cold/warm AOT passes 3,618,920 recovery cases per run, with 73
cold emissions, 1,262,191 warm hits and zero warm compilations. The grouped
cache-op replay independently repeats all 37,404 cases per process and reports
zero warm compilation. Stable build identity and malformed-cache fallback pass.
The full audit at
`build/68040-cache-op-output/68040-cache-op-source-audit.json` matches 179 CPU
inputs, seventeen fixtures and all 74 staged files. The verified bundle is
promoted to `build/68040-current/bin`, preserving its predecessor at
`build/68040-pre-cache-op-bin-20261125`.

ROMAW is rebuilt from the pinned application base `dc2e152e` with this CPU
overlay at `build/68040-cache-op-stage/bin`. Help exits zero with 7,669 bytes.
Isolated Browser/Speedometer startups reach WaitNextEvent with 30,943/76,933
native entries, no live checkpoint or win_exception, and one Speedometer FPU
query. Speedometer's private profile explicitly selects its 68K executable;
these are startup checks, not a completed interactive FPU workflow.

This step does not supply cache-push/fill format-7 delivery, descriptor-lockup
halt, MOVE16/snoop transactions, the remaining CAS/CAS2 bus sequencing or RTE
stack-policy integration. Those remain required for full FPU/MMU completion.

## Cache-aware mapped memory: CPU cache/fault integration ongoing (2026-11-25)

An optional `Syn68kMMU040Cache` binding now routes normal operands and table
searches through the physical cache in checked and closed native execution.
Descriptor reads/writes retain the separate table callbacks; locked U updates
push/invalidate first, then retain the embedding's atomic read/OR/write.
Permanent native RAM handles these operations inside the emitted image.
Cross-page locked writes assert LOCKE only on the final physical chunk.

Access results retain the physical cache failure separately from the logical
operand and translation. Ordinary failed table callbacks remain diagnostic
PTEST results; cache-push/lockup failures preserve MMUSR and remain visible to
the CPU, retaining live MMUSR changes made by physical callbacks on those
failure paths. A physical abort does not publish a negative logical ATC entry;
captured-push cleanup can retry the walk without a PFLUSH. The scalar SSW helper
rejects fill/push/lockup results that need their own frame or halt handling.
Cache-aware copyback stores can report all operand
bytes completed even when a subsequent victim push fails.

The mapped runner observes physical cache callbacks for host register/null
changes and tracks logical stores even when a cache hit avoids the bus.
Exception entry uses no-allocation traffic while retaining normal hit policy.
The memory/access/native invocation layouts grew and require a full rebuild.

The reference matrix passes 6,183 cases on Windows and Linux ASan/UBSan. All
fifteen emitted kernel groups pass on both ABIs, including 12,359 cache-aware
MMU cases per ABI with callback and permanent-RAM configurations. A separate
nine-case mapped CPU probe covers cached instruction/data views, native
PTEST/FPU execution and exception-stack hit/miss policy. Cold AOT emits two
records; fresh-process warm replay loads both with zero new compilations.

The corrected image passes 1,153,398 reported FPU-native cases, 8,107,168 MMU
stream/recovery cases, 101,224 flow/RTE cases and 100,906 flat MOVE16 cases.
Cold/warm AOT passes 3,618,920 recovery cases per run: 73 cold images,
1,262,191 warm hits and zero warm compilations. The cached CPU probe also
passes within the grouped AOT run, emitting two cold records and loading both
in a fresh warm process without compilation. Stable identity and malformed
cache fallback pass.

ROMAW is rebuilt from committed application base `dc2e152e` with this CPU
overlay at `build/68040-cache-binding-abort-stage/bin`. The complete audit at
`build/68040-cache-binding-abort-output/68040-cache-binding-abort-source-audit.json`
verifies 179 CPU inputs, fifteen fixtures and all 74 staged files. The bundle
is promoted to `build/68040-current/bin`, preserving its predecessor at
`build/68040-pre-cache-binding-abort-bin-20261125`. Help exits
zero with 7,669 output bytes. Isolated Browser/Speedometer startups reach
WaitNextEvent with 30,868/76,939 native entries, no live checkpoint and no
win_exception; Speedometer makes one FPU query. These are startup checks.

CPU cache-op decoding, cache-push/fill frame delivery, descriptor-lockup halt,
MOVE16/snoop integration, RTE stack policy and full CAS/CAS2 bus sequencing
remain work. ROMAW-wide guest paging and FPU capability reporting remain at
the existing boundary.

## Cache traffic classes: CPU routing pending (2026-11-25)

The physical cache component now distinguishes ordinary, exception, descriptor
and locked traffic. One-to-four-byte chunks include MMU three-byte page tails.
Preferred typed bus callbacks retain traffic class, operand/push identity and
LOCK/LOCKE intent; legacy callbacks remain supported. Bus faults capture their
cycle metadata by value. Native permanent RAM uses the same classification,
including the distinction between TM=0 pushes and alternate-space FC=0 writes.

Exception traffic does not allocate on misses. Descriptor traffic uses
write-through/no-allocation and reports the documented disabled-cache-hit
lockup without issuing a physical cycle. Locked accesses push/invalidate cache
hits even with DE clear, and the next enabled nonlocked data miss bypasses
allocation once after such a hit. Nonfinal write chunks suppress LOCKE.
Locked line maintenance prepares descriptor atomic updates; caller/bus
ownership still supplies complete RMW atomicity.

The ordinary Windows and Linux ASan/UBSan traffic matrix passes 8,560 cases.
Both ABIs pass all fourteen emitted kernel groups, including that matrix,
the existing 2,166 cache cases and 395 native RAM/chunk/bounds cases per ABI.
Link guards reject original C transfer and maintenance helpers. Cache storage,
bus/result and native cache packets grew; callers must rebuild. Other native
packet layouts are unchanged.

CPU operand/instruction routing, cache-aware descriptor/exception bindings,
cache-push frame delivery, MOVE16 and snoop integration remain work. Full
FPU-native validation passes 1,153,398 reported cases, with 8,107,168 MMU
stream/recovery cases, 101,224 flow/RTE cases and 100,906 flat MOVE16 cases.
Cold/warm AOT passes 3,618,920 recovery cases per run: 73 cold images,
1,262,191 warm hits and zero warm compilations. Stable identity and
malformed-cache fallback pass. These AOT checks cover the existing CPU paths;
they do not establish guest cache-instruction execution.

ROMAW was rebuilt from committed application base `201f97bf` with this CPU
overlay at `build/68040-cache-traffic-stage/bin`. The complete audit at
`build/68040-cache-traffic-source-audit.json` verifies 179 CPU inputs, thirteen
fixtures and all 74 staged files. The bundle is promoted to
`build/68040-current/bin`, with its predecessor preserved at
`build/68040-pre-cache-traffic-bin-20261125`. Help exits zero with 7,669 output bytes.
Isolated Browser/Speedometer startups reach WaitNextEvent with 30,873/76,937
native entries, no live checkpoint and no win_exception; Speedometer makes
one FPU query. These checks establish startup, not application workflows.

## Native physical cache kernels: CPU binding pending (2026-11-25)

The closed native image now includes cache-access and cache-maintenance roots
on Windows and Linux x64. Normal physical RAM reads, fills, copyback stores
and victim pushes execute inside that image. Alternate-space accesses retain
checked callbacks, including FC=0 requests that must not be confused with
TM=0 victim pushes. Packets retain live caller-owned cache/bus/RAM pointers;
existing native operation numbers and packet layouts are unchanged.

All thirteen emitted kernel groups pass on both ABIs. The new groups include
2,166 cache-policy/burst/fault/scope cases and 299 permanent-RAM, push,
alternate-space and bounds cases per ABI. Link-time guards abort calls to the
original C cache-access/maintenance functions, and image extraction rejects
live runtime imports. Linux boundaries run under ASan/UBSan. The Windows
regression image matches the CPU archive's generated image byte for byte.

The specification audit corrected cache-before-bus write ordering (table 4-4,
D3/V6/D6). Copyback write allocation now installs the new dirty operand before
the old victim push; write-through hits update their complete cache portion
before external beats. A push failure retains the new cached store and counts
its completed operand bytes. A write-through failure retains the cache update
and reports only completed external beats. The previous implementation failed
all four targeted callback observations. The maintained suite adds 473
callback-visible success/fault and cross-line completion regressions.

These entry points do not yet bind mapped CPU cache instructions or ordinary
mapped operands. Locked/MOVE16/snoop operations, exception/table traffic and
cache-push frame delivery remain part of the CPU integration work. See the
[cache component contract](cache040.md).

The corrected image passes the complete FPU-native group (1,153,398 reported
cases), 8,107,168 MMU stream/recovery cases, 101,224 control-flow/RTE cases and
100,906 flat MOVE16 cases. Cold/warm AOT passes 3,618,920 recovery cases per
run: 73 cold images, 1,262,191 warm hits and zero warm compilations. Stable
identity and malformed-cache fallback pass. These AOT regressions cover the
existing CPU paths; guest cache instructions still await CPU binding.

ROMAW was rebuilt from committed application base `0bab4a94` with this CPU
overlay and staged at `build/68040-cache-native-stage/bin`. The complete audit
at `build/68040-cache-native-source-audit.json` verifies 178 CPU inputs, twelve
fixtures and all 74 staged files. The bundle is promoted to
`build/68040-current/bin`, preserving its predecessor at
`build/68040-pre-cache-native-bin-20261125`. Help exits zero with 7,669 output
bytes. Isolated Browser/Speedometer 68K startups reach WaitNextEvent with
30,883/76,938 native entries, no live checkpoint and no win_exception;
Speedometer makes one FPU query. These are startup checks.

## Physical cache component: binding in progress (2026-11-25)

`cache040.c` adds caller-owned physical data/instruction banks with 64 sets,
four ways, 16-byte lines, per-longword dirty bits and independent two-bit
replacement counters. The reference is
[MC68040UM chapter 4](https://www.nxp.com/docs/en/reference-manual/MC68040UM.pdf).
The component handles critical-word-first fills, write-through/no-allocation,
copyback allocation, cache-inhibited data aliases, disabled-cache bypass and
explicit physical line/page/all invalidation or push selection. It does not
invalidate instruction data automatically after data-cache writes.

Fill failures discard clean victims and restore dirty victims. A trailing fill
fault can satisfy the original operand without allocating a line. The first
beat's cache-inhibit response prevents allocation; later responses are ignored.
Successful fills precede victim copyback. Push faults retain the complete
memory-addressed victim for subsequent exception delivery, while preserving
completed bus writes and maintenance prefixes. No CPU fault frame is delivered
by this component itself.

`bash syn68k/tests/check-core.sh cache040` groups 1,693 Windows cases and the
same Linux ASan/UBSan cases. Tests observe backing RAM, transfer order, all
sets/ways and replacement states, byte/word/long alignment, dirty-word masks,
fill/inhibit/error cases, maintenance scopes and push failures. The component
is included in the runtime archive; native-kernel publication and mapped CPU
binding remain work. In particular, locked/MOVE16/snoop transactions, exception
traffic classification and cache-push frame delivery still need integration.
This checkpoint does not claim working mapped CINV/CPUSH or a complete cache.
The archive-linked cache fixture also passes all 1,693 cases. Grouped validation
passes 8,107,168 MMU stream/recovery, 101,224 control-flow/RTE and 100,906 flat
MOVE16 cases. Complete cold/warm AOT passes 3,618,920 recovery cases per run:
73 cold images, 1,262,191 warm hits and zero warm compilations. Stable identity
and malformed-cache fallback pass. The emitted kernel is unchanged; the cache
component is not yet part of that image.

ROMAW was rebuilt from committed application base `49f26a6f` with this CPU
overlay and staged at `build/68040-cache-storage-stage/bin`. The audit at
`build/68040-cache-storage-source-audit.json` verifies 177 CPU inputs, nine
fixtures and all 74 staged files. The bundle is promoted to
`build/68040-current/bin`, preserving its predecessor at
`build/68040-pre-cache-storage-bin-20261125`. Help exits zero with 7,669 output
bytes. Isolated Browser/Speedometer 68K startups reach WaitNextEvent with
30,864/76,933 native entries, no live checkpoint and no win_exception;
Speedometer makes one FPU query. These are startup checks.

## MOVE16 read-fault recovery (2026-11-25)

Mapped MOVE16 data-read faults now offer a certified abort without retiring PC
or address updates. The format-7 frame retains the original burst address,
normal read transfer type and line size, with no pending write or push data.
Guest repair/RTE repeats the complete source read, including successful partial
reads, then performs destination writes and postincrements once. Entry trace is
reported only after successful reexecution. Extension-fetch faults use ordinary
instruction-read restart metadata. Callback changes to certified CPU/FPU state
deny restart without rollback.

This follows MC68040UM sections 7.4.2 and 8.4.6.2/4/7: the source operand is a
line transfer, FA retains the initial access address, and read faults do not
carry pending WB1/WB2 data. The physical bus still uses ordered scalar callbacks;
this change does not introduce a physical cache or whole-burst callback API.

An isolated baseline probe found unsupported exception delivery in all 81,920
tested MOVE16 read-fault combinations. The corrected focused regression passes
86,164 recovery and guard cases across both dispatch and bus bindings. It covers
both page sizes, all stack modes, five forms, aliases, burst start words, physical
and ATC faults at each word, guest descriptor repair/RTE, fresh source data on
retry, postincrements and trace. Malformed records and callback mutations are
checked before any delivery effects. A separate extension-fetch/partial-read/RTE
probe passes 36 cases. Grouped validation passes 8,107,168 MMU stream/recovery,
101,224 control-flow/RTE and 100,906 flat MOVE16 cases. Complete cold/warm AOT
passes 3,618,920 recovery cases per run: 73 cold images, 1,262,191 warm hits
and zero warm compilations. Stable identity and malformed-cache fallback pass.

The rebuilt bundle is at `build/68040-move16-read-stage/bin`, using committed
application base `dd45e595` plus the tested CPU overlay. Help exits zero with
7,669 output bytes. Isolated Browser/Speedometer 68K startups reach WaitNextEvent
with 30,864/76,931 native entries, no live checkpoint and no win_exception;
Speedometer makes one FPU query. These are startup checks. The emitted native
kernel image is byte-identical to the previously validated image. The complete
audit at `build/68040-move16-read-source-audit.json` verifies 175 CPU inputs,
eight fixtures and all 74 staged files. The bundle is promoted to
`build/68040-current/bin`, preserving its predecessor at
`build/68040-pre-move16-read-bin-20261125`.

## Forced-exception tracing (2026-11-25)

The mapped runner now latches instruction-entry T1 for taken TRAP, TRAPV,
TRAPcc, integer division/check exceptions and FTRAPcc. Delivery completes the
forced exception frame and vector fetch first, then stacks a format-2 trace
frame before executing the forced handler. That frame retains the handler PC
and original instruction address; two guest RTEs restore the original context.
Opcode callbacks changing live trace bits do not create or cancel the latch.
T0 alone does not trace these forced exceptions. Illegal/privilege/access faults
and FPU numeric/preinstruction exceptions retain their separate policies.

The focused suite passes 11,972 cases across both dispatch and bus bindings,
including all stack modes, malformed records, callback trace changes, delivery
faults and interrupt priority. Grouped MMU validation passes 8,021,004 stream
cases and 101,224 control-flow/RTE cases. Delivery faults retain completed frame
writes and return the existing access-fault result; embedding code still owns
double-fault recovery. Host step records grew; native packet layouts and the
previously validated emitted kernel image are unchanged.

The rebuilt bundle is at `build/68040-forced-trace-stage/bin`, using committed
application base `f21a354f` with the tested CPU overlay. Help exits zero with
7,669 output bytes. Isolated Browser/Speedometer 68K startups reach WaitNextEvent
with 30,874/76,937 native entries, no live checkpoint and no win_exception.
Speedometer makes one FPU query. These checks cover startup, not an FPU workflow.
The exact-source audit covers 175 CPU inputs, eight fixtures and 74 staged files.
Complete cold/warm AOT passes 3,575,838 recovery cases per run: 73 cold images,
1,262,191 warm hits and zero warm compilations. Stable identity and malformed
cache fallback pass. `build/68040-forced-trace-source-audit.json` records the
complete validation and source/binary hashes. The 74-file bundle is promoted to
`build/68040-current/bin`; its predecessor is preserved at
`build/68040-pre-forced-trace-bin-20261125`.

## MOVE16 captured-line writebacks (2026-11-25)

Mapped MOVE16 write faults now retain all four memory-aligned PD0-3 words.
Physical failures complete normal postincrements once, including same-register
aliases, and retain the following PC with WB1/TT=1/SIZE=line. Guest cleanup
does not reread source RAM. Entry T1 retains CT and the original instruction PC
until cleanup/RTE; callback changes to live trace bits do not change that latch.

Certified ATC aborts retain original PC/registers and WB2/PD0-3. A guest handler
can repair and retry, or drain the captured line, advance PC/postincrements and
emulate pending trace. Callback result changes deny certification without
rollback. The frame follows MC68040UM section 8.4.6.7/table 8-6 and the corrected
hard-cleanup case in the [manual addendum](https://www.nxp.com/docs/en/reference-manual/MC68040UMAD.pdf).
Host access metadata identifies the failed scalar; the line record separately
retains the original burst request. The bus remains an ordered scalar-transfer
boundary. MOVE16 read continuation and a physical block/cache bus remain work.
Host step records grew; callers must rebuild. Native packet layouts are unchanged.

Validation passes 107,772 focused cases, 8,009,032 grouped MMU stream/recovery
cases, 101,224 control-flow/RTE cases and 100,906 flat MOVE16 cases. Complete
cold/warm AOT passes 3,569,852 recovery cases per run: 72 cold images, 1,262,189
warm hits and zero warm compilations. Stable identity and malformed-cache
fallback pass. The native kernel image remains byte-identical to the validated
Windows/SysV and FPU-native image.

The rebuilt bundle is staged at `build/68040-move16-line-stage/bin` and promoted
to `build/68040-current/bin`, with all 74 files verified. The preceding bundle
is preserved at `build/68040-pre-move16-line-bin-20261125`. All 175
CPU inputs, eight fixtures and 74 staged files match the maintained/tested/
application snapshots; `build/68040-move16-line-source-audit.json` records them.
The coherent application base remains `e062e3db` with this CPU overlay. Windows
help passes (7,669 bytes, exit zero). Isolated Browser/Speedometer 68K startups
reach WaitNextEvent with 30,872/76,933 native entries, no live checkpoint and no
win_exception. Speedometer makes one FPU query. These are startup checks.

## MOVE16 effective-address burst order (2026-11-25)

MOVE16 now starts each source and destination burst at the longword selected
by that operand's effective address, wrapping within the 16-byte line. Holding
registers remain indexed by memory address, so differing source/destination
start words preserve the complete line layout. All reads still precede writes;
normal register updates and CCR behavior are unchanged.

The previous implementation always visited word zero first. A diagnostic found
incorrect callback order in 1,920 of 2,048 normal cases despite correct final RAM.
An isolated correction passes 184,320 maintained-regression cases: all five
forms, all source/destination low-address bits, both page sizes and privileges,
and every physical read/write fault position across dispatch/bus bindings.
Checks observe actual callback order, the committed write prefix, failing scalar,
CPU/FPU state and normal postincrement. The older mapped-flow fixture now expects
no earlier write when destination EA +7 faults on its first selected word +4.

The reference is the MOVE16 description in the
[M68000 Programmer's Reference Manual](https://www.nxp.com/docs/en/reference-manual/M68000PRM.pdf).
This corrects ordering within the existing word-transfer boundary. MOVE16 block
transfer attributes and captured-line format-7 writeback/restart remain work;
this increment does not introduce a physical cache or burst-bus model.

The fresh private CPU build passes grouped MMU validation: 7,901,260
stream/recovery and 101,224 control-flow/RTE cases. Flat MOVE16 passes 100,906
interpreter/native line, register, CCR, user-mode and bounded-fault cases.
Complete cold/warm AOT passes 3,515,966 guest recovery cases per run: 71 cold
images, 1,262,187 warm hits and zero warm compilations. Stable identity and
malformed-cache fallback pass. The normal native kernel image remains
byte-identical to the previously validated Windows/SysV and FPU-native image.

ROMAW was rebuilt with the exact tested CPU archive. All 175 CPU inputs, eight
fixtures and 74 staged files match the maintained/tested/application snapshots.
`build/68040-move16-order-stage/bin` is promoted to `build/68040-current/bin`;
the preceding store bundle is preserved at
`build/68040-pre-move16-order-bin-20261125`. The audit is recorded in
`build/68040-move16-order-source-audit.json`. The coherent application base
remains `e062e3db` with the current CPU overlay.

Windows help passes (exit zero, 7,669 bytes). Isolated-profile Browser startup
passes with 30,864 native entries; Speedometer's 68K startup passes with 76,933
entries and one FPU query. Both reach WaitNextEvent without a live native
checkpoint or win_exception entry. These remain startup checks, not a full FPU
application benchmark.

## MOVEM page-boundary and active-stack validation (2026-11-25)

The maintained stream suite now includes 35,712 additional MOVEM store cases
covering A0 through A7 as the EA register, indirect/predecrement addressing,
word/long operands, both page sizes and privilege levels, trace, and both
operand/aligned-transfer bus policies. Protected and missing second pages fault
after zero, one, two or three completed scalar bytes. For descending transfers,
a device callback invalidates/protects the page only after the preceding scalar's
last successful subcycle, preserving earlier writes.

Real guest WB2 cleanup and RTE verify CM/EA, MA, original scalar data, completed
register effects and the full repeated list. Active user A7 returns through its
separate supervisor stack; active supervisor A7 builds the access frame below
the remaining operand. Odd supervisor SP cases are excluded and remain part of
the double-fault audit. Handler scratch-register/source aliases remain visible
on replay. These tests pass against the committed store implementation without
CPU changes.

Grouped MMU validation passes 7,716,940 stream/recovery cases and 101,224
control-flow/RTE cases. Complete cold/warm AOT passes 3,423,806 guest recovery
cases per run: 71 cold images, 1,262,187 warm hits and zero warm compilations.
Stable identity and malformed-cache fallback pass. The tested CPU archive and
promoted ROMAW bundle remain byte-identical to the store checkpoint below;
all 74 promoted files were verified again. No binary rebuild is required for
this test-only increment. All 175 CPU inputs and six current fixtures match
the tested/application snapshots; the updated audit is recorded in
`build/68040-movem-page-stack-source-audit.json`.

## MOVEM store CM and writeback recovery (2026-11-25)

Mapped MOVEM store operand faults now carry CM and the original effective
address, plus the failed scalar in WB1 for a physical bus error or WB2 for an
ATC fault. The instruction retains its original PC and completed partial writes;
it is not retired. Guest cleanup drains the captured scalar before RTE repeats
the operand list from the saved EA. Predecrement resumes the original address
sequence and stored base-register value even after a handler changes the base.

This extends the existing CM mechanism without adding a guest continuation bit.
Further opcode, mask or extension fetch faults retain CM/EA and have no pending
writeback. Malformed CM/store records are rejected before CPU or bus effects.
The normal native kernel image is unchanged; checked continuation runs on the
exceptional path. The restart contract follows
[MC68040UM section 8.4.6.7](https://www.nxp.com/docs/en/reference-manual/MC68040UM.pdf).
The manual interpretation and hardware-validation limits in the read checkpoint
below still apply.

Focused checks pass 80,928 cases across both dispatch modes and bus bindings.
They cover legal store EA forms, brief/full indirect indexing, word/long lists,
all natural bus lanes and partial physical transfers, protected/missing-page
faults before the first store, entry trace modes, base-register replacement,
source-list aliases, guest WB1/WB2 cleanup and real RTE. A store overwrites its
own indirect pointer; CM replay must not reread it. Repeated data and instruction
fetch faults preserve the original EA. Interrupts remain deferred during CM.
The EA register in this matrix is A0; the page-boundary/active-stack checkpoint
above extends it across all address registers.

Grouped MMU validation passes 7,681,228 stream/recovery cases and 101,224
control-flow/RTE cases. Complete cold/warm AOT passes 3,405,950 guest recovery
cases per run: 71 cold images, 1,262,187 warm hits and zero warm compilations.
Stable source identity and malformed-cache fallback pass. The preceding native
Windows/SysV and FPU-native kernel validation is retained for the byte-identical
native image; the changed mapped fault boundary is covered by these full groups.

ROMAW was rebuilt with the exact tested archive. All 175 CPU inputs, six
fixtures and 74 staged files match the tested/application snapshots. The bundle
at `build/68040-movem-store-stage/bin` is promoted to `build/68040-current/bin`;
the preceding read bundle is preserved at
`build/68040-pre-movem-store-bin-20261125`. The audit is recorded in
`build/68040-movem-store-source-audit.json`. The coherent application base
remains `e062e3db` with the current CPU overlay.

Windows help passes (exit zero, 7,669 bytes). Isolated-profile Browser startup
passes with 30,865 native entries; Speedometer's 68K startup passes with 76,931
entries and one FPU query. Both reach WaitNextEvent without a live native
checkpoint or win_exception entry. These remain startup checks, not a full FPU
application benchmark.

## MOVEM read recovery checkpoint (2026-11-25)

Mapped MOVEM loads now retain CM/saved-EA continuation for every legal read
addressing mode, including indirect, postincrement, displacement and absolute
forms. Postincrement resumes from the saved EA even when partial execution or
the guest handler changed the base register. Faults before the first operand
instead permit ordinary restart only when the full register/state checkpoint
and callback guards certify it; they do not invent CM metadata.

This follows the general CM restart rule in
[MC68040UM section 8.4.6.7](https://www.nxp.com/docs/en/reference-manual/MC68040UM.pdf):
operand transfers repeat from the saved EA. Section 8.4.6.2 specifically discusses
indexed and PC-relative EA restoration. This checkpoint extends the earlier
implementation's addressing-mode restriction; historical entries below describe
their respective earlier checkpoints.

The private Windows CPU build passes. Focused checks pass 469,376 nonindexed
MOVEM continuation cases and 8,448 pre-operand restart/callback guard cases across
both dispatch and memory-bus bindings. They cover partial scalar/page accesses,
postincrement, base-register replacement, stack/frame overlap, guest repair/RTE,
trace retirement and repeated opcode/mask/extension fetch faults on continuation.
These checks do not constitute hardware capture validation.

Full grouped MMU validation passes 7,600,300 stream/recovery cases and 101,224
control-flow/RTE cases. The complete cold/warm AOT suite passes 3,365,486 guest
recovery cases per run: 68 cold images, 1,262,181 warm hits and zero warm
compilations. Stable cache identity and malformed-file fallback pass. An older
operand-page fixture now verifies successful CM delivery rather than retaining
its obsolete nonindexed-rejection expectation. Its focused rerun also passes.
The unchanged native kernel image retains the prior Windows/SysV native and
FPU-native validation; this increment changes the mapped recovery boundary.

ROMAW was rebuilt with the exact tested CPU archive. All 175 CPU inputs and six
fixtures match the tested and application snapshots; all 74 staged files were
verified and promoted to `build/68040-current/bin`. The candidate remains at
`build/68040-movem-stage/bin`; the preceding STOP bundle is preserved at
`build/68040-pre-movem-bin-20261125`. The source/binary audit is recorded in
`build/68040-movem-source-audit.json`. The coherent application base remains
`e062e3db` with the current CPU overlay.

Windows help passes (exit zero, 7,669 bytes). Isolated-profile Browser startup
passes with 30,862 native entries; Speedometer's 68K startup passes with 76,934
entries and one FPU query. Both reach WaitNextEvent with no live native
checkpoint or win_exception entry. These are startup checks, not a full FPU
application benchmark. MOVEM/MOVE16 store recovery and the other remaining
68040 compatibility work remain open.

## Mapped STOP suspension and wakeup (2026-11-25)

The mapped backend implements STOP with a persistent stopped latch. Privilege
is checked before fetching its immediate word; a fetch fault preserves the
incoming SR and reports the original instruction PC. Successful execution
loads the masked SR and selected stack bank, advances PC and reports STOPPED.
Further runs issue no instruction bus cycles while stopped. Zero budget retains
the latch and reports BUDGET. The embedding reset owner clears the latch.

Trace uses the instruction-entry SR, as specified in
[MC68040UM section 8.2.6](https://www.nxp.com/docs/en/reference-manual/MC68040UM.pdf).
A traced STOP reports format-2 trace after its SR load without stopping.
Newly loaded trace bits do not themselves wake an untraced STOP. A pending
interrupt eligible under the new mask reports INTERRUPT immediately; its
pending bit and the stopped latch remain until accepted exception entry.
Guest handlers and RTE resume at the following instruction, including user,
interrupt-stack and master-stack returns.

Native execution uses the new closed `native040_stop` root for the SR, CCR,
stack-bank and interrupt-notification updates, with emitted privilege/fetch
guards. Normal execution rejects semantic helpers. Host step and mapped-CPU
records gained STOP fields; callers must rebuild. Source-based native cache
identity covers the change.

Validation passes 786,432 status-root cases per ABI, 31,784 focused stream
cases, the full Windows/SysV native groups and the full FPU-native group.
MMU validation passes 7,122,476 stream/recovery and 101,224 control-flow cases.
Complete cold/warm AOT passes 3,126,574 recovery cases per run, with 67 cold
images and 1,262,179 warm hits with zero warm compilations. The STOP packet
fixture additionally forces disk restoration across 64 live status/mode cases;
its warm run compiles no bodies. The final cache identity remains stable.

The matching ROMAW bundle is staged at `build/68040-stop-stage/bin` and promoted
to `build/68040-current/bin`, with all 74 files verified byte for byte. The prior
trace bundle is retained at `build/68040-pre-stop-bin-20261125`.
All 175 CPU inputs and six fixtures match the maintained, tested and application
snapshots; `build/68040-stop-source-audit.json` records their hashes and the
tested archive. The application base remains `e062e3db` with these CPU inputs.
Windows help, Browser startup (30,940 native entries) and Speedometer's 68K
startup (76,938 entries, one FPU query) pass with no live native checkpoint.
These are startup checks; the remaining compatibility work below still applies.

## Mapped integer trace retirement (2026-11-25)

T1 now applies after every normally completed mapped integer instruction,
using the instruction-entry SR rather than callback-modified live trace bits.
The T0 serializing class includes NOP, SR changes, MOVE USP and CAS/CAS2 in
addition to the existing MMU/MOVES and control-transfer classes. CAS requires
a nonzero size encoding; immediate BSET remains an ordinary linear instruction.
Untaken TRAPV now uses shared retirement instead of bypassing trace handling.
Privilege, access and address errors retain priority over normal trace.

Queued CAS/CAS2 stores retain their T0 trace condition through format-7 CT,
guest writeback cleanup and RTE. TAS receives T1 tracing but does not join the
T0 class. The regression's normal reference runs now check architectural trace
exceptions instead of requiring a budget exit after a traced instruction.
CPU, memory, callback, frame and guest-recovery comparisons remain intact.

The focused fixture passes 3,520 cases across both dispatch and memory bindings,
including user privilege failures, opcode-fetch callbacks changing live SR,
untaken TRAPV and BSET's T0 negative case. The full MMU group passes 7,090,692
mapped stream/recovery cases and 101,224 control-flow/RTE cases. Complete
cold/warm AOT passes 3,110,682 recovery cases per run: 64 cold images,
1,262,171 warm hits and zero warm compilations. Source identity remains stable.
The AOT harness also creates its fixture parent directory in fresh snapshots.

ROMAW was relinked from application base `e062e3db` with the exact tested CPU
archive. All 175 CPU inputs and four fixtures match the maintained, tested and
application snapshots. The local audit is `build/68040-trace-source-audit.json`.
The matching bundle is staged at `build/68040-trace-stage/bin`; promotion to
`build/68040-current/bin` verifies all 74 packaged files byte for byte. The prior
bundle is retained at `build/68040-pre-trace-bin-20261125`.
Windows help passed; Browser and Speedometer's 68K startup reached WaitNextEvent
after 30,947 and 76,931 native entries with no live native checkpoint. Speedometer
made one FPU capability query. These remain startup checks rather than complete
FPU application workflows.

STOP/cache support, forced-exception tracing and the remaining compatibility
work below are not established by these normal-retirement checks.

## Resumed validation checkpoint (2026-11-25)

The branch/call candidate at `ac8295fd` has completed its missing gates. On resumption,
all 175 recorded CPU inputs matched the tested and release snapshots. The
remaining RTE run passed 101,224 control-flow cases and 7,087,172 mapped
stream/MMU/FPU/handler/RTE/delivery-fault cases. Two FPU fixtures still treated
odd speculative branch targets as normal execution. The register/predicate
fixture now checks 4,608 exceptional exits separately while 12,004 normal
predicate cases retain helper rejection. The flat compiled fixture now projects
component results through dispatch exception handling; all 23,700 flat cases
pass with CPU, memory and callback comparisons intact. These are test changes;
the runtime sources and candidate binaries are unchanged. The full FPU rerun
passed, including 159,050 mapped predicate cases, 50,970 mapped frame cases,
the flat matrix and checked-store coherence in both modes. The complete
`aot-x64` group passed, including 3,108,922 guest recovery cases per run:
64 cold images and 1,262,171 warm hits with zero warm compilations. The final
source-identity stability and malformed-cache fallback checks also passed.

ROMAW was rebuilt from committed application sources at `e062e3db`, with the
exact tested CPU archive, in `build/68040-resume-release-snapshot`. The matching
ROMAW/syngen/archive bundle is staged at `build/68040-resume-stage/bin` and
promoted to `build/68040-current/bin`; all 74 packaged files match byte for byte.
The previous bundle is retained at `build/68040-pre-resume-bin-20261125`.
The executable embeds the tested CPU source identity. The local audit manifest
is `build/68040-resume-source-audit.json`; the tested CPU snapshot retains three
generated configure/Makefile inputs omitted by the application's git archive.
All other recorded CPU inputs match both snapshots and the maintained tree.

Windows help passed. Browser reached WaitNextEvent after 30,939 native entries;
Speedometer 4.02's 68K startup reached it after 76,933 entries and one FPU
capability query. Both had no live native checkpoint. Speedometer used its
private profile's normal `PreferM68K=1` setting, without changing guest code.
These are startup checks, not complete FPU application workflows.

Work has resumed; the wider 68040 completion scope below remains unfinished.
A separate trace probe confirms missing mapped T1
and serializing integer trace coverage (80 failures in 112 cases); an isolated
prototype passes that probe but has not yet received grouped validation.

## Implemented execution

`src/runtime/fpu.c` provides checked F-line execution for the interpreter and
native exceptional exits. Normal x64 execution uses direct native lowering.
Arithmetic uses John Hauser's software
IEEE extended precision, with provenance and integration changes documented in
`third_party/softfloat/README.md`. It does not use host double arithmetic or
the host floating environment.

- FP0-FP7, FPCR/FPSR/FPIAR and reset/null state; state is appended to CPUState
  to preserve existing generated assembly offsets.
- FMOVE, FADD, FSUB, FMUL, FDIV, FSQRT, FABS, FNEG, FCMP and FTST, including
  explicit single/double variants, FPCR precision and four rounding directions.
- Byte/word/long integer, single, double and 12-byte extended operands;
  register, immediate, absolute, indirect, displacement, scaled indexed,
  full indexed/memory-indirect and PC-relative addressing.
- Static/dynamic FMOVEM data registers, control register transfers, all 32
  conditions for FBcc/FDBcc/FScc/FTRAPcc and FNOP.
  Control-register direct addressing follows the PRM's register-list rules:
  Dn permits one control register and An permits FPIAR alone. Invalid lists,
  reserved extension bits and invalid control-move destinations take vector
  11 with a format 0 frame before operand access or address-register updates.
  Malformed arithmetic/move lists, reserved conditions and invalid save/restore
  addressing use the same F-line frame and preserve null state. Reserved first
  words do not fetch an extension beyond guest RAM. Integer and FPU full-index
  extensions share the reserved-encoding validation described below.
- Idle/null FSAVE/FRESTORE and revision $41 unimplemented-instruction frames,
  with supervisor checks and invalid-frame traps. Pending unimplemented
  instructions retain their source/destination operands, tags and command;
  FSAVE transfers all 52 bytes and leaves the FPU idle. FRESTORE can restore
  that pending state or an idle frame after handler completion. In this frame
  E1 identifies packed operands rather than pending software work.
- FPSR condition, status and accrued exception bits; enabled exceptions;
  signaling-NaN classification retained across single/double conversion.
- Division E1 exceptions (divide by zero, operand error and signaling NaN)
  are posted by arithmetic and reported before the next F-line instruction.
  Integer execution continues, destination FP registers remain unchanged,
  and FSAVE suspends the pending state until a corresponding FRESTORE.
  MAME supplied the adapted priority helper and a model for this behavior;
  its pinned source/license assessment is in `third_party/mame/README.md`.
- Division E3 exceptions preserve a 100-byte busy frame with the recovered
  67-bit intermediate, guard/round/sticky information, extended exponent bit,
  original operands and both command encodings. Inexact division commits the
  rounded result before its pending trap. Overflow/underflow invoke the FPSP
  even when masked, using the selected single/double/extended exponent range.
- Add, subtract, multiply, square root and rounded move/absolute/negate now
  preserve the intermediate exponent before range checking. Arithmetic
  writeback exceptions use E3 busy frames; conversion-unit exceptions use E1
  frames. Enabled exceptions are deferred until the next FPU instruction.
  Motorola exponent-zero normalized operands work in arithmetic/comparison.
- FSGLDIV/FSGLMUL truncate excess input mantissa bits and round to 24 bits
  while retaining extended exponent range, independently of FPCR precision.
- Bounded FPU reads and whole-operand writes; invalid memory takes vector 2.
  No partial extended/double store crosses a rejected boundary. Format 3 RTE
  unwinds floating-point post-instruction CPU stack frames.
- Move-out conversion exceptions retain a busy E1/T frame and report through
  CPU format 3, preserving the destination EA. Inexact conversions commit
  their rounded value. Integer overflow/SNAN and the hardware's exact-negative-
  integer-minimum erratum are nonmaskable, as are exact tiny floating stores.
- Combined E3/E1 state restores report the older writeback exception first.
  After a handler clears E3, restoring the frame exposes E1. Masked inexact
  writeback resumes from its saved 67-bit value rather than rereading operands.
- Restored normalized operands honor ETEMP/FPTEMP's separate exponent bit 15.
  The arithmetic path carries their signed internal exponents through rounding,
  comparison and range checks, including normalization below the register range.
  FPSP also sets that bit for ordinary biased exponents at or below $3fff;
  only the wrapped $4000..$7fff range is sign-extended on restore. Both busy
  and unimplemented frames have regression cases for this distinction.
  Newly saved normalized/zero operand tags include the corresponding ETE15
  and FPTE15 bits, matching the package's `get_op` tag construction.

Packed decimal, denormal and unnormal operands take unsupported-data vector
55 and retain a 100-byte busy frame, including raw single/double denormal
fractions, tags, packed bits and the move-out T bit. Instructions requiring
Motorola's FPSP, including FINT, FINTRZ, FMOVECR
and transcendental operations, take vector 11 with a format 2 CPU frame.
Other coprocessor instructions retain the existing F-line path. ROMAW now
installs the original Motorola package in a system-heap allocation after its
final exception-vector reset. The installer validates the complete allocation
and vector extents, relocates the code, preserves previous exception handlers
and publishes vectors only after the executable image is ready.
The package lives under `third_party/fpsp040`; run `tests/check-core.sh fpsp`.
It exercises software instructions, unsupported-data recovery and enabled
exception forwarding in both dispatch modes, at two different load addresses.
The current suite runs 146 cases per dispatch mode, including FMOD/FREM quotient
bytes, FSCALE, FGETEXP/FGETMAN, paired FSINCOS outputs and exact packed-decimal
round trips with static/dynamic K-factors and address-register updates.
Eighteen nontrivial software
operations use independent 120-digit Decimal reference calculations with a
one-extended-ULP tolerance; exact values and integer rounding require exact bits.
Unsupported-data FSQRT frames recode operation 4 to the internal operation 5,
which FPSP needs to distinguish square root from conversion-unit work.
FPSP-generated S/X busy frames can post software E1/E3 exceptions. A posted
software E1 conversion exception saves the short frame expected by the package's
SNAN/operand handlers, retaining its software-exception identity on restoration.
Unimplemented instructions fetch their operand before taking the trap, including
address-register updates and immediate/EA extension words. The format 2 CPU
frame contains the following PC and the operand EA. The FPSP frame retains
packed operand bits and gives unimplemented instructions priority over unsupported
operand types. A failed operand fetch takes an access fault instead.

## Validation

After building the Windows archive, run in WSL with Windows interop enabled:

```sh
bash syn68k/tests/check-core.sh fpu --oracle
bash syn68k/tests/check-core.sh control-register
```

The first script checks both real dispatch modes, extended low-bit precision,
all rounding directions, NaNs, zero, division, conversions, distinct-register
FMOVEM order, dynamic lists, all 32 conditions against explicit truth tables,
supervisor violations, frame selection, RTE and bounded read/write faults.
It also checks a real guest handler executing FSAVE and RTE, full-size
predecrement saves/postincrement restores, byte-preserving frame round trips,
pending traps, truncated state transfers and return to the following instruction.

`--oracle` additionally requires QEMU's m68k user emulator and the m68k Linux
assembler/compiler. One original assembly fixture runs under QEMU's m68040
model and Syn68k, comparing 100 extended values and condition/quotient bytes
in each dispatch mode. Cases include arithmetic, precision/direction changes,
explicit single/double variants, conversions, full indexed/memory-indirect
addressing, FMOVEM and special values. QEMU does not accurately implement all
Motorola status/accrued exceptions; those bytes are excluded from the oracle
comparison and tested separately. The assembly snapshots FPSR before FMOVE
out because QEMU incorrectly changes condition codes during that transfer.

## Current integration boundary

Normal x64 FPU execution and mapped MMU instruction dispatch have direct JIT
and persistent AOT lowering. The mapped MMU roots include translation, table
searches, descriptor updates and ATC operations. Normal FPU memory forms use
the mapped or flat native operand binding. Checked helpers remain for faults,
unsupported hardware operands/instructions and pending exceptional state.

The mapped stream runner constructs supported CPU exception frames and handles
the read-restart and writeback cases described in [the MMU integration notes](mmu040.md).
Mapped FPU/MMU/MOVES trace uses instruction-entry T1/T0, including deferred CT after
retired FPU stores and trace after complete reexecution of unchanged aborts.
Read restart distinguishes owned EA/null-entry changes from external operand
and descriptor callback effects; callback changes to An or FPU null state deny
rollback. RTE retains its separate live privileged-SP restart protocol.
MOVES converts instruction-space FC2/6 to data FC1/5 and supports physical
alternate-space faults with TT=2. Its scalar stores retire with checked WB1/WB2
cleanup, while unchanged failed reads restart through the guest handler and RTE.
Physical bus prefixes and certified early stores are described in the
[writeback recovery contract](68040-writeback-recovery.md).
This is distinct from enabling paging in ROMAW: its Toolbox and host-pointer
interfaces still use flat guest memory. Physical cache/bus behavior belongs to
the embedding memory implementation; the operand boundary is not a cycle model.

## Still required for full compatibility

- General busy-frame continuation, including FPSP-generated internal states
  outside the documented operand/writeback fields. The model's revision $41
  rejects revision $40 frames rather than interpreting their different layout.
- Remaining mapped access-error work: further active-stack/call effective-address
  and double-fault variants, a physical block/cache bus, and the
  constructed user-mode throwaway-RTE case.
  Scalar/FPU/locked writebacks populate format-7 slots; MOVEM loads and stores
  use CM/saved-EA continuation as described in the current checkpoints above.
- Remaining trace/exception-priority audit, mapped cache
  support and the broader flat integer frontend. Normal mapped T1 and the
  supported serializing T0 classes now include CAS/CAS2 writeback/RTE checks;
  these do not establish the unsupported instruction classes.
- Reserved-encoding and effective-address legality audit, additional boundary
  and special-value arithmetic vectors, and real application FPU workflows.
- Broader privileged/control-register, MMU/cache and exception audit. Integer
  CRC regressions do not establish full processor or Mac OS boot compatibility.

The following audit notes are chronological. Their references to missing MMU
selectors, flat-only memory and earlier regression totals describe the state
at that point; the current mapped backend is documented in [mmu040.md](mmu040.md).

The preceding MOVEC audit corrected `movec_from_reg` to use write opcode
`0x4E7B`, preserving read direction `0x4E7A`. Its regression script runs both
directions with data/address registers and relocated exception vectors.
MOVEC now checks supervisor privilege before every selector and retains the
faulting PC. SFC/DFC retain their three-bit values, CACR exposes the 68040's
data/instruction enable bits rather than the old 68020 low-bit mask, and stack
registers use the active bank. CAAR is rejected on this 68040 model. MMU control
selectors remain unimplemented and raise an exception rather than accepting
ineffective writes; this is an explicit compatibility gap. All 4096 selectors
in both directions are tested for privilege violations, with masks, CCR and
illegal-selector fault frames checked in both dispatch modes.

CINV and CPUSH share a bounded instruction decoder. All cache selections and
line/page/all scopes check supervisor privilege; reserved scope zero takes the
illegal-instruction vector. Instruction-cache operations invalidate translated
blocks conservatively at every scope, while data-cache operations need no
writeback in the current flat RAM model. Tests cover every encoding, privilege
fault frames, CCR preservation and actual changed-code visibility in both
dispatch modes. This does not model physical cache tags, dirty data loss, MMU
translation or cache timing. The previous CPUSH-only generator lacked the
privilege check and invalidated translations even for a no-cache selection.

References:

- [Motorola M68040 User's Manual, sections 8-9](https://www.nxp.com/docs/en/reference-manual/MC68040UM.pdf)
- [M68000 Family Programmer's Reference Manual](https://www.nxp.com/docs/en/reference-manual/M68000PRM.pdf)

Division's rounded value and writeback data are additionally checked against
6,216 independent exact-integer rational cases (all precision/direction modes,
random significands/exponents and range boundaries). This does not rely on
QEMU or host floating-point arithmetic. The Windows instruction fixtures test
busy-frame transport, pending traps and truncated/malformed state transfers.
Add/subtract/multiply/divide/square-root, FSGL operations and unary rounding have 13,200
exact-integer cases, including exact integer square roots. These check the
rounded value, pre-rounding writeback, status and guard/round/sticky bits in
every precision/direction mode, including signed internal exponent boundaries.

Full-format indexed extensions are checked against M68000PRM Figure 2-2 and
Table 2-2 before operand side effects. Reserved BD size, bit 3 and IS/I/IS
combinations raise vector 4 in integer and FPU paths. Invalid extension words
remain inside the translated block's invalidation range, including when an
extension alone is changed into a valid instruction. PC base suppression now
also applies to non-indirect full extensions. The control-register test script
checks these cases in native and interpreter dispatch, including RAM boundaries,
auto-increment operands and unchanged FPU state. This is a bounded indexed-mode
audit; other effective-address and precise restart work remains outstanding.

RTE now checks supervisor privilege before reading the active stack. User-mode
RTE takes vector 8 at its own PC, including with an unmapped user stack, and
preserves the pre-instruction CCR in the exception frame. The full MC68040
accepts frame formats 0, 1, 2, 3 and 7; legacy 68010/68020/68030 formats now take
format-error vector 14 instead of being silently consumed. The focused fixture
checks all 16 format nibbles, format-1 ISP/MSP transitions and returns to the user
stack in both dispatch modes. Stack/bus-error restart details remain unfinished.
Reference: MC68040UM section 8.4; LC040/EC040 format 4 is not a full-040 frame.

Immediate ANDI/ORI/EORI to SR now share a privileged runtime decoder. User-mode
instructions take vector 8 with their original PC/SR/CCR before changing state.
Supervisor instructions mask reserved SR bits, update all CCR bits, switch the
active USP/ISP/MSP bank and retain the existing interrupt-notification behavior.
The test script checks 10,368 cases per dispatch mode: individual/complemented
immediate bits, all CCR combinations and all three initial stack banks. This
does not complete the privilege/trace audit for other SR and control operations.

FPU effective-address decoding now uses the shared bounded `runtime/ea.c`
implementation. The caller selects permitted register/addressing forms and
interprets explicit memory, reserved-index and invalid-mode errors. Contexts stop
at the first fault; byte/word/long writes validate the full extent before changing
memory. Focused tests cover malformed transfer sizes, truncated displacements,
blocked operations after a fault and rejected immediate destinations. This
extraction is separate from the subsequent integer privilege-path changes.

MOVE to/from SR now uses a terminal runtime decoder that validates the opcode's
EA form, checks supervisor privilege, then fetches extensions and accesses the
operand. MOVE from SR preserves CCR and the upper data-register word; MOVE to
SR masks reserved bits and selects the correct live stack bank after EA updates.
Direct MOVE USP is implemented with privilege checks and full 32-bit address
register transfers. RTE now masks reserved SR bits on restoration too.

Opaque control mappings cache only their opcode, so MOVE SR, immediate SR and
MOVEC extensions are fetched after privilege checks, including at RAM's end.
Supervisor missing-extension accesses produce bounded access-error frames.
Fixtures cover all EA encodings, 5,184 SR transfers and 6,144 USP cases per
mode, A7 postincrement/predecrement across bank changes, atomic failed stores,
changed runtime extensions, all CCR patterns and native producer/consumer chains.
Actual Motorola FPSP user-mode register and `(A7)+` software operations also
verify supervisor entry, distinct user/interrupt stacks and CCR restoration.
Precise access-error restart and the wider MMU/privilege audit remain unfinished.

MOVES byte/word/long transfers now use the same bounded terminal decoder. Legal
memory-alterable EAs check privilege before the register/direction extension;
reserved extension bits trap before auto-update. Data-register loads preserve
upper bits, address-register loads sign-extend, and same-An stores observe the
68040's updated address value. All CCR bits are preserved. SFC selects reads and
DFC selects writes; flat RAM supports FC 1/2/5/6, with instruction/data merging
as specified in MC68040UM table 3-2. Other alternate physical bus spaces have no
device mapping and produce access errors. This does not implement an MMU or
complete access-error transfer/status fields. Original fixtures check 39,936
transfers per dispatch mode, all general registers/CCR patterns, indexed and
indirect EAs, A7 byte steps, sign boundaries, unused function-code registers,
atomic failed stores, missing/reserved extensions and native/MOVEC chains.

Software trap conformance now covers 12,930 cases per dispatch mode: all TRAP
vectors, TRAPV and all TRAPcc conditions/optional-word forms, every CCR pattern,
and USP/ISP/MSP entry. Original guest RTE handlers verify frame-0/frame-2 returns
to the following instruction and restoration of the complete CCR and stack bank.
Native arithmetic chains check that condition-code optimization preserves the
full saved SR, including flags not used by the trap condition. Entry tests also
verify saved trace bits and clearing of the live trace bits. These cases passed
without changing runtime semantics. This does not validate automatic trace
generation, master-interrupt dual-frame entry or access-error restart.

Interrupt entry from supervisor master state now saves the normal frame on MSP
before selecting ISP and adding the format-1 throwaway frame. The handler runs
in interrupt state with the delivered autovector's mask and cleared trace bits.
43,040 fixture cases per dispatch mode cover every CCR/trace/mask combination,
all three initial banks, guest RTE returns and nested interrupts with relocated
vectors. Stack-write faults, interrupt-acknowledge bus cycles and precise
interrupt-recognition timing remain outside this entry-path test.

The saved format-1 SR retains the interrupted M bit, matching section 8.4.2's
RTE bank-selection contract and the pinned MAME interrupt implementation.
MC68040UM page 8-15's prose describes a different saved SR (M clear/new mask),
which conflicts with that return contract. This is an explicit interpretation
supported by the reference core, not physical-hardware validation of those SR
bits. See the [reference assessment](../third_party/mame/README.md).

RESET now checks supervisor privilege before asserting an optional external-bus
callback. It advances to the next instruction and preserves CPU registers, CCR,
stack banks, FPU/control state and RAM; it does not perform a processor reset.
Install `syn68k_set_reset_handler` after initialization, or pass NULL to remove
the callback. An unattached bus has no device effects. The API does not model
the 68040's 512-clock reset pulse or reset ROMAW's high-level host devices.
Original fixtures cover 1,539 cases per dispatch mode, including all CCR/mask
combinations, three stack banks, cached callback replacement and a native
arithmetic producer. STOP waiting/wakeup and the broader privileged-machine
model remain unfinished.

MOVE16 now uses a terminal decoder for all five addressing forms, fetching
absolute/register extensions at execution time. Both complete aligned 16-byte
lines are validated before copying through a temporary line. Identical source
and destination lines are safe; postincrement retains the original low bits,
and using the same register at both ends increments it once. CCR is preserved,
including flags from preceding native arithmetic, and user mode is supported.
Malformed register extensions retain the F-line fallback. Missing extensions
and invalid lines produce bounded access faults before transfers or increments.

Original fixtures cover 50,453 cases per mode: all address-register pairs and
CCR patterns, all line offsets, user-mode offset pairs, changed cached absolute
addresses/register extensions, protected-page truncation and native chains.
The legacy CRC harness declares its raw host-pointer address space explicitly;
it generates mapped operands, while the separate fixture checks actual bounds.
Physical burst ordering, partial bus transfers, exact access-error status and
restart/writeback behavior remain outside this synchronous flat-RAM model.

CHK word/long now fetches its effective-address bound through the bounded
runtime decoder before evaluating either signed limit. Negative Dn values no
longer skip operand reads, and access faults preserve the pre-instruction CCR.
The opcode is cached alone, so changed immediates/displacements are read during
execution and missing extensions cannot cause a translation-time host access.
X is preserved; the undefined Z/V/C bits retain their incoming values, while N
describes a negative value on a CHK trap. Format-2 frames retain the following
PC and the original instruction address.

The original fixture checks 51,343 cases per dispatch mode: word/long signed
limits, all Dn registers and CCR combinations, user/supervisor entry, every EA
encoding, A7 updates, changed cached extensions, protected-page operand and
instruction truncations, reserved indexes and a native arithmetic flag producer.
This follows M68000PRM's CHK definition; the pinned MAME opcode list also reads
the bound before testing Dn. No MAME implementation was copied. Precise bus
restart/writeback and physical processor timing remain outside this audit.

RTD fetches its signed 16-bit adjustment during execution and reads the stacked
return address through the bounded memory helper. Changing a cached extension
now changes the executed stack adjustment. Missing extension or stack bytes
produce the existing format-7 access fault at the original instruction PC,
before changing the live stack pointer. CCR is preserved, including native
arithmetic flags produced immediately before RTD. Both user and supervisor
execution remain supported.

The original fixture checks 66,949 cases per dispatch mode: all 65,536 signed
adjustments distributed across the three stack banks and all CCR patterns,
changed return targets, unaligned stack data, protected/truncated storage and
native arithmetic chains. This follows M68000PRM's RTD definition (4-166).
The checks cover the synchronous flat-memory model; target instruction-fetch
faults, physical bus ordering and precise restart/writeback remain unfinished.

CHK2/CMP2 now decode register and effective-address extensions at execution
time, validating both bounds before changing flags. Data-register operands use
the selected width; byte/word bounds for address registers are sign extended
before the full-register comparison. Wrapped intervals and equal bounds are
handled explicitly. X is preserved, and the architecturally undefined N/V bits
retain their incoming values. An out-of-range CHK2 produces vector 6 with the
following PC and original instruction address in its format-2 frame.

The bounds fixture checks 266,344 cases per dispatch mode: range endpoints,
wrapped ranges, every CCR pattern and operand register, all control addressing
modes, user-mode execution, cached extension changes, invalid encodings,
truncated operand/instruction storage and native arithmetic flag producers.
Memory faults precede flag updates. Access-error frames use the existing bounded
flat-memory exception model; precise bus-cycle restart is not claimed.

RTR now decodes its six-byte saved CCR/PC frame through the bounded runtime
reader. The complete frame is checked before restoring CCR or advancing SP;
the SR supervisor portion and active stack bank are unchanged. Unmapped or
truncated frames produce the existing format-7 access fault at the original
instruction address, retaining incoming flags. The generator explicitly retains
that address for the terminal decoder. The original fixture checks 66,945 cases
per dispatch mode: every saved word (including ignored upper bits), all three
stack banks, unaligned data, live return targets, protected-page truncations
and native arithmetic flag producers. This follows M68000PRM's RTR definition
(4-168) for successful returns. Fault handling remains the synchronous bounded
RAM model; precise bus restart and hardware writeback ordering are not claimed.

RTS now checks the complete saved PC before changing SP. The native path guards
the final stack byte against the current direct-memory window, including address
wrap and resized windows, while retaining its optional return cache for valid RAM.
Invalid frames use the bounded reader and existing format-7 access fault with
the original instruction PC and incoming CCR. Interpretation retains its return
cache for complete spans. A 1,797-case fixture per dispatch mode covers all CCR
patterns and stack banks, unaligned frames, changing targets, truncation,
unsigned wrap, live arithmetic flags, cached A7 offsets and actual JSR/cache
returns. This validates synchronous flat RAM; precise bus cycles, target-fetch
faults and access-error restart remain unfinished.

UNLK now bounds the complete saved address register before reading it. Valid
frames retain native execution, using the same live-window and address-wrap
guard as RTS. Faults use the bounded decoder with the original instruction PC,
incoming CCR and the SP selected from An. The synthetic stream retains that PC
without changing operand selection or ordinary instruction continuation; fault
dispatch bypasses the normal operand advance. Native cached An offsets are
included in the guard.

The original fixture checks 43,171 cases per dispatch mode: A0-A6 with all CCR
patterns and stack banks, changing saved values, unaligned frames, protected-page
truncations, unsigned wrap, native arithmetic flags, cached An offsets and
window resizing after native compilation. Incoming CCR liveness includes the
fault path, even if a later instruction would replace some or all flags. The
fixture checks earlier arithmetic results saved on faults and successful
continuation through MOVEQ and ADDQ, with cached An offsets and all registers.
Included A7-alias cases preserve the existing interpreter behavior; they are not
a physical-68040 oracle. Successful UNLK ordering follows M68000PRM (4-194).
Fault handling uses the synchronous flat-RAM model; precise hardware writeback,
exception-stack double faults and access-error restart remain unfinished.

LINK.W/L fetch their displacement through the bounded runtime decoder before
changing SP or An. Both dispatch modes use this terminal decoder, replacing the
old inline x86 LINK path. Changing an extension in a cached block changes the
next frame adjustment. Missing extension bytes report the original instruction
PC and first unavailable byte through the existing format-7 access fault,
without changing the frame registers. Protected-page tests cover actual host
memory boundaries as well as declared guest-memory extents.

The complete four-byte frame destination is checked before writing. Rejected
writes decrement SP but retain An and incoming CCR; no partial frame bytes are
written. All incoming condition codes remain live even if a following
instruction replaces them.

The original fixture checks 71,380 cases per dispatch mode: word/long signed
displacements, all registers/CCR patterns and stack banks, unaligned frames,
native arithmetic flags, cached SP offsets, protected-page truncation, unsigned
wrap, memory-window changes, cached extension edits and every truncated
extension prefix. LINK A7 now captures the original SP before its implicit
predecrement, in both the checked decoder and direct x64 emitter. The earlier
fixture retained a decremented-SP legacy expectation; that expectation has been
corrected. `tests/link-a7-reference.py` assembles an independent Linux m68k
program and checks LINK.W/L with QEMU's m68040: both save the original SP and
finish at original SP minus 20 for displacement -16. This is emulator evidence,
not a physical-68040 capture. Successful ordering follows M68000PRM LINK
(4-111); the Classic Mac captures below still exclude A7.

Mapped PEA and LINK final-store offers preserve the component decoder's fault
state. LINK captures its already-fetched displacement in host-step metadata;
the mapped runner commits An/SP assignments once, using live callback SP, and
clears that metadata before publishing the retired writeback record. Normal T1
and deferred CT use instruction-entry trace bits; these straight-line operations
do not trace for T0 alone. Calls and their odd-target exception priority use the
control-flow protocol described below.

The focused mapped group contains 215,040 component/retirement checks across
both instruction and bus bindings. It covers every LINK register, word/long
extensions, four PEA forms including A7 sources, both page sizes and privilege
states, aligned physical partial writes, ATC protection/missing pages, and live
SP/trace/CCR callback changes. Within that group, 40,960 user-mode cases execute
a guest handler that repairs the PTE, drains WB1/WB2, restores its scratch
registers and returns through RTE, including CT trace delivery. Supervisor-stack
frame overlap, additional PEA EA forms and stack-bank delivery faults still need
coverage; this group does not establish their completion.

The combined control-register (with QEMU reference), JIT, MMU, native040, RTE,
FPU-native and AOT checks pass for this increment. MMU and RTE each report
6,655,716 stream cases and 101,224 flow cases; these are overlapping suites,
not twice as many unique cases. Cold/warm AOT recovery each report 2,893,194
cases: 47 cold emissions and zero warm emissions, with a stable cache identity.
Windows help, Browser startup and Speedometer 4.02's 68K startup also pass.
Speedometer reaches its event loop with the ordinary no-FPU capability report;
this is not a completed FPU benchmark or physical-68040 validation.

## Control-flow prefetch and call recovery

Decoded integer branches retain their actual taken decision, so T0 does not
rely on comparing the target with the following PC. Calls, jumps and returns
qualify even when their target equals fallthrough. Their normal T1/T0 and queued
call-store CT use instruction-entry SR, preserving callback changes to live SR.
Other integer trace classes still require the wider audit.

MC68040UM 8.2.2 prefetches both conditional paths: an odd speculative target can
raise an address error even when execution falls through. Integer and FPU step
records retain the decoded reference without rereading an extension or issuing
an odd prefetch bus cycle. Checked and emitted FPU bodies capture FBcc/FDBcc
references before condition/counter resolution. The shared flat/mapped dispatch
helper reports format 2 with the source instruction PC and the reference's A0
cleared, before trace. Existing forced/FPU exceptions retain priority. An odd
entry PC also precedes a pending interrupt, following table 8-4.

The untraced flat FPU fast path checks the live displacement before changing
counter or condition state. An odd reference leaves through the checked
exception path. Only direct RAM has been consumed at this point; device ranges
leave before their fetch. The normal even-reference path remains native.
The FPU result and private invocation tail gained a prefetch field; earlier
field offsets and CPUState are unchanged. Source-based cache identity
invalidates images compiled against the old layout.

BSR/JSR final writes to even targets can retire through WB1/WB2 and CT. An odd
target instead requires certified abort/replay: all checkpointed results must
match instruction entry except the owned SP predecrement. The runner restores
only SP, retains the pending scalar and original PC, and lets guest cleanup/RTE
restart the call. After its store succeeds, the normal address error takes
priority over trace. Changed callback registers, banks, flags or FPU state deny
rollback and remain live. No continuation bit or undefined frame field stores
the pending address error.

Focused checks cover 393,216 integer branch cases, 3,584 FPU branch
prefetch/BSUN/fetch-fault cases, 33,600 user call writeback/RTE cases and 1,024
callback/replay guards across both instruction and bus bindings. Another 32
checks cover odd-PC/interrupt priority. The call group includes three BSR widths
and ten JSR forms, including A7, memory indirect, PC relative and full base
displacements, both page sizes, aligned physical prefixes and ATC faults. Guest
handlers preserve their general-register scratch state and DFC. Flat dispatch
and compiled FPU paths pass 352 trace/prefetch cases in cold and warm AOT runs.
Combined control-register, MMU and native040 checks pass. MMU validation reports
7,087,172 mapped stream cases. Full RTE, FPU-native and combined cold/warm AOT
checks also pass. The rebuilt current-application bundle passes Windows help,
Browser startup and Speedometer's 68K startup and is promoted to the current
bundle, as recorded above. Supervisor stack overlap,
further EA variants and the broader compatibility work remain separate
requirements.

## Classic Mac LINK comparisons

`tests/mac-link-reference.py` generates an original PPC application that invokes
68K LINK.W/L through Mixed Mode. It records CCR, normalized frame/SP values and
the saved address register for A0-A6, nine nonpositive displacements and all 32 CCR
patterns: 4,032 cases. A7 aliases, privileged state and FPU behavior are excluded.
Positive offsets are tested only by the isolated CPU fixture: they put the saved
frame below the active stack pointer, where a Mac interrupt can overwrite it
before the probe reads it. Short branches keep generated basic blocks bounded.
The staged Windows check,
`tests/check-hardware.ps1 mac-link`, validates the same probe in both 68K dispatch
modes.

On 2026-10-30 all 4,032 cases matched on both the G5 running Classic under
Mac OS X 10.4.11 and the Macintosh 5260/100 running System 7.5.5. Each capture
contains 64,512 bytes; both SHA-256 hashes are
`54b21d968d4c20c53deb13e1a0c38dea532cd6d725399316ee7a00156f33c64b`.
The 5260 remained responsive after the run. Deployment required refreshing its
Finder folder and placing the Finder-info entry before the resource-fork entry
in the local share's AppleDouble sidecar; this is an observed share workaround,
not a general AppleDouble format requirement.

Both machines execute these instructions through Apple's PowerPC 68K emulator.
These comparisons do not establish physical 68040 FPU, MMU or bus behavior.
The LC475 remains offline.

The Windows rebuild and staging for this LINK update passed the complete
`tests/testall.sh` integer reference comparison in all four modes (native,
interpreter, noncc and noncc-interpreter). Each mode matched all 480 saved
fixture rows at 10,000 trials per fixture. The control-register suite, FPU
suite with QEMU oracle, FPSP suite at two load addresses, staged LINK probe,
DWARF validation, ROMAW generated-file checks and command-line help checks
also passed. Repository source and PowerShell formatting checks passed.

## Long integer division audit

DIVS.L/DIVSL.L and DIVU.L/DIVUL.L now decode their register and source-EA
extensions at execution through the bounded decoder. The translator stores the
original opcode PC only; it no longer inserts its old precomputed following PC.
Reserved extension bits and illegal source modes take vector 4 before operand
access. Missing extension/operand bytes take the existing synchronous format-7
fault at the original PC. This does not implement physical MMU restart ordering.

The decoder completes source predecrement/postincrement before divide-by-zero
dispatch, including the address register visible inside the handler. The old
generated handler deferred that update until after dispatch; the original
`tests/divide-long-test.c` reproduced this failure before the change. A zero
divisor preserves destination registers and uses the following PC and original
instruction address in the format-2 frame. C clears and X is preserved. Undefined
N/Z/V on zero and N/Z on overflow retain their incoming values as an emulator
policy, not a hardware requirement.

Unsigned magnitudes implement signed division without invoking undefined host
`INT64_MIN / -1` arithmetic. A quotient outside the selected 32-bit range sets V
and preserves Dq/Dr. Successful results follow M68000PRM DIVS/DIVU (4-92 through
4-98), including signed remainders, 32/64-bit dividends and register aliases.
The focused fixture checks 852,358 cases per dispatch mode: every Dq/Dr/source
register combination, all CCR patterns, corner values, cached extension edits,
every source mode, native flag liveness, all reserved extension combinations
and protected-page faults. Divide-by-zero tests also verify A7 source updates
inside the handler in user, interrupt and master stack banks with all incoming
CCR patterns.

`tests/mac-divide-reference.py` builds an original Mixed Mode comparison app;
`tests/check-hardware.ps1 mac-divide` checks it in both staged ROMAW dispatch modes
and clears the result before each run. Its 5,632 nonzero-divisor cases also
matched on both PowerPC Macs on 2026-10-30. The G5/Classic capture SHA-256 is
`cdb779969199c81176cb446c66df9f464d0e8cddca1e6a9c9f913ce169f0e6f0`;
the System 7.5.5 5260 capture is
`1b017d2d05f177d6865e89519f56e3342bd96a8bf3de3eb8e99e995c042d1e79`.
Each contains 90,112 bytes. Their 128 differing records disagree only in
undefined overflow N/Z flags, which the independent Python-integer comparator
excludes. Both comparisons are against Apple's 68K emulator, not a physical
68040; they do not exercise divide-by-zero OS handlers or access faults.

Post-change validation passed all four full integer CRC modes against the saved
480-row references at 10,000 trials per fixture. The complete control-register
suite, FPU/QEMU oracle, FPSP at two load addresses, staged Mac divide probes,
ROMAW help/generated-file checks, DWARF validation and both formatter checks
also passed. The additional reserved-extension and A7-bank tests passed 852,358
focused cases in each dispatch mode without further runtime changes. ROMAW,
the Windows generator and the CPU archive were rebuilt and staged together.

## Long integer multiplication audit

MULS.L/MULU.L now fetch the register extension and source EA through the bounded
runtime decoder. A cached instruction observes extension edits on its next
execution. The original `tests/multiply-long-test.c` reproduced stale destination
selection before this change. Reserved extension combinations and illegal source
modes are rejected before evaluating the EA; truncated extensions and operands
take the synchronous format-7 access fault at the original instruction PC.

Both source values are captured before destination writes. Signed multiplication
widens both inputs to a safe 64-bit product. The decoder implements 32/64-bit
result flags and preserves X; faults preserve the entire incoming CCR, including
flags produced by an earlier native arithmetic instruction. The 851,970-case
fixture per dispatch mode covers register aliases, all CCR patterns, boundary
products, every source mode, every reserved extension combination, live cached
extensions, native flag liveness and protected-page truncations.

M68000PRM MULS/MULU (4-135 through 4-140) declares a 64-bit result with Dh == Dl
undefined. Local tests retain the previous high-word policy for this encoding;
hardware comparisons exclude it. `tests/mac-multiply-reference.py` supplies an
original Mixed Mode app and an independent Python-integer model. All 4,928
defined cases matched on the G5/Classic and System 7.5.5 5260 on 2026-10-30;
both 78,848-byte captures have SHA-256
`d13dcc60f0efd2ec088d342015f40ad72af0235c53e19ce12e5c8fefc3872aab`.
The 5260 remained responsive. The staged Windows probe also matched in both
dispatch modes. These are Apple 68K emulator comparisons, without physical
68040 bus/MMU/restart validation.

Post-change validation matched all four full integer reference modes (native,
interpreter, noncc and noncc-interpreter), including the existing long-multiply
CRC rows. The complete control-register suite, FPU/QEMU oracle and FPSP checks
also passed. One interpreter process initially failed to reserve the legacy
fixture's fixed-address Windows memory before executing any instructions; its
fresh-process retry passed. Both formatter checks and the rebuilt/staged probe
passed. No physical 68040 or MiSTer comparison was available for this batch.

## Word integer division audit

DIVS.W/DIVU.W now fetch source effective-address extensions and operands at
execution through the bounded decoder. Editing an immediate in a cached block
changes the next result; the original `tests/divide-word-test.c` reproduced the
stale divisor before this change. The translator supplies only the original
instruction PC, and the obsolete inline x86 signed-divide helpers were removed.
Illegal source modes take vector 4 before operand access. Truncated immediates
or memory words take the existing format-7 fault at the original instruction,
preserving the destination and incoming CCR.

The widened signed division handles INT32_MIN / -1 without host overflow.
Quotient overflow sets V and leaves the destination unchanged. Successful
division packs the signed/unsigned remainder above the 16-bit quotient, retains
X and clears C. Undefined overflow N/Z values retain the legacy emulator policy;
divide-by-zero preserves the destination and undefined N/Z/V, clears C and saves
the following PC plus original instruction address in the format-2 frame.
Source A7 updates occur in the original user/interrupt/master bank before the
exception frame is pushed. This is still the synchronous flat-RAM model, without
precise physical bus/MMU restart ordering.

The focused fixture passes 328,457 cases in each dispatch mode: all source and
destination data registers and aliases, all CCR patterns, signed/unsigned range
boundaries, every source mode, live immediate edits, all stack banks, native
arithmetic flag liveness and protected-page operand/instruction truncations.
`tests/mac-divide-word-reference.py` supplies an original Mixed Mode probe and
independent Python-integer model; zero divisors are excluded and undefined
overflow N/Z bits are masked. All 2,304 defined cases matched both staged Windows
dispatch modes and both PowerPC Macs on 2026-10-30. The G5/Classic and System
7.5.5 5260 captures are identical: 36,864 bytes, SHA-256
`668d2fbfc939b01836f7d4b5a68f1b6a5f45024db4c4d41958abeacee1bc9896`.
The 5260 remained responsive. These compare Apple's 68K emulators and do not
establish physical 68040 exception or MMU behavior.

The full CRC audit initially found a native `divs_reg` condition-code-only
difference (input, memory and register CRCs matched). The retired x86 helper
tested `quotient + 32768` on overflow, while the interpreter tested quotient.
The decoder retains the corresponding undefined N/Z policy for the previously
native signed forms, selected by the runtime dispatch setting. An explicit
overflow case checks both policies. Historical CRC references are unchanged;
these undefined bits are still excluded from the independent Mac comparison.

Final validation matched the saved 480-row integer references in all four
dispatch/condition-code modes at 10,000 trials per fixture. After preserving the
native overflow policy, the complete native rerun passed, including all 14
division CRC rows. The other three full modes passed unchanged. The expanded
control-register suite passed with 328,457 focused word-division cases per mode;
FPU/QEMU oracle and FPSP checks also passed. Final staged Mac probes, ROMAW
help/generated-file checks, DWARF validation and formatter checks passed.
The trap source also compiles with native translation disabled; its legacy x86
overflow policy is guarded by GENERATE_NATIVE_CODE. Syn68k, its Windows generator
and ROMAW were rebuilt and staged. The remaining physical MMU/access-error
restart work is not covered by this batch.

## Word integer multiplication audit

MULS.W/MULU.W memory and immediate forms now fetch source effective-address
extensions and operands at execution through the shared bounded decoder.
The register-only interpreter/native fast paths remain in use. The original
`tests/multiply-word-test.c` reproduced a stale cached immediate before the fix.
Obsolete native descriptors for the replaced forms have been removed; the
translator supplies the original instruction PC only to the runtime forms.

Products use the low 16 bits of both inputs with signed/unsigned widening,
set N/Z from the full 32-bit result, clear V/C and retain X. Illegal source
modes take vector 4; missing extension/operand bytes take the synchronous
format-7 access fault before changing the destination or CCR. This uses the
existing flat-RAM fault model, without precise 68040 MMU/restart or double
access-fault handling.

The focused fixture passes 328,416 cases per dispatch mode: all source/destination
registers and aliases, boundary products, every CCR pattern and source mode,
live cached immediates/displacements/absolute/indexed addresses, native flag
liveness, protected-page truncations, A0 pre/post updates on faults and A7
predecrement faults in each stack bank. A7 postincrement faults at the protected
page would also fault while stacking; that double-fault case is excluded.

The original Mixed Mode probe `tests/mac-multiply-word-reference.py` compares
against a Python-integer model. All 3,840 cases and all defined CCR bits matched
both staged Windows dispatch modes and the G5/Classic and System 7.5.5 5260 on
2026-10-30. Both Macs produced identical 61,440-byte captures with SHA-256
`2d1cc65801f8292a178b18c5d3925218b2ae8d4313b99a3c2bbe4c7a833936ed`.
The 5260 remained responsive. These validate against Apple's PowerPC 68K
emulators, not physical 68040 bus/MMU behavior.

Syn68k, the Windows generator and ROMAW were rebuilt and staged. FPU/QEMU
oracle, Motorola FPSP, staged Mixed Mode probe, help, generated-file and DWARF
checks passed. Both repository formatter checks passed, and the updated trap
source compiles with native translation disabled.

Final validation matched the unchanged 480-row integer references in all four
modes (native, interpreter, noncc and noncc-interpreter), at 10,000 trials per
fixture. The complete control-register suite also passed. The retained word
register fast paths and the bounded runtime memory/immediate forms match the
saved multiplication CRC rows. All three staged/built CPU library copies have
identical hashes. No physical 68040 or MiSTer was available for this audit;
MMU, precise access-error restart and double-fault handling remain incomplete.

## PEA effective-address and stack-write audit

PEA now resolves its complete source EA at execution through the bounded decoder
before decrementing SP and writing the four-byte address. It does not read the
final source address. This includes (A7), displacement/indexed A7 and PC-relative
bases. The original `tests/pea-test.c` reproduced stale cached absolute addresses
before the fix. Inline PEA forms and their dedicated native descriptor were
removed; shared MOVE/LEA native helpers remain. SWAP and BKPT retain their
separate encodings and execution paths.

Illegal PEA modes take vector 4. Missing extensions, indirect pointer reads or
stack-write bytes take the existing synchronous format-7 fault at the original
instruction. EA failures leave SP unchanged; a rejected stack write retains the
predecrement in the original stack bank and changes no prefix bytes. All CCR
bits are preserved, including live flags from preceding native arithmetic.
This remains the flat-RAM model, not precise 68040 MMU/restart/writeback or
double-access-fault support.

The focused fixture passes 12,037 cases in each dispatch mode: every PEA source
mode, all CCR patterns, all three stack banks, unaligned stack addresses, source
A7 aliases, cached absolute edits, native cached SP offsets, illegal modes,
whole-write guards, truncated EA extensions and full-index pointer faults.
Twenty fresh Windows runs and a debugger run passed; WSL runs also exited
successfully. Their result summaries can precede buffered trace lines in logs.

The original `tests/mac-pea-reference.py` Mixed Mode probe compares 2,880 cases
against a Python address model: indirect, signed displacement, scaled brief/full
indexed, absolute and PC-relative sources, including SP aliases and every CCR.
Both staged Windows modes and both Macs matched on 2026-10-30. The G5/Classic
and System 7.5.5 5260 produced identical 46,080-byte captures, SHA-256
`c1b28d2eb7be27ecd4c6823469d0e5476cc031650e6a83bfb0c09dd0f7740929`.
The 5260 remained responsive. These compare Apple's PowerPC 68K emulators,
without physical 68040 bus/MMU validation.

Syn68k, its Windows generator and ROMAW were rebuilt and staged. FPU/QEMU,
Motorola FPSP, staged Mixed Mode probes, ROMAW help, generated-file and DWARF
checks passed. Both repository formatter checks passed; the trap source also
compiles with native translation disabled.

Final validation matched all four unchanged 480-row integer reference files
(native, interpreter, noncc and noncc-interpreter) at 10,000 trials per fixture,
including the PEA rows. The complete control-register suite passed with the new
fixture. The built library and both installed/staged copies have identical
hashes. Physical 68040 MMU, precise access-error restart/writeback and double
access-fault handling remain incomplete and were not validated by these Mac
emulator comparisons.

## LEA extension and alias audit

LEA forms with extensions now calculate their complete source EA at execution
through the bounded decoder before replacing the destination address register.
The register-indirect interpreter/native fast path remains in use. The original
`tests/lea-test.c` reproduced a stale absolute address in a cached block before
the change. Dedicated inline displacement/indexed native descriptors were
removed; shared MOVE helpers remain. Destination aliases with the base or index
register read the original value. LEA does not read the final effective address.

Illegal source modes and reserved full-index extensions take vector 4. Missing
extension or indirect-pointer bytes take the existing synchronous format-7 fault
at the original instruction without modifying the destination or CCR. Native
source offsets and live arithmetic flags are committed before decoding. This
is still the flat-RAM model, without precise physical 68040 MMU/restart behavior.

EXTB.L occupies the otherwise-invalid mode-0 encoding with destination field 4.
The expanded fixture caught an overlap in the initial translation change;
EXTB.L is excluded from the added PC metadata path and has explicit regression
cases. Its established instruction implementation remains in use.

The focused fixture passes 100,978 cases in each dispatch mode: every destination
including A7, all LEA source modes, all CCR patterns and stack banks, source and
index aliases, every index register/size/scale, brief/full indexed forms, cached
absolute/displacement/index edits, native offsets/flag liveness, invalid encodings,
truncated extensions and indirect-pointer faults. Faults retain the destination,
including A7 in its original user stack bank.

The original `tests/mac-lea-reference.py` Mixed Mode probe compares 20,160 cases
against a Python address model. It covers every non-SP destination, signed
absolute/displacement and brief/full scaled indexed forms, PC-relative addresses,
source/destination aliases, source SP and every CCR pattern. Destination A7 is
excluded on real Macs to preserve their interrupt stack; the isolated fixture
covers it. Both staged Windows modes, G5/Classic and the System 7.5.5 5260 matched
on 2026-10-30. Both Macs produced identical 322,560-byte captures, SHA-256
`baedc6f7d3a3cfd4c9a9eb4b0f8795e7021082535b52fa079b45220eed2e5ebf`.
The 5260 remained responsive. These compare Apple's PowerPC 68K emulators,
not physical 68040 bus/MMU behavior.

Syn68k, the Windows generator and ROMAW were rebuilt and staged. FPU/QEMU,
Motorola FPSP, staged Mixed Mode probe, help, generated-file and DWARF checks
passed. The trap source also compiles with native translation disabled.
Both repository formatter checks passed.

Final validation matched all four unchanged 480-row integer reference files
(native, interpreter, noncc and noncc-interpreter), at 10,000 trials per fixture,
including LEA and EXTB.L. The complete control-register suite also passed with
the expanded fixture. The built library and both installed/staged copies have
identical hashes. Physical 68040 MMU, precise access-error restart/writeback and
double access-fault handling remain incomplete; the Mac captures validate Apple
68K emulator behavior, not physical 68040 bus behavior.

## JMP/JSR target and stack audit

JMP/JSR now resolve their complete target EA at execution through the bounded
runtime decoder. The original `tests/control-transfer-test.c` reproduced a
stale cached absolute JMP target before this change. The translator retains the
original instruction PC and no longer embeds JSR's return address; the decoder
uses the end of the live EA extensions. Block analysis no longer reads fixed
JMP/JSR extensions or precompiles their targets. Dedicated JSR native descriptors
were removed; shared branch helpers and BSR remain.

JSR resolves the target before decrementing SP, including source-SP aliases.
Its complete four-byte return write is checked before touching memory; a rejected
write retains the predecrement and changes no accessible prefix bytes. EA faults
leave SP unchanged, and all faults preserve incoming CCR. Illegal modes take
vector 4; missing EA/pointer/stack bytes take the existing synchronous format-7
fault at the original instruction. Successful targets use the usual address
mask. This does not implement instruction-target alignment/fetch faults, physical
bus ordering or precise MMU/restart/writeback behavior.

JMP/JSR now use checked terminal dispatch rather than embedded fixed native
targets. At commit `c3d15e0`, JSR no longer populated the old fixed-target return cache;
RTS retained its optional BSR cache and handled uncached JSR returns. The later
BSR/cache follow-up below restores warm return caching for both checked calls. The RTS fixture adds 128 warm/repeated BSR return cases (1,925 total per
mode). BSR's cached extension and unchecked return-write paths were left for
the separate follow-up below.

The focused fixture passes 23,838 cases per dispatch mode: every source mode,
all CCR patterns and stack banks, data-stack alignments with even instruction
targets, source-SP aliases, signed/PC/indexed/indirect targets, exact return PCs,
live cached target edits, native offset/CCR producers, illegal modes, truncated
EA/pointer reads and whole-stack-write guards. Target markers and return-site
markers independently check the paths actually taken.

The original `tests/mac-control-transfer-reference.py` Mixed Mode probe compares
4,672 JMP/JSR entry-SP, saved-PC and CCR records on both staged Windows modes
and both Macs. It uses A0-A6 and PC-relative targets, including full memory
indirection. Absolute and source-SP targets are covered only by the isolated
fixture. The probe snapshots flags before data moves and restores its entry CCR
before RTS; the target/return stack records are normalized against real caller
addresses. Both Macs matched on 2026-10-30 and remained responsive. Their
74,752-byte captures are identical, SHA-256
`f7eb349ef76545b8cf11d4f1b3665c67cbae2d86d9d5c0ac0c4a9d45f14720eb`.
These compare Apple's PowerPC 68K emulators, not physical 68040 bus/MMU behavior.

Syn68k, the Windows generator and ROMAW were rebuilt and staged. FPU/QEMU,
Motorola FPSP, staged Mixed Mode probes, help, generated-file, DWARF and both
formatter checks passed. The trap source compiles with native translation
disabled. Both full suites have completed for commit `c3d15e0`: all four
unchanged 480-row integer references matched at 10,000 trials per fixture,
and the complete control-register suite passed, including 1,925 RTS cases per
mode. These results precede the separate follow-up BSR/cache work.

## BSR displacement and checked-call return-cache follow-up

BSR byte/word/long forms now fetch their displacement at execution through the
bounded decoder and write the correct following PC through the whole-write
helper. Word displacement sign extension and the PC+2 target base are separate
from the following-PC value. The original `tests/bsr-test.c` reproduced a cached
word-displacement edit taking the old target before this change. Translation
and block analysis no longer read BSR extensions or embed its target/return PC;
its instruction span is only the opcode until execution resolves the operand.
BSR's dedicated native descriptor and obsolete shared inline call form were
removed. Other relative branches retain their existing implementations.

A missing displacement preserves SP and CCR. A rejected return write retains
its predecrement but touches no accessible prefix bytes. Both errors use the
existing synchronous format-7 fault at the original BSR. This does not implement
instruction-target alignment/fetch faults, physical MMU/writeback/restart or
exception-stack double faults.

Checked JSR and BSR calls now populate the optional return cache only when the
return block already exists. They do not compile or read a missing return site
before the callee returns to it. Cache lookup/update blocks virtual interrupts,
retains the existing native/interpreter cache layout and stores the return tag
in guest byte order. Direct block destruction now clears the cache before
freeing code, including the callback-removal path that previously bypassed the
outer invalidation helpers. The focused test verifies warm hits, explicit
return-block invalidation, cold recovery and direct callback block destruction.

The BSR fixture passes 52,943 cases per dispatch mode: even byte displacements
outside the call/return storage, signed word/long boundaries within RAM, larger
long forward targets, all CCR patterns, all stack banks, unaligned data stacks,
live cached word edits, warm JSR/BSR cache hits, invalidation, protected-page
and byte-truncated displacement reads and whole-return-write guards. The target
and return markers check execution rather than merely decoding operands.

The original `tests/mac-bsr-reference.py` probe compares 768 calls across all
three displacement widths, both directions, four distances and every CCR.
Normalized entry SP and saved return PC are checked along with target/return
CCR snapshots. Both staged Windows modes and both Macs matched on 2026-10-30;
the Macs remained responsive. Their identical 12,288-byte captures have SHA-256
`4a22567bf073c6c971665bb4e30b33e8b1541016b39f338d142f59850b3958b1`.
These compare Apple's PowerPC 68K emulators, not physical 68040 bus/MMU behavior.

Syn68k, the Windows generator and ROMAW were rebuilt and staged. Both staged
BSR and JMP/JSR probes, help, generated-file, DWARF and formatter checks passed;
the trap source compiles with native translation disabled. All three built/
installed/staged CPU archives have identical hashes. Final validation matched all four unchanged 480-row integer references at
10,000 trials per fixture, and the complete control-register suite passed.
The final FPU/QEMU and FPSP runs passed again after central cache invalidation. In-progress older
reference processes were stopped after adding direct-destruction invalidation;
final checks run against that candidate. Initial native and final noncc fixture
processes each failed to reserve their fixed-address test memory before executing
instructions (Windows error 487); fresh-process retries are used, not reference
changes.

## Extended BRA/Bcc displacement audit

Word/long BRA and conditional branches now fetch their displacements at
execution through the bounded decoder. Byte forms retain their existing inline
interpreter/native fast paths. BSR remains in its separately checked call path.
The original `tests/branch-test.c` reproduced a cached BNE.W displacement edit
using the old target before the change. Block analysis no longer reads the
extended displacement or compiles both destinations; the terminal decoder
selects its live target or the PC after the displacement. The target base is
PC+2 for both widths, with signed word extension and 32-bit address arithmetic.

The runtime condition evaluator uses all 16 standard integer conditions and
preserves every CCR bit. Missing extension bytes take the existing synchronous
format-7 access fault before selecting a target, including when the condition
is false. This remains the bounded flat-RAM model; instruction-target alignment/
fetch faults and precise physical MMU/access-error restart are not implemented.

The focused fixture passes 42,257 cases in each dispatch mode: every non-BSR
condition, all incoming CCR patterns, three displacement widths/directions,
three stack banks, native arithmetic flag producers, cached word/long edits
and protected-page/byte-truncated extensions. Independently enumerated truth
masks select target markers, and unchanged SP/CCR are checked on both outcomes.

The original `tests/mac-branch-reference.py` Mixed Mode probe compares 2,880
branch decision/SP/CCR records against a Python condition model. Both staged
Windows modes and both Macs matched on 2026-10-30. The identical 46,080-byte
G5/Classic and System 7.5.5 5260 captures have SHA-256
`30e3049e8c52effdf91758d489175b98229fae2abec0fc03e6b92213da56f990`.
The 5260 remained responsive. These compare Apple's PowerPC 68K emulators,
not physical 68040 bus/MMU behavior. The first share deployment appeared as a
document and did not run; a new application name with sidecar metadata deployed
before the data fork was recognized and executed. Only completed result
captures are counted.

Syn68k and ROMAW builds/staging, complete control-register suite, FPU/QEMU,
Motorola FPSP, staged branch and BSR probes, help, generated-file, DWARF and
formatter checks passed. The trap source compiles with native translation
disabled. All four unchanged 480-row integer references passed at 10,000
trials per fixture. The staged probe passed again after distinguishing the
68000 word and 68020 long opcode definitions. The initial native fixture process failed to reserve fixed-address test RAM before
execution (Windows error 487); its fresh-process retry is used.

## DBcc live displacement and counter audit

All DBcc conditions now fetch their signed word displacement at execution
through the bounded decoder. The original `tests/dbcc-test.c` reproduced a
cached DBF displacement edit using the old target before the change. The
translation stream retains the original PC only; block analysis no longer
reads DBcc extensions or precompiles either destination. The dedicated inline
DBRA descriptor was removed. Scc and other condition-setting operations retain
their existing instruction mappings.

A true condition falls through without changing the counter. A false condition
decrements only the selected register's low word, branches from PC+2 unless the
new word is $ffff, and preserves the upper half and every CCR bit. Missing
displacement bytes take the existing format-7 access fault before any counter
change, including DBT. This remains the synchronous bounded flat-RAM model;
physical instruction-prefetch/bus ordering, target alignment/fetch faults and
MMU/access-error restart are not covered.

The focused fixture passes 121,874 cases per dispatch mode: every condition,
all data registers, all CCR patterns, upper-half preservation, every possible
16-bit decrement input, signed counter boundaries, live cached displacement
edits, actual backward targets, all three stack banks, native flag producers
and protected-page/byte-truncated extension reads. Independent truth masks and
target markers verify both counter and actual path.

The original `tests/mac-dbcc-reference.py` Mixed Mode probe compares 32,768
records with all conditions/registers/CCR patterns, boundary counters and both
directions. It retains the output pointer and caller SP in address registers
so D6/D7 can also be tested as counters. Counter snapshots use MOVEA before CCR
capture, preserving the flags being measured. Both staged Windows modes and
both Macs matched on 2026-10-30. The G5/Classic and System 7.5.5 5260 produced
identical 524,288-byte captures, SHA-256
`4b35ffd7bc8681efcd4ddec794863a8c065e26f6621d7d967c6b7a15824b984f`.
The 5260's remote input request timed out while the large probe ran, then the
agent responded; its completed output matched, remote input was released and
a fresh screenshot showed the responsive Finder. These compare Apple's PowerPC
68K emulators, not physical 68040 bus/MMU behavior.

Syn68k, the Windows generator and ROMAW were rebuilt and staged. FPU/QEMU,
Motorola FPSP, staged DBcc and branch probes, help, generated-file, DWARF and
both formatter checks passed. The trap source compiles with native translation
disabled. All four unchanged 480-row integer references passed at 10,000 trials
per fixture, including DBcc's actual loop cases; the complete control-register
suite also passed. The interpreter fixture twice failed to reserve fixed-address
Windows RAM before executing instructions (error 487); its separate fresh-process
retry passed. All 29 staged PPC integration probes passed. The checked terminal
DBcc path is slower for the legacy reference's long loops than the retired
inline path; no application-performance equivalence is claimed.

## Final cross-runtime regression and reference-runner reliability

After the DBcc change, the complete standalone PowerPC CPU/PEF suite and all
29 staged PPC bridge probes passed. This includes native integer blocks,
code publication, Mixed Mode callbacks, FPSP, components and application-facing
Toolbox probes. These are regression results, not a claim of full PPC or Carbon
compatibility. The standalone suite also completed 312,800 independent fused
FPSCR cases.

The integer reference runner now handles the observed Windows pre-execution
reservation failure with at most five fresh launches. Retries require exit 1,
empty stdout and only the exact error-487 reservation diagnostic; each failed
startup log is retained separately. CPU errors, partial test output, extra
error diagnostics and reference mismatches are not retried. Seven isolated
runner tests cover success, recovery, exhaustion and each nonretryable case.
This changes test orchestration only; it does not alter CPU code or saved CRC
references. The full CPU references above passed before this runner update.

## MOVEP live displacement and sparse-transfer audit

MOVEP.W/L in both directions now fetches its signed displacement at execution
through the bounded decoder. The new original fixture reproduced a cached
instruction using its old displacement before the change. Translation retains
only the opcode and original PC; all incoming CCR bits remain live for access
faults. Other dynamic bit instructions retain their mappings.

Byte transfers follow the high-to-low register order and two-byte address
stride documented in M68000PRM pages 4-131/4-132. Word loads preserve Dn's upper
half. Address registers and CCR are unchanged. Each accessed byte is bounded;
gap bytes are neither accessed nor modified. A load commits Dn after all bytes
are available. A store retains completed byte writes if a later sparse byte
faults. Missing displacement bytes fault before transfers. These are bounded
flat-RAM access errors, not a complete 68040 bus/MMU/writeback/restart model.

The focused fixture passes 115,144 cases per dispatch mode. It covers every
Dn/An pair, both widths/directions, all CCR patterns, signed displacement
boundaries, odd/even data addresses, untouched registers/gap bytes, cached
extension edits, protected-page failures at every transferred byte, truncated
displacements and native-produced flags/address offsets on success and fault.
An inaccessible gap immediately after the last byte does not fault.

The separate original Mixed Mode probe executes 8,192 cases with all data
registers, A0/A1 sources, both widths/directions, signed displacements, odd/even
addresses and every CCR pattern. Both staged Windows modes and both Macs
matched on 2026-10-30. The G5/Classic and System 7.5.5 5260 produced identical
131,072-byte captures, SHA-256
`dc9cff3f6ce0e558b39629387059fd2df052fc5ba4471150e064809ebcb78da5`.
These compare Apple's PowerPC 68K emulators, not a physical 68040 or physical
fault-cycle behavior. Run `tests/check-hardware.ps1 mac-movep` for the staged probe.

MOVEP validation completed: Syn68k/ROMAW rebuild and staging, the complete
control-register suite, FPU/QEMU, Motorola FPSP, all 29 staged PPC bridge probes,
generated-file regeneration, DWARF, Windows help and both formatter checks
passed. The trap source also compiles with native translation disabled. All
four unchanged 480-row integer references passed at 10,000 trials per fixture;
independent mode runs use private executable snapshots and output directories.
Observed pre-execution RAM reservation failures used the exact error-487 retry
guard; no CPU/CRC failure was retried. Built, installed and staged Syn68k
archives have identical SHA-256 values. This closes the MOVEP audit, not the
remaining physical 68040 MMU, exception-restart or bus-model work.

## MOVEM live mask and bounded transfer audit

MOVEM.W/L now fetches its register list and complete EA extensions at execution.
The original fixture reproduced a cached register-mask edit still selecting the
old destination before the change. Legal forms use terminal mappings, preserving
all incoming CCR bits for faults. EXT.W/L encodings remain separate; illegal
MOVEM addressing modes trap before a mask fetch. The old inline long predecrement
and postincrement descriptors were removed. No performance equivalence with
those inline paths is claimed.

Transfers follow M68000PRM pages 4-127 through 4-130: normal lists use ascending
register order, predecrement reverses both the mask and traversal, and word
loads sign-extend to 32 bits in both register banks. A predecrement base included
in the list stores its initial value minus one operand size (the 68020/030/040
rule); a postincrement base load is discarded in favor of the final increment.
An empty list leaves an automatic base unchanged. PC-relative EAs use the EA
extension's address after the register mask, including full indexed indirection.

Each word/long transfer is bounded before it commits. Completed transfers survive
a later fault; postincrement retains successful increments, and predecrement
retains the attempted decrement. Missing mask/EA extensions or invalid indexed
forms fail before transfers. This is the existing synchronous flat-RAM format-7
policy, not precise physical 68040 bus/SSW/writeback/restart behavior.

The focused fixture passes 562,531 cases per mode. It covers every 16-bit mask
in both widths/directions and ordinary/automatic forms, every base register,
CCR patterns, odd/even addresses, source/base aliases, untouched memory/registers,
live mask/displacement edits, all legal EAs and stack banks, full indexed
indirection, reserved/illegal forms, protected transfers, truncated masks/long
EAs, descending address wrap and native-produced flags/cached address offsets.

The separate original Mixed Mode probe checks 10,752 cases with D0/D1/A0/A1
lists and A2 base-register aliases, word/long transfers, ordinary/predecrement/postincrement modes, both
alignments and every CCR pattern. Each case records 48 bytes of CCR, normalized
base, all four transferred registers and the complete 24-byte memory/guard area. Stored
base values are normalized in place after capturing CCR/registers, preserving
word wrap. Both
staged Windows modes and both Macs matched on 2026-10-30. The 516,096-byte
G5/Classic and System 7.5.5 5260 captures have identical SHA-256:
`49edd0dce87d789f609fdff1834430c26d22ac8a0eaa07914f9d1fb8e4320de7`.
These compare Apple's PowerPC 68K emulators. All base registers and physical
fault-state choices have synthetic/manual checks; this Mac probe does not
validate physical fault cycles or a physical 68040.
Run `tests/check-hardware.ps1 mac-movem` for both staged dispatch settings.

MOVEM validation completed: Syn68k/ROMAW rebuild and staging, complete
control-register suite, FPU/QEMU, Motorola FPSP, all 29 staged PPC bridge
probes, generated-file regeneration, DWARF, Windows help and both formatter
checks passed. All four unchanged 480-row integer references passed at 10,000
trials per fixture using independent private snapshots/output directories.
The trap source compiles with native translation disabled. Built, installed
and staged Syn68k archives have identical hashes. Browser, Coloring Book and
MacWrite reached their event loops under Windows GDB; these are startup smoke
checks, not full application workflows. No CPU reference output was changed.

## MMU translation engine foundation

The internal `mmu040.c` engine now provides 4K/8K table walking, TTRs,
permissions, used/modified descriptor updates, separate 64-entry ATCs,
qualified flushes, PTEST/MMUSR and control/reset semantics. Its 300,077 cases
pass on Linux ASan/UBSan and Windows PE32 against the built archive. Descriptor
callbacks receive physical addresses; failed searches do not dereference guest
pointers. The manual's descriptor-update flowchart was visually checked to
preserve pipelined U writes and locked page-U update requests.

This engine is not wired into CPU fetch/data/native/exception accesses yet;
guest MMU control selectors remain rejected. It does not establish guest paging,
physical cache/bus arbitration, access-error restart or Mac OS VM compatibility.
See [the engine contract and remaining integration](mmu040.md).

The internal MMU operand boundary now splits byte/word/longword transfers across
noncontiguous pages, propagates translation attributes and RMW write intent, and
reports partial completion without rolling back earlier writes or U/M updates.
Its 147,528 mapped cases pass on Linux ASan/UBSan and Windows against the built
archive. This is not yet connected to guest CPU execution or format-7 restart.

Bounded EA contexts can now explicitly use mapped memory, with separate
instruction fetches, immediate-source identity, data indirection, FC overrides,
RMW intent and retained fault/partial-access metadata. The Windows archive-linked
mapped EA probe passes 71 cases. Flat EA, complete control-register/addressing,
FPU dispatch and all 29 staged PPC probes pass. Global guest MMU enable remains
gated pending full interpreter/native/fetch/exception and Toolbox integration.

The FPU now routes opcode, extension, immediate, operand and saved-frame traffic
through bounded EA/mapped memory helpers. An internal stepping API returns
exception metadata without writing a flat stack for mapped callers. Its 295,765
mapped cases, flat interpreter/native FPU checks, 100 independent QEMU cases per
mode, and 146 Motorola FPSP cases per mode at two load addresses pass. This does
not enable global guest paging or implement format-7 continuation/writeback.

Mapped scalar access results now retain their original address, FC, size,
direction, locked-transfer intent and write value/read prefix. Basic format-7
SSW transfer fields distinguish ATC/table faults from physical operand faults
and qualify second-page MA correctly. The additional 121 metadata/SSW cases
pass on Linux ASan/UBSan and Windows, including propagation through mapped FPU
faults. Complete continuation, writeback and restart handling remain unfinished.

## Checked-store translation coherence

Checked MOVEM, MOVEP, LINK, MOVES, MOVE from SR and MOVE16 stores now invalidate
translated blocks overlapping completed writes. Sparse and multiple transfers
invalidate each successful transfer before a later fault; a failed atomic
transfer leaves its code and bytes intact. Checked pushes share the same path.
The regression fixture reproduces a stale cached instruction with the previous
archive and checks seven store forms, partial transfers, rejected writes and a
MOVEM store overwriting its own executing block in both dispatch modes.
This is conservative emulator translation coherence, not a physical cache or
68040 writeback/restart model.

Flat FPU numeric, control-register, FMOVEM and FSAVE stores now use the same
invalidation helper after each completed scalar write. Rejected whole-span
stores preserve cached code. The fixture adds all six supported numeric output
formats, control/multiple transfers, idle/null saved frames, a rejected double
store and an FPU instruction overwriting its own translated block. There are
23 cases per execution mode. Mapped FPU stepping retains caller-owned coherence.

## Mapped integer arithmetic

An explicit `syn68k_integer_step` boundary now executes word/long
MULU/MULS/DIVU/DIVS using the same arithmetic as ordinary dispatch, with mapped
opcode/extension/immediate/indirect/operand reads and returned exception metadata.
The 295,528-case mapped suite covers 4K/8K pages, both privilege modes, every
operand offset, supported addressing families, split reads, physical/translation
faults and zero divisors without touching a flat exception stack. Flat dispatch
retains its existing trap delivery. General mapped integer/native execution,
exception-stack translation, access-error restart and guest MMU enable remain
unfinished; see [the MMU integration contract](mmu040.md).

MOVEM now shares this mapped boundary as well. All register masks, both widths
and both transfer directions are checked with ordinary and automatic addressing;
the 721,036 mapped cases also cover 4K/8K page offsets, base aliases, completed
and partial transfers before translation/physical faults, instruction-mask
fetch failures and overwriting the currently executing opcode/mask. Flat
MOVEM behavior and code invalidation remain on their existing paths. Mapped
code-cache coherence and exception/restart delivery remain caller-owned.

MOVES now uses that boundary for alternate-function-code operands while
preserving ordinary opcode/EA fetch identity. The 296,112 mapped cases check
normal/merged instruction FCs, bypass-space device banks, widths, register
aliases, address updates, privilege and partial faults. The 39,936-case flat
fixture remains unchanged. Operand callbacks now carry FC separately from
physical address and translation attributes. This remains explicit component
stepping; general guest paging and restart handling are not enabled.

The explicit integer stepping boundary also covers JSR/JMP/LEA/PEA with mapped
address formation and stack writes. Its 49,298 cases cover target calculation
without target fetch, stack-page splits and faults, A7 aliases and CCR
preservation. Mapped callers receive a logical next PC without using the flat
return cache; whole-program dispatch and exception restart remain unfinished.

MOVEP now routes its displacement and sparse byte operands through the same
explicit mapping boundary. Its 99,432 mapped cases check page crossings,
registers, sparse partial stores/faults and self-overwriting code. Failed loads
retain Dn, and flat behavior remains covered by 115,144 cases per mode.

## Mapped instruction streams and exception delivery

The explicit mapping backend now executes bounded instruction streams through
the shared integer/FPU decoders. Register/scalar/complex operations, branches,
stack/control/status operations, CHK/CMP2/CHK2 and MOVE16 use mapped live fetches
and operands. MOVEC, PTEST and PFLUSH operate on the supplied MMU engine.
The scalar/register differential fixture passes 80,672 cases, and the mapped
control/bounds/MOVE16 fixture passes 101,224 cases. Shared flat MOVE16 and bounds
fixtures pass 50,453 and 266,344 cases respectively in each execution mode.

Mapped exception entry writes ordinary frames and vector references through
supervisor mappings, preserving both master-interrupt stack frames. An aligned
opcode-fetch fault can now generate a restricted format-7 frame: a guest handler
repairs the real page descriptor, flushes the ATC and returns through RTE to
execute the faulted opcode. Read faults with unchanged register/CCR/SR/FPU state
and no attempted stores can now use this path too. Partial/RMW reads and
MOVEM/MOVE16/RTE/FPU continuations remain excluded. The 4,154-case stream fixture
also checks byte/word/long data and extension-read recovery, MMU enable
from guest code, FPU single fetches, page-crossing loops, trap handlers, stack
banks, delivery failures and rejection without side effects.

This does not complete 68040 compatibility. Operand-fault continuation, MOVEM
restart, pending writebacks, full FPU/cache state and ROMAW native/Toolbox
mapping integration remain outstanding. Flat ROMAW MMU enables stay gated.
See [the current contract and limits](mmu040.md); older foundation notes above
describe the integration stages before this explicit stream backend existed.

Validation for this integration batch: `make -C syn68k` and `JOBS=10 make -C
romaw` compiled/linked the Win64 outputs. Windows locked the old staged
executable, so the verified new build was staged by renaming the loaded sibling;
the running session was left alone. The installed/staged CPU archives match.
The stream, scalar/register, control/bounds/MOVE16 and mapped FPU fixtures pass
4,154, 80,672, 101,224 and 295,765 cases respectively against the built archive.
Flat MOVE16 and CHK2/CMP2 fixtures pass in both interpreter/native modes.
Generated-file regeneration, DWARF checks, Windows help and scoped clang-format
checks pass. MacWrite and Browser reach GetNextEvent/WaitNextEvent under Windows
GDB with native execution active in an isolated app profile; these are startup
checks, not full interactive workflows or physical 68040 conformance tests.

## Store writebacks and mapped read continuations

Aligned scalar final stores now retire with their calculated CCR and next PC,
publishing one WB1 or WB2 slot for the guest handler to complete. WB1 data uses
memory lanes, while WB2 data is register aligned. Actual guest MOVES cleanup
and RTE tests verify exactly one successful physical store. Pending T1 traces
carry CT and become a format-2/vector-9 exception after cleanup; T0 alone does
not trace these scalar stores.

Ordinary read faults restore speculative EA register updates without undoing
completed reads or descriptor history. Split word/long reads can recover after
the guest maps the missing page and returns through RTE. Indexed and PC-relative
MOVEM loads instead preserve completed register loads and carry CM/saved EA.
Restart skips address recalculation and indexed indirect pointer reads, and
defers interrupts until completion. An unavailable or changed non-MOVEM opcode
during that continuation stops explicitly with its saved state retained.

The final stream suite passes 51,322 cases. Additional archive-linked fixtures
pass 71 EA, 721,036 MOVEM, 49,298 control-address, 101,224 flow/bounds/MOVE16,
80,672 scalar/complex and 295,765 mapped FPU cases. Both flat interrupt/RTE
modes pass 43,040 cases each. The shared scalar kernels compile with the x64
JIT disabled. Scoped clang-format, generated-file, DWARF and Windows help
checks pass. ROMAW was linked and staged with matching built/installed/staged
CPU archives; MacWrite and Browser reach their native event loops under Windows
GDB using isolated app copies. User-running processes were left running.

This remains incomplete 68040 compatibility. Other MOVEM addressing modes and
stores, resumed-opcode fault delivery, MOVE16/locked-operation recovery,
FPU/cache continuations and ROMAW-wide MMU/host-pointer integration remain
outstanding. Flat ROMAW guest paging is still gated. The current details and
limits are in [the MMU execution contract](mmu040.md).

## Win64 JIT and persistent native AOT

The JIT covers all 56,790 opcode samples in the current decoder. Persistent
native prefixes are now available through the opt-in `SYN68K_AOT_CACHE`
directory setting. Warm restoration skips the emitter, relocates current host
addresses and uses normal Block ownership and invalidation. See
[operation, format and validation](aot-x64.md).

The November 18, 2026 validation passed `jit-x64`, `aot-x64`, the FPU oracle
and Motorola FPSP suites. All four 10,000-iteration integer CRC modes matched
their references (`native`, `interpreter`, `noncc` and `noncc-interpreter`).
The isolated AOT fixture restored 16 blocks with zero
emitter calls in a fresh warm process. MacWrite and Browser reached their event
loops under Windows GDB in cold and warm launches; warm runs restored 549 and
388 blocks respectively. Dynamic/unmatched blocks still compiled normally.
These are startup smoke checks, not complete interactive workflows.

Win64 ROMAW and syngen were rebuilt/staged, with identical built, installed
and staged CPU archives. Windows help, generated-file dependency propagation,
DWARF and scoped C/Python/shell formatting checks passed. The PowerShell check
passed; the full formatting check reported an unrelated in-progress
`system/tests/clipboard_live_probe.py` file. The existing user-running
ROMAW session was retained while staging the new executable.

This completes the persistent-cache part of JIT/AOT for the current flat
runtime. It does not remove the mapped CPU, restart/writeback, FPU-continuation
or ROMAW-wide MMU integration gaps described above.

## Direct FPU/MMU lowering (in progress)

The stricter direct-native goal is in progress. Win64 JIT/AOT register arithmetic
now executes emitted, closed integer arithmetic templates for FMOVE/FABS/FNEG,
FADD/FSUB/FMUL/FDIV/FSQRT, explicit single/double forms, FSGLDIV/FSGLMUL and
FCMP/FTST. The command word and source/destination registers remain live; saved
AOT code does not capture their old values. Ordinary finite/zero operands and
masked inexact results commit in generated code. NaNs, unsupported operands,
enabled/range exceptions and pending saved-state replay keep checked exits.
FPU fetch guards retain the FPU decoder's fault priority.

The same build-time image contains native MMU translation, PTEST, flush,
control/reset and scalar operand roots. The maintained table walker and
operand boundary are compiled into verified relocatable code, with no live
imports or writable global state. Optional permanent ordinary RAM bypasses
physical callbacks for descriptor reads, locked U-bit updates and operands;
alternate FC spaces and accesses outside the span retain checked callbacks.
These roots are validated components; they do not yet connect ROMAW-wide
guest paging or complete mapped JIT/AOT integration.

The emitted kernel suite passes 34,680 arithmetic vectors, 300,077 MMU cases,
147,528 operand cases, 121 access-status cases, 48 FC-bank cases and 280 declared
RAM/bounds/history/rewalk cases under Windows and Linux ASan/UBSan. Windows
publication tests inject allocation/protection/flush failures and verify
rollback. Arithmetic templates bind SoftFloat state to their invocation rather
than the compiled runtime globals; the register base is saved/restored by each
entry. The FPU closure is compiled as one translation unit, while MMU sources
use LTO, avoiding cross-partition global-register declarations.

The direct register suite passes 57,344 live-command, alias, signed-zero,
comparison, rounding and precision cases with a link-time semantic-tail guard.
Four exceptional cases match the checked saved frames. A missing-command test
reproduced an incorrect integer-decoder exit and now verifies the FPU exit and
unchanged guest state. Existing FPU/oracle and FPSP groups pass; fresh cold/warm
AOT processes reject semantic tails for direct FADD/FSUB while retaining the
cache corruption/publication checks.

Direct FBcc/FDBcc, register FScc and untaken FTRAPcc now use emitted predicate
logic, including quiet/unordered results and masked BSUN updates. The grouped
suite adds 16,612 predicate/counter/byte/boundary cases. Cross-page longword
extensions remain native; a missing second half retains the original FPU fault
PC. Fresh-process AOT replay guards direct arithmetic, conditional counters,
unordered byte results and signed branches against semantic tails.

Remaining CPU work includes direct memory operands/control-memory/transfer/save/restore forms,
MMU-aware native instruction fetch/data and persistent-cache identity, and
remaining access-error continuations. The current goal covers the complete CPU
backend through its mapped-memory bus. ROMAW-wide Toolbox paging is a
separate integration: guest paging and capability reporting remain gated.

The closed native image now also provides effective-address calculation with
an invocation-local CPU, mapped instruction/data accesses and the same optional
ordinary RAM span. It reuses the maintained EA decoder and shared indexed
encoding rule. A saved calculated EA skips repeated indirect reads during CM
restart. This primitive is not yet connected to the FPU memory emitters.
The grouped Windows and Linux ASan/UBSan runner adds 135,699 EA cases: every
indexed extension, addressing/register updates, exact callback traffic,
split-fetch metadata, mapped RAM and nested native callback entry. Native root
entry restores its context register and never publishes a stack-bound bus.

FPCR/FPSR/FPIAR transfers to and from data registers, plus FPIAR address-register
transfers, now execute directly with live command words and architectural input
masks. The grouped FPU test adds 4,096 normal transfers without semantic tails
and 704 invalid control encodings with no pre-exit side effects. A further 63
reserved register-arithmetic EA encodings now take the checked exit; the first
case reproduced an incorrect successful FMOVE against the previous archive.
Fresh-process AOT replay also covers changed arithmetic/control commands and
FPIAR address-register transfers. The full mapped CPU suite passed after the EA
increment, including its 51,322 stream/restart/delivery cases.

The native image now contains fixed integer/single/double input and output
conversion roots. Single/double source classification and integer destination
conversion are shared with the FPU model, including raw NaN payload/signaling
tags, unsupported-denormal tags, integer saturation and pre-clamp rounded
values for deferred OPERR frames. Floating output retains range status and
the intermediate value for deferred writeback. These are conversion primitives;
the FPU memory instructions still need integration. The seven-group Windows
and Linux ASan/UBSan runner adds 176,046 conversion/rounding/state-isolation
cases. The verified image has 29 roots and no runtime imports or writable
globals; its Windows form is 51,328 bytes and its Linux form exercises 83
internal pointer relocations.
The existing FPU value/condition oracle, exact arithmetic/division fixtures,
146 FPSP cases in both dispatch modes, 295,765 mapped-FPU cases, direct-native
FPU checks and cold/warm AOT corruption/publication checks also pass after
sharing the conversion routines. Syn68k was rebuilt and staged.

A private checked arithmetic exit now accepts a captured source and completed
access certificate. It does not fetch the command, calculate an EA or read a
device again, and does not treat captured execution as FRESTORE replay. The
ordinary model and this exit share result/exception metadata assembly. Invalid
packets leave the CPU and output unchanged. The grouped native FPU regression
adds 46,620 model/frame/device-read/EA/invalid-packet cases, including every
supported arithmetic operation, all six non-packed input formats, pre/post
updates, indexed indirection, PC-relative/immediate/register sources and PC
wrap. No bus read is allowed after capture. This is exceptional-path groundwork;
the FPU memory emitters and mapped native coordinator remain unfinished.

Native publication now has an immutable 16-byte RX header with a managed-entry
flag. Managed invocations retain allocations until their oldest recursive
frame exits and preserve invocation scratch across callbacks into other guest
code. Their mutable lifetime state lives in C frames. AOT format 2 validates
and restores the managed flag. Checkpoint/unwind hooks restore an ancestor
scope before a nonlocal exit; Macintosh application chaining uses the hook and
MinGW's non-SEH setjmp expansion, avoiding an OS walk through generated frames
without SEH metadata. Real executable-page tests cover callback return after
eviction, nested scratch restoration, same-allocation recursion and partial/
complete nonlocal unwind. Production memory emitters still need to select
managed entry when they begin calling the mapped bus.
The full JIT suite, direct/captured FPU groups and cold/warm AOT validation
passed against a frozen committed-source snapshot plus this increment; this
kept concurrent generator and Icon Services edits out of the build inputs.
The rebuilt CPU outputs and an updated `romaw-68040-direct-progress.exe`
checkpoint are staged. Windows help checks passed. Browser reached
WaitNextEvent with native execution enabled in a 24-bit startup smoke using
explicit 2 MB system/4 MB application zones; this is not an interactive or
application-chaining workflow test. The real RX fixture covers the native
scope cleanup used by the chaining hook.

Scalar Dn sources now feed native arithmetic directly for integer long,
IEEE single, integer word and integer byte formats. The live command selects
conversion and arithmetic roots; signed narrow inputs retain their register's
upper bytes. All 28 supported arithmetic/comparison operations share this
path. The grouped FPU test passes 215,456 normal format/register/rounding/
precision cases without semantic tails and 32 checked extended/double/packed/
ROM encodings with unchanged pre-exit CPU state. Fresh-process AOT replay
changes a cached command between arithmetic, control and single input, and
also checks signed D7 byte input. Memory operands and scalar destinations
remain to be integrated.
The CPU archive/generator and ROMAW progress checkpoint were rebuilt and
staged after this input increment. Windows help and Browser's bounded 24-bit
native startup smoke passed again.

Scalar move-out to Dn now uses native integer long/word/byte and IEEE single
conversion roots. It preserves floating CCs and untouched destination bytes,
updates status/accrued flags and FPIAR, and exits before CPU commits for range
or enabled exceptions. The grouped native test adds 51,200 normal output cases
without semantic tails and 96 reserved/range/enabled-exception guards. AOT
replay changes a cached body from byte input to rounded byte and IEEE single
output while checking preserved floating CCs. Emission-budget checks cover all
64 EA fields both at an empty buffer and at the reserved prefix boundary.
CPU and ROMAW progress binaries were rebuilt/staged; Windows help and the
bounded Browser native startup smoke passed after the output increment.

The mapped operand bus now has optional caller-owned native dispatch storage.
Three typed entries match the ordinary read/write/RMW ABI, including the
structure return, and execute the closed MMU walker and transfers. Ordinary
physical RAM can bypass descriptor and operand callbacks; alternate function
codes and devices retain checked callbacks. Template builds suppress the
outer dispatch so a native entry cannot recurse back into itself. Binding
failure preserves both caller objects. Mapped instruction scopes copy the
dispatch table to track attempted RAM writes without changing its owner's
table or retaining pointers to expired instruction frames.

This connects the emitted walker to existing mapped CPU accesses; it does
not yet supply mapped FPU instruction bodies or a mapped native cache
coordinator. ROMAW-wide Toolbox paging remains outside this CPU-backend
work and the flat-memory capability gate remains in place.

The eight native-kernel groups pass on Windows x64 and Linux ASan/UBSan.
The typed-bus group compares 17,280 requests per host with the model, including
the returned access record, ATC state, descriptor/operand bytes, callbacks and
write tracking. A linker guard rejects host-walker calls during native
dispatch; disabling native reads reproduces that assertion. Publication
failures leave binding storage unchanged. The full mapped group, direct and
captured FPU groups and cold/warm AOT checks pass. Mapped streams now run both
with C and emitted buses, plus successful RAM-backed instruction/FPU/self-
modifying-code streams: 102,808 cases. As with the preceding increments,
validation used the frozen source base plus the owned changes. CPU outputs
and the matching ROMAW progress checkpoint were rebuilt/staged; Windows
help and the bounded Browser native startup smoke passed again.

A second private checked exit accepts a consumed class-0 FPU command before
EA/operand consumption. It advances the command PC without reading it again;
pending frames still exit before command fetch through the ordinary API.
Invalid arguments preserve CPU/output state. The grouped FPU test compares
all 65,536 command encodings plus all EA fields, failed extensions/operands
and PC wrap: 69,632 cases, checking CPU, result, RAM and ordered bus transfers.
The bus rejects repeated instruction reads while permitting data aliases to
the command address. Direct/captured FPU groups, exact-rational/integer and
independent FPU oracles, FPSP and cold/warm AOT checks pass after the change.
CPU outputs were rebuilt/staged. The ROMAW progress checkpoint remains
the matching native-bus checkpoint described above; this new checked exit
still awaits use by mapped native instruction bodies.

Checked FPU contexts are now invocation-local. Their mutable storage belongs
to the caller of the function containing setjmp, retaining defined metadata
after faults without sharing a jump buffer between nested calls. The wrappers
restore SoftFloat rounding, precision, flags and tininess on return. A device
callback probe previously changed an outer FADD's next PC/FPIAR to the child's
addresses and leaked the child's inexact status; the probe now passes.
The grouped regression adds 52,416 CPU/result/RAM/ordered-transfer cases and
180,000 callbacks, including three nested levels, deferred child exceptions,
outer read/write failures, EA updates and C/emitted buses. FPU/oracle, FPSP,
295,765 mapped-FPU cases, 102,808 stream cases and cold/warm AOT checks pass.
CPU outputs and a matching ROMAW progress checkpoint were rebuilt/staged;
Windows help and the bounded Browser native startup smoke passed again.

Mapped CPU clients can now opt into native execution with `native = 1`.
Managed class-0 register FPU bodies fetch live commands through emitted MMU
code, execute normal arithmetic/control/Dn transfers directly, and return
checked outcomes before invoking exceptional helpers. Command access faults
retain their access records without replay; pending-frame exits precede the
command read. The instruction-local bus also selects native MMU transfers.
A bounded 16-entry process cache keys bodies by logical PC/opcode. Current
CPU/bus/RAM pointers enter through an invocation packet, including after a
callback or mapping change. The manager saves an outer packet before copying
a child's arguments and captures outputs before restoring its parent.

AOT format 3 separates mapped and flat cache keys and requires managed
publication for mapped bodies. Tests reject an intact-checksum unmanaged
mapped entry and restore live arithmetic/control/input/output commands in
fresh processes. Allocation/protection/flush failures and mapped emission
budgets are checked alongside the existing cache guards.
The mapped FPU suite passes 48,434 cases while rejecting normal semantic
helpers and host MMU walks. Cases include cold/warm ATCs, 4 KB/8 KB pages,
different physical mappings, command/opcode page separation, ordinary RAM,
translation/transfer faults, pending priority, native CPU dispatch and cache
eviction during a callback before generated code returns. The full JIT,
direct/captured/checked FPU, AOT and 102,808 mapped-stream checks pass against
the frozen source plus owned increments. CPU outputs and the matching
ROMAW progress checkpoint were rebuilt/staged; Windows help and the
bounded Browser native startup smoke passed again.
Mapped MOVEC/PTEST/PFLUSH bodies now execute natively as described in
[the MMU contract](mmu040.md), including the expanded 205,616-case stream
suite. ROMAW-wide Toolbox paging remains separate from mapped CPU execution.

Mapped FPU memory-source arithmetic now uses the closed EA, MMU read and
conversion roots for integer byte/word/long, IEEE single/double and extended
operands. Commands and address extensions remain live. The generated body
validates the operation and format before evaluating an EA, commits automatic
addressing once, and reads every operand word, including extended padding.
Immediate operands use instruction function codes. Each multiword transfer
uses the current SR, so a callback's privilege change affects later transfers.

Exceptional arithmetic exits carry the complete source, conversion tags, EA,
next PC and completed-access certificate outside the native frame. They do not
repeat address evaluation or device reads. EA and transfer faults retain their
own metadata and partial effects; missing native resources after an EA effect
stop without replay. Successful bodies return the EA decoder's dynamic next PC,
including instruction extension and 32-bit wrap cases.

Instruction-entry FPCR and null state now survive command/operand callbacks.
A command-fetch callback that changed FPCR reproduced a native SQRT rounding
mismatch. Native arithmetic and both checked exit forms retain entry rounding
without undoing later architectural control changes. Memory-source execution
also preserves null-state changes made after EA evaluation instead of clearing
them again at arithmetic completion.

The 149,112-case mapped source group compares CPU/result/MMU/descriptor/RAM state
and ordered operand traffic against checked execution. Normal bodies reject host
semantic decoders, EA evaluation and MMU walkers. Cases include all supported
arithmetic operations and addressing fields, rounding/precision, partial reads,
reserved full-index extensions, signaling/unsupported inputs, target/range
exceptions, control-state callbacks, 4 KB/8 KB pages, changed physical mappings,
cold/warm ATCs, RAM spans, PC wrap and callback eviction of a returning body.
Fresh-process AOT checks reuse the same body with changed formats, values,
exceptions and mappings.

Grouped native/captured FPU, cold/warm AOT, exact-rational/integer references,
the independent FPU oracle, both FPSP dispatch modes and the 205,616 mapped
stream cases passed after the callback fixes. The complete JIT suite passed
for the memory-source increment before those focused FPU context fixes.
Validation used the frozen source base plus the owned increments. CPU outputs
and the matching `romaw-68040-direct-progress.exe` were rebuilt and staged;
Windows help and a bounded Browser startup with native execution passed.
The startup smoke check does not establish full application interaction.

Mapped FPCR/FPSR/FPIAR memory lists now execute directly in architectural
register order. Their live mask determines the complete EA size before
automatic addressing; each selected transfer observes current control values
and function codes. Input masks, partial CPU/RAM effects and MMU attributes
match checked execution. Callbacks can change privilege/control state or evict
the returning body without replaying earlier transfers.

The 19,617-case control-memory group covers every mask/direction/addressing
field, reserved lists, full-index extensions, partial reads/writes, protection
and translation failures, page splits, RAM spans, CPU dispatch, PC wrap and
nested callbacks. Cold/warm AOT replays live masks, directions and values.
Grouped native FPU, AOT, publication failures and 205,616 mapped stream cases
passed. CPU outputs and the matching Windows progress checkpoint were rebuilt
and staged; help and the bounded Browser native startup smoke passed again.

Mapped floating-point stores now lower integer byte/word/long, IEEE single/
double and extended output directly. The source register is captured before
EA callbacks; FPIAR and status commit before the first store. Extended stores
write the exponent, zero padding and significand in the same scalar order as
checked execution. Later faults retain earlier RAM effects and EA updates.
Exceptional conversions use a private captured-destination exit outside the
native frame, retaining the source and consumed EA without repeating extension
reads, automatic addressing or device callbacks. Invalid captured requests
preserve CPU/result state and perform no bus accesses.

The 121,204-case store group compares pipeline state at every bus callback as
well as CPU/result/MMU/descriptor/RAM state and ordered transfers. It covers
all source registers and normal formats/modes, numeric conversion exceptions,
partial faults, current function codes, callback changes to the source FPR,
FPCR and null state, cache eviction, page splits, CPU dispatch, full-index
errors and PC wrap. Cold/warm AOT, grouped native FPU, independent FPU and
arithmetic references, FPSP, publication/buffer bounds and 205,616 mapped
stream cases pass. The matching Windows checkpoint and CPU outputs were
rebuilt/staged; help and the bounded Browser native startup smoke passed.

Mapped FMOVEM now selects static/dynamic masks and evaluates the complete list
EA in generated code. Two fixed native raw-register primitives consume only
CPU/bus/address/mask packets; they decode no instructions or EAs and classify
no floating values. Loads fetch all three extended words before publishing a
register. Stores capture each live register after preceding callbacks and
write exponent/padding/significand scalars in order. Both retain completed
registers/stores before faults and use the current SR for each scalar request.

The verified image now contains 36 closed roots without imports. All ten
kernel groups pass on Windows and Linux ASan/UBSan, including 73,738 raw-list
cases per host. The 104,254-case instruction suite compares every mask,
static/dynamic selector, legal/illegal mode, partial transfer, callback-visible
pipeline state, page protection, full-index faults, CPU dispatch and PC wrap
against checked execution. AOT replay changes masks, Dn selectors and transfer
direction without re-emission. Grouped native FPU, allocation/publication and
205,616 mapped stream checks pass. CPU outputs and the matching Windows
progress checkpoint were rebuilt/staged; help and the bounded Browser native
startup smoke passed again.

Mapped FScc/FDBcc/FBcc/FTRAPcc now consume live conditions, displacements and
EAs in generated code. BSUN status and enabled exceptions, reserved encodings
and taken traps return explicit exception packets outside the native frame.
Byte stores use the closed mapped bus; failed accesses retain preceding EA
and callback effects. Consumed condition words can enter checked exceptional
execution without fetching them again.

The 118,090-case predicate group compares truth tables, counters, traps,
BSUN masking, ordered transfers, partial faults, callback-visible state, page
boundaries, CPU dispatch and PC wrap against checked execution. Cold/warm AOT
changes conditions, counters, displacements and FPCCs without re-emission.
Grouped native FPU, independent references, FPSP, allocation/publication and
205,616 mapped stream checks pass. Matching CPU outputs and the Windows
progress checkpoint were rebuilt/staged; help and the bounded Browser native
startup smoke passed.

Mapped FSAVE/FRESTORE now lower normal null/idle frames directly. Privilege
and reserved-mode checks precede EA evaluation. FSAVE observes live null
state after EA callbacks; FRESTORE resets the complete FPU for a null header
or clears only the idle state fields for an idle header. Postincrement waits
until a complete valid restore. Frame data uses data space for every EA.

Pre-existing busy saves remain checked exceptional paths. A busy frame posted
during EA evaluation, a busy restore or a malformed restore header enters a
private captured continuation outside the native frame. It retains the EA,
initial header and entry rounding controls. The existing busy-frame recovery
sequence runs without another EA evaluation or standalone header fetch.

The 50,970-case frame group compares full CPU state at every bus callback,
ordered transfers, MMU/descriptor/RAM state and exception results. It covers
all encodings, privilege priority, null/idle/busy headers, transfer faults,
callbacks, nested cache eviction, page splits/protection, CPU dispatch, full
indexing, PC wrap and invalid continuation packets. Cold/warm AOT consumes
live headers and FPU state. Grouped native FPU, independent references, FPSP,
allocation/publication and 205,616 mapped stream checks pass. Matching CPU
outputs and the Windows progress checkpoint were rebuilt/staged; help and
the bounded Browser native startup smoke passed.

The closed native image now includes flat scalar read/write and bounded span
roots, bringing the total to 39 roots without imports. They use live RAM,
address-mask, alias-table and device-hook bindings. Ordinary RAM and alias
bytes execute inside the native image; device reads retain the checked hook.
Scalar stores validate every byte before writing, and separate span checks
support the remaining multiword FPU integration.

The existing mapping implementation now lives in flat-memory.c so checked
and native tests share the actual runtime model. Its five moved function
bodies are byte-identical to the preceding implementation. The eleven kernel
groups pass on Windows and Linux ASan/UBSan; the new group checks 34,240 RAM,
alias, wrap, span, device, mapping-change, reentry and protected-page cases
per host. Grouped native FPU, cold/warm AOT, allocation/publication and 205,616
mapped streams pass. CPU outputs and the matching Windows progress checkpoint
were rebuilt/staged; help and bounded Browser native startup passed.

Native packet and typed memory buses can now use an explicit internal flat
binding. The closed transfer path bypasses the MMU and uses the same live
flat primitives, including from the native EA root. Bounds failures retain
the first bad unmasked address and publish no scalar bytes. A separate span
root preflights flat multiword stores and remains a no-op for mapped buses,
whose actual transfers retain their existing partial-fault behavior.

The image now has 40 closed roots without imports. All eleven kernel groups
pass on Windows and Linux ASan/UBSan, including 178,264 flat raw/packet/typed
bus, EA, wrap, callback, alias, protected-page and mapped-span cases per host.
An invalid MMU root remains untouched during flat execution. Native FPU,
cold/warm AOT, allocation/publication and 205,616 mapped stream checks pass.
Matching CPU outputs and the Windows progress checkpoint were rebuilt/staged;
help and bounded Browser native startup passed.

Flat FPU memory instructions now enter the same persisted native bodies as
mapped execution through an internal flat binding. Generated code consumes
live command words, EAs and operands; the C entry manages the packet, cache
and exceptional exit. Null/idle frames, predicates, control transfers and
register lists use the closed bus. Double/extended stores preflight their
whole flat span, and list stores preflight each register. Device callbacks
and checked numeric/fault continuations retain consumed operands and EAs.
Entry guard proof allows a callback to post a busy frame without restarting
the instruction or repeating its reads.

Successful native writes notify code-cache invalidation only when a touched
masked page contains translated code. Failed scalar stores leave code live;
successful self-writes can retire the active managed block safely. The flat
instruction group compares 23,348 decoded/compiled cases with checked CPU,
memory, ordered callback and exception results, including aliases, 24-bit
wrap, boundary faults, reentry and live mapping changes. Cold/warm AOT covers
both direct entry and compiled outer blocks with live formats, masks,
conditions and frame headers. The coherence fixture checks real cached code
for data/control/list/frame stores, rejected stores and active self-writes.

The eleven kernel groups pass on Windows and Linux ASan/UBSan, including
178,274 flat cases per host and notification ordering/reentry checks. The
remaining format-7 continuation cases still need integration. This does
not establish ROMAW-wide Toolbox paging.

Grouped native FPU, independent arithmetic/FPU references, FPSP, cold/warm
AOT, allocation/publication, code coherence and 205,616 mapped stream cases
pass for this increment. CPU outputs and the matching Windows progress
checkpoint were rebuilt/staged. Windows help passed; the bounded Browser
startup reached WaitNextEvent with 31,902 native entries and no live native
checkpoint. This is a startup check, not a full interactive application test.

Mapped FPU reads with no committed result can now enter the ordinary format-7
read-restart path. Its state certificate tolerates only the instruction's
null-to-non-null entry transition; other FPU results and attempted stores
still block it. Speculative EA register changes are restored while completed
reads and descriptor updates remain visible. The expanded 207,368-case stream
group adds 1,728 actual guest page-repair/RTE/retry cases across normal source
formats, scalar fault positions, automatic addressing, null states, page sizes,
roots and physical/translation faults, in both dispatch and bus modes. FPU
committed-result and writeback continuation still need integration.

The grouped MMU suite, updated stream/guard cases and cold/warm AOT pass.
CPU outputs and the matching ROMAW progress checkpoint were rebuilt and
staged; Windows help and bounded Browser native startup passed (31,907 entries
at WaitNextEvent, no live native checkpoint).

Mapped RTE now dispatches guest format-7 CP/CU pending FPU exceptions
immediately after restoring status and stack state. CU reports vector 11 with
a format-2 EA; CP reports its saved FPU vector with a format-3 EA. Trace bits
remain in restored status for the next handler. Invalid CP vectors produce a
format error with the original frame intact. The 244,252-case stream suite
adds 36,864 continuation/delivery cases across page sizes, saved status/trace,
stack banks and FPU vectors, plus 20 malformed-vector guards. Producing these
continuations with faulted FPU stores/writebacks is still unfinished.

Control-register/EA/coherence, FPSP, expanded stream and cold/warm AOT groups
pass for this increment. CPU outputs and the matching ROMAW progress
checkpoint were rebuilt/staged. Windows help and bounded Browser native
startup passed (31,903 entries at WaitNextEvent, no live native checkpoint).

Format-7 RTE now reads only its 14-byte header, with a word-sized SSW, and
advances SP by 60 bytes. It does not read the writeback tail owned by the
handler. The 245,732-case stream suite adds 1,280 unmapped/rejecting-tail cases
across continuations, status/trace and stack banks, and 200 necessary-header
fault cases preserving SP/status/FPU state. Focused `check-core.sh rte` checks
pass alongside 101,224 mapped flow cases and legacy flat RTE checks; compiled
cache identity matches the frozen source. RTE validation-fault delivery and
the remaining FPU writeback integration still need work.

The original bounded tail-fault reproduction now returns normally. CPU
outputs and the matching ROMAW progress checkpoint were rebuilt/staged;
Windows help and bounded Browser native startup passed (31,907 entries at
WaitNextEvent, no live native checkpoint).

RTE header read faults now use ordinary format-7 delivery when every register,
including SP, still matches instruction entry and status/banks/CCR/FPU results
are unchanged. The 250,868-case stream suite adds 5,120 actual guest page-
repair/RTE/retry cases and 16 partial-throwaway guards. The old frame remains
intact; the access-error frame is stacked below it. If RTE has already consumed
a throwaway frame, its completed stack/status progress is retained and this
certificate is not offered. That partial-progress continuation remains open.

The focused RTE group and frozen cache identity check pass. CPU outputs and
the matching ROMAW progress checkpoint were rebuilt/staged. Windows help
and bounded Browser native startup passed (31,903 entries at WaitNextEvent,
no live native checkpoint or win_exception entry in the startup log).

The mapped RTE decoder now offers frame-read restart at the current privileged
SP, including after completed throwaway frames. Stack/status progress and
callback effects stay committed; recovery does not reread consumed frames.
The 256,012-case stream suite adds 5,120 privileged throwaway-progress guest
repair/RTE cases across both stack banks, page sizes, header fields, CCRs and
physical/translation faults, plus 24 callback-state retention cases. Sixteen
guards retain the explicit stop for a fault after a throwaway return to user
mode. The focused RTE group and frozen cache identity check pass.

CPU outputs and the matching ROMAW progress checkpoint were rebuilt and
staged. Windows help and bounded Browser native startup passed (31,904 entries
at WaitNextEvent, no live native checkpoint or win_exception startup entry).

Uncommitted TAS/CAS/CAS2 data-read faults can now enter format-7 read restart
while preserving LK/size/FC and translation/physical fault status. No attempted
store or changed architectural result may be discarded; locked instruction
fetches remain rejected. The 280,608-case stream suite adds 24,576 actual
guest page-repair/RTE cases and 24 callback-state guards. Watched reads retain
the first CAS2 access through the handler and count the architectural retry;
watched stores complete once. The focused RTE group, 80,672 mapped operand
cases and frozen cache identity check pass. Locked writes remain open.

CPU outputs and the matching ROMAW progress checkpoint were rebuilt and
staged. Windows help and bounded Browser native startup passed (31,903 entries
at WaitNextEvent, no live native checkpoint or win_exception startup entry).

Normal FPU move-outs now offer checked metadata when their last aligned scalar
store faults with no deferred FPU exception. Component stepping retains its
fault PC. The mapped runner retires completed FPU/EA state without changing
CCRs, advances the stacked PC, and uses WB1/WB2 for the remaining store. Earlier
double/extended words are retained. RTE after guest writeback cleanup resumes
the following instruction or reports CT trace.

The 403,520-case stream suite adds 122,880 actual guest writeback/RTE cases
across formats, signs, rounding, addressing, lanes, trace/CCR states, roots and
page sizes, plus 32 non-final/unaligned/deferred guards. FPU state, memory and
address updates match successful execution and pending stores complete once.
Grouped native FPU, cold/warm AOT, independent references, FPSP and allocation/
publication checks pass. Deferred FPU exceptions and non-final/unaligned
writebacks still need integration.

CPU outputs and the matching ROMAW progress checkpoint were rebuilt and
staged. Windows help and bounded Browser native startup passed (31,903 entries
at WaitNextEvent, no live native checkpoint or win_exception startup entry).

Final aligned FPU move-out faults now also preserve deferred numeric exceptions.
Converted operands are captured before stores, and fault cleanup retains the
pending busy frame without repeating operand or EA callbacks. The mapped runner
stacks CP with WB1/WB2 and the original calculated EA. CP takes priority over CT;
after guest writeback cleanup, RTE delivers the pending numeric exception with a
format-3 EA. Non-final and unaligned faults retain pending FPU state but still
require further writeback integration.

The 624,740-case stream suite adds 221,184 deferred inexact, operand-error,
overflow and underflow cases, including actual guest writeback cleanup and
FSAVE/FRESTORE/RTE handlers. Busy frames and completed memory match successful
execution; pending writes complete once. Additional guards reject malformed CP
records and unsupported retirement without discarding pending FPU state.

Grouped native FPU, cold/warm AOT, independent FPU/arithmetic references,
FPSP and allocation/publication checks pass. CPU outputs and the matching
ROMAW progress checkpoint were rebuilt and staged. Windows help and bounded
Browser native startup passed (31,899 entries at WaitNextEvent, no live native
checkpoint or win_exception startup entry). These are startup checks, not full
application interaction validation.

Double and extended FPU move-outs can now retire a fault on their penultimate
aligned longword. Converted data for the final longword is retained before the
failed access. Physical faults use WB1/WB2, and translation faults use WB2/WB3;
the guest drains the failed word first and the following word second. Earlier
successful transfers are never replayed. Deferred numeric exceptions retain
their busy frame and CP priority through this path.

The stream suite adds 49,152 actual guest two-writeback cases, with separate
counters checking that each pending word completes once. Four malformed-tail
guards cover invalid validity, size, retirement state and frame format without
changing CPU state or accessing the bus. Earlier extended-word faults and
unaligned, list and frame stores still need writeback integration.

The complete stream suite passes 673,896 cases. Grouped native FPU, cold/warm
AOT, independent references, FPSP and allocation/publication checks also pass.
CPU outputs and the matching ROMAW progress checkpoint were rebuilt and
staged. Windows help and bounded Browser native startup passed (31,902 entries
at WaitNextEvent, no live native checkpoint or win_exception startup entry).
Full application interaction was not tested.

FScc now retires its completed condition and EA state when its byte store
faults, exposing the pending result through WB1/WB2. Disabled BSUN status is
retained; enabled BSUN still traps before destination evaluation or any store.
The guest completes the byte before RTE resumes the following instruction or
dispatches CT trace. The expanded stream suite passes 723,048 cases, including
49,152 FScc guest writeback/RTE cases across false/true and signaling conditions,
NaN/zero flags, null state, lanes, automatic addressing, trace/CCRs, roots/page
sizes and physical/protection faults. The pre-fix fixture reproduced the missing
retirement metadata on the first faulted FScc store.

Grouped native FPU, cold/warm AOT, independent references, FPSP and allocation/
publication checks pass. CPU outputs and the matching ROMAW progress
checkpoint were rebuilt and staged. Windows help and bounded Browser native
startup passed (31,906 entries at WaitNextEvent, no live native checkpoint or
win_exception startup entry). Full application interaction was not tested.

FMOVEM now offers retirement for its final aligned longword store. Earlier
register and word stores remain committed; the guest completes the last word
through WB1/WB2 before RTE resumes the following instruction or dispatches CT.
The failing value is retained from its actual transfer, without rereading a
live FPR. Normal native register-transfer kernels are unchanged.

The expanded stream suite passes 731,260 cases, adding 8,192 FMOVEM guest
writeback/RTE cases across static/dynamic, sparse/full and reversed masks,
automatic addressing, trace/CCRs, roots/page sizes and physical/protection
faults. Twenty guards reject earlier or unaligned store faults without
exception-delivery side effects. The pre-fix fixture reproduced missing
retirement metadata on the final word of the first faulted register list.

Grouped native FPU, cold/warm AOT, independent references, FPSP and allocation/
publication checks pass. CPU outputs and the matching ROMAW progress
checkpoint were rebuilt and staged. Windows help and bounded Browser native
startup passed (31,905 entries at WaitNextEvent, no live native checkpoint or
win_exception startup entry). Full application interaction was not tested.

Control-register lists now offer retirement for their final aligned longword
store. Generated code records the selected register index before each store,
allowing the fault exit to verify that no selected registers remain. Earlier
words and EA updates stay committed, and the actual failed value is buffered
through WB1/WB2 without rereading live controls. Normal transfers remain direct.

The expanded stream suite passes 743,560 cases, adding 12,288 control-list
guest writeback/RTE cases across sparse/full masks, data values, entry null
state, automatic addressing, trace/CCRs, roots/page sizes and physical/protection
faults. Twelve guards reject earlier or unaligned faults without exception-
delivery side effects. The pre-fix fixture reproduced missing retirement
metadata on the first faulted final control word.

Grouped native FPU, cold/warm AOT, independent references, FPSP and allocation/
publication checks pass. CPU outputs and the matching ROMAW progress
checkpoint were rebuilt and staged. Windows help and bounded Browser native
startup passed (31,912 entries at WaitNextEvent, no live native checkpoint or
win_exception startup entry). Full application interaction was not tested.

FSAVE now offers retirement for its final aligned longword, including pending
exception frames. Component stepping keeps a faulted saved frame intact and
offers its pending cleanup separately. After validating the store candidate,
the mapped runner clears saved-frame size/pending-vector state while retaining
frame bytes, FPRs and controls. The guest completes WB1/WB2 before RTE resumes
the following instruction or dispatches CT trace.

The expanded stream suite passes 747,676 cases, adding 4,096 FSAVE guest
writeback/RTE cases for null/idle frames and actual 52/100-byte frames produced
by unimplemented and inexact exceptions. Twenty guards/checks cover earlier or
unaligned faults and verify that component stepping does not clear a pending
frame before retirement. The pre-fix fixture reproduced missing metadata on
the first final FSAVE store fault.

Grouped native FPU, cold/warm AOT, independent references, FPSP and allocation/
publication checks pass. CPU outputs and the matching ROMAW progress
checkpoint were rebuilt and staged. Windows help and bounded Browser native
startup passed (31,902 entries at WaitNextEvent, no live native checkpoint or
win_exception startup entry). Full application interaction was not tested.

FMOVEM now also retires faults on its penultimate aligned longword. The native
transfer packet captures the final word before the penultimate bus callback;
recovery retains callback changes to FPRs while completing the original data.
The guest drains WB1/WB2 or WB2/WB3 in order, retaining every earlier store.
Earlier and unaligned list stores still require further continuation work.

The expanded stream suite passes 755,872 cases, adding 8,192 two-word FMOVEM
guest writeback/RTE cases and four unaligned guards. Physical-fault callbacks
change source FPRs once, proving recovery uses captured data while preserving
those changes. All eleven native-kernel groups pass on Windows and ASan/UBSan
Linux builds; 73,738 raw-register cases per build additionally check pending-tail
validity and values. Both generated kernel images retain 40 roots and no imports.

Grouped native FPU, cold/warm AOT, independent references, FPSP and allocation/
publication checks pass. A dependency dry run verifies that the register-packet
header schedules kernel regeneration and recompilation of its consumer. CPU
outputs and the matching ROMAW progress checkpoint were rebuilt and staged.
Windows help and bounded Browser native startup passed (31,900 entries at
WaitNextEvent, no live native checkpoint or win_exception startup entry).
Full application interaction was not tested.

Pending FPU tails now retain a separate data function code sampled after the
failed callback, matching the following normal transfer. The failed word keeps
its original SSW/writeback function code. This fixes two-word recovery when a
callback changes privilege between words; both writebacks previously inherited
the failed word's code.

The expanded stream suite passes 755,936 cases, adding 48 double/extended/
FMOVEM cases that switch privilege and stack banks during the failed callback.
Actual guest cleanup uses DFC 5 for the failed word and DFC 1 for the tail, with
different physical mappings, then returns through RTE preserving the callback
state. Sixteen guards reject malformed tail function codes without delivery
side effects. The pre-fix fixture reproduced an incorrect tail writeback status.

Both the grouped restart suite and a separate run of the final stream-test
source pass 755,936 cases. Grouped native FPU, cold/warm AOT, independent
references, FPSP and allocation/publication checks pass. CPU outputs and the
matching ROMAW progress checkpoint were rebuilt and staged. Windows help
and bounded Browser native startup passed (31,904 entries at WaitNextEvent,
no live native checkpoint or win_exception startup entry). Full application
interaction was not tested.

Final TAS/CAS writes and CAS2's second write now offer explicit locked-store
retirement. The mapped runner validates the marker against RMW intent, commits
completed CCR/EA results, and exposes WB1/WB2 with LK retained in the SSW. The
guest completes the pending store without repeating locked reads or CAS2's
first completed write. Component stepping retains its old flags until the
runner accepts the candidate; bus locking remains the embedding caller's job.

The expanded stream suite passes 802,092 cases, adding 46,080 locked-store
guest repair/writeback/RTE cases across sizes, automatic addressing, byte lanes,
TAS results, trace/CCRs, roots/page sizes and physical/ATC failures. ATC failures
are induced after a successful locked read by changing its descriptor and
flushing the ATC. Seventy-six guards/checks cover CAS2's first write fault,
unaligned accesses, malformed markers and the component retirement boundary.
The pre-fix fixture reproduced missing final locked-write retirement metadata.

Mapped operand checks pass 80,672 cases. Grouped CPU/JIT, allocation/publication,
opcode coverage and cold/warm AOT checks pass. The broad run found an outdated
RTE fixture expecting a writeback-tail read; the separately committed correction
passes 10,561 trap/RTE cases against both the previous and current CPU libraries.
CPU outputs and the matching ROMAW progress checkpoint were rebuilt and
staged. Windows help and bounded Browser native startup passed (31,907 entries
at WaitNextEvent, no live native checkpoint or win_exception startup entry).
Full application interaction was not tested.

CAS2 first-write faults now retire with both pending operands. The second
writeback has its own captured address and data function code, so it need not
follow the failed operand in memory. Values and addresses are captured before
the locked reads; the following transfer's function code reflects callback
changes. Guest writeback cleanup preserves live callback register changes and
does not repeat either locked read. Unaligned locked stores remain unsupported.

The expanded stream suite passes 804,428 cases, including 2,304 additional
CAS2 first-write guest repair/writeback/RTE cases and 32 malformed paired-record
guards. Physical callbacks change both update registers and the second address
register, while guest cleanup still completes the originally captured stores.
The earlier first-write guard now rejects an unaligned second operand without
retirement or delivery side effects. The pre-fix fixture reproduced missing
first-write retirement against the previously staged library.

Mapped operand checks pass 80,672 cases, native complex differential checks pass
1,105,920 cases, and opcode coverage and grouped cold/warm AOT checks pass.
CPU outputs and the matching ROMAW progress checkpoint were rebuilt and
staged. Windows help and bounded Browser native startup passed; full application
interaction was not tested.

Control-register lists and FSAVE now offer retirement for faults on their
penultimate aligned longword. The following source is sampled after the failed
callback, matching normal execution: controls can change between stores, and
saved-frame bytes can change before the final word is read. Earlier stores and
callback effects remain committed; FSAVE clears pending frame state only after
the runner validates retirement. Earlier and unaligned stores remain unsupported.

The regression changes controls or saved-frame bytes in physical callbacks and
compares actual guest writeback/RTE recovery with successful execution. Sparse
lists include both FPSR and FPIAR as pending tails. Existing guards now retain
earlier or unaligned cases, and component stepping preserves pending frame state.

The final stream suite passes 818,764 cases: 11,264 additional penultimate
control-list/FSAVE cases and 3,072 added FPCR/FPSR final-store cases. The grouped
RTE/control-flow suite also passes 101,224 cases. The baseline reproduced missing
penultimate-store retirement before the implementation changed.

Grouped native FPU, mapped control/frame, flat decoded/compiled, checked-store
coherence and cold/warm AOT checks pass. CPU outputs and the matching ROMAW
progress checkpoint were rebuilt and staged. Windows help and bounded Browser
native startup passed (31,902 entries at WaitNextEvent, no live native checkpoint
or win_exception startup entry). Full application interaction was not tested.

MOVEM saved-EA restart remains restricted to indexed and PC-relative modes,
as specified in MC68040UM 8.4.6.2. The preceding extension to other load modes
was withdrawn after reviewing the complete CM description; its regression
agreement did not establish architectural correctness. Other modes and stores
remain unsupported by this restart path.

Restart-time mask and extension fetch faults now preserve the original CM/EA
record even when fetching fails before the operand loop. Independent exception
validation checks the marker and bounds instruction fetches to the maximum
MOVEM encoding; a generic read certificate cannot bypass malformed CM metadata.
Tests use indexed and PC-relative modes, including a full PC-relative extension
that straddles code pages. Component and stream records pass through actual
guest repair/RTE without losing the saved address or repeating data reads before
the architectural replay. The baseline reproduced CM/EA loss on a mask fault.

The corrected stream suite passes 821,244 cases: the preceding 818,764-case
baseline, 2,112 added full PC-relative page-boundary cases, 320 restart-fetch
fault cases and 48 independent CM-record guards. The broader nonindexed
saved-EA expectations are removed. Grouped RTE/control-flow checks pass
101,224 cases.

The corrected library passes 80,672 mapped operand cases, 1,050,112 native
MOVEM cases, 10,561 trap/RTE cases and 56,790 opcode-coverage samples. Grouped
cold/warm AOT, identity and malformed-cache checks pass. CPU outputs and the
matching ROMAW progress checkpoint were rebuilt and staged, replacing the
intermediate broader-MOVEM checkpoint. Windows help and bounded Browser native
startup passed (31,909 entries at WaitNextEvent, no live native checkpoint or
win_exception startup entry). Full application interaction was not tested.

Retired FPU writeback now supports unaligned word/longword transfers when no
bytes of the failed scalar completed. WB1 data follows the rotated bus-lane
layout in MC68040UM table 8-5; WB2/WB3 stay register aligned. Normal transfer
widths are unchanged, and integer candidates/locked stores retain their
alignment restrictions.

The expanded stream suite passes 965,628 cases, adding 144,384 unaligned FPU
guest cleanup/RTE cases. They exercise wrapped word/longword WB1 values, WB2
protection failures after earlier successful stores, pending tails, numeric
exceptions, trace and saved-frame cleanup. Old unaligned guards now check
partially completed scalar stores across pages, which remain unretired and
retain their pending FPU state. Grouped control-flow checks pass 101,224 cases.

Grouped native FPU, mapped/flat frontend, frame, coherence and cold/warm AOT
checks pass. CPU outputs and the matching ROMAW progress checkpoint were
rebuilt and staged. Windows help and bounded Browser native startup passed
(31,911 entries at WaitNextEvent, no live native checkpoint or win_exception
startup entry). Full application interaction was not tested.

An ATC fault after a completed first-page prefix can now retire a final FPU
scalar store, retaining its whole original value in WB2. The frame reports MA
and the original scalar address; guest cleanup repairs the second page and
writes the full sized operand. It can repeat the completed prefix explicitly.
Partial physical bus faults and partial locked stores remain unsupported.

The expanded stream suite passes 1,035,568 cases, including 69,888 added partial-
ATC guest cleanup/RTE cases and 52 malformed-record guards. The final focused
69,888-case run additionally checks the original FA/WB2A despite the failed chunk
being on the next page. Cases cover one/two pending words, retained values,
callback changes, numeric exceptions, trace and saved-frame cleanup. Invalid
store metadata cannot bypass validation through a read-restart certificate.

Grouped native FPU, mapped/flat frontend, frame, coherence and cold/warm AOT
checks pass. CPU outputs and the matching ROMAW progress checkpoint were
rebuilt and staged. Windows help and bounded Browser native startup passed
(31,915 entries at WaitNextEvent, no live native checkpoint or win_exception
startup entry). Full application interaction was not tested.

Early three-register control-list stores now have a checked abort/replay path
for a first-longword ATC fault. It requires unchanged architectural state apart
from FPU non-null entry, excludes auto-update addressing and trace, and retains
the failed value in WB2 with the original PC. Guest cleanup completes WB2;
RTE reexecutes the list. This is separate from final-store retirement, and
earlier FMOVEM/FSAVE stores still require additional recovery work.

The stream suite passes 1,037,752 cases, adding 2,048 guest control-list
abort/cleanup/RTE/reexecution cases and 136 callback-state, trace, addressing
and malformed-record guards. Grouped control-flow checks pass 101,224 cases;
native FPU and cold/warm AOT checks pass. The focused 2,184-case replay fixture
also passes with a persistent cache: ten cold-compiled images are reused on
the warm run with zero compilations and 1,068 cache hits.

CPU outputs and the matching ROMAW progress checkpoint were rebuilt and
staged. Windows help and bounded Browser native startup passed (31,906 entries
at WaitNextEvent, no live native checkpoint or win_exception startup entry).
Full application interaction was not tested. Changed CPU files pass the pinned
format/header checks; the repository-wide check reports four unrelated files
with formatting drift.

Unchanged-state abort/replay now also covers earlier non-auto-update FMOVEM
and FSAVE stores, including partial ATC scalars. WB2 retains the full failed
operand and the frame keeps the original PC. Guest cleanup and RTE reexecute
the instruction, explicitly repeating its successful prefix. FMOVEM keeps its
2/2/4/4 transfer order; FSAVE preserves saved-frame state until reexecution
finishes. Source mutations, trace, auto-update and partial physical failures
remain outside this certificate.

The expanded stream suite passes 1,129,492 cases, adding 91,648 guest pure-store
abort/cleanup/RTE/reexecution cases and 92 guards. Grouped control-flow checks
pass 101,224 cases. The focused 91,728-case fixture also passes cold/warm AOT:
fourteen persisted images yield 46,198 hits and zero warm compilations.

Grouped native FPU and cold/warm AOT checks pass. CPU outputs and the matching
ROMAW progress checkpoint were rebuilt and staged. Windows help and bounded
Browser native startup passed (31,906 entries at WaitNextEvent, no live native
checkpoint or win_exception startup entry). Full application interaction was
not tested. Changed CPU files pass pinned format/header checks; the global
check retains the same four unrelated formatting drifts.

Mapped FPU/MMU trace now uses the instruction-entry SR, including T0 for their
serializing forms, every FDBcc and taken FBcc even when its target equals
fallthrough. Normal trace reports format 2 with the next PC and the original
instruction address. Retired FPU stores preserve the same eligibility through
CT; unchanged early-store aborts keep the original SR with no CT and report
trace after complete reexecution. Forced/FPU exceptions retain priority and
leave trace handling to their guest handler.

The stream suite passes 1,320,204 cases, including 5,376 new normal trace and
guest-handler cases, 504 retired-store entry/live-SR samples and expanded
traced pure-store recovery. Grouped control-flow checks pass 101,224 cases.
The focused 282,588-case fixture passes JIT and cold/warm AOT; 26 persisted
images yield 140,003 warm hits with zero compilations.

Grouped native FPU and cold/warm AOT checks pass. The flat compiled fixture now
projects the component result onto its PC/trap return interface; direct component
calls still compare trace eligibility, and CPU/memory/callback checks remain intact.
CPU outputs and the matching ROMAW progress checkpoint were rebuilt and staged.
Windows help passed. The former user-profile Browser path was unavailable, so
startup used an isolated resource-only Browser fixture copied from a preserved
workspace backup. It reached WaitNextEvent after 129,487 native entries, with no
live native checkpoint or win_exception startup entry. This checks startup, not
complete application interaction. Changed CPU files pass pinned format/header
checks; the global check reports four unrelated formatting drifts.

Flat FPU dispatch now consumes the same trace result policy as the mapped
runner. The checked dispatcher samples SR before execution; compiled register
and conditional fast paths test live trace bits before consuming their command
and enter the managed native body when tracing is enabled. Untraced inline
bodies remain available. Forced/FPU exceptions retain priority, and component
stepping still returns eligibility metadata without reporting trace itself.

The focused 210-case flat frontend fixture covers both dispatchers, serializing
forms, taken/fallthrough branches, exception priority and operand callbacks that
change live trace bits. The complete cold/warm AOT suite passes after creating
the trace-cache directory during setup: the trace fixture reuses 20 persisted
images with 153 warm hits and zero compilations. Grouped mapped stream/control
checks pass 1,320,204/101,224 cases, and the native FPU suite passes.

Current ROMAW sources were rebuilt and staged with matching `ROMAW.exe`,
`syngen.exe` and `libsyn68k.a` in `windows-binaries/bin/`; an isolated matching
bundle is also retained locally. Windows help passed. Bounded native startup of
the real `.romaw/Apps/Browser`, using an isolated profile, reached WaitNextEvent
after 31,279 native entries with no live native checkpoint or `win_exception`
entry. This validates startup, not complete application interaction.

Early three-control-register store faults now support both postincrement and
predecrement abort/replay. The component supplies its 12-byte EA certificate;
the runner requires the captured first operand, the exact address-register
update and unchanged remaining CPU/FPU state. Only after validating the ATC
writeback does it restore that one address register. Callback mutations keep
their live state and deny replay. Guest cleanup writes the full failed scalar
through WB2, and RTE repeats the original instruction without a hidden
continuation or privilege bypass.

The grouped stream suite passes 1,331,004 cases and control-flow/RTE checks pass
101,224. Coverage includes A0 through A7 (user stack), four alignment lanes,
4K/8K pages, user/supervisor data spaces, partial ATC stores, trace eligibility
and mutation guards. Native FPU checks pass, including component-result parity
and helper rejection on normal paths. The full cold/warm AOT suite now includes
the guest recovery fixture: 9,570 cases per run, 24 persisted images, 7,087 warm
hits and zero warm compilations. Global formatting/header and PowerShell checks
pass. Physical subcycle writebacks, nonindexed MOVEM recovery,
MOVEM/MOVE16 store faults and constructed user-mode throwaway-RTE
faults still need completion.

The matching ROMAW/CPU bundle was rebuilt and staged locally at
`build/68040-current/bin/`, preserving the running standard application.
Windows help and isolated-profile startup of the real Browser passed: 31,205
native entries at WaitNextEvent, no live native checkpoint and no
`win_exception` log entry. This is a startup smoke check, not a complete
application workflow or guest-OS paging validation.

Early predecrement FMOVEM and FSAVE stores now use the same abort/replay
protocol. The component captures the whole operand size before traversing the
list or saved frame. The runner checks the exact address-register update,
failed-transfer bounds and unchanged CPU/FPU source state before restoring that
one register. It retains partial physical effects and one full-scalar WB2;
guest cleanup and RTE repeat the prefix. FMOVEM keeps its 2/2/4/4 transfer order,
and FSAVE preserves its saved state until reexecution completes. Callback
mutations deny replay without being undone.

Grouped stream/control checks pass 1,398,588/101,224 cases. Native FPU and
complete cold/warm AOT checks pass, including 178,570 guest recovery cases per
cache run: 29 persisted images, 210,142 warm hits and zero warm compilations.
The expanded fixtures cover static/dynamic one-, two- and eight-register lists,
52/100-byte saved states, partial ATC transfers, entry trace modes and source
mutation guards. The changed sources pass pinned formatting/header checks;
global formatting/header and PowerShell checks also pass.

Concurrent shared builds required an isolated current-source snapshot for the
final application build. All 171 CPU source inputs match the maintained tree.
The isolated archive was revalidated with the complete grouped suites, and the
matching `ROMAW.exe`, `syngen.exe` and `libsyn68k.a` bundle is staged locally at
`build/68040-current/bin/`. Windows help and real Browser startup with an
isolated profile pass: 31,214 native entries, no live checkpoint and no
`win_exception` entry. Full application interaction and guest-OS paging were
not tested.

Early numeric FMOVE.X ATC store faults now retain computed FPSR and instruction
FPIAR while offering abort/replay. The checked model and emitted native body
capture output status before any destination store callback. The runner checks
that captured status, the instruction-owned address update and unchanged
remaining source state. A callback changing FPSR/FPIAR, source registers,
control/frame state or CPU registers/status denies replay without being undone.
No new RTE continuation bit or hidden instruction state is introduced.

Only extended move-outs have early scalar stores outside the existing final/
penultimate retirement path. An enabled extended SNAN suppresses stores and
reports its numeric exception even when the destination page is protected;
its busy frame is retained. Masked SNAN, quiet NaN and finite output can replay
from unchanged source state, with accrued status remaining idempotent. Numeric
T0 remains ineligible for trace; entry T1 traces complete reexecution.

Grouped stream/control checks pass 1,426,404/101,224 cases, including 27,816 new
numeric recovery and mutation/priority cases. Native FPU checks and the full
cold/warm AOT suite pass. The combined store-replay fixture passes 192,478 cases
per cache run, with 29 cold images, 224,106 warm hits and zero warm compilations.
The numeric fixture fails against the prior CPU archive. All seven changed
code files pass pinned format/header checks; the global check reports eleven
unrelated Carbon/Finder/QuickDraw formatting drifts and no missing headers.

The packaged application uses committed ROMAW sources at `aea4dea2` plus these
CPU sources, avoiding unrelated archive UI edits in progress. All 171 CPU source
inputs match the maintained tree. It links the exact byte-for-byte archive
validated above, and matching `ROMAW.exe`, `syngen.exe` and `libsyn68k.a` bundles
are staged locally at `build/68040-numeric-stage/bin/` and
`build/68040-current/bin/`. Windows help and real Browser startup with an isolated
profile pass: 31,205 native entries, no live checkpoint and no `win_exception`.
Full application interaction and guest-OS paging were not tested.
