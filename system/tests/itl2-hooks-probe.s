/*+===================================================================
  File:      itl2-hooks-probe.s

  Summary:   Original 68000 callbacks for testing ROMAW's itl2 register/frame ABI.

  Classes:   None.

  Functions: Local assembly entry points; see labels below.

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Original 68000 callbacks for testing ROMAW's itl2 register/frame ABI. */
        .text
start:
        .word init-start,fetch-start,vernier-start,project-start
        .word 0xffff,finish-start
        .org 64
init:
        addq.l #1,init_count-start(%a4)
        move.b #0x5a,-54(%a6)
        rts
fetch:
        addq.l #1,fetch_count-start(%a4)
        cmpi.w #0x2300,%d4
        bne.s 1f
        move.w #0x5a00,%d4
1:      rts
vernier:
        addq.l #1,vernier_count-start(%a4)
        tst.w vernier_mode-start(%a4)
        beq.s 2f
        not.b %d4
        moveq #1,%d0
        rts
2:      moveq #0,%d0
        rts
project:
        addq.l #1,project_count-start(%a4)
        move.w project_mode-start(%a4),%d0
        cmpi.w #1,%d0
        beq.s 3f
        cmpi.w #2,%d0
        beq.s 4f
        moveq #0,%d0
        rts
3:      eori.w #0xff00,%d4
        moveq #1,%d0
        rts
4:      cmpi.w #0x6100,%d4
        blo.s 5f
        cmpi.w #0x7a00,%d4
        bhi.s 5f
        subi.w #0x2000,%d4
5:      moveq #1,%d0
        rts
finish:
        addq.l #1,exit_count-start(%a4)
        cmpi.b #0x5a,-54(%a6)
        beq.s 6f
        move.w #0x7ffe,20(%a6)
6:      tst.w exit_mode-start(%a4)
        beq.s 7f
        neg.w 20(%a6)
7:      rts
        .org 384
invoke_pack6:
        subq.l #2,%sp
        move.l %a0,-(%sp)
        move.l %a1,-(%sp)
        move.w %d0,-(%sp)
        move.w %d1,-(%sp)
        move.l %a2,-(%sp)
        move.w #0x1a,-(%sp)
        .word 0xa9ed
        move.w (%sp)+,%d0
        ext.l %d0
        rts
        .org 512
project_mode:   .word 0
vernier_mode:   .word 0
exit_mode:      .word 0
               .word 0
init_count:     .long 0
fetch_count:    .long 0
vernier_count:  .long 0
project_count:  .long 0
exit_count:     .long 0
