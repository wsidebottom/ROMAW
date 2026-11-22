# Local classic Macintosh reference disassembly

This tool copies System and Finder **with their AppleDouble companions**, extracts
resource forks, inventories `cfrg` members, and writes PowerPC PEF section listings
and selected linear 68K resource listings. It resolves CFM import glue through
loader relocations instead of guessing symbols from addresses or byte strings.
PEF parsing follows the maintained loader in `synppc/src/pef-loader.cpp`.

From the workspace root, in Windows PowerShell:

```powershell
python -m pip install --target disasm/system9/python-packages -r tools/classic-disasm/requirements.txt
python tools/classic-disasm/disassemble.py --source 'Y:/System 9 Folder'
python -m unittest discover -s tools/classic-disasm -p 'test_*.py'
```

Omit `--source` to regenerate from the saved copies. The default output is
`disasm/system9/`. `--output` selects a different directory. The script checks
PEF/resource extents and pattern output sizes. It does not execute either file.
Resource decompression uses `tools/classic-disasm/decode_resource.py`, with
python-rsrcfork 1.8.0. Capstone 5.0.7 produces the instruction listings.

Outputs for each application include:

- `manifest.json`: fork hashes, resource names/attributes and fragment paths.
- `resource-fork.bin` and `resources/*.bin`: complete original resource bytes.
- `pef/*/pef.json`: sections, entries, exports, imports and direct import calls.
- `pef/*/section-*.ppc.s`: annotated, section-relative PowerPC listings.
- `pef/*/copy-threads.txt`: contexts surrounding thread/fork-I/O import calls.
- `m68k/*.s`: linear 68K listings for known executable resource types.

Linear decoding can include headers, jump tables and inline data. It does not
recover complete source, function boundaries or indirect call graphs. Unsupported
compressed resources remain intact in `resources/`; their manifest entries have
`decode_error`, and the command exits 1 after recording warnings. In this System
image, 34 resources require unsupported `dcmp 3`, including seven candidate code
resources. All detected PowerPC PEF containers decode without warnings.

Apple inputs and generated listings are local, ignored analysis assets. Commit
the tool, its synthetic tests and the findings, rather than the Apple binaries.
