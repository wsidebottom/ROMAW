# StuffIt bridge

The C++ bridge uses the `stuffit-ffi` C ABI from
[stuffit-rs](https://github.com/benletchford/stuffit-rs), pinned to commit
`4d34ff0b40533bef600a620733033ca8970ecfd1` (0.4.0, ABI 2).
Vendored files retain their MIT/Apache-2.0 notices. Only the library sources,
manifests, lock file, public header, README and licenses are included.

Local changes encode and decode archive names as MacRoman instead of UTF-8 and
reject unrepresentable names. This prevents filename corruption with classic decoders.
Empty data forks use method 0, since they have no compressed tree; independent
decoders reject a zero-byte stream advertised as method 13.
Sibling-pointer updates also refresh the affected entry header CRCs.
The extraction integration retains available complete StuffIt 5 FinderInfo,
checks decompressed fork sizes and CRC-16 values, and bounds catalog traversal,
compressed copies and aggregate decompressed output. Deflate reads are bounded
before allocating their complete result. A ROMAW-specific C ABI extension
returns all 32 FinderInfo bytes without changing the existing ABI 2 structures.

`romaw_stuffit.hpp` owns buffers and handles, creates genuine StuffIt 5 archives
with method 13, and reads the result back to compare every fork, entry name,
type/creator, Finder flags and timestamp before returning output. Callers retain
responsibility for private staging, publication and move-source deletion.
Archive input is bounded to 256 MiB including overhead. Cancellation is checked
between entries and before/after the library's synchronous compression call.
The bridge rejects paths deeper than 128 folders before entering the recursive encoder.

The engine exposes type/creator codes and Finder flags, not all 32 FinderInfo
bytes or arbitrary AppleDouble entries. Callers must preserve those separately
or refuse a lossy transfer; the bridge does not claim full FinderInfo support.
Finder converts the private staged folder to OSX layout before packaging and
includes its AppleDouble files as ordinary archive entries. Both forks and the
native type/creator/flags are also encoded in StuffIt's own fields. Extended
metadata therefore survives in the archived AppleDouble companions, even when
an external extractor does not apply it to its host filesystem.

Install Rust with the `x86_64-pc-windows-gnu` target and MinGW x64, then run:

```sh
python3 stuffit/build.py --output-dir build/stuffit-windows
```

Use `--native` for host regression builds. C++ consumers compile
`stuffit/src/archive.cpp` with `stuffit/include` and
`stuffit/third_party/stuffit-rs/stuffit-ffi/include`, linking the generated
`libstuffit_ffi.a`. Windows also needs `ws2_32`, `userenv`, `bcrypt`, `ntdll`
and `synchronization`. Current Rust Windows targets require Windows 10 or later.

Run `python3 stuffit/tests/check.py` in WSL with `unar`/`lsar` installed for the
bridge regression and independent byte-for-byte fork/metadata/header-CRC checks.
This host check does not establish Windows application runtime compatibility.
