# MacTCP on Windows

ROMAW provides a native `.IPP` Device Manager driver and a native `dnrp` resolver resource. The driver uses the ordinary driver table: an installed guest driver or named `DRVR` resource keeps its normal initialization and precedence. A failed guest initializer is returned to the caller.

The implementation covers address discovery and ICMP echo; TCP create, passive/active open, send, copy/no-copy receive, buffer return, close, abort, status, extended statistics, release and global information; UDP create, read, buffer return, write, release, maximum MTU, status and port-range operations; and LAP interface information.

The resolver implements OpenResolver, CloseResolver, StrToAddr, AddrToStr, EnumCache, AddrToName, HInfo and MXInfo. It supports the optional guest Hosts file through the File Manager, cached and numeric synchronous answers, and asynchronous Windows DNS queries. Hosts parsing accepts A, CNAME, NS, PTR, HINFO and MX records, quoted fields and ordinary IPv4 aliases. Invalid files and CNAME cycles produce bounded errors.

## Execution and ownership

Windows owns sockets, routing, ICMP, DNS and transport retransmission. Guest parameter blocks and buffers remain on the emulated CPU thread. Worker threads publish copied DNS results; they never call guest routines. SystemTask and event polling deliver completions.

MacTCP I/O completions use their C stack ABI. Stream notifications and resolver result/enumeration callbacks use their Pascal ABI. Both 68K routines and PowerPC routine descriptors are supported. Opaque user values retain all 32 bits.

TCP sends and receives retain submission order. Synchronous waits progress other pending operations. No-copy receive buffers remain leased until returned. KillIO cancels requests submitted before the cancellation, including accepted echo notifications, without cancelling requests newly created by a completion. Driver close releases transport ownership; application heap reset also closes resolver ownership.

TCP ULP timeout values and actions are retained and returned by status. Notify-only timeouts leave the connection available; abort timeouts cancel the connection and notify termination. Receive command timeouts do not abort the connection. UDP writes apply their checksum flag, and maximum MTU queries select the Windows route.

The implementation currently bounds ownership to 32 streams, 128 pending stream operations, 16 echo requests, 32 pending DNS queries and 32 resolver cache entries. Exhaustion returns the corresponding MacTCP error.

## Host boundaries

This is a compatibility implementation over the Windows network stack, not an emulation of Open Transport's packet engine. Windows determines supported IP options, security options, routing, packet retransmission and network availability. IP security options which cannot be represented by the backend are rejected.

Echo notifications include the payload and a normalized ICMP header with a valid checksum. Windows does not expose the original echo identifier and sequence through this backend, so those header fields are zero. Returned IP options are copied from the actual Windows reply.

TCP_INFO supplies live MSS, windows, byte counters and RTT. EStats supplies wire sequence numbers, packet counts and additional RTT measurements when Windows permits collection. Unavailable measurements and historical histograms remain zero; application byte counts are not substituted for wire sequence numbers. Collection failure does not request elevated privileges. Interface and ARP data come from IP Helper. Arbitrary incoming ICMP quotations are not exposed by Winsock through this backend.

## Reference evidence

All files under the local `references/Open Transport Files` set were analyzed with `tools/classic-disasm/disassemble.py --all-files`. The ignored output is under `build/mactcp/open-transport-disasm`. It includes 72 PPC fragments and the ASLM resource families, plus resource listings for the configuration and initialization code.

The TCP/IP configuration resources contain `.ipp` and a DNR delegate into Open Transport ASLM code; Inet68K contains the MacTCP and DNR shims. MacTCP Ping's own resource code confirms control 17, separate request and echo callbacks, the eight-byte ICMP header, and SystemTask polling. The native implementation follows the local public MacTCP headers and these calling conventions; it does not skip an Apple initializer or fabricate private Open Transport boot state.

## Validation

Run from the checkout through the designated build console:

```sh
bash system/tests/check-runtime.sh mactcp
bash system/tests/check-runtime.sh mactcp-guest /absolute/path/to/ROMAW.exe
bash system/tests/check-runtime.sh mactcp-ppc /absolute/path/to/ROMAW.exe
```

The first suite executes Windows host binaries, including actual loopback I/O and deterministic DNS record decoding. The guest suite executes original 68K applications covering the driver, callbacks, stream ordering, buffer leases, resolver selectors, custom Hosts files and guest driver precedence. The PPC suite executes an original PowerPC application in interpreter and JIT modes with C completions and Pascal notifications. These checks do not establish compatibility with every MacTCP application or every physical network.
