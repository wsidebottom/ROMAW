#!/usr/bin/env python
# +===================================================================
#   File:      tiger_resource_roundtrip.py
#
#   Summary:   Run on Tiger's Python 2.3 against a disposable file named SystemProbe.
#
#   Classes:   None declared.
#
#   Functions: No public module functions.
#
#   Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Run on Tiger's Python 2.3 against a disposable file named SystemProbe.

Copy a real resource file and its AppleDouble into a test directory first.
Afterward compare every original resource payload with the resulting fork.
Read-only Resource Manager calls and Finder copies miss multi-entry write damage.
This source deliberately also uses syntax supported by Tiger's bundled Python.
"""

import os
import sys

from Carbon import File, Res

if len(sys.argv) != 2 or os.path.basename(sys.argv[1]) != "SystemProbe":
    raise ValueError("Expected a disposable SystemProbe file, never the live System")

ref, is_directory = File.FSPathMakeRef(sys.argv[1])
refnum = Res.FSOpenResFile(ref, 3)
try:
    Res.UseResFile(refnum)
    if Res.Count1Resources("Tst!"):
        raise ValueError("Probe already contains test resources")
    handle = Res.Resource("ROMAW write round trip")
    handle.AddResource("Tst!", 30000, "Probe")
    handle.WriteResource()
    Res.UpdateResFile(refnum)
finally:
    Res.CloseResFile(refnum)

refnum = Res.FSOpenResFile(ref, 3)
try:
    Res.UseResFile(refnum)
    handle = Res.Get1Resource("Tst!", 30000)
    if handle.data != "ROMAW write round trip":
        raise ValueError("Saved resource data changed")
    handle.RemoveResource()
    Res.UpdateResFile(refnum)
finally:
    Res.CloseResFile(refnum)

refnum = Res.FSOpenResFile(ref, 1)
try:
    Res.UseResFile(refnum)
    total = 0
    for index in range(1, Res.Count1Types() + 1):
        total += Res.Count1Resources(Res.Get1IndType(index))
    sys.stdout.write("Resource write/reopen/remove passed; %d resources remain\n" % total)
finally:
    Res.CloseResFile(refnum)
