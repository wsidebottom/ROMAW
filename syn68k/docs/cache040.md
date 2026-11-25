# Physical 68040 cache component

The caller-owned `Syn68kCache040` stores independent data and instruction banks.
`syn68k_cache040_init` initializes storage for a board; hardware reset instead
clears CACR without invalidating cache storage. The architectural reference is
[MC68040UM chapter 4](https://www.nxp.com/docs/en/reference-manual/MC68040UM.pdf).

`syn68k_cache040_access` accepts a scalar physical request, its function code,
translation attributes and CACR value. Attribute bits 6:5 choose caching mode;
CACR selects enabled banks. `no_allocate` prevents miss allocation while
retaining normal hit behavior. Alternate spaces bypass cache lookup. The
component splits external scalar transfers at naturally aligned widths and
handles line crossings without assuming adjacent backing storage.

The physical callbacks transfer big-endian bytes. A line access carries
`burst=16`, a beat index and the selected way; ordinary scalar requests carry
`burst=0`. Instruction-cache way values have no hardware TLN meaning. Only
the first fill beat's inhibit response prevents allocation. Pushes carry TM/FC
zero and either one dirty longword or all four memory-addressed longwords.
The caller's invocation distinguishes these pushes from alternate-space
FC=0 requests. Callbacks complete or fail each whole beat.

The result reports operand completion and a physical failure address. A push
failure also returns its original push address, size, failed word and complete
line bytes for the exception recipient. Earlier successful bus writes remain
visible. Successful replacement fills remain installed after victim-push
failure. No CPU exception frame is constructed here. Read results publish only
after successful operand completion; an unrequested trailing fill fault can
abort allocation while leaving the operand successful.

`syn68k_cache040_maintain` selects neither/data/instruction/both banks and a
physical line, page or complete cache. Page selection supports 4K and 8K.
Invalidation can discard dirty data; push first writes selected dirty data.
Both operations apply to disabled banks. A failure retains completed earlier
operations and returns the failed push data to its recipient.

Writes follow table 4-4 D3/V6/D6: a copyback allocation updates the incoming
cache line before pushing its old victim; write-through hits update cache bytes
before external write beats. Those cache updates survive a subsequent bus
failure. `completed` includes copyback operand bytes already committed before
a victim-push fault, while write-through counts only completed external beats.
The failed victim's captured data remains separate from the new cached operand.

The caller must serialize concurrent use of a cache object. Its active guard
rejects recursive execution of that same object from a physical callback.
Multimaster snooping, locked and
MOVE16 transactions are not yet bound by this component. Exception/table
traffic classification and mapped CPU operand/instruction integration remain
work; a successful component test does not establish working CPU CINV/CPUSH.

The `NATIVE040_CACHE_ACCESS` and `NATIVE040_CACHE_MAINTAIN` entries accept a
`Syn68kNative040Cache` packet. These roots and their reachable routines are
compiled into the verified image with no live runtime imports or writable
globals. A valid permanent, nonwrapping RAM window permits emitted byte access
inside that window. Out-of-window and alternate-space requests use checked
callbacks; host exceptions must be caught before returning through native frames.
All packet pointers are live invocation bindings, not persisted guest-code data.

Run `bash syn68k/tests/check-core.sh cache040` for the ordinary Windows and
Linux ASan/UBSan component matrices. `native040` adds emitted cache and RAM
groups to the shared Windows/Linux runner, with link-time guards against
original C cache helpers. See [the current validation checkpoint](68040-status.md)
for grouped CPU/AOT and ROMAW build results.
