# +===================================================================
#   File:      application_palette_probe.gdb
#
#   Summary:   Run an isolated color application to its event loop, then source this file. This exercises the real
#              Palette Manager and the display's indexed CLUT.
#
#   Classes:   None.
#
#   Functions: Debugger command sequence; no exported functions.
#
#   Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

# Run an isolated color application to its event loop, then source this file.
# This exercises the real Palette Manager and the display's indexed CLUT.
python
import gdb
import struct

inferior = gdb.selected_inferior()
base = int(gdb.parse_and_eval('ROMlib_offset'))

def call(expression):
    return int(gdb.parse_and_eval(expression))

def invoke(expression):
    gdb.execute('call ' + expression, to_string=True)

def read_pointer(address):
    guest = int.from_bytes(inferior.read_memory(address, 4), 'big')
    return base + guest

def display_color(index):
    device = read_pointer(read_pointer(base + 0x8a4))
    pixmap = read_pointer(read_pointer(device + 22))
    table = read_pointer(read_pointer(pixmap + 42))
    return struct.unpack('>3H', inferior.read_memory(table + 8 + index * 8 + 2, 6))

front = call('(void *)C_FrontWindow()')
assert front, 'The fixture needs a front window'
rgb = call('(void *)_NewPtr_flags(6,1,0)')
default = call('(void *)C_NewPalette(8,0,12,0)')
explicit = call('(void *)C_NewPalette(8,0,12,0)')
assert rgb and default and explicit

def set_color(palette, color):
    inferior.write_memory(rgb, struct.pack('>3H', *color))
    invoke('C_SetEntryColor((void *)%d,7,(void *)%d)' % (palette, rgb))

first = (0x1234, 0x5678, 0x9abc)
second = (0x2345, 0x6789, 0xabcd)
override = (0x3456, 0x789a, 0xbcde)
set_color(default, first)
invoke('C_SetPalette((void *)-1,(void *)%d,1)' % default)
assert display_color(7) == first, 'Application palette was not activated'
# Reassigning the same application palette must publish edited colors too.
set_color(default, second)
invoke('C_NSetPalette((void *)-1,(void *)%d,0xe000)' % default)
assert display_color(7) == second, 'Reassigned application palette stayed stale'
# A window-specific palette must retain precedence over the application palette.
set_color(explicit, override)
invoke('C_SetPalette((void *)%d,(void *)%d,1)' % (front, explicit))
assert display_color(7) == override
set_color(default, first)
invoke('C_SetPalette((void *)-1,(void *)%d,1)' % default)
assert display_color(7) == override, 'Default palette replaced window palette'
print('APPLICATION_PALETTE_OK: activation, replacement and window precedence')
end
