/*+===================================================================
  File:      call.S

  Summary:   Original PPC SysV-to-classic transition-vector bridge, eight word arguments. Keep the classic 24-byte
             linkage area and 32-byte argument save area.

  Classes:   None.

  Functions: remote_call

  Origin:    Existing repository source in tools/classic-remote; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Original PPC SysV-to-classic transition-vector bridge, eight word arguments.
   Keep the classic 24-byte linkage area and 32-byte argument save area. */
        .text
        .globl remote_call
        .type remote_call,@function
remote_call:
        mflr 0
        stwu 1,-80(1)
        stw 0,84(1)
        stw 2,20(1)
        lwz 0,0(3)
        lwz 2,4(3)
        mtctr 0
        mr 11,4
        lwz 3,0(11)
        lwz 4,4(11)
        lwz 5,8(11)
        lwz 6,12(11)
        lwz 7,16(11)
        lwz 8,20(11)
        lwz 9,24(11)
        lwz 10,28(11)
        bctrl
        lwz 2,20(1)
        lwz 0,84(1)
        addi 1,1,80
        mtlr 0
        blr
        .size remote_call,.-remote_call
        .section .note.GNU-stack,"",@progbits
