# Repository guidelines

## Working tree and scope

Run `git status` at the repository root before editing. Preserve unrelated changes
and commit from this root; source directories are ordinary directories, not
independent repositories. Former repository metadata and full-history bundles
remain local under `build/git-history/`. Do not initialize nested repositories or
add gitlinks.

Windows x64 is the supported application host. WSL supplies cross-build tools
and isolated Linux regression harnesses. See [the build guide](README.windows-build)
for prerequisites, commands, build profiles and staging; see
[the documentation index](docs/README.md) for subsystem contracts and status.

## Source and generated files

- ROMAW managers, `include/`, `platform/` and `frontend-sdl/` live at the root;
  [the source map](system/README.sources.md) describes ownership.
- `syn68k/src/` contains the required 68K runtime and generator; `syn68k/tests/`
  contains its fixtures. `synppc/src/` contains the PPC runtime, PEF loader and
  optional ROMAW bridge. Standalone `libsynppc.a` must not require ROMAW headers.
- `finder/` is the native desktop; `finder7/` is reference-only and is not built.
  `finder-metadata/`, `classic-ui/`, `disk-images/` and `carbonlib/` are shared libraries.
- Keep generated output in the selected object tree. The debug profile uses
  `system/obj/` and `synppc/obj/`; optimized uses `build/romaw-optimized/`.
  Syn68k uses `syn68k/obj/`. Never configure or compile in source directories.
- Edit asset inputs, templates and generators, not `obj/generated/*.inc`.
  Preserve explicit `generated/` include paths so legacy source-tree output cannot
  shadow current files.
- Keep local apps, Apple runtime assets, SDKs, debug logs and dependency prefixes
  ignored. Do not format `references/`, `downloaded/` or vendored sources.

## Build and validation rules

Run WSL Bash commands from `/mnt/d/Source/ROMAW` (the Windows checkout is
`D:/Source/ROMAW`). Use the root build to coordinate dependencies. Do not run
overlapping builds against the same object directory or staged executable.
Generators sharing scratch files must run serially before parallel compilation.
After changing Automake inputs, run `autoreconf -fi` at the repository root.
Old MSYS2 `.deps/*.Po` files may contain drive paths that WSL Make cannot parse;
truncate only those generated files before cleaning, never tracked source.

The wrappers stage `ROMAW.exe`, `syngen.exe` and `libsyn68k.a` under
`windows-binaries/bin/`. Preserve final executable dependencies on both CPU
archives, including `EXTRA_ROMAW_DEPENDENCIES`, so library updates relink
ROMAW. Keep matching runtime DLLs beside each executable.

Use [the regression suite guide](system/tests/README.md) and subsystem docs to
select checks appropriate to the change, then exercise the affected application
flow on Windows. Cross-built tests must execute on Windows or through WSL
interop; compiling is not runtime validation. Keep new robustness fixes bounded
and reproduce malformed inputs or allocation failures in focused fixtures.
Prefer reporting suites over one executable or application per small assertion.
For Carbon migration work, retain the requested batch size of at least 100 API
entry bodies before batch validation and commit.

For generated-file changes, verify stale legacy files cannot shadow output and
that touching an input rebuilds the output and its consumer. Run
`bash system/tests/check-build.sh generated-file` after building.

For syn68k assembly-filter changes, run
`python3 syn68k/tests/test-assembly-cleanup.py`, rebuild and stage both CPU and
ROMAW, then run `bash system/tests/check-build.sh debug-info`. Preserve every
DWARF section byte, including opcode names in debug strings. Check Windows GDB
source resolution. Compare generated CPU text before and after a debug-only
filter change; if it changes, run the CPU regression suite.

Normal launches are GUI-only. `-debug trace` enables a console and potentially
large `stdout.txt`/`stderr.txt` files in the launch working directory. Inspect
`win_exception` entries and use GDB `bt` for failures. For WSL-built sources, use
`set substitute-path /mnt/d/Source/ROMAW D:/Source/ROMAW`. Startup changes require
both absolute-path and bare-name/PATH launch checks from another directory.

Do not equate smoke tests or export counts with full application, physical CPU,
MMU or Carbon compatibility. Preserve actual library initialization, guest
patches, resource forks, ownership and real fallback paths; do not fabricate
private boot structures or skip failed initializers to mask missing services.
Leave Defender quarantine and security settings intact when a fixture is blocked.

## Coding Style & Naming Conventions

The repository formatting rules apply to maintained source in `system/`, `syn68k/`, `synppc/`, `packages/` and `tools/`. Keep LF line endings and a final newline. New text uses UTF-8; preserve the encoding of historical files. `finder7/` is reference-only and is excluded from repository formatting. `.editorconfig` supplies editor defaults; language formatter configuration takes precedence over those defaults.

