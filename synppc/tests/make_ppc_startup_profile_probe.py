#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_startup_profile_probe.py
#
#   Summary:   Original library/app fixtures for bounded startup retries and saved prefs.
#
#   Classes:   None declared.
#
#   Functions: generate
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original library/app fixtures for bounded startup retries and saved prefs."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def generate(destination, mode="success"):
    target = 0x860 if mode == "main-failure" else 0x922
    if mode == "library-failure":
        target = 0x7FFF
    code = [
        0x9421FFC0,
        0x7C0802A6,
        0x90010044,
        0x93C10038,
        0x7C5E1378,
        0x3C607379,
        0x60637376,
        0x389E0028,  # Gestalt('sysv', &version).
        0x819E0008,
        0x800C0000,
        0x804C0004,
        0x7C0903A6,
        0x4E800421,
        0x807E0028,
        0x2C030000 | target,
        0x3860F4FB,  # -2821: the real CarbonLib initializer failure.
        0x40820008,
        0x38600000,
        0x83C10038,
        0x80010044,
        0x7C0803A6,
        0x38210040,
        0x4E800020,
    ]
    if mode == "initializer-fault":
        code[15:18] = [0x38600000, 0x41820008, 0]
    library_imports = [("InterfaceLib", "Gestalt", 2)]
    if mode in ("launch-stub", "running-failure"):
        # The shared launch stub succeeds. A real private CarbonLib instance
        # checks the system version, using a counter in an ordinary shared import.
        write_fragment(
            destination, "ProfileGate", pef([0x38600000, 0x4E800020], bytes(16), exports=[("Count", 1, 0, 1)]), 0
        )
        library_imports.append(("ProfileGate", "Count", 1))
        code = (
            code[:5]
            + [0x819E000C, 0x816C0000, 0x2C0B0000, 0x396B0001, 0x916C0000, 0x4082000C, 0x38600000, 0x48000038]
            + code[5:]
        )
    library = pef(
        code,
        bytes(64),
        imports=library_imports,
        exports=[("RetryToken", 1, 32, 1)],
        init=0,
        reloc=[0x4600, 0x4A01 if len(library_imports) == 2 else 0x4A00],
    )
    if mode == "termination-failure":
        term_code = [
            0x38600000,
            0x4E800020,
            0x9421FFC0,
            0x7C5E1378,
            0x387E0040,
            0x3C807077,
            0x60847063,
            0x38A00005,
            0x38DE0050,
            0x38FE0054,
            0x39000000,
            0x819E0010,
            0x800C0000,
            0x804C0004,
            0x7C0903A6,
            0x4E800421,
            0,
        ]
        term_data = bytearray(128)
        term_data[8:16] = struct.pack(">II", 8, 0)
        term_data[64:77] = b"\x0cInterfaceLib"
        library = pef(
            term_code,
            term_data,
            imports=[("InterfaceLib", "ROMAWProfileTermFailure", 2)],
            exports=[("RetryToken", 1, 96, 1)],
            init=0,
            term=8,
            reloc=[0x4601, 0x4A00],
        )
    write_fragment(destination, "CarbonLib", library, 0)
    app_code = [0] if mode == "main-failure" else [0x38600000, 0x4E800020]
    data = bytearray(256)
    imports = [("CarbonLib", "RetryToken", 1)]
    if mode in ("launch-stub", "running-failure"):
        imports += [("InterfaceLib", name, 2) for name in ("Gestalt", "GetSharedLibrary", "GetNextEvent")]
        app_code = [0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378]

        def call(index):
            app_code.extend([0x819E0008 + index * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421])

        if mode == "running-failure":
            app_code.extend([0x38600000, 0x389E0080])
            call(3)
        app_code.extend([0x3C607379, 0x60637376, 0x389E0040])
        call(1)
        app_code.extend([0x807E0040, 0x2C030922])
        branch = len(app_code)
        app_code.append(0x41820000)
        app_code.extend(
            [
                0x387E0060,
                0x3C807077,
                0x60847063,
                0x38A00005,
                0x38DE0044,
                0x38FE0048,
                0x39000000,
            ]
        )
        call(2)
        app_code[branch] |= (len(app_code) - branch) * 4
        app_code.extend([0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020])
        data[96:106] = b"\x09CarbonLib"
    app = pef(
        app_code,
        data,
        imports=imports,
        main=0,
        reloc=[0x4600, 0x4A00 | (len(imports) - 1)],
    )
    write_fragment(destination, "PPC Startup Profile Probe", app, 1)


if __name__ == "__main__":
    generate(Path(sys.argv[1]), sys.argv[2] if len(sys.argv) > 2 else "success")
