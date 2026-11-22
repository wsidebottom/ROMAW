/*+===================================================================
  File:      ShutDown.h

  Summary:   shutdown manager define/routines

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* shutdown manager define/routines */

#define sdOnPowerOff 1
#define sdOnRestart 2
#define sdOnUnmount 4
#define sdOnDrivers 8
#define sdOnRestartOrPower (sdOnPowerOff + sdOnRestart)

extern pascal trap void C_ShutDwnPower(void);
extern pascal trap void C_ShutDwnStart(void);
extern pascal trap void C_ShutDwnInstall(ProcPtr shutdown_proc, int16 flags);
extern pascal trap void C_ShutDwnRemove(ProcPtr shutdown_proc);
