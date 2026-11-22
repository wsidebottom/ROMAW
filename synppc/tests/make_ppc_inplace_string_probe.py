#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_inplace_string_probe.py
#
#   Summary:   Check legacy InterfaceLib in-place string conversions, pointer ABI and bounded failures.
#
#   Classes:   None declared.
#
#   Functions: make_inplace_string_probe
#
#   Origin:    Original PPC regression fixture for DiagC99I2C's InterfaceLib p2cstr import.
#
#   Copyright and Legal notices: See synppc/COPYING.
# ===================================================================+

"""Original PPC code; no Apple runtime or diagnostic code is embedded."""
import sys
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe


def make_inplace_string_probe(destination, mode="valid"):
    p = Probe([("InterfaceLib", "p2cstr", 2), ("InterfaceLib", "c2pstr", 2), ("StdCLib", "memcmp", 2)])
    p.data = bytearray(12288)
    if mode != "valid":
        # Unmapped 32-bit pointers exercise the real checked bridge.
        if mode not in ("bad-pascal", "bad-c"):
            raise ValueError(mode)
        p.emit(0x3C60FFFF, 0x6063FFFF)
        p.call("p2cstr" if mode == "bad-pascal" else "c2pstr")
        p.emit(0)
    else:
        cases = [
            ("p2cstr", b"\0"),
            ("p2cstr", b"\x01\x80"),
            ("p2cstr", b"\x03A\0\xff"),
            ("p2cstr", b"\xff" + bytes(range(255))),
            ("c2pstr", b"\0"),
            ("c2pstr", b"\x80\0"),
            ("c2pstr", b"A\xffB\0"),
            ("c2pstr", b"X" * 255 + b"\0"),
            ("c2pstr", b"Y" * 300 + b"\0"),
        ]
        for i, (name, source) in enumerate(cases):
            base = 256 + i * 1024
            original = bytearray(b"GUAR" + source + b"\xa5" * 8 + b"TAIL")
            expected = bytearray(original)
            if name == "p2cstr":
                length = source[0]
                expected[4 : 4 + length] = source[1 : 1 + length]
                expected[4 + length] = 0
            else:
                length = min(source.index(0), 255)
                expected[5 : 5 + length] = source[:length]
                expected[4] = length
            p.data[base : base + len(original)] = original
            p.data[base + 512 : base + 512 + len(expected)] = expected
            p.emit(0x387E0000 | (base + 4))
            p.call(name)
            p.pointer(base + 4, f"{name} returns original guest pointer, case {i}")
            p.emit(0x387E0000 | base, 0x389E0000 | (base + 512), 0x38A00000 | len(expected))
            p.call("memcmp")
            p.expect(0, f"{name} preserves guards and exact payload, case {i}")
        for name in ("p2cstr", "c2pstr"):
            p.emit(0x38600000)
            p.call(name)
            p.expect(0, f"{name} preserves null input")
    p.finish(destination, "PPC Inplace String Probe")


if __name__ == "__main__":
    make_inplace_string_probe(Path(sys.argv[1]), sys.argv[2] if len(sys.argv) > 2 else "valid")
