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
stack = allocate(32)
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
