#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_print_cancel_probe.py
#
#   Summary:   Generate PPC checks for explicit and implicit print-dialog cancellation and host failures.
#
#   Classes:   Probe, imported fixture builder.
#
#   Functions: make_probe.
#
#   Origin:    Original regression fixture for the Printing Manager's PrOpenDoc cancellation path.
#
#   Copyright and Legal notices: See synppc/COPYING.
# ===================================================================+

import sys
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe


def make_probe(destination):
    p = Probe(
        [
            ("InterfaceLib", name, 2)
            for name in (
                "InitWindows",
                "PrClose",
                "NewHandleClear",
                "DisposeHandle",
                "PrintDefault",
                "PrJobDialog",
                "PrOpenDoc",
                "PrError",
                "PrSetError",
                "GetPort",
                "DebugStr",
            )
        ]
    )
    p.call("InitWindows")
    p.call("PrClose")  # Reset status before exercising the built-in record APIs.
    p.emit(0x38600078)  # The packed classic TPrint record occupies 120 bytes.
    p.call("NewHandleClear")
    p.nonzero("allocate the real print record")
    p.emit(0x907E0180)
    p.call("PrintDefault")
    p.call("PrError")
    p.expect(0, "initialize the real print record")
    p.emit(0x387E0184)
    p.call("GetPort")
    p.emit(0x807E0184)
    p.nonzero("retain the current graphics port before printing")

    # GDB intercepts the host dialog: three cancellations, then a host failure.
    p.emit(0x807E0180)
    p.call("PrJobDialog")
    p.expect(0, "explicit cancellation returns false")
    p.call("PrError")
    p.expect(0, "explicit cancellation retains its Boolean error contract")
    for error in (128, 128, -1):
        p.emit(0x38600000)
        p.call("PrSetError")
        p.emit(0x807E0180, 0x38800000, 0x38A00000)
        p.call("PrOpenDoc")
        p.expect(0, "a cancelled or failed dialog publishes no print port")
        p.call("PrError")
        p.expect(error, "implicit cancellation reports iPrAbort and preserves host errors")
        p.emit(0x387E0188)
        p.call("GetPort")
        p.emit(0x807E0188, 0x809E0184)
        p.equal("failed printing preserves the caller's graphics port")
    p.call("PrClose")
    p.emit(0x807E0180)
    p.call("DisposeHandle")
    marker = b"PRINT_CANCEL_OK"
    p.data[0x200 : 0x201 + len(marker)] = bytes([len(marker)]) + marker
    p.emit(0x387E0200)
    p.call("DebugStr")
    p.finish(destination, "PPC Print Cancel Probe")


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
