# PPC translation cache lifecycle

This document owns preparation, eviction and disk-cache behavior. For current
instruction lowering and validated x64 host ABIs, see
[translation status](translation-status.md).

## Selecting execution

ROMAW interprets PPC by default. `-ppc-jit` enables native blocks;
`-ppc-aot` also prepares linked fragments and persists translations.
`-no-ppc-jit` selects interpretation. The standalone runtime requires
`ROMLIB_PPC_EXEC_JIT` explicitly and exposes availability/execution counters.
Unsupported instructions or unavailable native publication retain checked execution.

## Preparation and replacement

Persistent translation supports Windows and Linux x64. Native blocks receive CPU and
status state as entry arguments; they contain no addresses of a previous CPU
instance. A caller-owned `ROMlib_ppc_translation_cache` can be shared by successive
executions. Reentrant execution shares code with private status scratch, and every
block checks its current PPC source words before entry. On either host a block can
return a native prefix at an instruction limit or before the next poll. The
dispatcher accounts for the executed prefix and rechecks the next guest
instruction after polling. The optional eager PEF pass translates code
sections after relocation and linking. It prepares
main, initialization, termination and exported entry points first. It also
prepares local entry points from the final relocated 8- and 12-byte transition
vectors before sweeping the remaining code. Flat blocks split at direct branch
targets and at fixed-address CTR targets loaded with LIS, ORI or ADDI, and MTCTR,
including targets in other code sections. In 750 mode the planner
prepares reachable instructions first, following direct targets, possible
fallthroughs and linked return sites from the known entries. It can inspect up
to twice the block limit to find short conditional loops beyond a long prefix.
Those loops get first use of the remaining slots, followed by the other
reachable instructions and the section sweep.
ROMAW prepares newly linked application and library fragments together.
Lifecycle entries precede exports and local entries across the group. The
section sweep shares the remaining cache space and reuses unused shares.
After loading a full cache, AOT protects matching entry blocks and restores
missing or stale entries before guest startup. Restoration can replace older
translations while keeping the same cache limit.
Fragments linked by CFM during a PPC host service are prepared before their
initializers run. The planner uses the caller's translation cache while guest
execution is suspended in the service. At capacity, the late section sweep can
reuse unexecuted slots outside the linked group and its dependencies. It
publishes each replacement before releasing the old slot and preserves
previously executed blocks during the sweep. The existing private-memory
fixture fills the startup cache and replays the same application and profile
to check disk reuse of the late code. ROMAW checkpoints newly prepared late
blocks while the owning guest is suspended, before the fragment initializer
runs. The fixture also exits through the Toolbox and checks that the warm
launch reuses its saved code without another late checkpoint.
In 750 AOT mode MakeDataExecutable also prepares complete instruction words
from synchronized backing memory while the calling guest is suspended. It
uses the real-mode address and any supplied effective address whose current
instruction mapping agrees with the data mapping. The live 750 fetch selects
the record on every execution.
The JIT tracks interpreted starts separately from executable blocks. Once a
start becomes hot, it can replace an unused AOT block or the least recently
used translation in the full cache. Flat and architectural blocks share the
same space limit. Eager range preparation at capacity can refresh a rewritten
start in its existing slot. Publication preserves its eviction age and keeps
the previous record if allocation or executable protection fails. The flat JIT
keeps a previous block while its replacement warms and compiles. It can reuse
that block if the original instruction words return.
Recognized runtime instructions can form singleton blocks, including terminal
branches and memory instructions. Operations without inline lowering use native
tail calls to their ahead-compiled checked kernels. Runtime-only host-service
opcodes remain explicit dispatch boundaries. This covers the current decoder;
it does not establish full physical 750, exception or MMU compatibility.

Architectural records validate fetched instruction words through the 750
I-cache view. Their current lowering and emitted memory/state kernels are
specified in [translation status](translation-status.md), rather than the older
semantic-tail checkpoints.

## Persistent artifacts

ROMAW stores bounded `ppc-aot-*-v4.cache` files in the user's `.romaw`
profile. After linking, it selects a cache by application, guest code addresses,
linked code bytes, entry points and execution mode. Different layouts retain
separate caches. The artifacts are specific to the current build and host ABI.
PEF code and executable-data sections both participate in preparation and in
the cache identity. Private fragments share eligible read-only code and keep
their own writable executable data. [Apple PEF section types](https://leopard-adc.pepas.com/documentation/mac/runtimehtml/RTArch-92.html)

Loading checks the build identity, ABI identifier, checksum, record limits
and byte limits. The loader re-emits every saved PPC block into ordinary memory
and compares the native body and execution metadata against the record.
It allocates executable pages only after all records pass. This work happens
before guest execution. Publishing uses writable pages followed by read/execute
protection, and saving replaces the file atomically using an exclusively
created, flushed temporary sibling.
Cache I/O or allocation failure leaves execution available. A failed load keeps
the previous cache intact, including partial executable-publication failures.
When a resident cache exceeds the 32 MiB file limit, the save fits whole
records in order of most recent execution or entry-point preparation.
Smaller records can use the remaining bytes after a larger record is skipped.
The snapshot retains the guest words for blocks whose native bodies do not fit.
An eager section scan can advance over those words on a warm launch if the
address, mode and source still match and the block fits within the current
scan range. A matching omission counts toward the eager scan's block limit.
Changed code, explicit entry points and normal JIT execution remain eligible
for native translation.

Use `bash synppc/tests/check-core.sh translation-all` for current emission,
cache corruption, publication rollback and fresh-process replay checks.
`synppc/tests/check-runtime.ps1 aot` checks cold preparation and disk reuse at
two linked layouts. `run-romaw-tests.ps1 -Aot` uses private profiles for
application integration. See [translation validation](translation-status.md#build-and-regression-commands).
