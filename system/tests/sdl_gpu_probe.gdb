# +===================================================================
#   File:      sdl_gpu_probe.gdb
#
#   Summary:   Check GPU activation, software overlays, held frames, fallback and scale changes in a live guest.
#
#   Classes:   None.
#
#   Functions: Debugger command sequence; no exported functions.
#
#   Origin:    Executor SDL display regression fixture.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

# Supply a classic application using GDB set args before loading this script.
# Use -syszone 2M -applzone 4M -display-scale 200 to avoid a memory-policy restart.
set pagination off
set confirm off
set print thread-events off
break C_GetNextEvent
break C_WaitNextEvent
run
disable
call vdriver_update_screen(0,0,vdriver_height,vdriver_width,0)
if !gpu_surface_stale
printf "GPU_NOT_ACTIVE\n"
quit 1
end
call ROMlib_sdl_software_presentation(1)
if gpu_surface_stale || software_presentation != 1
printf "GPU_OVERLAY_NOT_SOFTWARE\n"
quit 1
end
call ROMlib_sdl_software_presentation(0)
call vdriver_update_screen(0,0,vdriver_height,vdriver_width,0)
if !gpu_surface_stale
quit 1
end
call sdl_gpu_reset()
set unavailable = 1
call vdriver_update_screen(0,0,1,1,0)
if gpu_surface_stale || !unavailable
printf "GPU_SOFTWARE_FALLBACK_FAILED\n"
quit 1
end
call sdl_gpu_reset()
call vdriver_update_screen(0,0,vdriver_height,vdriver_width,0)
call sdl_hold_presentation(1)
if gpu_surface_stale || !presentation_held
printf "GPU_HOLD_FAILED\n"
quit 1
end
set $saved = ((unsigned char*)screen->pixels)[0]
set $guest = vdriver_fbuf[0]
set vdriver_fbuf[0] = $guest ^ 255
call vdriver_update_screen(0,0,1,1,0)
if ((unsigned char*)screen->pixels)[0] != $saved
printf "GPU_HELD_FRAME_CHANGED\n"
quit 1
end
set vdriver_fbuf[0] = $guest
call sdl_hold_presentation(0)
call vdriver_update_screen(0,0,vdriver_height,vdriver_width,0)
if !gpu_surface_stale || software_presentation != 0
printf "GPU_RESUME_FAILED\n"
quit 1
end
set ROMlib_display_scale = 100
call vdriver_set_mode(vdriver_width,vdriver_height,vdriver_bpp,0)
call vdriver_update_screen(0,0,vdriver_height,vdriver_width,0)
if gpu_surface_stale
printf "GPU_1X_BYPASS_FAILED\n"
quit 1
end
set ROMlib_display_scale = 200
call vdriver_set_mode(vdriver_width,vdriver_height,vdriver_bpp,0)
call vdriver_update_screen(0,0,vdriver_height,vdriver_width,0)
if !gpu_surface_stale
printf "GPU_RESIZE_RESUME_FAILED\n"
quit 1
end
printf "GPU_LIVE_APP_OVERLAY_HOLD_FALLBACK_SCALE_OK\n"
quit 0
