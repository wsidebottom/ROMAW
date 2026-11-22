/*+===================================================================
  File:      fpu-oracle.S

  Summary:   Implements fpu oracle support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: fpu_oracle

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* The same original instruction bytes execute in QEMU and Syn68k. */
.text
.globl fpu_oracle
.type fpu_oracle,@function
.macro snapshot
  fmove.l %fpsr,12(%a0)
  fmove.x %fp0,(%a0)
  lea 16(%a0),%a0
.endm
fpu_oracle:
  move.l %a2,-(%sp)
  move.l 8(%sp),%a0
  fmove.l #0,%fpcr
  fmove.l #7,%fp0
  fmove.l #3,%fp1
  fadd.x %fp1,%fp0
  snapshot
  fsub.x %fp1,%fp0
  snapshot
  fmul.x %fp1,%fp0
  snapshot
  fdiv.x %fp1,%fp0
  snapshot
  fsqrt.x %fp0,%fp0
  snapshot
  fneg.x %fp0,%fp0
  snapshot
  fabs.x %fp0,%fp0
  snapshot
  .irp rounding,0,16,32,48,64,80,96,112,128,144,160,176
    fmove.l #\rounding,%fpcr
    fmove.x oneplus(%pc),%fp0
    snapshot
    fabs.x %fp0,%fp0
    snapshot
    fneg.x %fp0,%fp0
    snapshot
    fmove.x one(%pc),%fp0
    fmove.x small(%pc),%fp1
    fadd.x %fp1,%fp0
    snapshot
    fmove.x negative(%pc),%fp0
    fsub.x %fp1,%fp0
    snapshot
    fdiv.x %fp1,%fp0
    snapshot
  .endr
  fmove.l #0,%fpcr
  fmove.x one(%pc),%fp0
  fmove.x small(%pc),%fp1
  fsadd.x %fp1,%fp0
  snapshot
  fdadd.x %fp1,%fp0
  snapshot
  fssqrt.x %fp0,%fp0
  snapshot
  fdsqrt.x %fp0,%fp0
  snapshot
  fmove.x infinity(%pc),%fp0
  fcmp.x %fp0,%fp0
  snapshot
  fmove.x qnan(%pc),%fp0
  ftst.x %fp0
  snapshot
  fmove.x negative_zero(%pc),%fp0
  ftst.x %fp0
  snapshot
  /* Finite exponent limits. QEMU omits the 040's nonmaskable range traps;
     exceptional ranges are checked by the independent integer fixtures. */
  fmove.x large_single(%pc),%fp0
  fsadd.x %fp0,%fp0
  snapshot
  fmove.x large_double(%pc),%fp0
  fdmul.x %fp0,%fp0
  snapshot
  /* Distinct values detect reversal in predecrement FMOVEM. */
  fmove.l #17,%fp0
  fmove.l #-23,%fp1
  fmovem.x %fp0-%fp1,-(%sp)
  fmove.l #0,%fp0
  fmove.l #0,%fp1
  fmovem.x (%sp)+,%fp0-%fp1
  snapshot
  fmove.x %fp1,%fp0
  snapshot
  /* Original assembler encodings exercise brief/full indexed and indirect EA. */
  sub.l #128,%sp
  move.l %sp,%a1
  move.l #37,32(%a1)
  move.l #8,%d1
  fmove.l 16(%a1,%d1.w*2),%fp0
  snapshot
  fmove.l 0(%a1,%d1.l*4),%fp0
  snapshot
  fmove.l 32(%a1,%za2.l),%fp0
  snapshot
  lea 20(%a1),%a2
  move.l %a2,16(%a1)
  fmove.l ([16,%a1],12),%fp0
  snapshot
  move.l %a2,8(%a1)
  fmove.l ([0,%a1,%d1.l],12),%fp0
  snapshot
  fmove.l ([8,%a1],%d1.l,4),%fp0
  snapshot
  move.l #0xabcdef81,%d1
  fmove.b %d1,%fp0
  snapshot
  fmove.w %d1,%fp0
  snapshot
  fmove.s #0r1.5,%fp0
  snapshot
  fmove.d #0r-3.25,%fp0
  snapshot
  add.l #128,%sp
  move.l %a0,%d0
  sub.l 8(%sp),%d0
  move.l (%sp)+,%a2
  rts
.balign 4
one: .long 0x3fff0000,0x80000000,0
oneplus: .long 0x3fff0000,0x80000000,1
negative: .long 0xbfff0000,0x80000000,0
small: .long 0x3fc00000,0x80000000,0
infinity: .long 0x7fff0000,0x80000000,0
qnan: .long 0x7fff0000,0xc0000000,1
negative_zero: .long 0x80000000,0,0
large_single: .long 0x407d0000,0x80000000,0
large_double: .long 0x41fe0000,0x80000000,0
.section .note.GNU-stack,"",@progbits
