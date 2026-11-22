# +===================================================================
#   File:      document_transition_probe.gdb
#
#   Summary:   Provides document transition probe debugger commands for regression tests and test fixtures.
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
break ROMlib_sdl_select_application
run
disable 1
call ROMlib_set_mmu_mode(1)
call ROMlib_resize_screen(1024,1021)
set $screen=(unsigned long long)ROMlib_guest_framebuffer-ROMlib_offset
if vdriver_height != 1021 || $screen+(unsigned long long)vdriver_row_bytes*vdriver_height <= 0x1000000
printf "LARGE_FINDER_DISPLAY_FIXTURE_FAILED\n"
quit 1
end
finish
call sdl_queue_window_size(640,480)
break ROMlib_set_mmu_mode if mode32==0
continue
if vdriver_width != 640 || vdriver_height != 480
printf "APPLICATION_DISPLAY_NOT_APPLIED_BEFORE_24BIT_CHECK\n"
quit 1
end
finish
if syn68k_address_mask != 0x00ffffff
printf "APPLICATION_DID_NOT_ENTER_24BIT_MODE\n"
quit 1
end
break C_GetNextEvent
continue
if syn68k_address_mask != 0x00ffffff
quit 1
end
printf "FINDER_TO_24BIT_DOCUMENT_TRANSITION_OK\n"
quit 0
