/*+===================================================================
  File:      mixed_mode.h

  Summary:   Declares mixed mode support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_mixed_mode_h_)
#define _mixed_mode_h_

#include <stdarg.h>

typedef uint8 ISAType;
typedef uint16 CallingConventionType;
typedef uint32 ProcInfoType;
typedef uint16 RegisterSelectorType;
typedef uint16 RoutineFlagsType;

enum { kM68kISA = 0, kPowerPCISA = 1 };

enum { kCFM68kRTA = 0x10 };

enum {
    kPascalStackBased,
    kCStackBased,
    kRegisterBased,
    kThinkCStackBased = 5,
    kD0DispatchedPascalStackBased = 8,
    kD0DispatchedCStackBased = 9,
    kD1DispatchedPascalStackBased = 12,
    kStackDispatchedPascalStackBased = 14,
    kSpecialCase,
};

typedef uint8 RDFlagsType;

typedef struct PACKED RoutineRecord {
    ProcInfoType procInfo;
    uint8 reserved1;
    ISAType ISA;
    RoutineFlagsType routineFlags;
    PACKED_MEMBER(ProcPtr, procDescriptor);
    uint32 reserved2;
    uint32 selector;
} RoutineRecord;

typedef struct PACKED RoutineDescriptor {
    uint16 goMixedModeTrap;
    uint8 version;
    RDFlagsType routineDescriptorFlags;
    uint32 reserved1;
    uint8 reserved2;
    uint8 selectorInfo;
    uint16 routineCount;
    RoutineRecord routineRecords[1];
} RoutineDescriptor;

enum { MIXED_MODE_TRAP = 0xAAFE };

enum { kRoutineDescriptorVersion = 7 };

enum { kSelectorsAreNotIndexable = 0 };

enum {
    kNoByteCode,
    kOneByteCode,
    kTwoByteCode,
    kFourByteCode,
};

enum { kCallingConventionWidth = 4 };

enum { kStackParameterPhase = 6 };

enum { kStackParameterWidth = 2 };

enum { kResultSizeWidth = 2 };

enum {
    kRegisterD0 = 0,
    kRegisterD1, /* 1 */
    kRegisterD2, /* 2 */
    kRegisterD3, /* 3 */
    kRegisterA0, /* 4 */
    kRegisterA1, /* 5 */
    kRegisterA2, /* 6 */
    kRegisterA3, /* 7 */
    kRegisterD4, /* 8 */
    kRegisterD5, /* 9 */
    kRegisterD6, /* 10 */
    kREgisterD7, /* 11 */
    kRegisterA4, /* 12 */
    kRegisterA5, /* 13 */
    kRegisterA6, /* 14 */
    kCCRegisterCBit = 16,
    kCCRegisterVBit, /* 17 */
    kCCRegisterZBit, /* 18 */
    kCCRegisterNBit, /* 19 */
    kCCRegisterXBit  /* 20 */
};

typedef RoutineDescriptor* UniversalProcPtr;

#define RESULT_SIZE(n) ((n) << kCallingConventionWidth)
#define STACK_ROUTINE_PARAMETER(arg, n) ((n) << (kStackParameterPhase + ((arg) - 1) * kStackParameterWidth))

extern long CallUniversalProc_from_native(UniversalProcPtr proc, ProcInfoType info, ...);

typedef enum {
    args_via_stdarg,
    args_via_68k_stack,
    args_via_68k_regs,
} where_args_t;

extern long CallUniversalProc_from_native_common(va_list ap, where_args_t where, ProcPtr proc, ProcInfoType info);

extern UniversalProcPtr C_NewRoutineDescriptor(ProcPtr proc, ProcInfoType info, ISAType isa);

extern void C_DisposeRoutineDescriptor(UniversalProcPtr ptr);

extern UniversalProcPtr C_NewFatRoutineDescriptor(ProcPtr m68k, ProcPtr ppc, ProcInfoType info);

extern OSErr C_SaveMixedModeState(void* statep, uint32 vers);

extern OSErr C_RestoreMixedModeState(void* statep, uint32 vers);

#if defined(CYGWIN32) && defined(SDL) && defined(SYN68K)
extern boolean_t ROMlib_ppc_callback_failed(void);
extern void ROMlib_ppc_event_loop_entered(void);
extern void ROMlib_ppc_set_jit(boolean_t enabled);
extern void ROMlib_ppc_set_aot(boolean_t enabled);
extern void ROMlib_ppc_set_system(boolean_t enabled);
#endif

#endif
