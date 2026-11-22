# +===================================================================
#   File:      gworld_output_probe.gdb
#
#   Summary:   Provides gworld output probe debugger commands for regression tests and test fixtures.
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
break C_WaitNextEvent
run
delete breakpoints
set $out=(unsigned char *)_NewPtr_flags(24,0,1)
set $rect=(Rect *)($out+16)
set $out[4]=0x12
set $out[5]=0x34
set $out[6]=0x56
set $out[7]=0x78
set $rect->top=0
set $rect->left=0
set $rect->bottom=0x2000
set $rect->right=0x2000
set $error=C_NewGWorld((HIDDEN_GWorldPtr *)$out,24,$rect,0,0,0)
if $error || $out[4]!=0x12 || $out[5]!=0x34 || $out[6]!=0x56 || $out[7]!=0x78
printf "NEW_GWORLD_GUARD_FAILED error=%d\n",$error
quit 1
end
set $guest=(unsigned int)($out[0]<<24 | $out[1]<<16 | $out[2]<<8 | $out[3])
set $world=(GWorldPtr)syn68k_guest_to_host($guest)
set $pmh=(unsigned char *)C_GetGWorldPixMap($world)
set $pmguest=(unsigned int)($pmh[0]<<24 | $pmh[1]<<16 | $pmh[2]<<8 | $pmh[3])
set $pm=(unsigned char *)syn68k_guest_to_host($pmguest)
if $pm[30]!=0 || $pm[31]!=16 || $pm[32]!=0 || $pm[33]!=32 || $pm[34]!=0 || $pm[35]!=3 || $pm[36]!=0 || $pm[37]!=8
printf "NEW_GWORLD_24_RGB_FORMAT_FAILED\n"
quit 1
end
set $flags=C_UpdateGWorld((HIDDEN_GWorldPtr *)$out,24,$rect,0,0,0)
if $flags != 0
printf "UPDATE_GWORLD_24_ALIAS_CHANGED_DEPTH flags=%x\n",$flags
quit 1
end
set $rect->right=0x4000
set $flags=C_UpdateGWorld((HIDDEN_GWorldPtr *)$out,32,$rect,0,0,0)
if $flags != 0x180000
printf "UPDATE_GWORLD_RESIZE_FAILED flags=%x\n",$flags
quit 1
end
if $out[4]!=0x12 || $out[5]!=0x34 || $out[6]!=0x56 || $out[7]!=0x78
printf "UPDATE_GWORLD_GUARD_FAILED\n"
quit 1
end
set $guest=(unsigned int)($out[0]<<24 | $out[1]<<16 | $out[2]<<8 | $out[3])
set $world=(GWorldPtr)syn68k_guest_to_host($guest)
call C_DisposeGWorld($world)
call DisposPtr($out)
printf "LIVE_NEW_UPDATE_GWORLD_GUARDS_OK flags=%x\n",$flags
quit 0
