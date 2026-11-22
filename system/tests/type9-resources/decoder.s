/*+===================================================================
  File:      decoder.s

  Summary:   Implements decoder support for regression tests and test fixtures.

  Classes:   None.

  Functions: Local assembly entry points; see labels below.

  Origin:    Existing repository source in executor/tests/type9-resources; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

 .text
 .word 6,18,12
 move.l (%sp)+,%a0
 addq.l #4,%sp
 jmp (%a0)
 move.l (%sp)+,%a0
 addq.l #4,%sp
 jmp (%a0)
 link %a6,#0
 move.l %a0,-(%sp)
 move.l %a1,-(%sp)
 move.l %a2,-(%sp)
 move.l %d0,-(%sp)
 move.l 16(%a6),%a0
 move.l 12(%a6),%a1
 move.l 8(%a6),%a2
 cmpi.l #0xa89f6572,(%a2)
 bne.s done
 cmpi.l #0x00120901,4(%a2)
 bne.s done
 move.l 8(%a2),%d0
 subq.l #1,%d0
 bmi.s done
loop:
 move.b (%a0)+,(%a1)+
 subq.l #1,%d0
 bpl.s loop
done:
 move.l (%sp)+,%d0
 move.l (%sp)+,%a2
 move.l (%sp)+,%a1
 move.l (%sp)+,%a0
 unlk %a6
 move.l (%sp)+,%a0
 adda.w #12,%sp
 jmp (%a0)
