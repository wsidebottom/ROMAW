#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_thread_probe.py
#
#   Summary:   Exercise cooperative PPC thread execution, contexts, results, suspension and cancellation.
#
#   Classes:   None declared.
#
#   Functions: make_thread_probe
#
#   Origin:    Original PowerPC instruction fixture for the native Thread Manager adapter.
#
#   Copyright and Legal notices: Applicable repository license: synppc/COPYING.
# ===================================================================+
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_thread_probe(destination):
    names = [
        "NewThread",
        "GetCurrentThread",
        "GetThreadState",
        "YieldToAnyThread",
        "YieldToThread",
        "ThreadBeginCritical",
        "ThreadEndCritical",
        "SetThreadState",
        "DisposeThread",
        "CreateThreadPool",
        "GetFreeThreadCount",
        "GetSpecificFreeThreadCount",
        "SetThreadStateEndCritical",
        "GetDefaultThreadStackSize",
        "GetThreadCurrentTaskRef",
        "ThreadCurrentStackSpace",
        "GetThreadStateGivenTaskRef",
        "SetThreadReadyGivenTaskRef",
        "SetThreadScheduler",
        "GetThreadScheduler",
        "SetThreadSwitcher",
        "SetThreadTerminator",
        "SetDebuggerNotificationProcs",
    ]
    carbon_names = {
        "NewThread",
        "GetCurrentThread",
        "ThreadBeginCritical",
        "YieldToThread",
        "CreateThreadPool",
        "GetSpecificFreeThreadCount",
        "SetThreadStateEndCritical",
        "GetThreadCurrentTaskRef",
        "SetThreadReadyGivenTaskRef",
        "SetThreadScheduler",
        "SetThreadSwitcher",
        "SetDebuggerNotificationProcs",
    }
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(name):
        emit(0x819E0008 + names.index(name) * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def check(value):
        emit(0x2C030000 | (value & 65535))
        failures.append(len(code))
        emit(0x40820000)

    def equal_words(left, right):
        emit(0x807E0000 + left, 0x809E0000 + right, 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)

    def set_word(offset, value):
        emit(0x38600000 | value, 0x907E0000 | offset)

    def phase(value):
        emit(0x807E0200)
        check(value)

    def new_thread(mode=0, options=4, stack_units=1, result=0):
        set_word(0x220, mode)
        set_word(0x200, 0)
        emit(0x38600001, 0x389E0280, 0x38BE0200, 0x3CC00000 | stack_units, 0x38E00000 | options, 0x391E020C, 0x393E0208)
        call("NewThread")
        check(result)

    def state(value):
        emit(0x807E0208, 0x389E0210)
        call("GetThreadState")
        check(0)
        emit(0xA07E0210)
        check(value)

    def disposed():
        emit(0x807E0208, 0x389E0210)
        call("GetThreadState")
        check(-618)

    emit(0x9421FF00, 0x7C0802A6, 0x90010104, 0x93C100F8, 0x7C5E1378)
    emit(0x387E0204)
    call("GetCurrentThread")
    check(0)
    emit(0x807E0204)
    check(2)
    emit(0x387E0228)
    call("GetThreadCurrentTaskRef")
    check(0)
    emit(0x807E0228, 0x2C030000)
    failures.append(len(code))
    emit(0x41820000)
    emit(0x38600001, 0x389E022C)
    call("ThreadCurrentStackSpace")
    check(0)
    emit(0xA07E022C)
    check(3)
    emit(0xC9DE02A0)  # lfd f14, main value; this nonvolatile FPR belongs to this CPU
    new_thread()
    state(0)
    call("YieldToAnyThread")
    check(0)
    phase(1)
    equal_words(0x208, 0x214)
    emit(0xD9DE02B0)
    equal_words(0x2A0, 0x2B0)
    equal_words(0x2A4, 0x2B4)
    call("ThreadBeginCritical")
    check(0)
    call("YieldToAnyThread")
    check(-619)
    phase(1)
    call("ThreadEndCritical")
    check(0)
    emit(0x807E0208)
    call("YieldToThread")
    check(0)
    phase(2)
    equal_words(0x20C, 0x224)
    disposed()
    # A stopped fiber cannot run until explicitly made ready.
    new_thread(1)
    call("YieldToAnyThread")
    check(0)
    phase(1)
    state(1)
    emit(0x807E0208, 0x38800000, 0x38A00000)
    call("SetThreadState")
    check(0)
    emit(0x807E0208)
    call("YieldToThread")
    check(0)
    phase(2)
    disposed()
    # Cancellation unwinds an already stopped CPU without executing its tail.
    new_thread(1)
    call("YieldToAnyThread")
    check(0)
    phase(1)
    emit(0x807E0208, 0x3C802468, 0x60841357, 0x38A00000)
    call("DisposeThread")
    check(0)
    phase(1)
    emit(0x807E020C, 0x3C802468, 0x60841357, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    disposed()
    # Suspended creation and disposal never execute the entry function.
    new_thread(0, 5)
    state(1)
    call("YieldToAnyThread")
    check(0)
    phase(0)
    emit(0x807E0208, 0x38800000, 0x38A00000)
    call("DisposeThread")
    check(0)
    disposed()
    # Scheduler hooks run synchronously across both library names. The
    # application scheduler can override the suggestion before the debugger.
    emit(0x387E0300)
    call("SetThreadScheduler")
    check(0)
    emit(0x387E0360)
    call("GetThreadScheduler")
    check(0)
    emit(0x807E0360, 0x389E0300, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x387E0310, 0x389E0318, 0x38BE0320)
    call("SetDebuggerNotificationProcs")
    check(0)
    new_thread()
    equal_words(0x208, 0x350)
    for argument, incoming in ((0x344, 1), (0x348, 0)):
        emit(0x807E0208, 0x389E0308, 0x38BE0000 | argument, 0x38C00000 | incoming)
        call("SetThreadSwitcher")
        check(0)
    emit(0x807E0208, 0x389E0308, 0x38BE034C)
    call("SetThreadTerminator")
    check(0)
    set_word(0x340, 1)  # kCurrentThreadID overrides the suggested child.
    emit(0x807E0208)
    call("YieldToThread")
    check(0)
    phase(0)
    for offset, value in ((0x330, 16), (0x334, 2), (0x33C, 0), (0x358, 2)):
        emit(0x807E0000 | offset)
        check(value)
    equal_words(0x338, 0x208)
    set_word(0x340, 0)
    emit(0x807E0208)
    call("YieldToThread")
    check(0)
    phase(1)
    equal_words(0x344, 0x208)
    equal_words(0x348, 0x208)
    emit(0x807E0208)
    call("YieldToThread")
    check(0)
    phase(2)
    equal_words(0x34C, 0x208)
    equal_words(0x354, 0x208)
    equal_words(0x20C, 0x224)
    disposed()
    emit(0x38600000)
    call("SetThreadScheduler")
    check(0)
    emit(0x38600000, 0x38800000, 0x38A00000)
    call("SetDebuggerNotificationProcs")
    check(0)

    # The CarbonLib and ThreadsLib imports share the same pool and scheduler.
    def check_pool(expected, stack_units=None):
        emit(0x38600001)
        if stack_units is None:
            emit(0x389E0210)
            call("GetFreeThreadCount")
        else:
            emit(0x3C800000 | stack_units, 0x38BE0210)
            call("GetSpecificFreeThreadCount")
        check(0)
        emit(0xA07E0210)
        check(expected)

    check_pool(0)
    for count, stack_units in ((2, 1), (1, 2)):
        emit(0x38600001, 0x38800000 | count, 0x3CA00000 | stack_units)
        call("CreateThreadPool")
        check(0)
    check_pool(3)
    check_pool(2, 1)
    check_pool(1, 2)
    new_thread(options=19)
    check_pool(1, 1)
    state(1)
    emit(0x807E0228, 0x809E0208, 0x38BE0210)
    call("GetThreadStateGivenTaskRef")
    check(0)
    emit(0xA07E0210)
    check(1)
    emit(0x38600000, 0x809E0208)
    call("SetThreadReadyGivenTaskRef")
    check(-619)
    emit(0x807E0208, 0x389E022C)
    call("ThreadCurrentStackSpace")
    check(0)
    emit(0xA87E022E)
    check(-256)
    emit(0x807E0228, 0x809E0208)
    call("SetThreadReadyGivenTaskRef")
    check(0)
    phase(0)
    emit(0x807E0208)
    call("YieldToThread")
    check(0)
    phase(1)
    emit(0x807E0208, 0x389E022C)
    call("ThreadCurrentStackSpace")
    check(0)
    emit(0xA87E022E)
    check(-320)
    emit(0x807E0208, 0x38800000, 0x38A00001)
    call("DisposeThread")
    check(0)
    disposed()
    check_pool(2, 1)
    new_thread(options=19, stack_units=2)
    check_pool(0, 2)
    emit(0x807E0208, 0x38800000, 0x38A00000)
    call("DisposeThread")
    check(0)
    new_thread(options=19, stack_units=2, result=-108)
    emit(0x807E0208)
    check(0)
    new_thread(options=23, stack_units=2)
    call("ThreadBeginCritical")
    check(0)
    emit(0x807E0208, 0x38800001, 0x38A00000)
    call("SetThreadStateEndCritical")
    check(0)
    emit(0x807E0208, 0x38800000, 0x38A00000)
    call("DisposeThread")
    check(0)
    emit(0x38600001, 0x389E0218)
    call("GetDefaultThreadStackSize")
    check(0)
    emit(0xA07E021A)
    check(0)
    emit(0xA07E0218)
    check(1)
    emit(0x83C100F8, 0x80010104, 0x7C0803A6, 0x38210100, 0x38600000, 0x4E800020)
    child = len(code) * 4
    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    emit(0xC9DE02A8, 0x387E0214)
    call("GetCurrentThread")
    check(0)
    emit(0x387E0230)
    call("GetThreadCurrentTaskRef")
    check(0)
    equal_words(0x228, 0x230)
    set_word(0x200, 1)
    emit(0x807E0220, 0x2C030000)
    stopped = len(code)
    emit(0x40820000)
    call("YieldToAnyThread")
    check(0)
    skip = len(code)
    emit(0x48000000)
    code[stopped] |= (len(code) - stopped) * 4
    emit(0x38600001, 0x38800001, 0x38A00002)
    call("SetThreadState")
    check(0)
    code[skip] |= (len(code) - skip) * 4
    emit(0xD9DE02B8)
    equal_words(0x2A8, 0x2B8)
    equal_words(0x2AC, 0x2BC)
    set_word(0x200, 2)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x3C601234, 0x60635678, 0x4E800020)
    callbacks = {
        0x300: [
            0x80830000,
            0x90820330,
            0x80830004,
            0x90820334,
            0x80830008,
            0x90820338,
            0x80A3000C,
            0x90A2033C,
            0x80620340,
            0x2C030000,
            0x40820008,
            0x7C832378,
            0x4E800020,
        ],
        0x308: [0x90640000, 0x4E800020],
        0x310: [0x90620350, 0x4E800020],
        0x318: [0x90620354, 0x4E800020],
        0x320: [0x80630008, 0x90620358, 0x4E800020],
    }
    data = bytearray(1024)
    reloc = [0x4600, 0x4A00 | (len(names) - 1), 0xA000, 0x280, 0x4600]
    for offset, words in callbacks.items():
        struct.pack_into(">II", data, offset, len(code) * 4, 0)
        reloc.extend((0xA000, offset, 0x4600))
        emit(*words)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    struct.pack_into(">II", data, 0x280, child, 0)
    struct.pack_into(">I", data, 0x224, 0x12345678)
    struct.pack_into(">dd", data, 0x2A0, 3.0, 5.0)
    image = pef(
        code,
        data,
        imports=[("CarbonLib" if name in carbon_names else "ThreadsLib", name, 2) for name in names],
        main=0,
        reloc=reloc,
    )
    write_fragment(destination, "PPC Thread Probe", image, 1)


if __name__ == "__main__":
    make_thread_probe(Path(sys.argv[1]))
