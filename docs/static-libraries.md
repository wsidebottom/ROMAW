# Runtime static-library organization

Build from the repository root in WSL. Windows x64 is the runtime target; Linux
programs remain build generators and test harnesses. `references/` is outside
this build and was not moved or changed.

```sh
JOBS=10 make                 # CPU dependencies, libraries, executable and staging
JOBS=10 make libraries       # All runtime archives; no ROMAW.exe link
make library-memory         # One Toolbox module; no application link
make library-classic-ui     # Shared drawing package only
make library-synppc         # Portable PowerPC runtime only
make list-libraries         # Configured Toolbox source ownership
make check-libraries        # Ownership, membership, rebuild and staged-copy checks
make check                  # Build and runtime suites; build prerequisites once
make check-all              # Full build/runtime/CPU/PPC/UI sweep
make check-ui               # One selected suite
```

The root Makefile coordinates shared build directories serially. `JOBS` controls
parallel compilation inside a selected build. Do not start another build against
the same object directories from a different shell.

## Library ownership

Paths below using `system/obj` or `synppc/obj` describe the debug profile.
The default optimized profile uses `build/romaw-optimized/obj` and
`build/romaw-optimized/ppc`; staged archive snapshots add `/optimized` beneath
`windows-binaries/bin/libraries/x64`. See [build profiles](../README.windows-build#build-profiles-and-output).

The 28 internal Toolbox archives live in `system/obj/libraries`. Their source
directories are at the repository root (`memory/`, `quickdraw/`, `files/`, and
the other manager directories below). Shared Toolbox interfaces live in
`include/`. Root `Makefile.am` and `configure.ac` configure in the selected
profile's object tree; the handwritten root `Makefile` remains the developer
entry point.

The SDL frontend is in `frontend-sdl/`, avoiding a case-insensitive collision
with the external SDK's `SDL/SDL.h`. Windows host/architecture sources live in
`platform/`; the SDL sound backend is under `sound/sdl/`. Movie services are in
`movies/`, volume creation is in `files/mkvol/`, and build generators/templates
are under `tools/romaw/`. Native Finder source lives in root `finder/`.

| Archive suffix (`libromaw-*.a`) | Responsibility/source directory |
| --- | --- |
| controls | Control Manager and standard control definitions |
| dialogs | Dialog/Alert managers |
| files | File Manager, host catalogs, Standard File, desktop/association databases, volume creation |
| hfs | HFS catalog, extents, allocation and mounted image volumes |
| lists | List Manager |
| menus | Menu Manager, MDEF and MBDF |
| printing | Print Manager and raster/PDF support |
| quickdraw | QuickDraw, color, regions, bitmap transfer, pictures and graphics worlds |
| resources | Resource Manager, embedded resources and resource preferences |
| textedit | TextEdit |
| windows | Window Manager and the QuickDraw classic-drawing adapter |
| appleevents | AppleEvent descriptors, handlers and coercion |
| sound | Sound Manager, speech, sample playback and SDL audio backend |
| numerics | SANE and numeric Toolbox operations |
| runtime | Startup, processes, traps, execution bridges, profile/configuration and shutdown |
| options | Command-line parsing and per-application execution configuration |
| memory | Macintosh zones, handles, heap allocation and temporary memory |
| events | Event Manager, interrupts, input dispatch and VBL handling |
| services | Components, notifications, timers, OS utilities, packages, scrap and editions |
| movies | QuickTime-compatible movie services |
| devices | Device, serial and communications managers |
| text | Fonts, strings, locale, script and character conversion |
| host | Host path, metadata, trash and disk-image integration |
| display | Refresh and presentation scheduling |
| ui | Icons, Appearance services, desk accessories, About and preferences |
| diagnostics | Errors, trap names, dumps and system diagnostics |
| platform | Windows host services and x64 architecture support |
| sdl | SDL display, input, clipboard and window integration |

The following runtime archives have independent build owners:

| Archive | Source/build owner |
| --- | --- |
| `libromaw-classic-ui.a` | `classic-ui`: pure C drawing primitives shared by Finder and QuickDraw |
| `libfinder.a` | `finder`, built by `tools/romaw/native_finder/build.py` |
| `libromaw-images.a` | `disk-images`: HFS/ISO/DMG/image codecs and direct image writes |
| `libromaw-finder-metadata.a` | `finder-metadata`: resource icons and host metadata |
| `libromaw-carbon.a` | `carbonlib`: native Carbon/CoreFoundation compatibility |
| `libsyn68k.a` | `syn68k`: 68K interpreter, x64 JIT/AOT support and FPU/MMU execution |
| `libsynppc.a` | `synppc`: portable PPC execution and PEF loading |
| `libsynppc-romaw.a` | `synppc/src/romaw`: guest ABI and ROMAW Toolbox bridge |

Finder also links `libromaw-archives.a` and `libstuffit_ffi.a` from its own
object tree. These nested dependencies are built by the native Finder builder
and are not separately copied into the staged library inventory. See
[archive formats](../archives/README.md) and [the StuffIt bridge](../stuffit/README.md).

Use `library-finder`, `library-images`, `library-finder-metadata`, `library-carbon`,
`library-syn68k` and `library-synppc-romaw` for these additional targeted builds.
Select an internal Toolbox archive by its table suffix, such as `library-memory`.
ROMAW's runtime subsystem boundaries use static archives; the former external
native application SDK and its DLL probes have been retired.

The shared classic renderer lives in root `classic-ui/`.
Finder no longer compiles a second copy into its archive. The common
`include/classic-ui/classic_ui.h` interface forwards to the canonical package
header. The guest/QuickDraw adapter is `windows/quickdraw_adapter.c`.

## Executable, linking and dependencies

`ROMAW.exe` compiles only the Windows `winmain.c` entry point directly. Runtime
startup, the Toolbox and host services come from archives. Embedded Windows
resources remain resource objects. Host generators, command-line utilities,
examples and regression executables remain programs rather than artificial
libraries. `fonts/`, `bitmaps/`, documentation, local runtime data and preserved
Git histories retain their roles; historical standalone examples are not added
to the application link.

Internal Toolbox modules still share classic globals and dispatch contracts;
they are build/link boundaries, not isolated public SDKs. The final link retains
their complete object sets with `--whole-archive`, including trap and startup
definitions that have no ordinary direct C caller. External CPU/package
archives retain their existing linking rules and licenses. This reorganization
does not claim to remove coupling or create a stable ABI for internal managers.

Each internal source has exactly one owning archive. Archives depend on the
configured Makefile as well as their objects, so membership changes replace old
archives instead of retaining retired members. Generated tables stay in
`system/obj/generated`; their existing consumer dependencies remain active.
Changes to CPU, bridge, Finder and package archives continue to trigger the final
executable link through explicit prerequisites.

## Artifacts and checks

Both application and library-only builds stage the internal Toolbox archives and
the independent archives listed above under `windows-binaries/bin/libraries/x64`
(with an `optimized` subdirectory for that profile). `manifest.tsv` records each
filename and its current build source. `ROMAW_LIBRARY_STAGE_DIR` overrides this directory;
otherwise it follows `ROMAW_STAGE_DIR`. Intermediate objects stay with their
build owners, never in maintained source directories. The required
`ROMAW.exe`, `syngen.exe` and `libsyn68k.a` retain their established staging
locations directly in `windows-binaries/bin`.

`tools/check-static-libraries.py` reads the configured source lists and checks
unique ownership, exact archive membership, source-to-archive rebuild propagation,
application relinking, and absence of the duplicate renderer in Finder. With
`--stage`, it also verifies the full archive inventory and compares every staged
copy with its current build output. These checks supplement runtime integration
tests; producing archives alone does not establish application compatibility.

Regression commands are consolidated by `tools/run-checks.py` into five suites:
`build`, `runtime`, `cpu`, `ppc` and `ui`. `make check` runs the first two after one
prerequisite build; `make check-all` runs all five without repeating suite steps.
Use `make check-SUITE` for affected areas during development. Existing individual
fixtures remain available for diagnosis, but do not need to be invoked one by one.
The runner prints compact progress and keeps detailed output in
`build/checks/<suite>/<step>.log`. It stops at the first failing step and reports
that log, retaining the existing test coverage rather than silently dropping cases.

Existing root and file-specific licensing/provenance stays in place, including
the CPU, image-codec and other third-party notices. No source was moved into
`references/`, and nothing there is a build input.
