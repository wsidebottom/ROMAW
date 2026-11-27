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


For folders such as the local Open Transport reference set, analyze every
file that has an AppleDouble companion:

```bash
build/format-venv/bin/python tools/classic-disasm/disassemble.py --all-files \
  --source "references/Open Transport Files" --output build/mactcp/open-transport-disasm
```

This also lists ASLM code resources (cDxx/cdxx), Open Transport glue,
control-panel entry points and dnrp resolver entries. Headers, relocation
records and inline data can decode as instructions; restart decoding at a
verified entry offset when following a function.

The supplied Open Transport files contain 21 paired files and 72 detected
PowerPC PEF fragments. TCP%2FIP CDEV contains DRVR 22 named .ipp and dnrp 0
named DNR; both delegate into the Open Transport/ASLM libraries.
Open Tpt Inet 68K Library carries the ot:Shim$IPLib interface in libr 66
and its code in cD66 1/2. Its DNR module is libr 75 with cd75 1/2.
The manifests retain source hashes and resource names for checking these
identities against the local inputs.

The user's MacTCP Ping CODE 1 independently establishes the application ABI:
SendIcmpEcho sets control 17, an asynchronous PBControl and a separate echo
callback; its completion reads the result at +16, timestamps at +50/+54,
echoed length at +58 and user data at +68. It subtracts eight bytes from
the echoed length. WaitTillPingComplete yields with SystemTask. OpenResolver
loads the first dnrp and calls a C selector entry (1=open, 2=close,
3=StrToAddr). These observations guide ROMAW's native MacTCP adapter;
they do not establish compatibility with the full Open Transport stack.
