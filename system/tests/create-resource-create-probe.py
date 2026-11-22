# +===================================================================
#   File:      create-resource-create-probe.py
#
#   Summary:   Generate original 68K probes for Resource Manager trap return state.
#
#   Classes:   None declared.
#
#   Functions: probe, generate
#
#   Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Generate original 68K probes for Resource Manager trap return state."""

import json
import struct
import sys
from pathlib import Path


def probe(hierarchical, failure):
    code = bytearray()
    fixups = []
    labels = {}

    def words(*values):
        code.extend(struct.pack(">" + "H" * len(values), *values))

    def branch(opcode, label):
        fixups.append((len(code), label))
        words(opcode)

    if hierarchical:
        words(0x4267, 0x42A7)  # Push vRefNum=0, dirID=0.
    words(0x4879, 0, 0)  # PEA filename; patched after guest allocation.
    pointer = len(code) - 4
    words(0x203C, 0x1234, 0x5678)  # D0's upper word must survive.
    words(0x44FC, 0x001B)  # X/N/V/C set; Z clear before the trap.
    words(0xA81B if hierarchical else 0xA9B1)
    words(0x41EE, 0)  # LEA 0(A6),A0: MacWrite does this before BNE.
    branch(0x6700 if failure else 0x6600, "flags")
    words(0x42C1, 0x0241, 0x001F)  # Read CCR, retain X/N/Z/V/C.
    words(0x0C41, 0x0018 if failure else 0x0014)
    branch(0x6600, "flags")
    words(0x0C80, 0x1234, 0xFFD0 if failure else 0)  # D0.W=ResErr.
    branch(0x6600, "register")
    words(0x2F7C, 0, 0, 4, 0x4E75)  # Pascal long result = success.
    labels["flags"] = len(code)
    words(0x2F7C, 0, 1, 4, 0x4E75)
    labels["register"] = len(code)
    words(0x2F7C, 0, 2, 4, 0x4E75)
    for offset, label in fixups:
        displacement = labels[label] - offset - 2
        assert 0 < displacement < 128
        code[offset + 1] = displacement
    return code, pointer


