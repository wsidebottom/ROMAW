/*+===================================================================
  File:      agent.ld

  Summary:   Implements agent support for ROMAW build and development support.

  Classes:   None.

  Functions: No explicitly exported functions.

  Origin:    Existing repository source in tools/classic-remote; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

ENTRY(remote_main)
SECTIONS
{
  . = 0;
  .image : {
    *(.text*) *(.rodata*) *(.got2) *(.data*) *(.got*)
  }
  . = ALIGN(16);
  .bss (NOLOAD) : { *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
