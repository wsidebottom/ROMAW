# +===================================================================
#   File:      create-finder-quit-probe.py
#
#   Summary:   Check Finder's host File/Quit item and its actual clean process exit.
#
#   Classes:   None declared.
#
#   Functions: generate
#
#   Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Check Finder's host File/Quit item and its actual clean process exit."""

import json
from pathlib import Path
import sys


def generate(directory, application, mode):
    assert mode in ("key", "selection")
    body = r"""
import gdb
memory=gdb.selected_inferior()
offset=int(gdb.parse_and_eval('ROMlib_offset'))
gdb.execute('call C_DrawMenuBar()',to_string=True)
menu=int(gdb.parse_and_eval('(void *)finder_file_menu'))
assert menu, 'Finder File menu was not found'
count=int(gdb.parse_and_eval('(int)C_CountMItems((void *)'+str(menu)+')'))
for repeat in range(3):
 gdb.execute('call C_DrawMenuBar()',to_string=True)
 assert int(gdb.parse_and_eval('(int)C_CountMItems((void *)'+str(menu)+')'))==count
name=int(gdb.parse_and_eval('(void *)_NewPtr_flags(256,1,1)'))
gdb.execute('call C_GetItem((void *)'+str(menu)+','+str(count)+',(void *)'+str(name)+')',to_string=True)
assert bytes(memory.read_memory(name,5))==b'\x04Quit'
record=int.from_bytes(memory.read_memory(menu,4),'big')+offset
menu_id=int.from_bytes(memory.read_memory(record,2),'big',signed=True)
flags=int.from_bytes(memory.read_memory(record+10,4),'big')
assert flags&1 and flags&(1<<count), 'Quit must be enabled'
selection=(menu_id<<16)|count
creator=int(gdb.parse_and_eval('ROMlib_creator'))
gdb.execute('set ROMlib_creator=0')
assert int(gdb.parse_and_eval('(int)finder_quit_selection('+str(selection)+')'))==0
gdb.execute('set ROMlib_creator='+str(creator))
exits=[]
def exited(event):
 exits.append(event.exit_code)
gdb.events.exited.connect(exited)
exit_routes=[]
class ExitRoute(gdb.Breakpoint):
 def stop(self):
  frame=gdb.newest_frame()
  host_menu=False
  while frame:
   if frame.name()=='finder_quit_selection': host_menu=True
   frame=frame.older()
  exit_routes.append(host_menu)
  return False
ExitRoute('C_ExitToShell',internal=True)
expression='(int)C_MenuKey(81)' if MODE=='key' else '(int)finder_quit_selection('+str(selection)+')'
try:
 gdb.parse_and_eval(expression)
except gdb.error:
 pass
if not exits:
 # Finder saves its session when it processes the queued core quit event.
 gdb.execute('continue')
assert exits==[0], ('Finder did not exit cleanly',exits)
assert exit_routes==[False], ('Host menu bypassed Finder shutdown',exit_routes)
print('FINDER_FILE_QUIT_OK '+MODE)
"""
    body = body.replace("MODE", repr(mode))
    script = [
        "set pagination off",
        "set confirm off",
        "set print thread-events off",
        "break ROMlib_win_drive_preferences",
        "commands",
        "silent",
        "return 0",
        "continue",
        "end",
        "break C_WaitNextEvent",
        f"run -system 7.5.5 -applzone 8m {json.dumps(application.replace(chr(92), '/'))}",
        "disable 1 2",
        "python",
        body.strip(),
        "end",
        "quit 0",
    ]
    (directory / "probe.gdb").write_text("\n".join(script) + "\n")


if __name__ == "__main__":
    destination = Path(sys.argv[1]).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    generate(destination, sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else "key")
