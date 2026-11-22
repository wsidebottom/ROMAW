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

For example, launch an application with Netatalk as the fallback layout:

```powershell
.\windows-binaries\bin\ROMAW.exe -netatalk "D:\Mac Apps\App"
```

These switches do not convert existing sidecars or override a detected directory
layout. Basilisk II/SheepShaver folders are detected from `.finf` or `.rsrc`;
legacy Executor `%name` companions remain readable without a separate setting.
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
