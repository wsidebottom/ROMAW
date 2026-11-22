/*+===================================================================
  File:      fpsp-adapter.S

  Summary:   ROMAW's flat-memory adapter, separate from Motorola's package. Exception jump operands are patched to
             the previous vector on installation.

  Classes:   None.

  Functions: fpsp_done, fpsp_fmt_error, mem_read, real_ovfl

  Origin:    Existing repository source in syn68k/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* ROMAW's flat-memory adapter, separate from Motorola's package.
   Exception jump operands are patched to the previous vector on installation. */
.text
.globl fpsp_done
fpsp_done:
  rte
.globl mem_read,mem_write
mem_read:
mem_write:
  tst.l %d0
  beq 2f
1:
  move.b (%a0)+,(%a1)+
  subq.l #1,%d0
  bne 1b
2:
  rts
.globl fpsp_fmt_error,real_bsun,real_fline,real_inex,real_operr
.globl real_ovfl,real_snan,real_trace,real_unfl
fpsp_fmt_error:
  .word 0x4ef9
  .long 0
real_bsun:
  .word 0x4ef9
  .long 0
real_fline:
  .word 0x4ef9
  .long 0
real_inex:
  .word 0x4ef9
  .long 0
real_operr:
  .word 0x4ef9
  .long 0
real_ovfl:
  .word 0x4ef9
  .long 0
real_snan:
  .word 0x4ef9
  .long 0
real_trace:
  .word 0x4ef9
  .long 0
real_unfl:
  .word 0x4ef9
  .long 0
