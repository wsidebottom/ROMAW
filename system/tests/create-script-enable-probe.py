#!/usr/bin/env python3
# +===================================================================
#   File:      create-script-enable-probe.py
#
#   Summary:   Generate Windows GDB checks for Finder's private KeyScriptEnable trap and process state.
#
#   Classes:   None.
#
#   Functions: generate; script entry point.
#
#   Origin:    ROMAW regression fixture; ABI verified against the local System 9 ScriptUtils reference.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

"""Generate a bounded GDB probe; pass an output directory and the Finder 8.6 application path."""

import json
from pathlib import Path
import sys


def generate(directory, application):
    script = r"""
set pagination off
set confirm off
set print thread-events off
set substitute-path /mnt/d/Source/ROMAW D:/Source/ROMAW
break ROMlib_process_enable_scripts
run -system 8.6 -syszone 16m -applzone 64m -sdlaudio test APPLICATION
disable 1
finish
python
import gdb, struct
mem = gdb.selected_inferior()
base = int(gdb.parse_and_eval('ROMlib_offset'))
def call(expr): return int(gdb.parse_and_eval(expr))
storage = call('(void *)_NewPtr_flags(256,1,1)')
assert storage
cpu = call('&cpu_state')
saved = bytes(mem.read_memory(cpu, gdb.parse_and_eval('cpu_state').type.sizeof))
process = gdb.parse_and_eval('current_process_info')
assert int(process)
mask = int(process['keyboard_scripts'])
unicode = int(process['keyboard_unicode'])
try:
 for codes in [[], [0], [7, 31, 0], [0x7e], [-1, 32, 64, 127], [7, 7]]:
  data = storage + 128
  mem.write_memory(data, b''.join(struct.pack('>h', code) for code in codes) + b'\xde\xad')
  address = data - base
  stack = struct.pack('>IIIhI', 0x123456, 0x8006ffae, address, len(codes), 0xdeadbeef)
  mem.write_memory(storage, stack)
  gdb.execute('set cpu_state.regs[15].ul.n=' + str(storage - base), to_string=True)
  assert call('(unsigned int)_ScriptUtil(0,0)') == 0x123456
  assert call('cpu_state.regs[15].ul.n') == storage - base + 14
  assert bytes(mem.read_memory(storage + 14, 4)) == b'\xde\xad\xbe\xef'
  assert bytes(mem.read_memory(data + len(codes) * 2, 2)) == b'\xde\xad'
  expected = 1
  for code in codes:
   if 0 <= code < 32: expected |= 1 << code
  assert call('current_process_info->keyboard_scripts') == expected
  assert call('current_process_info->keyboard_unicode') == 0
 for address, count in [(0, 1), (0xffffffff, 2), (0, -1)]:
  before = call('current_process_info->keyboard_scripts')
  mem.write_memory(storage, struct.pack('>IIIhI', 0x123456, 0x8006ffae, address, count, 0xdeadbeef))
  gdb.execute('set cpu_state.regs[15].ul.n=' + str(storage - base), to_string=True)
  assert call('(unsigned int)_ScriptUtil(0,0)') == 0x123456
  assert call('cpu_state.regs[15].ul.n') == storage - base + 14
  assert bytes(mem.read_memory(storage + 14, 4)) == b'\xde\xad\xbe\xef'
  assert call('current_process_info->keyboard_scripts') == (1 if count < 0 else before)
 print('PASS: KeyScriptEnable Pascal stack, byte order, Roman fallback, script masks and invalid lists', flush=True)
finally:
 mem.write_memory(cpu, saved)
 gdb.execute('set current_process_info->keyboard_scripts=' + str(mask), to_string=True)
 gdb.execute('set current_process_info->keyboard_unicode=' + str(unicode), to_string=True)
 gdb.execute('call (void)DisposPtr((void *)' + str(storage) + ')', to_string=True)
end
quit 0
"""
    script = script.replace("APPLICATION", json.dumps(application.replace("\\", "/")))
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "probe.gdb").write_text(script.lstrip(), encoding="utf-8")


if __name__ == "__main__":
    generate(Path(sys.argv[1]), sys.argv[2])