- C, C++, Objective-C and C header templates use clang-format **18.1.8** with the [C++ Core Guidelines naming and layout recommendations](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#S-naming) as the style reference. The root `.clang-format` implements NL.4, NL.17, NL.18 and NL.20: four spaces, no indentation tabs, a 120-column target, Stroustrup braces (function braces on a new line; control, class and struct braces on the same line), spaces before control parentheses only, and pointer/reference markers beside the type. Separate function definitions with a blank LF line, placing the separator before any comment attached to the following function (`SeparateDefinitionBlocks: Always`). Keep include order, comment text and string literals intact. Apply the layout to both C and C++; keep their existing language modes. Do not change ABI packing, macro meaning, address translation or byte order to satisfy a formatter. Formatting does not establish compliance with the guidelines' ownership, resource-management or architectural rules; those require separate semantic review and targeted testing.
- Python uses Black **24.10.0**, four spaces and 120 columns, configured in `pyproject.toml`.
- Bourne/Bash shell uses shfmt **3.8.0**, two spaces, indented case labels and binary operators starting continued lines (`-i 2 -ci -bn`). Keep scripts in their original shell dialect.
- Perl uses Perl::Tidy **20230309** and the root `.perltidyrc`: four spaces, 120 columns, cuddled else and tight delimiter interiors. Preserve shebang options and generated string contents.
- PowerShell uses PSScriptAnalyzer **1.23.0** through `tools/format-powershell.ps1`: four spaces, opening braces on the same line and consistent operator/separator spacing.
- Make recipes retain literal tabs. Assembly, AWK, Scheme, yacc/lex inputs, source-generating templates and other specialized languages retain their existing grammar-specific layout; use consistent nearby indentation for changes. Do not run a C formatter over these languages. Binary assets, Macintosh resource-fork files (including `%` sidecars), vendored `third_party/` code and build/generated output retain their upstream or generator-owned format. Update generators rather than formatting generated output.

Every maintained code file starts with a descriptive file header. Keep executable shebangs, Python encoding cookies, BOMs and XML declarations in their required positions before the header. Use the following block for C-family source and interfaces; use native comments for other languages (`#` for scripts/Makefiles, `;` for Scheme/import definitions, `dnl` for Autoconf and XML comments for manifests):

```c
/*+===================================================================
  File:      MYFILE.EXT

  Summary:   Brief summary of the file contents and purpose.

  Classes:   Classes declared or used (in source files).

  Functions: Functions exported (in source files).

  Origin:    Evidence of the source's provenance and inheritance;
             references belong here, not a change history.

  Copyright and Legal notices: Retain original file-specific notices
             below this header and reference the applicable license.
===================================================================+*/
```

Use the actual filename and a meaningful summary. List identifiable externally linked source definitions in `Functions`; distinguish declarations, imports and script entry points where appropriate. `Classes` may state that the language has no classes. Update metadata when a file's purpose or API changes. Preserve all existing copyright, license and provenance text verbatim below the header; do not invent authors, dates, ownership or historical origins. Generated artifacts, vendored/upstream helpers, historical backups and ignored `references/` content keep their original headers.

`tools/file_headers.py` adds missing headers; `--refresh` regenerates existing metadata and must be reviewed. Explicit provenance is retained; headers containing embedded attribution or license text must be updated manually so that legal notices are never discarded. Its `--check` mode checks the required fields without parser dependencies. Install the pinned discovery dependencies with `python3 -m pip install -r tools/file-headers-requirements.txt` before adding or refreshing C/C++ headers. Header discovery understands the legacy `A0`/`P0` family (including the `C_` prefix produced by `P` declarations). Run its regression checks with `python3 -m unittest discover -s tools/tests -p test_file_headers.py`.

In WSL, install `shfmt` and `perltidy` (the versions above), plus `python3 -m pip install clang-format==18.1.8 black==24.10.0` in a virtual environment. Run `python3 tools/format-code.py` at the workspace root to add missing headers and format all tracked and non-ignored new supported source files. Set `CLANG_FORMAT` to an alternate executable path if necessary. Run `python3 tools/format-code.py --check` for a read-only header and formatting check. In PowerShell with PSScriptAnalyzer 1.23.0 installed, run `./tools/format-powershell.ps1`, or add `-Check` to verify. Both checks must pass after formatting changes. Review formatting diffs and rebuild/test affected targets; formatter success does not establish runtime correctness.

Keep `ROMlib_`, `C_`, Mac Toolbox names and uppercase four-character resource constants. New or changed functions must have explicit parameter types and proper declarations. The Windows build treats implicit-int and implicit-function-declaration diagnostics as errors. Keep behavioral changes narrow and separate from broad formatting when practical. MS-DOS and NeXT ports have been removed; do not reintroduce their platform defines or backends. `docs/cleanup_ideas` is historical guidance, not an authoritative deletion list; some files it calls obsolete are still built. Internal modal dialogs always use the implementation supporting movable dialogs; `ALLOW_MOVABLE_MODAL` is no longer a build option.

## Commits and reviews

Use short imperative commit subjects. Stage only the files belonging to the
change. Include validation commands and manual scenarios in commit or review
notes, distinguishing startup smoke checks from full workflows. Keep runtime
changes separate from structural cleanup when practical. Generated logs, staged
binaries, Apple assets and local reference material do not belong in commits.

Historical cleanup lists are design notes, not deletion queues. Do not remove
live build inputs or rearrange Macintosh subsystems solely to match old proposals.
Current implementation details belong in the linked subsystem documents rather
than an appended session history in this file.
