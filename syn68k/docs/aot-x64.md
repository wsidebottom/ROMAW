# 68K x64 JIT and persistent AOT

The Win64 runtime can persist native prefixes reached by an application and
restore them on later launches. A warm hit skips the x64 emitter and executes
restored machine code. The current decoder and ordinary Block construction
still run, validating the current instruction boundaries and registering normal
invalidation ownership. This is a profile-driven native-code cache, rather than
an offline scan of CODE resources, which can also contain data.

The JIT lowers all 56,790 opcode samples supported by the current decoder.
Complex instructions can use ahead-compiled semantic kernels or checked native
tails. This establishes coverage of the existing runtime, not complete physical
68040, MMU restart, Carbon or application compatibility. See
[68040 status](68040-status.md) and [the MMU contract](mmu040.md).

## Enabling the cache

Caching is opt-in. Set `SYN68K_AOT_CACHE` to an existing, trusted local directory
before starting ROMAW. For example, in PowerShell:

```powershell
$env:SYN68K_AOT_CACHE = "$env:LOCALAPPDATA/ARDI/syn68k-aot"
New-Item -ItemType Directory -Force $env:SYN68K_AOT_CACHE | Out-Null
& D:/Source/ROMAW/windows-binaries/bin/ROMAW.exe
```

The first run publishes entries; later runs restore matching entries. Native
execution must be enabled (ROMAW's default). `-notnative` keeps interpreter
execution. Unset the environment variable to disable persistence. Cache files
may be deleted while ROMAW is stopped; they are disposable compilation artifacts.

Files contain executable code. Use a directory writable only by trusted users;
the integrity checksum detects accidental damage, not deliberate tampering.
The runtime does not create the directory, evict old entries or authenticate
files. Missing/unwritable directories, incompatible entries and damaged files
fall back to ordinary compilation. A failed native allocation, protection change
or instruction-cache flush retains the existing interpreter fallback.

## Format and lifetime

Each bounded entry contains the starting guest PC, decoded lengths and exact
source bytes for up to 64 candidate instructions, flag-optimization mode,
translated instruction count, terminal state, canonical native bytes, symbolic
relocations and a checksum. Hashes select filenames; exact source and metadata
comparisons decide hits. Live extensions outside the decoder's recorded lengths
remain execution-time reads, including indexed addressing and long arithmetic.

The generated SHA-256 build identity covers runtime/generator sources, public
and private layouts, third-party helpers, configured headers and Makefile,
compiler version and effective build flags. The identity header changes only
when its inputs change and is an explicit prerequisite of the emitter object.
Builds without a generated identity cannot persist native code.

All host pointers are stored as zero bytes with ordered, disjoint relocation
records. Restoration resolves current addresses for the RAM window, alias
metadata, translated-page counters, checked trap dispatch, indexed-address and
paired-operand helpers, dynamic continuation storage and complex-integer kernels.
Old process pointers are never restored. Header/source/checksum/relocation
validation precedes executable allocation; sizes and offsets are bounded.

Publication writes an exclusively created temporary sibling, flushes and closes
it, then atomically replaces the destination with Windows MoveFileEx. Incomplete
files are never published. Concurrent readers may miss an entry and compile
normally. Native pages use the existing RW-to-RX publication and instruction-cache
flush. Restored allocations belong to the ordinary Block and are freed by its
usual invalidation path, including guest stores and resource reloads. Guarded
memory accesses read current window and alias metadata.

## Validation

After `make -C syn68k`, run from WSL:

```sh
bash syn68k/tests/check-core.sh aot-x64
bash syn68k/tests/check-core.sh jit-x64
```

The AOT suite checks every truncation and single-byte corruption of a fixture,
structurally invalid relocations with recomputed checksums, trailing data,
missing directories, damaged-entry replacement and allocation/protection/flush
failures. Build identity checks cover unchanged rebuilds, source/helper/ABI
changes and compiler flags.

Separate cold/warm Windows processes assert emitter bypass and execute register,
RAM, indexed, paired-operand, complex-kernel and real FPU prefixes. They check changed
source, live extensions, host aliases, bounded nonterminal prefixes, a callback
freeing its calling block, normal Block replacement and stores invalidating
translated code. Each process checks the same explicit architectural results.
Process-local hit, emitter-attempt and execution counters are available through
`syn68k_aot_hit_count()`, `syn68k_native_compile_count()` and
`syn68k_native_execution_count()`.

The fixture reports translation/restoration time separately. The optional
`jit-x64-benchmark` check measures steady-state register/RAM loops after initial
translation. Disk caching can remove warm translation work; it does not make
the same emitted instruction sequence execute faster. These fixtures do not
establish application performance or complete interactive compatibility.
