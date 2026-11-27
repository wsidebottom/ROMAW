# Documentation index

Use [the root overview](../README.md) to start. Current instructions live in the
owning document below; dated audits record evidence for their stated checkpoint,
not a guarantee that every later checkout passes those checks. Build generators,
local SDKs, third-party notices and captured test data keep their own documentation.
Commands run from the repository root unless a document explicitly says otherwise.
Local Apple sources, SDKs and reference-machine paths are prerequisites or recorded
evidence; they are not distributed with ROMAW.

## Setup and contributing

- [Windows build, profiles, staging and debugging](../README.windows-build)
- [Runtime settings, profiles, files and input](runtime-guide.md)
- [Macintosh sidecar formats and command-line settings](../README.md#macintosh-sidecar-files)
- [Contributor rules and formatting](../Agents.md)
- [ROMAW source layout](../system/README.sources.md)
- [Static-library ownership and targeted builds](static-libraries.md)
- [Regression suite selection](../system/tests/README.md)
- [Windows x64 ABI and migration notes](windows-x86_64.md)
- [Cooperative execution contexts](../system/docs/cooperative-contexts.md)

## Runtime and shared packages

- [CPU status and external references](cpu-runtime-status.md)
- [syn68k build and tests](../syn68k/README.md): [68040](../syn68k/docs/68040-status.md),
  [MMU](../syn68k/docs/mmu040.md),
  [writeback recovery](../syn68k/docs/68040-writeback-recovery.md),
  [JIT/AOT](../syn68k/docs/aot-x64.md)
- [synppc build and embedding](../synppc/README.md): [750 architecture](../synppc/docs/750-status.md),
  [native translation](../synppc/docs/translation-status.md), [CFM-68K](../synppc/tests/CFM68K.md),
  [native runtime libraries](../synppc/docs/native-runtime-libraries.md),
  [cache lifecycle](../synppc/docs/translation-cache.md),
  [flat-core fixtures](../synppc/docs/application-core.md),
  [hardware comparison](../synppc/docs/hardware-validation.md),
  [PPC CFM ownership](../synppc/docs/cfm.md),
  [reference captures](../synppc/tests/data/README.md)
- [Native Carbon compatibility](../carbonlib/README.md) and
  [API contracts](../carbonlib/docs/api-notes.md) and
  [implementation audit](../synppc/docs/carbon-native.md)
- [Native Finder](../finder/README.md) and [reference audit](../finder/REFERENCE_AUDIT.md)
- [Finder guest resource procedures](../finder/guest/README.md)
- [Native resource inventory, embedded hooks and validation](native-code-resources.md)
- [MacTCP](mactcp.md): Windows transport, resolver, ownership, reference evidence and regression checks.
- [Shared classic UI](../classic-ui/README.md) and [QuickDraw host adapter](../windows/README.md)
- [Icons and host metadata](../finder-metadata/README.md)
- [Disk-image support and limits](../disk-images/README.md)
- [ZIP, StuffIt, BinHex and MacBinary archives](../archives/README.md) and
  [StuffIt bridge and provenance](../stuffit/README.md)
- [Bitmap artwork](../bitmaps/README.md)
- [Embedded resources](../system/resources/README.md) and
  [System resource catalog](../system/resources/system/README.md) and
  [system-error procedure port](../system/resources/system/syserr.md)

## Application investigations

- [Finder 8.6 startup and runtime dependencies](../synppc/docs/finder86-startup.md)
- [Kid Pix compatibility](kidpix-compatibility.md),
  [PPC startup](../synppc/docs/kidpix-startup.md),
  [Studio startup](../synppc/docs/kidpix-studio-startup.md)
- [MyABCD / CarbonLib](../synppc/docs/myabcd-startup.md)
- [MacTest Pro](../synppc/docs/mactest-pro.md)
- [Resplendence](resplendence-compatibility.md)
- [SuperMario integration audit](supermario-integration-audit.md)

## Focused regression references

- [24-bit memory](../system/tests/README.24bit),
  [Browser](../system/tests/README.browser-compat),
  [host names](../system/tests/README.host-names),
  [sidecars](../system/tests/README.sidecars),
  [volume information](../system/tests/README.volume-info)
- [Sound](../system/tests/README.sound), [video](../system/tests/README.video),
  [window resizing](../system/tests/README.window-resize),
  [Toolbox additions](../system/tests/README.toolbox-additions)
- [Document associations](../system/tests/document-associations/README.md),
  [mouse input](../system/tests/mouse-input/README.md),
  [startup display](../system/tests/startup-display/README.md),
  [system-error dialogs](../system/tests/dsat-dialogs/README.md),
  [type-9 resources](../system/tests/type9-resources/README.md)

## Reference-machine tools

- [Classic remote control](../tools/classic-remote/README.md)
- [Classic disassembly](../tools/classic-disasm/README.md)
- [Netatalk / AFP transfer](../tools/netatalk/README.md)
- [LC475 VGA startup](../tools/lc475-vga-startup/README.md)
