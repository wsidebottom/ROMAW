# Physical 68040 cache component

The caller-owned `Syn68kCache040` stores independent data and instruction banks.
`syn68k_cache040_init` initializes storage for a board; hardware reset instead
clears CACR without invalidating cache storage. The architectural reference is
[MC68040UM chapter 4](https://www.nxp.com/docs/en/reference-manual/MC68040UM.pdf).

`syn68k_cache040_access` accepts a one-to-four-byte physical chunk, its function code,
translation attributes and CACR value. Attribute bits 6:5 choose caching mode;
CACR selects enabled banks. `no_allocate` prevents miss allocation while
retaining normal hit behavior. Alternate spaces bypass cache lookup. The
component splits external scalar transfers at naturally aligned widths and
handles line crossings without assuming adjacent backing storage. Three-byte
chunks preserve the cache access count when an unaligned MMU operand has a
three-byte tail on its next page.

`syn68k_cache040_transfer` classifies exception, descriptor and locked traffic.
Exception stack/vector accesses do not allocate on misses; hits retain the
page's caching mode. Descriptor accesses are aligned longwords with
write-through/no-allocation behavior. A descriptor hit with DE clear returns
`CACHE040_DESCRIPTOR_LOCKUP`, representing the documented halt condition without
spinning inside the host. No physical cycle is issued for that result.

Locked operands force cache-inhibited serialized traffic and push/invalidate
matching D-cache lines, including hits while DE is clear. A disabled-cache
locked hit suppresses the next enabled nonlocked data-miss allocation once.
Instruction accesses and data hits do not consume that pending suppression.
`CACHE040_LOCKED_CONTINUE` keeps a nonfinal physical write chunk from asserting
LOCKE. The caller still owns atomicity across the complete RMW pair.

The physical callbacks transfer big-endian bytes. Preferred `read_cycle` and
`write_cycle` callbacks retain traffic class, operand/push kind and LOCK/LOCKE
intent. A legacy callback is used only if its corresponding cycle callback is
absent. A line access carries
`burst=16`, a beat index and the selected way; ordinary scalar requests carry
`burst=0`. Instruction-cache way values have no hardware TLN meaning. Only
the first fill beat's inhibit response prevents allocation. Pushes carry TM/FC
zero and either one dirty longword or all four memory-addressed longwords.
The cycle kind distinguishes these pushes from alternate-space FC=0 requests.
Callbacks complete or fail each whole beat. Bus-fault results copy the failing
cycle metadata by value.

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

`syn68k_cache040_maintain_traffic` also prepares a data line for a locked atomic
descriptor update. Only data-cache/line/push selection is accepted for this
class. Its push carries LOCK without LOCKE; the caller performs and completes
the following atomic table update.

Writes follow table 4-4 D3/V6/D6: a copyback allocation updates the incoming
cache line before pushing its old victim; write-through hits update cache bytes
before external write beats. Those cache updates survive a subsequent bus
failure. `completed` includes copyback operand bytes already committed before
a victim-push fault, while write-through counts only completed external beats.
The failed victim's captured data remains separate from the new cached operand.

The caller must serialize concurrent use of a cache object. Its active guard
rejects recursive execution of that same object from a physical callback.
Multimaster snooping and MOVE16 transactions are not yet bound by this component.
An optional MMU memory binding now routes normal operands, instruction fetch,
table traffic and exception entry through these policies. Physical cache-fault
metadata remains separate from the logical operand. Mapped CINV/CPUSH now
uses physical An selection in checked and native execution. Scope zero is
illegal before privilege checking; a valid supervisor operation without a board
cache binding reports unsupported execution. T0/T1 trace follows retirement.

`syn68k_cache040_maintain_interruptible` accepts live pending-interrupt and SR
inputs. It checks before maintenance and after each completed selected line.
An eligible interrupt retains completed pushes/invalidations, leaves the
instruction PC unchanged and defers trace until replay completes. A failed
push takes priority over an interrupt raised during its failed write beat.
Normal maintenance, including interrupt sampling, executes in the closed native
image; physical callbacks remain checked boundaries.

Cache-push/fill frame delivery and descriptor-lockup halt handling remain work.
Captured push data is retained separately, and the scalar SSW constructor
rejects these physical faults rather than fabricating a scalar writeback.

The `NATIVE040_CACHE_ACCESS` and `NATIVE040_CACHE_MAINTAIN` entries accept a
`Syn68kNative040Cache` packet. These roots and their reachable routines are
compiled into the verified image with no live runtime imports or writable
globals. A valid permanent, nonwrapping RAM window permits emitted byte access
inside that window. Out-of-window and alternate-space requests use checked
callbacks; host exceptions must be caught before returning through native frames.
All packet pointers are live invocation bindings, not persisted guest-code data.
Cache storage, bus, result and native cache packets grew for traffic metadata;
callers must rebuild. Other native packet layouts are unchanged.

Run `bash syn68k/tests/check-core.sh cache040` for the ordinary Windows and
Linux ASan/UBSan component matrices. `native040` adds emitted cache and RAM
groups to the shared Windows/Linux runner, with link-time guards against
original C cache helpers. See [the current validation checkpoint](68040-status.md)
for grouped CPU/AOT and ROMAW build results.
