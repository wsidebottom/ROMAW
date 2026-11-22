# PowerPC 750 architectural core: active work

The target is a complete functional 32-bit PowerPC 750 CPU/MMU core integrated
with ROMAW. It is not a new device-level Macintosh system emulator. Neither
native compiler coverage nor application startup establishes architectural
completion. Do not enable a partial translation path and call this finished.

The primary references are the [MPC750 user manual](https://www.nxp.com/docs/en/reference-manual/MPC750UM.pdf)
and [PowerPC Programming Environments, revision 1](https://www.nxp.com/docs/en/user-guide/MPCFPE_AD_R1.pdf).
Processor-specific behavior follows the 750, rather than the existing 603/G5
hardware captures when those machines differ.

## Current boundary

The existing application adapter has accurate, tested user-level integer and
software FP paths, optional inline native blocks and compiled semantic tails.
Its legacy MSR read is a constant, SDR1 is a placeholder, and the default bus
returns flat host spans. These paths are not an architectural supervisor core.

The new `mmu750` component is a separately tested translation engine. It handles
real mode, four instruction/data BAT pairs, segment keys and no-execute,
primary/secondary hashed PTEGs, physical R/C transactions, independent 128-entry
two-way I/D TLBs and the 750's four-entry `tlbie` invalidation. It performs no
operand transfer or exception entry itself; the runtime now connects it to
instruction fetch and scalar operands. A board can assert TLBISYNC during a
DMA tenure. TLBSYNC retires once and yields at the following PC until release.
An asserted input by itself permits preceding instructions to run. ROMAW's
single-master board leaves the pin deasserted.
Its Windows and Linux ASan/UBSan fixture currently passes 3,117 cases.

The supervisor-state component passes 2,242 reset/exception/checkstop/
RFI cases on Windows and Linux ASan/UBSan. The bus component passes 249 scalar,
byte-range and external-control cases on both hosts, including endian, page,
wrap and partial-fault behavior. Scalar reads commit their output after
success, and byte ranges report the completed prefix. Scalar operands,
instruction fetch and physical PTE walks pass through the L1 banks selected by HID0.
Reset leaves both caches disabled.

The caller-owned `cache750` bank implements 32-Kbyte/eight-way L1
storage with 128 sets and 32-byte lines, all seven PLRU bits, lowest-invalid
allocation and DCFA selection. It supports critical-doubleword-first fills,
write-back allocation, write-through/conditional-store no-allocation, locked
miss bypass, dirty copyback, block controls, virtual-index invalidation and
qualified MEI snoops. R/C history can modify an exclusive cached PTE; disabled
or locked-miss history uses the physical atomic callback. Independent banks
preserve stale instruction bytes until ICBI, including after data copyback.
`check-core.sh cache750` checks 72,542 combined L1/L2 cases on Windows and Linux ASan/UBSan,
including every PLRU state, byte order at the physical boundary, storage
attributes, partial fills/copybacks and zero-cause bus faults. HID0 wires enable/lock/flash invalidation, DCFA and touch suppression.
Cache controls preserve the 750's translation/alignment priorities, including
DCBZ W/I alignment before BAT/TLB protection. Hints suppress MMU faults; an
attempted physical fill/copyback can still report TEA as machine check. ABE and
global DCBZ produce explicit zero-byte address-only bus transactions. Successful
conditional stores always write through and update a cache hit after bus success.
Physical snoops connect cache transitions and reservation monitoring. Native
service/poll/task-switch boundaries now push and invalidate enabled D-cache;
I-cache remains controlled by guest code maintenance.
The inhibited-alias behavior follows the explicit 750 rule in section 3.3.3,
which disagrees with the generic-looking transitions in table 3-7. The
[2014 manual addendum](https://www.nxp.com/docs/en/reference-manual/MPC750UMAD.pdf)
also requires ISYNC after MTSR/MTSRIN/MTMSR before segment translation is used;
this does not make MTMSR itself context-synchronizing.

The new standalone `l2cache750` bank models the 750's external SRAM interface:
two ways; 2,048 or 4,096 tag sets; two sectors for 256/512 KiB and four for
1 MiB; independent 32-byte valid/modified state and replacement of complete tags.
Both ways, every set and every sector are checked, including a later full-cache
readback that detects SRAM aliases. Full read bursts allocate with the requested
critical-doubleword order; single-beat misses bypass without allocation.
Cacheable single writes update hits; page/global write-through, inhibited and
data-only paths preserve their distinct rules. Explicit L1 pushes normally write
through and invalidate the L2 sector, while L2TS test inputs retain them in L2
and suppress single-store misses. STORE and FLUSH both push/invalidate L2 sectors;
uncached snoop reads can retain an exclusive copy. Modified sectors are pushed
before replacing a tag, with completed pushes retained across later failures.
Incoming/victim transaction scopes support re-entrant retry and unwind on host
exceptions; global invalidation clears tags/LRU without erasing SRAM bytes.

The bank is caller-owned heap storage (roughly 1.2 MiB), separate from CPU
state. ROMAW allocates one bank per architectural context and releases it
with that context; standalone users attach storage after reset. L2CR (SPR 1017)
now has supervisor access, a read-only IP bit and a disabled global-invalidation
request completing after 32,768 supplied core cycles. I stays set until software
clears it. Writable unused fields retain their values; functional accesses check
capacity and supported clock/RAM encodings. L2PE enables per-byte odd parity on SRAM writes and reads, including dirty copyback
and page-table history. A mismatch raises the L2 data parity machine-check cause.
DLL settling and SRAM pin timing remain outside this functional model.

L1 fills and dirty pushes now cross an optional whole-line lower interface,
preserving burst identity and critical-word order. Scalar and atomic PTE-history
requests use routed bus callbacks. Dirty L1 replacements can remain modified in
L2 without prematurely updating RAM. Explicit cache instructions, L2TS, global
DCBZ probes/broadcast suppression and qualified external snoops follow separate
paths. A snoop intervention does not become an L2TS test write: uncached reads
can retain the latest L1 data in an existing exclusive L2 sector without
allocating a new one. Disabled L1 tags are ignored by external snoops even when
L2 remains enabled. Active L2 requests also protect instruction fills from
re-entrant snoops while L1 data caching is disabled.

HLE boundaries push/invalidate enabled L1 data and L2 sectors before host RAM
access, including L2-only dirty data. Disabled banks retain their tags. Failed
pushes preserve pending machine checks across successful retries; host fences
do not manufacture guest cache-performance events. Synchronous L2 observations feed
request hits/misses, cache-operation hits and completed sector-push/snoop-push
counters before later callbacks can change counting state. The independent
I-cache retains guest-controlled staleness.

The CPU suite covers L2-enabled interpreter and pretranslated native loops with
zero runtime compilations, SPR permissions, invalidation progress, PTE R/C
history, two-level snoop/cache-control state matrices and accepted LWARX/STWCX
in copyback/write-through modes. A clean conditional hit lacking an L1 copyback
hit uses the external/invalidate path; the manual does not separately enumerate
an L1 miss on a copyback page, so that hardware detail remains an audit boundary.
The new `PPC L2 Cached Toolbox Probe` enables and initializes L2, exercises native
Toolbox calls, publishes/replaces hot RAM code, fences and restores configuration.
It joins the existing reporting app as its twelfth check, without another normal
application launch. The board interface drives four address or eight data parity signals according to
HID0[DBP] and samples incoming snoop addresses and complete read beats under
HID0[EBA/EBD]. Enabled mismatches latch SRR1 address/data causes for machine
check or checkstop. ROMAW RAM uses the error-free bus path. Full 750
conformance remains open.

The functional power controller suspends fetch after MTMSR selects POW with
one HID0 doze, nap or sleep selector. Nap and sleep request QACK from the
board. Doze and nap retain TB/DEC. Acknowledged sleep stops both clocks and
core-clock sampling. Snoops retry during sleep or acknowledged nap. After
QACK falls in nap, eight supplied bus clocks enable snooping again. With
L2CTL set, L2ZZ holds external SRAM asleep until wake and snoops targeting
the enabled L2 still retry. An accepted interrupt wakes the processor at the suspended PC. DPM is software transparent.
The CFM board acknowledges quiescence, advances its clocks during waits, and
samples pending host interrupts onto INT. Standalone callers can yield and
resume a suspended core. The combined guest app exercises nap and exception
return. PLL/DLL relock, pin timing and analog power consumption are unmodeled.

The optional `environment.system` and `physical_bus` activate translated CPU
execution. Real guest handlers can repair an ISI/DSI by installing BATs and use
RFI to retry. Initial MSR/SR/SPR/BAT/TLB instructions, syscall/trap, privilege,
FP-unavailable and alignment exceptions are connected. Undefined SPRs trap.
The original flat application adapter remains the default. ROMAW's experimental
`-ppc-system` option binds persistent architectural state and a physical bus to
the CFM context. CFM enters a flat application context with FP enabled, rather
than resetting into a device firmware vector. Host ticks advance TB/DEC at entry
and polls. Only registered import/return stubs may use embedding opcode 6;
ordinary opcode 6 still raises program/illegal. ROMAW authenticates each stub by the physical word address
returned by the original fetch, including endian lane selection. An instruction
alias can dispatch the registered service while the guest keeps its effective
PC and LR. QuickDraw point and rectangle services in InterfaceLib and CarbonLib
use their native geometry handlers with halfword reads and writes through the
architectural bus. Writes check each destination page before storing fields,
with full faults and page-table searches. The preflight preserves PTE R/C
bits and TLB contents until the actual store.
DebugStr reads the complete translated Pascal string before emitting diagnostic
output, and GetDateTime stores through the bus. StdCLib memcpy, memmove,
memset, memcmp, strlen and strcmp use guest byte order through the same bus.
InterfaceLib and CarbonLib BlockMove and BlockMoveData use the same translated
copy path for the original OS trap.
Copies snapshot the source before writing, including across effective aliases
of the same physical bytes. BlockMove checks cached 68K translations against
the physical destination writes. Enabled L1 banks use coherent host-service
boundaries.

The architectural JIT/AOT path uses distinct singleton native records. Every
instruction first passes the checked fetch, interrupt, privilege, breakpoint and
FP/alignment front end. Arithmetic and branches can use inline native bodies.
Integer and floating-point scalar loads and stores generate the effective
address and store operand in native code, including STFS rounding and the raw
low word for STFIWX. LWARX and STWCX. also prepare operands in native code and
use the interpreter's reservation and CR0 rules. The checked MMU/cache bus runs
after the generated code returns. Result and update registers are committed
after successful transfers.
Multiple-register and string operations use the same checked per-transfer
sequence as the interpreter. Indexed string code reads the live XER byte
count. A late fault preserves earlier transfers and leaves the unfinished
load destination unchanged. Cache-block and external-control operations also
generate addresses in native code and perform their effects through the
checked bus. SYNC and EIEIO retain their address-only broadcasts.
MFMSR, segment-register moves and side-effect-free SPR transfers use inline
bodies that resolve register storage through the current 750 instance.
TLBIE clears both ways and replacement state for the selected set in each TLB
bank in native code. TLBSYNC latches the live TLBISYNC input. The dispatcher
still handles waits and interrupt or reset delivery. MTMSR captures the live
source in native code and applies the MSR transition through the same helper
as the interpreter. Side-effecting SPR writes capture the source register in
native code and apply the checked write after the generated block returns.
MTFSB0, MTFSB1, MTFSFI and MTFSF also use inline native bodies. The common
front end checks FP availability and delivers enabled exceptions from the
updated FPSCR at the writing instruction.
FCMPU and FCMPO compare raw binary64 operands in native code, preserve the
guest's NaN exception rules and publish CR and FPSCR before 750 exception
delivery.
FCTIW and FCTIWZ split and round the binary64 significand in native code.
They publish FPSCR and optional CR1 before exception delivery, and preserve
the destination when the instruction raises an enabled invalid operation.
There is no raw linear-RAM operand path in this mode. A 750 record is keyed
by the effective address and the opcode supplied by the checked instruction
fetch, including stale I-cache lines and LE/mapped aliases. Different views
at one address warm independently and can be persisted together within the
same bounded cache. Controls and trusted host stubs retain their checked
dispatch boundaries. TW and TWI test the five trap conditions in native code.
SC records its exception request in the native block. Both return to the
architectural handler, which saves the faulting PC for a trap and the next
PC for SC. RFI assembles the masked MSR and aligned return address in native
code after encoding and privilege checks. The shared MSR transition carries
power and PMU effects and the dispatcher delivers pending exceptions.
Singleton
execution preserves exact faults/trace and avoids
speculatively fetching later instructions before earlier effects complete.
Native admission checks static memory and cache encodings and the 750 opcode
exclusions. The translation runner samples both flat and 750 opcode spaces
and checks that architectural records never use the flat linear-RAM path.
It emits 158,489 admitted 750 samples, including 8,544 checked semantic tails.
Canonical ISYNC synchronizes context after native retirement. The 750
dispatcher rejects ISYNC with reserved bits set. Indexed string loads still
validate the live XER count during execution.
Under the AOT slot cap, direct branch targets and continuations reachable
from linked entry points take priority over the section sweep. The planner
also recognizes fixed-address CTR veneers built with LIS, ORI or ADDI, and
MTCTR. Executing the branch still uses the live CTR and checked fetch.
The planner can inspect twice the slot limit to find short conditional loops farther
along a reachable path and prepare them ahead of that path's prefix. The
existing 750 batch exercises this planning with skipped code and repeated
calls in its FP and Toolbox checks.
The cold batch prepares the two linked Toolbox loops at startup. Its two
guest-written functions and their revisions are prepared when
MakeDataExecutable publishes them.
`EXPAOT04` persists separate flat/architectural records. Before allocating
executable pages, the loader re-emits each saved guest block into ordinary
memory and verifies its code and metadata. Both modes can share
the same PC without sharing a lowering. Callback allocation failures propagate
once; a semantic callback may replace its own cached record without a subsequent
metadata read. General physical 750 completion is still outstanding.

`check-core.sh system750` executes 9,154 CPU/guest-handler/endian/bus-fault cases
with both execution flag settings. The same fixture passes with the complete
Linux ASan/UBSan library through `check-core.sh sanitized system750`.
MFSPR and MFTB read the user TBL/TBU aliases (268/269); only supervisor
MTSPR to 284/285 writes the time base. `check-core.sh timebase` and its sanitized
variant also check the legacy adapter against a deterministic clock, including
four retained native semantic kernels with no runtime compilation.
Run `check-core.sh mmu750`, `state750` and `bus750` for the component fixtures.
`clock750` checks 83,999 deterministic TB/DEC/thermal/performance/input-priority cases. Time advances
through a caller-serialized tick API; MCP edges are ignored while HID0[EMCP] is clear; enabled edges latch.
The board can drive TBEN through `ROMlib_ppc_750_set_tben`. Negation freezes
time-base counting and the resulting performance events. DEC continues to
run and can wake the processor from doze or nap. ROMAW's board leaves TBEN
asserted. Zero-initialized and hard-reset state use the asserted level.
[MPC750 User's Manual, §7.2.9.7.4](https://manualzz.com/doc/22971371/nxp-mpc745-host-processor-reference-manual)

SRESET latches on assertion, takes priority below machine check, and wakes a
suspended processor without clearing caches. External CKSTP_IN freezes clocks
and releases the quiescence and SRAM power outputs until hard reset.
SMI/INT and optional raw performance/thermal requests use explicit levels. DEC sign transitions latch until accepted,
including software writes. CPU execution delivers eligible requests before fetch,
implements DEC/TB access and enters trace after completed instructions, preserving
the 750 ISYNC exclusion. The embedding must supply ticks and interrupt levels;
Performance and TAU condition generation are connected. Detailed pipeline
event production remains open.
HID1 reflects the embedding's four-bit PLL_CFG input in bits 0..3, with reserved
bits zero. Supervisor MTSPR to HID1/PVR is a no-op (MPC750UM 2.3.2.4.3), while
user accesses remain privileged. The previous PVR-write illegal trap is fixed.

ICTC (SPR 1019) exposes the supervisor-only FI and E fields. The functional
fetcher spaces successive aligned groups of up to four consecutive instruction
words by the programmed core-clock interval. Redirects start a new group and
ICTC writes restart the schedule. Standalone execution advances missing core
cycles; ROMAW samples its board clock, including TB/DEC, during the wait.
Interrupts raised during a wait enter before the next fetch. This models
forwarding bandwidth, without simulating dispatch queues or junction cooling.

THRM1/2/3 now implement threshold/direction/enables, read-only TIN/TIV, writes
that rearm the appropriate channels, and single/alternating dual comparisons.
Interrupting channels hold their result until reprogramming, while the other
channel continues; EE masks delivery and accepted requests are acknowledged.
THRM3 includes AN1800's calibration sign and 20/8/4/2-degree controls. Temperature
input is a caller-supplied ideal integer Celsius signal; analog accuracy/noise,
settling error and physical heat generation are not simulated. A zero SITV uses
one core clock as a deterministic digital choice below the recommended minimum.
Large clock advances use bounded work and preserve phase across partitioning.
The independent core-cycle API does not advance TB/DEC. ROMAW supplies a
functional 250-MHz core clock alongside its 25-MHz TB source and a nominal 25-C
junction input. This clock interface does not establish cycle-accurate issue or
PLL lock behavior. See [AN1800](https://www.nxp.com/docs/en/application-note/AN1800.pdf)
and MPC750UM 10.3 for thermal operation and calibration.
`PPC 750 Thermal Probe` checks read-only SPR writes, installs a guest thermal
handler, samples both thresholds, takes the interrupt and returns through RFI,
then rearms/clears status and restores controls and vectors. It is included in
the existing reporting app, rather than adding a separate normal test launch.

MMCR0/MMCR1, PMC1-PMC4 and SIA now have supervisor access and read-only user
aliases. Reserved MMCR1/select encodings hold zero/current values, respectively.
The counters implement privilege/marked filters, 32-bit wrap, sign-bit interrupt
conditions, ENINT self-clear, pending delivery at EE, DISCOUNT freezing and
PMC1-triggered secondary counting. The 750 leaves DIS and PMCTRIGGER unchanged
on overflow; PMCINTCONTROL can exempt PMC2-PMC4 from the freeze. SIA records the
input instruction address at signaling, or the last completed address for clock
inputs. Writing an enabled negative counter signals immediately, a deterministic
choice permitted by the manual. MMCR0 writes rearm the internal freeze state.
TB transitions use the RTCSELECT control-field definitions (numeric bits 0/8/12/16);
the manual's event tables list these in inconsistent/reversed order. Bulk clock
inputs preserve sign/trigger/freeze chronology and agree with split advances,
including UINT64_MAX.

The strict frontend supplies functional fetch/dispatch/completion, execution-unit,
reservation, barrier, branch direction, IABR match and MSR transition events for
interpreter and native execution. Successful SC completes; faulting or privileged
instructions do not. Branch direction uses the input CR/CTR even when a taken
branch targets fallthrough. Eager native/interpreter full-state comparisons also
include active performance counters. The current frontend does not speculate or
fold branches, so these counts do not reproduce a physical pipeline's speculative
or folded-branch statistics. The bus now supplies separate instruction/data L1
misses, dirty replacement castouts, hardware I/D table-search misses, qualified
L1 snoops and retry events. Cache results retain actual tag attempts and completed
dirty replacement pushes across partial fills and errors. Optional synchronous
cache observers publish tag misses before fill callbacks and castouts after the
dirty push, so a callback-triggered clock interrupt cannot freeze a miss that
already occurred out of the count. A locked cache still
checks tags; disabled and inhibited accesses bypass them and produce no tag miss.
Zero allocations and cache hints use the same cache-result reporting; explicit
flush/store/snoop/host-fence pushes are not replacement castouts. PTE reads use
the data bank and therefore contribute D-cache events even for an instruction
translation. TLB hits, BATs, real mode and suppressed hint walks do not produce
a hardware-search miss; a C-bit reload does, following MPC750UM 1.8. Failed or
missing-PTE searches count once, not once per PTE read.

The cache-control integration audit also corrected reversed DCBST/DCBF decoding:
secondary opcode 54 retains the clean line after copyback, while 86 invalidates
it. Their optional ABE broadcast identifiers now agree. Literal guest encodings
check invalid/exclusive/modified lines, disabled and locked banks, successful
copyback and bus faults. These checks caught a mapping error that raw cache-bank
operation tests could not establish.

Core-cycle advances supplied inside physical callbacks also produce table-search
time and successful load-miss service time. A successful single-miss scalar read
can feed the latency threshold with its measured service time; a two-line miss
has only aggregate time and does not fabricate either line's threshold latency.
Table traffic and instruction fetches do not become load-latency samples. ROMAW's
current physical callbacks supply no cycle timing, so they generate functional
miss events without simulated memory latency. Parity and detailed pipeline/stall/prediction timing remain outstanding; the explicit logical-event input API
supports those inputs. This is functional monitoring, not cycle-accurate PMU
emulation; external event inputs must be serialized with CPU/clock accesses.

`PPC 750 Performance Probe` checks precise completed/integer/load-store/branch
counts, user aliases and a cold/warm data-cache sequence, restores the controls
and joins the existing batch.
The reporting app now runs twelve checks in each of the three engine modes,
plus one injected failure check; no additional normal application launch is
needed for performance monitoring.

The original `PPC 750 System Probe` initializer installs a DSI handler, enables
DR, repairs a missing mapping with a BAT, retries through RFI and restores vectors,
BATs and MSR before return. It also rewrites a hashed page-table entry, observes
the old mapping before TLBIE and verifies the new mapping after TLBIE and
TLBSYNC. The standalone PEF execution fixture checks it under
both flags. `check-runtime.ps1 system750` runs the original 750 initializers and the Toolbox
probe in staged ROMAW with interpreter/JIT/AOT settings. Both native settings
require actual native architectural execution. Static code reuse is separately
checked with zero runtime compilations; generated code can require compilation. The CPU fixture
includes 240 eager native/interpreter full-state comparisons across mappings, LE,
L1/lock settings, load/store updates, protection, page misses, bus failures and
DABR; it also checks front-end priority, FP/trace, nested record replacement and
no replay after a callback allocation failure.
Enabled FP conditions now enter program/FP for every nonzero FE mode, preserving
the software kernels' inhibited or scaled results, FPSCR and CR. MTMSR/RFI that
enable an existing condition save the next PC with SRR1[15], following MPCFPE
6.4.7. `PPC 750 FP Probe` runs a guest program handler that enables FP, clears
FPSCR and returns to retry the divide, then restores the shared vectors and
caller state. Instruction-form/exception-priority coverage remains part of the
full conformance audit.
Alignment entry supplies the MPCFPE table 6-12 operation/register fields in DSISR.
The CPU fixture covers immediate/indexed FP, multiple/string, reservation and
disabled-cache DCBZ faults in both byte orders, verifying DAR and no operand
transfer or register update. Immediate strings use only rA for their fault address;
the byte-count field is not treated as a register index.
With L1 disabled, DCBF/DCBST still check load translation/protection and DCBI
checks supervisor privilege and store protection. These operations can walk PTEs
and report physical table failures without transferring an operand. ICBI remains
virtual-index-only, disabled-cache hints perform no walk, and direct-store cache
controls are no-ops. Enabled-cache operations use the same checked translations and update the
persistent L1 banks, including dirty copyback and zero allocation under DLOCK.
EAR has supervisor-only access, an enable bit and the 750's four-bit RID.
ECIWX/ECOWX check EAR before alignment/translation, enforce data protection and
send a distinct resource transaction with WIMG=0101. Device addresses/words are
operands rather than ordinary-memory byte lanes. Tests cover all RIDs, both
privilege/byte-order modes, protection, disabled/misaligned forms, PTE history,
TLB attribute isolation and failed device transfers. The ROMAW physical bus
has no resource devices attached and rejects these transactions with TEA;
standalone callers can supply devices through the physical callback.
IABR implements privileged read/write, BE/TE and effective-word matching, with
ISI fetch faults preceding breakpoints and breakpoints preceding instruction
dispatch. Writes retain the previous comparison context until ISYNC, RFI or
exception entry; SYNC alone does not qualify. Tests cover the immediately following
synchronizing instruction, IRQ/SC/RFI activation and replacement CPU instances.
`PPC 750 Break Probe` installs a guest breakpoint handler in the Toolbox-vector
overlay, disables/synchronizes IABR and retries through RFI, then restores vectors
and the prior breakpoint setting. The C bridge asserts native supervisor-state
alignment so Macintosh record packing cannot silently change the C/C++ boundary.

DABR implements supervisor-only SPR access and effective-doubleword matching for
read/write enables and the selected DR context. Matches enter DSI with DAR and
DSISR[9]/store status before operand transfer. Translation/protection and physical
page-table failures take priority, including a protected second page. Normal PTE
reference/change updates may occur on a stopped access. Instruction fetches and
cache controls do not trigger this hook; undefined external-control, zero-length
string and failed conditional-store matches are chosen absent. Tests cover byte
ranges, wrapped addresses, LE lanes, physical aliases, scalar/FP/reversed/multiple/
string/reservation paths, inhibited updates and replacement CPU instances.
`PPC 750 Watch Probe` clears DABR in a guest DSI handler and retries its load with
RFI, then restores DABR and the shared vectors. The staged `system750` check
runs one reporting app under JIT by default. The `-Full` option runs the app
under all three engines and verifies named failure reporting with an injected
failure. `system750-isolated` retains individual launches for diagnosis.

Existing kernel guards for reserved memory Rc bits, invalid update registers,
multiple/string load overlaps and reservation forms now enter program/illegal in
architectural mode. Flat application mode retains its diagnostic text. These are
chosen exceptions for invalid forms, not a claim that every undefined encoding
requires a specific exception. `PPC 750 Program Probe` runs a guest handler that
replaces an invalid update-form load, retries through RFI and restores the shared
vectors and original instruction. Full invalid-form/priority auditing remains.
The same check installs program and syscall handlers, runs taken and untaken
word traps and SC, and verifies that RFI resumes at the expected instruction.

`PPC 750 Cache Probe` enables both L1 banks, verifies dirty-data discard and
copyback, then calls and modifies original guest code. It observes the old
instruction after data copyback and the new instruction after ICBI/ISYNC, restores
the generated code, flushes modified lines and restores HID0 before host return.
The CPU fixture also alternates two BAT instruction mappings at one effective PC,
checking lazy compilation and reuse of both prepared views. The AOT fixture
reloads both views in a fresh process and checks cache replacement, publication
failure and return to a previously cached opcode after I-cache invalidation.
Other CPU checks cover cached hardware PTE/R/C accesses, write-through STWCX,
LE lane mapping, failed broadcasts and partial cached transfers. Large CPU
fixtures keep the now-expanded persistent state on the heap. Address-only
transactions are synchronous in ROMAW, which has no external cache masters.
Performance/power/parity controls beyond the cache bits and EMCP still need their
implementation audits; storing the remaining HID0 fields is not complete emulation.

The physical reservation flag/address now persist with the supervisor state.
LWARX establishes the reservation before the atomic operand transaction, so an
intervening write can cancel it before the load completes. STWCX consumes the
flag, remains nonspecific with respect to its own target and always writes
through. Atomic bus flags distinguish these transactions from replacement
copybacks. Native service/poll/task switches clear reservations; explicit CPU checkpoints
preserve them.
`check-core.sh snoop750` checks 4,674 transfer-type/global/cache-state/reservation
cases. Global snoops affect enabled data banks; non-global writes still monitor
reservations. Write-with-kill discards dirty data. Write-with-flush, kill-block and
cacheable read/RWITM snoops push it; uncached reads retain an exclusive copy. I-cache is never snooped.
The five-bit transfer-type distinctions follow tables 3-5/3-6, including TT=18
canceling any reservation regardless of address. Bus failures latch an unmaskable
machine check for the next boundary; synchronous/asynchronous causes are merged
and acknowledged once. Tests include failed pushes with zero cause, disabled/
locked banks, LE/mapped aliases, CPU replacement and a write during the LWARX
read. `PPC 750 Reserve Probe` exercises successful and consumed reservations,
including a conditional store to a different physical granule, in ROMAW.

`check-core.sh collision750` checks 30 in-flight transaction scenarios and
91,776 re-entrant snoop/address combinations on Windows and Linux ASan/UBSan.
Pending fills, dirty victims, explicit pushes, bypass packets and physical PTE
history updates reserve their physical granules until the callback unwinds.
A colliding global snoop returns `ROMLIB_PPC_MMU_RETRY`, without a machine check;
the bus controller retries after the original callback returns. Noncolliding
snoops may push another line recursively. Failures and C++ exceptions unwind the
private transaction stack. CPU operations, reset and snapshots still require
serialization; the bank's private native pointer must be null at an idle snapshot.
This implements functional address-phase arbitration, not cycle-accurate queues.

`check-core.sh host750` exercises native service/poll boundaries, nested guest
checkpoints, trusted returns, disabled/locked banks, all 1,024 data-cache lines,
re-entrant synchronization retries and each failed push beat. Boundaries publish
guest dirty data before host access and invalidate before nested guest callbacks
can observe host changes. Nested guest returns publish data even on a CPU limit
or embedding failure; a plain CPU checkpoint preserves dirty data. Copyback
failures suppress host dispatch, enter or latch machine check, and remain pending
even after successful retry. I-cache remains stale until explicit maintenance.
`PPC Cached Toolbox Probe` enables both L1 banks, dirties a rectangle before
OffsetRect, checks native outputs, publishes/replaces a hot RAM function through
MakeDataExecutable and restores HID0. Publication invalidates the addressed
I-cache sets after the host data fence. Empty ranges leave the pointer unread.
[Apple OPS19](https://developer.apple.com/library/archive/qa/ops/ops19.html)

With data translation enabled, MakeDataExecutable validates each translated
page before invalidating the corresponding instruction-cache sets. The L2
Toolbox check publishes through a read-only BAT alias, verifies that the old
instructions stay visible before publication, and executes the replacement
after publication.
In AOT mode the service also reads the synchronized backing words with the
750's endian lanes and prepares bounded native records before returning.
Matching current instruction aliases can share that preparation. The
dispatcher selects by effective PC and fetched opcode, so the usual MMU and
I-cache rules govern both the original and rewritten code.
The L2 Toolbox check also calls SetRect through a distinct instruction BAT alias
with a separate data BAT alias for its output rectangle. It checks the top field
while translation is enabled and calls PtInRect with a read-only data mapping.
The same app checks CarbonLib geometry through aliases, including SectRect
with an aliased output. It changes a destination BAT from read-only to
writable in a DSI handler, then retries OffsetRect once through RFI. The same
batch checks mapped StdCLib byte ranges across a page boundary, overlapping
physical aliases, zero-length calls and the protected memmove retry.
It also checks InterfaceLib BlockMove and CarbonLib BlockMoveData with
overlapping physical aliases and nonpositive signed counts.
The app restores the original MSR, BATs and exception vector.

The ROMAW binding has passed all 120 existing application probes separately
with native execution, interpretation and AOT, plus cold/warm persistent AOT
reuse. An initial parallel AOT speech probe exceeded its own four-second limit;
the isolated retry and full serial AOT run passed. Functional cache and SPR
completion are still outstanding; these application checks do not establish them.
The 750's optional-instruction exclusions are important: FSQRT/FSQRTS and TLBIA
must take the strict model's illegal-instruction exception even though the
legacy application interpreter offers software square root. The 750 also does
not implement the optional floating-point-assist exception vector.

## Completion requirements

- Persistent architectural state: MSR, SRR0/SRR1, DAR/DSISR, SR/BAT/SDR1,
  documented SPR permissions/masks, time base, decrementer and implementation
  registers. Correct reset and supervisor/user transitions.
- Architectural instruction fetch and operand bus, including independent IR/DR,
  cross-page and wrapped accesses, byte order, alignment and storage attributes.
  Host aliases and Toolbox callbacks must not bypass translation accidentally.
- Privileged instructions and invalid-form checks, RFI, syscall/trap, FP
  unavailable/enabled exceptions and precise saved-PC/state rules.
- External, decrementer and other implemented 750 exception inputs with masks,
  priority, handler entry and restart. Controlled host diagnostics remain for
  embedding failures, rather than replacing guest architectural exceptions.
- Architectural cache management/coherency and reservation effects, including
  cache-inhibited/guarded memory, self-modifying code, dirty lines and aliases.
- JIT/AOT source validation against the current instruction view, helper ABI
  identity, mapped-memory fallback/guards, callbacks, faults and invalidation.
- ROMAW integration and executable supervisor/MMU probes covering page-table
  repair, handlers and returns, both execution settings, with existing CFM,
  Mixed Mode and Toolbox application regressions preserved.

Each stage needs explicit positive, malformed-input and failure tests. Keep
this document's boundary current; passing an isolated MMU fixture is only the
translation-engine stage. Cycle/pin timing is not established by functional
instruction tests.
