# Direct disk image access

ROMAW and native Finder read MFS, classic HFS, HFS+/HFSX, ISO 9660 and UDIF DMG
images directly. Mounting creates no directories, placeholder files or sidecars
under `cache/images`. Catalogs and container tables stay in RAM; file blocks
come from the original image on demand. Copies to host folders preserve both
forks and all 32 FinderInfo bytes using OSX AppleDouble sidecars.

Raw HFS, DiskCopy 4.2 and HFS partitions in Apple Partition Maps support direct
in-place writes. Classic ROMAW opens these read/write, falling back to a
locked volume if the host denies write access. `.iso`, `.cdr` and `.toast`
containers default to read-only even when they contain classic HFS. Explicit
`-mount-rw PATH` requests write access; `-mount-ro PATH` forces read-only access.
The Windows picker starts with Read only checked; clear it to request writes.
Host read-only attributes and unsupported write formats still take precedence.
Mounted image paths and access selections are saved in the user's
`ROMAW.prefs` and restored on subsequent launches. Finder/application handoffs
carry the same selections. Toolbox UnmountVol and Eject remove an image from
that list after its last partition is put away; open files prevent unloading.
Native Finder uses libhfs for folder creation, imports, rename and Trash, checks host write permissions and
obtains a host write lock. DiskCopy data checksums are refreshed after writes;
partition headers and tags are retained. Moving an image item to a host folder
copies it, retaining the original.

MFS, ISO 9660 and compressed DMG remain read-only. HFS+/HFSX also currently have
a read-only decoder; in-place HFS+ updates are not implemented. Classic File
Manager accesses these filesystems through an HFS catalog in RAM. Small
projections map fork extents back to the original container. Large projections
contain catalog blocks only: reads resolve catalog IDs and fork offsets directly
to the source, so combined file sizes can exceed the classic 2 GiB disk address
space. Individual forks retain the classic signed 32-bit length limit. Long,
Unicode and colliding classic names receive deterministic sibling aliases of at
most 31 MacRoman bytes; original names remain in the direct catalog.
Native Finder can browse larger catalogs without that facade. Mounted native
image items currently support transfers and Get Info; direct application
launch from the native image catalog is not implemented.

The bounded classic reader follows catalog leaf chains and overflow extents,
rejecting cycles, invalid extents and missing overflow records. HFS floppies
that omit unused trailing allocation blocks are accepted only when every
referenced extent fits the available source. MFS reads its packed 12-bit chains
on demand and validates logical/physical lengths, termination and directory bounds.
B-tree buffers
are limited to 128 MiB. Files copy in bounded chunks; icon-fork reads have a
128 MiB limit. Container archive tables remain alive for deferred DMG/APM
partition reads. The legacy snapshot APIs remain available for compatibility
and decoder tests, but runtime mounting uses the direct API.

The reader supports zlib, bzip2, ADC, LZFSE and XZ DMG blocks, as supplied by
7-Zip. NDIF v10â€“12 images, including self-mounting `.smi` applications, support
raw, zero-filled and ADC chunks. Their `bcem` resource is read from OSX,
Netatalk or BasiliskII sidecars (or a native macOS resource fork). Chunks are
decoded on demand, with one decoded chunk retained in RAM; mounting creates
no expanded image or `cache/images` tree. Compressed NDIF remains read-only.
A single raw region at offset zero without a checksum retains direct-write
support, subject to the host lock. KenCode compression, segmented/encrypted
images and the older NDIF v2 layout remain unsupported and report an error.
HFS+ filesystem compression uses the same decoder. A selection is limited to 32
volumes, one million HFS+ catalog records (100000 for classic HFS/ISO), 256
directory levels and 16 GiB of combined fork sizes. Large classic projections
have a 256 MiB catalog metadata limit. Windows case/name collisions cause an error instead of
overwriting files. Macintosh filenames use ROMAW's percent-escaped MacRoman
host spelling; unrepresentable Unicode is shown as escaped code points. Long
encoded names exceeding host limits are rejected.

Build independently with CMake, for example:

    cmake -S disk-images -B disk-images/obj-linux
    cmake --build disk-images/obj-linux -j10

