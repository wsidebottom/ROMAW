# +===================================================================
#   File:      macwrite-quit-probe.gdb
#
#   Summary:   Provides macwrite quit probe debugger commands for regression tests and test fixtures.
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

set pagination off
set confirm off
set print thread-events off
break C_GetNextEvent
run @APPLICATION@
disable 1
set $menu=C_GetMHandle(2)
if !$menu || C_MenuKey(113) != 0
printf "Expected MacWrite's File menu without Command-Q\n"
quit 1
end
call ROMlib_prepare_quit_menu_key()
if C_MenuKey(113) != 0x20008
printf "Host quit did not select File / Quit\n"
quit 1
end
if C_MenuKey(113) != 0
printf "Host quit leaked into a subsequent keyboard event\n"
quit 1
end
call C_DisableItem($menu,8)
call ROMlib_prepare_quit_menu_key()
if C_MenuKey(113) != 0
printf "Disabled Quit was selected\n"
quit 1
end
call C_EnableItem($menu,8)
call C_DisableItem($menu,0)
call ROMlib_prepare_quit_menu_key()
if C_MenuKey(113) != 0
printf "Disabled File menu was selected\n"
quit 1
end
call C_EnableItem($menu,0)
call C_EnableItem($menu,1)
call C_SetItemCmd($menu,1,81)
call ROMlib_prepare_quit_menu_key()
if C_MenuKey(113) != 0x20001
printf "Existing Command-Q equivalent was overridden\n"
quit 1
end
call C_SetItemCmd($menu,1,0)
call C_DisableItem($menu,1)
call ROMlib_prepare_quit_menu_key()
set $other=C_MenuKey(120)
if C_MenuKey(113) != 0
printf "Unrelated menu key retained the host quit request\n"
quit 1
end
printf "QUIT_MENU_CHECKS_OK\n"
call ROMlib_send_quit()
break C_ExitToShell
continue
printf "MACWRITE_GUEST_QUIT_OK\n"
quit 0
