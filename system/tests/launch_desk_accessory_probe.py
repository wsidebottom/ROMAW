# +===================================================================
#   File:      launch_desk_accessory_probe.py
#
#   Summary:   Run inside Windows GDB at the private probe's ExitToShell breakpoint.
#
#   Classes:   None declared.
#
#   Functions: value, allocate, pascal, dispatch
#
#   Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Run inside Windows GDB at the private probe's ExitToShell breakpoint."""

import gdb
import struct

memory = gdb.selected_inferior()
base = int(gdb.parse_and_eval("ROMlib_offset"))


def value(expression):
    return int(gdb.parse_and_eval(expression))


def allocate(size):
    return value(f"(void *)_NewPtr_flags({size},1,1)")


def pascal(text):
    data = text.encode("ascii")
    pointer = allocate(len(data) + 1)
    memory.write_memory(pointer, bytes([len(data)]) + data)
    return pointer


assert value("(int)ROMlib_osdispatch_available(0x36)")
# Native calls made between dispatches can reenter guest code. Reserve room
# below the synthetic Pascal frame for that downward-growing guest stack.
stack = allocate(8192) + 4096
saved = [value(f"cpu_state.regs[{i}].ul.n") for i in range(16)]


def dispatch(file, name, expected):
    frame = (
        struct.pack(
            ">IHIIH",
            0x1234ABCD,
            0x36,
            name - base if name else 0,
            file - base if file else 0,
            0xA55A,
        )
        + b"guard"
    )
    memory.write_memory(stack, frame)
    gdb.execute(f"set cpu_state.regs[15].ul.n={stack - base}", to_string=True)
    returned = value("(unsigned)_OSDispatch(0,0)")
    assert returned == 0x1234ABCD
    assert value("cpu_state.regs[15].ul.n") == stack - base + 14
    result = bytes(memory.read_memory(stack + 14, 2))
    assert int.from_bytes(result, "big", signed=True) == expected, result.hex()
    assert bytes(memory.read_memory(stack + 16, 5)) == b"guard"


try:
    assert value("(int)ROMlib_osdispatch_available(1)")
    task_info = allocate(24)
    for slots, output in ((0, 0), (-1, task_info + 2), (1, task_info + 2)):
        memory.write_memory(task_info, b"\xa5" * 24)
        frame = struct.pack(">IHHIH", 0x1234ABCD, 1, slots & 0xFFFF, output - base if output else 0, 0xA55A) + b"guard"
        memory.write_memory(stack, frame)
        gdb.execute(f"set cpu_state.regs[15].ul.n={stack - base}", to_string=True)
        assert value("(unsigned)_OSDispatch(0,0)") == 0x1234ABCD
        assert value("cpu_state.regs[15].ul.n") == stack - base + 12
        assert bytes(memory.read_memory(stack + 12, 2)) == b"\0\1"
        assert bytes(memory.read_memory(stack + 14, 5)) == b"guard"
        actual = bytes(memory.read_memory(task_info, 24))
        if slots == 1:
            assert actual[:2] == b"\xa5" * 2 and actual[12:] == b"\xa5" * 12
            assert actual[4:8] == b"FwPr", actual.hex()
        else:
            assert actual == b"\xa5" * 24
    print("GET_ACTIVE_TASK_INFO_STACK_AND_BOUNDS_OK")
    assert value("(int)ROMlib_osdispatch_available(2)")
    switch_info = allocate(16)
    for output in (switch_info + 4, 0):
        memory.write_memory(switch_info, b"\xa5" * 16)
        frame = struct.pack(">IHI", 0x1234ABCD, 2, output - base if output else 0) + b"guard"
        memory.write_memory(stack, frame)
        gdb.execute(f"set cpu_state.regs[15].ul.n={stack - base}", to_string=True)
        assert value("(unsigned)_OSDispatch(0,0)") == 0x1234ABCD
        assert value("cpu_state.regs[15].ul.n") == stack - base + 10
        assert bytes(memory.read_memory(stack + 10, 5)) == b"guard"
        expected = b"\xa5" * 4 + b"\0" * 8 + b"\xa5" * 4 if output else b"\xa5" * 16
        assert bytes(memory.read_memory(switch_info, 16)) == expected
    print("GET_SWITCH_INFO_STACK_AND_BOUNDS_OK")
    dispatch(0, 0, -50)
    dispatch(0, pascal(".Missing"), -192)
    current = value("(int)C_CurResFile()")
    dispatch(0, pascal(".SelectorA"), 0)
    assert value("(void *)GetDCtlEntry(-18)"), "driver was not installed"
    closed = value("(int)CloseDriver(-18)")
    assert closed == 0, ("close driver", closed)
    spec = allocate(80)
    filename = pascal("Accessory")
    volume = value("ROMlib_exevrefnum")
    located = value(f"(int)C_FSMakeFSSpec({volume},0,(void *){filename},(void *){spec})")
    assert located == 0, ("locate fixture", located)
    dispatch(spec, pascal(".Missing"), -192)
    restored = value("(int)C_CurResFile()")
    assert restored == current, ("resource file", current, restored)
    gdb.set_convenience_variable("accessory_file", gdb.Value(spec))
    gdb.set_convenience_variable("accessory_name", gdb.Value(pascal(".SelectorB")))
    print("LAUNCH_DESK_ACCESSORY_STACK_AND_RESOURCE_OK")
finally:
    for i, number in enumerate(saved):
        gdb.execute(f"set cpu_state.regs[{i}].ul.n={number}", to_string=True)
