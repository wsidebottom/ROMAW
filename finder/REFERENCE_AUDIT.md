# Classic Finder reference audit: folder presentation

This comparison concerns the native folder View command path. It does not
establish feature parity for the entire reconstructed Finder.

| Reference | Native gap found | Integration |
| --- | --- | --- |
| `finder7/src/interface/menu_parser.c`, `finder_insert_menu_definition` | The classic menu has dynamic `view` entries; native View was a fixed Icons/List pair. | Added Small Icons and seven column sort commands. |
| `finder7/src/desktop/desktop_launch.c`, `finder_create_menu_command` | Classic menu commands carry a view selector; native commands had no sort/view model. | `views.cpp` owns native view/sort preferences and deterministic comparisons. |
| Preserved Finder `STR#` 3000 in `finder/resources/text_resources.r`, entries 14Ã¢â‚¬â€œ20 | Name, Date, Size, Kind, Comments, Label and Version had no list columns. | The build generates these column/menu names directly from the editable resource. Metadata uses host file dates, data/resource size, classic type, comments, labels and `vers`. |
| Existing per-folder native window persistence | Closing a folder lost its presentation. | Validated `ViewsNative` settings retain view, column and direction. Large-icon positions stay in their existing independent storage. |

List view uses native report rows and small icons. Small Icons uses compact
columns with 16 px images. Large-icon layout and the desktop retain the existing
icon grid. Name comparisons use Windows Unicode ordinal ordering; no claim is
made that this reproduces every classic Script Manager collation rule.

Validation includes settings round trips and malformed input, every sort column,
64-bit sizes/dates, Unicode names, stable name tie-breaks, native menu/column
interaction, view switching and close/reopen persistence.


## Transfer integration

The native backend follows the operation lifetime, independent cancellation and
pending-decision principles identified in
`references/disasm/system9-copy-findings.md` (FinderLib `NewThread`/switchers,
termination and outstanding-request drainage). It uses host workers, rather
than importing Apple's PPC object layouts or requiring the guest Thread Manager.
Finder 7's preserved `STR#` 9000 replacement warnings and 5000 error strings in
`finder/resources/text_resources.r` establish explicit replacement
choices and rejection of folder/file type mismatches and ancestor replacement.
SuperMario's `Interfaces/CIncludes/Events.h` supplies the classic Option modifier
contract; the Windows adapter maps that to Alt and also accepts Control as a
host copy modifier. Shift supplies an explicit cross-volume move override.

Focused tests cover volume/modifier decisions, overlapping copies and moves,
independent cancellation, source edits, Macintosh forks, exact-name conflicts,
Skip/Stop/Replace, replacement rollback, displaced-item Undo and classic dialog
keyboard routing. They do not establish classic alias, running-application or
clipboard integration; those are excluded from this work.
