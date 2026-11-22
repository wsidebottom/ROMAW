/*+===================================================================
  File:      link-a7-reference.s

  Summary:   Observe LINK.W/L A7 source capture independently of ROMAW.

  Classes:   None.

  Functions: Linux m68k _start entry point.

  Origin:    Original regression fixture; uses Linux system calls for output.

  Copyright and Legal notices: Applicable repository license: syn68k/COPYING.
===================================================================+*/
.text
.globl _start
_start:
    lea arena+256,%sp
    move.l %sp,%d4
    link.w %a7,#-16
    move.l %d4,%a0
    move.l -4(%a0),%d0
    sub.l %d4,%d0
    move.l %d0,out
    move.l %sp,%d0
    sub.l %d4,%d0
    move.l %d0,out+4
    lea arena+256,%sp
    move.l %sp,%d4
    link.l %a7,#-16
    move.l %d4,%a0
    move.l -4(%a0),%d0
    sub.l %d4,%d0
    move.l %d0,out+8
    move.l %sp,%d0
    sub.l %d4,%d0
    move.l %d0,out+12
    moveq #4,%d0
    moveq #1,%d1
    move.l #out,%d2
    moveq #16,%d3
    trap #0
    moveq #1,%d0
    moveq #0,%d1
    trap #0
.data
.balign 4
out: .space 16
.bss
.balign 4
arena: .space 512