Windows cross build: additionally specify CMAKE_SYSTEM_NAME=Windows and the
MinGW C/C++ compilers. ROMAW's Automake target builds and links this archive
in the selected profile's `obj/images`; see
[build profiles](../README.windows-build#build-profiles-and-output). `image-probe IMAGE CACHE` prepares a snapshot for tests.

## Third-party source and licensing

The selected 7-Zip reader/decoder sources come from
https://github.com/ip7z/7zip at b88c6bc09339de32f7d18bbab900686b244f42ba.
Their LGPL-2.1-or-later/public-domain/BSD licenses remain in the vendored source
and DOC/License.txt. No RAR/unRAR decoder is included. The original reader adapter retains its MIT license; the libhfs integration
and sparse HFS facade are GPL-2.0-or-later. The full reader source and CMake build are
provided so the LGPL component can be modified and the executable relinked.
Windows staging includes licenses/7zip.txt.

Local SDK changes: HfsHandler retains FinderInfo and exposes its 32 bytes as a
private 0x10000 hex property; Hfs/Dmg/Apm/Iso handlers have archive-link anchors;
HandlerCont omits optional ext filesystem recognition (not part of this build).
All other vendored code is copied unchanged, with original formatting.

Direct HFS writes and the sparse classic catalog use the unchanged libhfs sources
from hfsutils 3.2.6 (Robert Leslie), GPL-2.0-or-later. See
`third_party/hfsutils/README.executor` and `COPYING` for provenance and license.
Maintained file-only I/O and FinderInfo helpers live in `src/`; they never open
physical devices. Windows staging includes `licenses/hfsutils.txt`.

`image-write-test WORK_DIRECTORY` creates disposable images and exercises
imports, both forks, full FinderInfo, rename, Trash, host export, host locks,
APM preservation, DiskCopy checksums and sparse HFS block reads. Use a fresh
work directory for each run.

`ctest --test-dir BUILD_DIRECTORY -R ndif --output-on-failure` checks raw,
sparse and ADC chunks, overlapping references, truncated input, malformed maps
and write eligibility. `tests/ndif_test.cpp` can also be compiled with
AddressSanitizer/UndefinedBehaviorSanitizer using the 7-Zip include directory
and `CPP/Common/MyWindows.cpp`.

For a local OSX-sidecar SMI, run `python tests/decode_ndif_fixture.py SOURCE
REFERENCE.hfv`, then `image-ndif-test SOURCE REFERENCE.hfv`. This independently
decodes the fixture and compares every catalog entry, FinderInfo record, data
fork and resource fork, then checks the classic File Manager's RAM device.
The optional final argument is the expected raw write eligibility (0 or 1).
Fixtures and reference images are local test artifacts, not distributed assets.

Incomplete HFS+ images are detected by comparing declared filesystem size with
the available stream. Windows asks for a complete replacement and offers explicit
read-only access or an exact backup to a newly chosen file. A backup preserves
the source bytes and its existing damage; it does not expand or repair the image.
No zero-fill repair is offered for missing content. Reads into missing areas
return I/O errors. Merely mounting creates no backup or expanded file.

`ctest --test-dir BUILD_DIRECTORY -R projection --output-on-failure` checks an
original 12000-file catalog with 20 GiB of logical forks, deterministic aliases,
B-tree map continuations, deferred reads and malformed/truncated MFS chains.
`image-direct-test IMAGE...` validates every projected catalog entry with libhfs
and compares selected data/resource forks with source reads, without creating
files. `--allow-unavailable` additionally checks controlled failures for missing
source bytes; use it only for a deliberately incomplete local fixture.
`system/tests/image_warning_test.c` intercepts dialogs and uses disposable
files to verify exact backups, no overwrite and cancel/read-only choices.

Finder also reads existing Desktop DB/DF icons, application registrations and
comments directly through the mounted File Manager volume. The reader retains
one B-tree metadata node at a time and reads icon bytes by offset. Classic
Desktop resource forks remain an icon fallback. No database is rebuilt on a
mounted image by these reads.

For the original Desktop metadata fixture, compile and run
`system/tests/image_desktop_fixture.cpp` with `files/desktop_database.cpp` and
`libromaw-images.a`, passing a new build directory. Then run
`system/tests/check-files.ps1 image-desktop -Image PATH/DesktopMetadata.hfv` on
Windows. It verifies a nested application registration, a document icon, a
comment, Toolbox Desktop Manager access and an unchanged source hash.
`system/tests/native-finder/image_copy_test.cpp` exercises direct image-to-host
copies and verifies both fork sizes, Finder type and progress counts.
