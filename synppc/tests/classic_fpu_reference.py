# +===================================================================
#   File:      classic_fpu_reference.py
#
#   Summary:   Original classic Mac PEF hardware probe writer; no Apple runtime assets.
#
#   Classes:   None declared.
#
#   Functions: arithmetic_body, generate_classic
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original classic Mac PEF hardware probe writer; no Apple runtime assets.

Shared by the floating-point and integer hardware generators. The FPU body is
identical to the Mach-O probe's, with input/output addresses supplied by this
wrapper. Transfer the complete generated application with its resource fork.
"""

import json
import struct
import sys

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def arithmetic_body(cases):
    """Evaluate each three-input/FPSCR row and store result plus raw MFFS."""
    code = []
    for case in cases:
        for reg in range(4):
            code.append((50 << 26) | (reg << 21) | (6 << 16) | (24 if reg == 0 else (reg - 1) * 8))
        code.extend(
            [
                (63 << 26) | (255 << 17) | (711 << 1),
                case["opcode"],
                (63 << 26) | (5 << 21) | (583 << 1),
                (54 << 26) | (4 << 21) | (7 << 16),
                (54 << 26) | (5 << 21) | (7 << 16) | 8,
                (14 << 26) | (6 << 21) | (6 << 16) | 32,
                (14 << 26) | (7 << 21) | (7 << 16) | 16,
            ]
        )
    return code


def generate_classic(
    directory,
    cases,
    body,
    *,
    application_name="PPC FPU Hardware Probe",
    result_name="PPC FPU Results",
    input_data=None,
    description="floating-point",
    extra_imports=(),
):
    names = [
        "GetCurrentProcess",
        "GetProcessInformation",
        "HSetVol",
        "FSOpen",
        "FSWrite",
        "FSClose",
        "ExitToShell",
    ] + list(extra_imports)
    code = []

    def emit(*words):
        code.extend(words)

    def immediate(reg, value):
        emit(
            (15 << 26) | (reg << 21) | (value >> 16),
            (24 << 26) | (reg << 21) | (reg << 16) | (value & 65535),
        )

    def call(name):
        emit(
            0x819E0008 + 4 * names.index(name),
            0x800C0000,
            0x804C0004,
            0x7C0903A6,
            0x4E800421,
        )

    def check():
        # Local failure avoids a conditional branch across the large unrolled
        # arithmetic body, beyond the instruction's signed 16-bit reach.
        emit(0x2C030000, 0x41820008, 0)

    # Preserve caller's LR, TOC, nonvolatile r30 and FPSCR. All used FPRs are
    # volatile under the classic PPC ABI. Calls use transition vectors.
    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    emit(0xFC00048E, 0xD8010030)  # mffs f0; stfd f0,48(r1).
    emit(0x387E0070)
    call("GetCurrentProcess")
    check()
    emit(0x3860003C, 0x907E0080, 0x387E00C0, 0x907E00B8)
    emit(0x387E0070, 0x389E0080)
    call("GetProcessInformation")
    check()
    # Select the application's parent folder using its actual FSSpec.
    emit(0x38600000, 0xA89E00C0, 0x80BE00C2)
    call("HSetVol")
    check()
    emit(0x387E0150, 0x38800000, 0x38BE0110)
    call("FSOpen")
    check()
    inputs = b"".join(struct.pack(">4Q", *c["inputs"], c["fpscr"]) for c in cases) if input_data is None else input_data
    input_offset = 0x200
    output_offset = input_offset + len(inputs)
    output_size = len(cases) * 16
    immediate(6, input_offset)
    emit(0x7CDE3214)  # add r6,r30,r6.
    immediate(7, output_offset)
    emit(0x7CFE3A14)  # add r7,r30,r7.
    emit(*body)
    # Disable any probe status before calling OS routines, restoring the exact
    # incoming FPSCR. FSWrite receives a guest-endian count and reference.
    emit(0xC8010030, 0xFDFE058E)
    immediate(3, output_size)
    emit(0x907E0114, 0xA87E0110, 0x389E0114)
    immediate(5, output_offset)
    emit(0x7CBE2A14)  # add r5,r30,r5.
    call("FSWrite")
    check()
    emit(0x807E0114)
    immediate(4, output_size)
    emit(0x7C632050)  # subf r3,r3,r4: check the full requested byte count.
    check()
    emit(0xA87E0110)
    call("FSClose")
    check()
    emit(0xC8010030, 0xFDFE058E)
    call("ExitToShell")
    emit(0x7FC2F378)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040)
    emit(0x38600000, 0x4E800020)
    data = bytearray(input_offset) + inputs + bytes(output_size)
    filename = result_name.encode("ascii")
    if not filename or len(filename) > 31:
        raise ValueError("The dedicated result filename must fit classic HFS")
    data[0x150 : 0x151 + len(filename)] = bytes([len(filename)]) + filename
    app = pef(
        code,
        data,
        imports=[("InterfaceLib", name, 2) for name in names],
        main=0,
        reloc=[0x4600, 0x4A00 + len(names) - 1],
    )
    write_fragment(directory, application_name, app, 1)
    # Precreate the dedicated output, so the probe never needs to create or
    # truncate arbitrary user files. Repeated runs replace these exact bytes.
    (directory / filename.decode("ascii")).write_bytes(bytes(output_size))
    (directory / "cases.json").write_text(json.dumps(cases), encoding="utf-8")
    print(f"Generated {len(cases)} classic PPC {description} hardware cases")
