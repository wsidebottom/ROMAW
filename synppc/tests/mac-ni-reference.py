# +===================================================================
#   File:      mac-ni-reference.py
#
#   Summary:   Original classic PPC probe for NI results with normalized input operands.
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

"""Original classic PPC probe for NI results with normalized input operands.

The 603 and 750 differ for denormal operands. This hardware subset deliberately
uses normal operands (or zero), so it can check their shared result-flush rule.
"""

import argparse
from pathlib import Path
import runpy

from classic_fpu_reference import arithmetic_body, generate_classic


def generate(directory, underflow_enabled=False):
    directory.mkdir(parents=True, exist_ok=True)
    cases = []
    for single in (False, True):
        minimum = 0x3810000000000000 if single else 0x0010000000000000
        step = 1 << 29 if single else 1
        for operation in (12, 18, 20, 21, 25, 28, 29, 30, 31):
            if operation == 12 and not single:
                continue
            sources = (
                (0x3800000000000000, minimum - 1, minimum)
                if operation == 12
                else (minimum, minimum + step, minimum + (1 << 52) - step)
            )
            for source in sources:
                for negative in (False, True):
                    for mode in range(4):
                        for ni in (0, 4):
                            a = source | (int(negative) << 63)
                            b, c = 0x4000000000000000, 0x3FE0000000000000
                            if operation in (20, 21):
                                b = minimum + step
                                b |= int(negative ^ (operation == 21)) << 63
                            elif operation >= 28:
                                b = 0
                            opcode = (59 if single else 63) << 26
                            opcode |= (4 << 21) | (operation << 1) | (1 << 16)
                            if operation != 25:
                                opcode |= 2 << 11
                            if operation == 25 or operation >= 28:
                                opcode |= 3 << 6
                            if operation == 12:
                                opcode = (63 << 26) | (4 << 21) | (2 << 11) | 24
                                b = a
                            case = dict(
                                opcode=opcode,
                                inputs=[a, b, c],
                                fpscr=mode | ni | (0x20 if underflow_enabled else 0),
                            )
                            cases.append(case)
    generate_classic(
        directory,
        cases,
        arithmetic_body(cases),
        application_name=("PPC NI Enabled Probe" if underflow_enabled else "PPC NI Hardware Probe"),
        result_name="PPC NI Enabled Results" if underflow_enabled else "PPC NI Results",
        description="normalized-input NI",
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode-binary", type=Path)
    parser.add_argument("--underflow-enabled", action="store_true")
    args = parser.parse_args()
    if args.decode_binary:
        reference = runpy.run_path(str(Path(__file__).with_name("mac-fpu-reference.py")))
        reference["decode"](args.directory, args.decode_binary, binary=True)
    else:
        generate(args.directory, args.underflow_enabled)
