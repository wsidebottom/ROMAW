# PPC CFM connection ownership

The PEF loader and ROMAW bridge share architecture-checked fragment loading
with [CFM-68K](../tests/CFM68K.md). This document records PPC private-copy and
closure ownership rules. Standalone loader tests use original generated fixtures;
real Carbon bundle tests additionally require ignored local Apple libraries.

## Private copies and closures

GetDiskFragment, GetSharedLibrary and GetMemFragment accept kLoadNewCopy (5).
A private connection has fresh data, its own initialization/termination and
connection ID, and is excluded from import/name lookup. Its import libraries
remain ordinary shared connections, following Apple's
[CFM options reference](https://dev.os9.ca/techpubs/mac/runtimehtml/RTArch-19.html).
Shared and find requests retain their different reference-count behavior.
Memory find requests do not prepare new images; invalid option words return
paramErr. A native-only library without PEF storage returns unimpErr for private
instantiation rather than pretending its globals are independent.

The PEF loader shares identical, unrelocated PIC code with ref-counted ownership,
while preparing each data image from the original container. Code sections with
relocations or differing contents remain separate. Closing a private connection
runs its terminator once, invalidates its ID and releases its image; surviving
instances retain shared code.

Each reference/private load now owns a distinct closure handle and snapshots its
root/import connections. Initializers receive that closure ID separately from
their connection ID. Find requests borrow connection IDs; CloseConnection rejects
borrowed IDs and the launch closure. Closing an owning handle releases one
reference from every member, terminates zero-reference members in reverse actual
initialization order and unloads their images. Other closures and application
imports keep shared members alive. Reloading creates fresh data and connection
IDs, including memory containers. Components and resource routines retain their
own internal closures until launch cleanup. Private CFM inspection still covers
only the existing record subset; this does not establish full CFM compatibility.

The three original-code private probes cover named, disk and memory APIs,
independent mutations, shared imports, initializer connection IDs, code sharing,
named invisibility, closure termination and surviving instances after close.
The separate closure probe checks repeated shared loads, two roots sharing an
unpinned dependency, per-load initialization IDs, borrowed-ID rejection, actual
closure enumeration, final-release termination order and fresh reloads.
The real bundle-copy suite requires the user's CarbonLib assets and exercises
native and original CFBundle loading, function/data exports and unload in each
PPC engine. The archive export count does not increase for this dependency work.