def generate(directory, application):
    root = Path(__file__).resolve().parents[2]
    script = [
        "set pagination off",
        "set confirm off",
        "set print thread-events off",
        f"set substitute-path /mnt/d/Source/ROMAW {root.as_posix()}",
        "break C_WaitNextEvent",
        f"run {json.dumps(application.replace(chr(92), '/'))}",
    ]
    for hierarchical in (False, True):
        filename = directory / ("hierarchical.rsrc" if hierarchical else "classic.rsrc")
        # Use an absolute Macintosh path, independent of the app's current WD.
        name = (filename.drive + ":".join(filename.parts[1:])).encode("mac_roman")
        assert len(name) < 256
        name = bytes([len(name)]) + name
        script += [
            "set $name=(unsigned char *)_NewPtr_flags(256,1,1)",
            f"set {{unsigned char[{len(name)}]}} $name = " + "{" + ",".join(map(str, name)) + "}",
            "set $proc=(unsigned char *)_NewPtr_flags(128,1,1)",
        ]
        for failure in (False, True):
            code, pointer = probe(hierarchical, failure)
            script += [
                "call destroy_blocks((unsigned int)((unsigned long long)$proc-"
                "(unsigned long long)ROMlib_offset),128)",
                "python",
                "import gdb,struct",
                f"code=bytearray.fromhex('{code.hex()}')",
                "address=int(gdb.parse_and_eval('$name'))-" "int(gdb.parse_and_eval('ROMlib_offset'))",
                f"struct.pack_into('>I',code,{pointer},address)",
                "gdb.selected_inferior().write_memory(" "int(gdb.parse_and_eval('$proc')),code)",
                "end",
                "set $result=(long long)CToPascalCall($proc,4)",
                "if $result != 0",
                f'printf "RESOURCE_CREATE_FAILED hierarchical={int(hierarchical)} '
                f'failure={int(failure)} check=%d\\n",$result',
                "quit 1",
                "end",
            ]
        if hierarchical:
            script += [
                "set $oldmap=(int)C_CurResFile()",
                "set $intlfile=(int)C_HOpenResFile(0,0,$name,3)",
                "if $intlfile == -1",
                "quit 1",
                "end",
            ]
            for identifier in (0, 1):
                script += [
                    f"set $source=(void *)C_IUGetIntl({identifier})",
                    "set $sourcehome=(int)C_HomeResFile($source)",
                    "set $sourcesize=(int)GetHandleSize($source)",
                    "python",
                    "def handle_bytes(expression):",
                    " h=int(gdb.parse_and_eval(expression))",
                    " mem=gdb.selected_inferior()",
                    " address=int.from_bytes(bytes(mem.read_memory(h,4)),'big')",
                    " n=int(gdb.parse_and_eval('(int)GetHandleSize('+expression+')'))",
                    " return bytes(mem.read_memory(int(gdb.parse_and_eval('ROMlib_offset'))+address,n))",
                    "original=handle_bytes('$source')",
                    "end",
                ]
                for repeat in range(2):
                    script += [
                        f"call C_IUSetIntl($intlfile,{identifier},$source)",
                        "if (int)C_ResError() != 0",
                        "quit 1",
                        "end",
                        f"set $copy=(void *)C_Get1Resource(0x494e544c,{identifier})",
                        "if $copy == 0 || $copy == $source",
                        'printf "INTL file=%d current=%d source=%p copy=%p error=%d\\n",$intlfile,(int)C_CurResFile(),$source,$copy,(int)C_ResError()',
                        'printf "INTL_SOURCE_HANDLE_SHARED\\n"',
                        "quit 1",
                        "end",
                        f"if (int)C_Count1Resources(0x494e544c) != {identifier + 1}",
                        'printf "INTL_DUPLICATE_RECORD\\n"',
                        "quit 1",
                        "end",
                        "python",
                        "assert handle_bytes('$copy') == original",
                        "assert handle_bytes('$source') == original",
                        "end",
                    ]
            script += [
                "call C_CloseResFile($intlfile)",
                "if (int)GetHandleSize($source) != $sourcesize || (int)C_HomeResFile($source) != $sourcehome",
                'printf "INTL_SOURCE_HANDLE_DISPOSED\\n"',
                "quit 1",
                "end",
                "call C_UseResFile($oldmap)",
                'printf "INTL_RESOURCE_OWNERSHIP_OK repeated copies and source preservation\\n"',
            ]
        script += ["call DisposPtr($proc)", "call DisposPtr($name)"]
    # Obsolete references must pop their arguments, preserve D0/X and return
    # the classic errors without reading the (deliberately null) parameters.
    script += ["set $proc=(unsigned char *)_NewPtr_flags(128,1,1)"]
    for removing in (False, True):
        words = [0x240F, 0x42A7]  # Save SP in D2, push null resource Handle.
        if not removing:
            words += [0x3F3C, 123, 0x42A7]  # ID and null name.
        words += [
            0x203C,
            0x1234,
            0x5678,
            0x44FC,
            0x001B,
            0xA9AE if removing else 0xA9AC,
            0x42C1,  # Save the returned CCR before checking anything.
            0xB40F,
            0x661E,  # SP must equal D2.
            0x0C80,
            0x1234,
            0x5678,
            0x6616,  # D0 unchanged.
            0x0241,
            0x001F,
            0x0C41,
            0x0014,
            0x660C,
            0x0C78,
            0xFF3B if removing else 0xFF3D,
            0x0A60,
            0x6604,
            0x7000,
            0x6002,
            0x7001,  # Return 0 for success, 1 otherwise.
            0x2F40,
            4,
            0x4E75,
        ]
        code = struct.pack(">" + "H" * len(words), *words)
        script += [
            "call destroy_blocks((unsigned int)((unsigned long long)$proc-" "(unsigned long long)ROMlib_offset),128)",
            "python",
            f"code=bytes.fromhex('{code.hex()}')",
            "gdb.selected_inferior().write_memory(" "int(gdb.parse_and_eval('$proc')),code)",
            "end",
            "set $result=(long long)CToPascalCall($proc,4)",
            "if $result != 0",
            f'printf "OBSOLETE_REFERENCE_FAILED removing={int(removing)}\\n"',
            "quit 1",
            "end",
        ]
    script += [
        "call DisposPtr($proc)",
        'printf "RESOURCE_CREATE_FLAGS_OK success, duplicate, D0.W, NZVC and X\\n"',
        "quit 0",
    ]
    (directory / "probe.gdb").write_text("\n".join(script) + "\n", encoding="utf-8")


if __name__ == "__main__":
    generate(Path(sys.argv[1]).resolve(), sys.argv[2])
