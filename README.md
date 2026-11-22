# ROMAW — Run Old Mac Apps on Windows

ROMAW (**Run Old Mac Apps on Windows**) runs classic Macintosh applications on
Windows x64. It is based on **ARDI Executor**, combining its Toolbox implementation
with the syn68k 68040 runtime and the synppc PowerPC 750 runtime.
The built-in desktop is a native Windows Finder inspired by System 7.

This is a work in progress. PowerPC, Carbon and physical CPU/MMU compatibility
remain incomplete; support depends on the application and its required services.
See [CPU status](docs/cpu-runtime-status.md) and the application investigations
in [the documentation index](docs/README.md).

AI tools were used to create some of this project's contents.

ROMAW is provided for educational purposes. No copyright infringement is intended.
All third-party copyrights and trademarks belong to their respective owners.
Third-party software and assets remain subject to their applicable licenses.

## Download

[Download ROMAW 1.0 for Windows x64](https://github.com/wsidebottom/ROMAW/releases/download/v1.0/ROMAW-1.0-windows-x64.zip).
Extract the entire ZIP, then run `ROMAW.exe`. Windows 10 or later is required.
See the [version 1.0 release](https://github.com/wsidebottom/ROMAW/releases/tag/v1.0)
for release notes and the SHA-256 checksum.

## Build and run

Build from WSL at `/mnt/d/Source/ROMAW` after preparing the dependencies in
[the Windows build guide](README.windows-build):

```sh
JOBS=10 make
```

The root build coordinates CPU dependencies, static libraries and staging.
Launch `windows-binaries/bin/ROMAW.exe` on Windows to open Finder, or supply
a classic application path. Keep the staged support files and matching DLLs
with the executable. Preferences, apps and caches live in `%USERPROFILE%/.romaw`.

Original licenses, copyright notices and provenance remain in their source files.
Current source paths, library/API names and build settings use ROMAW.

The profile Apps folder is mounted as `ROMAW:` inside the emulator.

Settings and file-type mappings use `ROMAW.prefs` and `ROMAW.ftm` in that profile.
Installation settings and master templates seed missing profile files.
The generated font resource beside the executable is `ROMAW.rsrc`.

The user profile is always `.romaw`. A new profile is created directly there;
startup does not discover or import other profile directories.

## JIT and AOT execution

Both CPU runtimes support just-in-time (JIT) translation to native x64 code.
68K native execution is enabled by default. Its opt-in persistent ahead-of-time
(AOT) cache saves translated code for reuse on later launches; see
[68K JIT and AOT](syn68k/docs/aot-x64.md) for setup and cache behavior.

PowerPC uses interpretation by default. `-ppc-jit` enables native translation;
`-ppc-aot` also prepares linked code before execution and persists translations
between launches. See [PowerPC translation caching](synppc/docs/translation-cache.md)
for preparation, persistence and fallback behavior.

## Loading disk images

Open a disk image in ROMAW's Finder by double-clicking it or choosing File > Open.
The mounted volume appears on the desktop; double-click it to browse its files.
Use Put Away, Eject or drag the volume to Trash to unmount it. Mounted images and
their access settings are remembered for subsequent launches.

You can also mount an image from PowerShell:

```powershell
.\windows-binaries\bin\ROMAW.exe -mount-ro "D:\Mac Images\Macintosh.img"
```

ROMAW reads MFS, classic HFS, HFS+/HFSX, ISO 9660 and supported DMG images
directly, without extracting their contents. Common image extensions include
`.image`, `.iso`, `.cdr`, `.toast`, `.dmg`, `.img`, `.dsk`, `.hfv` and `.hda`.
The image's actual format determines support.

Use `-mount-ro` for read-only access or `-mount-rw` to request in-place writes
to supported classic HFS images. MFS, HFS+/HFSX, ISO 9660 and compressed images
remain read-only. The Windows image picker defaults to read-only access.
See [direct disk image access](disk-images/README.md) for supported containers,
compression formats and write limitations.

## Macintosh sidecar files

ROMAW supports Macintosh resource forks and Finder metadata stored in OS X,
Netatalk, Basilisk II/SheepShaver and legacy Executor sidecar formats. For a
data file named `App`, the supported layouts are:

| Format | Companion files |
| --- | --- |
| OS X (AppleDouble) | `._App` beside the data file. |
| Netatalk (AppleDouble) | `.AppleDouble/App` in the containing directory. |
| Basilisk II / SheepShaver | `.finf/App` for Finder metadata and `.rsrc/App` for the raw resource fork. |
| Executor (legacy AppleDouble) | `%App` beside the data file. |

Keep the data file and its companions together when transferring applications
or documents. ROMAW detects existing companions automatically, so reading these
formats requires no command-line switch. New sidecars follow the containing
directory's detected convention; an unmarked directory uses the command-line
default below. Sidecars are created when metadata or resource writes require
them, rather than for every ordinary host file.

| Command-line setting | Effect |
| --- | --- |
| No sidecar switch (default) | Use OS X `._name` AppleDouble files for new sidecars in unmarked directories. |
| `-netatalk` | Use Netatalk `.AppleDouble/name` files for new sidecars in unmarked directories. |
| `-afpd` | Select the afpd conventions, including the same sidecar default as `-netatalk`. On Windows its sidecar behavior is the same as `-netatalk`; the additional legacy Linux behavior selects alternate default System Folder and application directories. |
| `-basiliskii` | Use Basilisk II/SheepShaver `.finf/name` metadata and `.rsrc/name` resource forks for new sidecars in unmarked directories. |
| `-executor` | Use legacy Executor `%name` AppleDouble files for new sidecars in unmarked directories. |

For example, launch an application with Netatalk as the fallback layout:

```powershell
.\windows-binaries\bin\ROMAW.exe -netatalk "D:\Mac Apps\App"
```

These switches do not convert existing sidecars or override a detected directory
layout. Select only one format per run; `-netatalk` and its `-afpd` variant may
be combined. These defaults apply to the guest File Manager. Native Finder
transfers use their [destination storage convention](finder/README.md#shared-macintosh-storage-layouts).
See the [sidecar guide](system/tests/README.sidecars) for interoperability,
conversion tools and regression checks, and the
[host-drive settings](docs/runtime-guide.md#profile-and-host-volumes) for enabling sidecar
support on host volumes.

## Documentation

- [Windows build and debugging](README.windows-build)
- [Runtime settings, files and input](docs/runtime-guide.md)
- [All subsystem and application references](docs/README.md)
- [Source layout](system/README.sources.md) and [static libraries](docs/static-libraries.md)
- [Regression suites](system/tests/README.md)
- [Contributor rules](Agents.md)

Git operations belong at this workspace root. Build output, dependency prefixes,
runtime apps and preserved repository histories under `build/git-history/` are
local artifacts excluded from Git.

ROMAW identifies builds as `ROMAW version <git-commit>` in command-line output,
About dialogs and Windows file properties. The commit is read from Git at build time.
