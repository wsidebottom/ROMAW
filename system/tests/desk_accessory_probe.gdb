# +===================================================================
#   File:      desk_accessory_probe.gdb
#
#   Summary:   Set args to a standalone desk accessory before sourcing this probe. For the legacy Control Panel, install
#              cdev files in System Folder/Control Panels.
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

# Set args to a standalone desk accessory before sourcing this probe.
# For the legacy Control Panel, install cdev files in System Folder/Control Panels.
set pagination off
set confirm off
set print thread-events off
set $ready = 0
set $opened = 0
break run_accessory
commands
silent
set $system = *(short *)(ROMlib_offset+0xa58)
set $system_map = *(unsigned int *)(ROMlib_offset+0xa54)
set $boot = *(short *)(ROMlib_offset+0x210)
set $opened = 1
continue
end
break C_WaitNextEvent
commands
silent
if $opened && ROMlib_desk_host_active && !$ready
if C_FrontWindow() == 0
printf "ACCESSORY_NO_WINDOW\n"
quit 1
end
set $ready = 1
set ROMlib_desk_host_close = 1
end
continue
end
break ROMlib_listcall
commands
silent
if mess == 1 && (rp->left == rp->right || rp->top == rp->bottom)
printf "ACCESSORY_EMPTY_CELL_DRAW\n"
quit 1
end
continue
end
break C_UseResFile
commands
silent
if $ready && !ROMlib_desk_host_active
if *(short *)(ROMlib_offset+0xa58) != $system || *(unsigned int *)(ROMlib_offset+0xa54) != $system_map || *(short *)(ROMlib_offset+0x210) != $boot
printf "ACCESSORY_CONTEXT_NOT_RESTORED\n"
quit 1
end
printf "ACCESSORY_HOST_OK: opened, closed and restored System resource/boot context\n"
quit 0
end
continue
end
break system_error
commands
silent
printf "ACCESSORY_HOST_ERROR\n"
quit 1
end
break C_ExitToShell
commands
silent
printf "ACCESSORY_CLOSED_BEFORE_READY\n"
quit 1
end
run
quit 1
