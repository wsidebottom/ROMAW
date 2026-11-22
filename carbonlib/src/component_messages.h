/*+===================================================================
  File:      component_messages.h

  Summary:   Dispatch scripting, sound and text-service messages to registered components.

  Classes:   None.

  Functions: Private PowerPC frame adapters for the Carbon export registry.

  Origin:    CarbonLib 1.4 PEF exports and Universal Interfaces 3.4.2 OSA.h,
             OSAGeneric.h, ASDebugging.h, AppleScript.h, Sound.h and TextServices.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_COMPONENT_MESSAGES_H
#define ROMAW_CARBON_COMPONENT_MESSAGES_H

#include "component_entries.h"

#define CARBON_TYPED_COMPONENT(name, selector, layout, reference_library)       \
    static int carbon_component_##name(ROMlib_carbon_ppc_frame* call)           \
    {                                                                           \
        if (!ROMlib_component_can_dispatch(call->words[0])) {                   \
            ROMlib_carbon_reference saved;                                      \
            if (ROMlib_carbon_load_reference(reference_library, #name, &saved)) \
                return ROMlib_carbon_invoke_frame(saved.function, call);        \
        }                                                                       \
        return carbon_component_message(call, selector, layout);                \
    }

/* AppleScriptLib */
CARBON_TYPED_COMPONENT(OSALoad, 0x0001, 0444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAStore, 0x0002, 04444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAExecute, 0x0003, 04444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSADisplay, 0x0004, 04444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAScriptError, 0x0005, 0444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSADispose, 0x0006, 04, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSASetScriptInfo, 0x0007, 0444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAGetScriptInfo, 0x0008, 0444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSASetActiveProc, 0x0009, 044, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAGetActiveProc, 0x000a, 044, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAScriptingComponentName, 0x0102, 04, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSACompile, 0x0103, 0444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSACopyID, 0x0104, 044, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAGetSource, 0x0201, 0444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSACoerceFromDesc, 0x0301, 0444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSACoerceToDesc, 0x0302, 04444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSASetSendProc, 0x0401, 044, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAGetSendProc, 0x0402, 044, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSASetCreateProc, 0x0403, 044, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAGetCreateProc, 0x0404, 044, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSASetDefaultTarget, 0x0405, 04, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAStartRecording, 0x0501, 04, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAStopRecording, 0x0502, 04, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSALoadExecute, 0x0601, 04444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSACompileExecute, 0x0602, 04444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSADoScript, 0x0603, 044444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSASetCurrentDialect, 0x0701, 02, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAGetCurrentDialect, 0x0702, 04, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAAvailableDialects, 0x0703, 04, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAGetDialectInfo, 0x0704, 0244, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAAvailableDialectCodeList, 0x0705, 04, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSASetResumeDispatchProc, 0x0801, 044, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAGetResumeDispatchProc, 0x0802, 044, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAExecuteEvent, 0x0803, 04444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSADoEvent, 0x0804, 04444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAMakeContext, 0x0805, 0444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSADebuggerCreateSession, 0x0901, 0444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSADebuggerGetSessionState, 0x0902, 044, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSADebuggerSessionStep, 0x0903, 044, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSADebuggerDisposeSession, 0x0904, 04, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSADebuggerGetStatementRanges, 0x0905, 044, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSADebuggerGetBreakpoint, 0x0910, 0444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSADebuggerSetBreakpoint, 0x0911, 0444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSADebuggerGetDefaultBreakpoint, 0x0912, 044, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSADebuggerGetCurrentCallFrame, 0x0906, 044, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSADebuggerGetCallFrameState, 0x0907, 044, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSADebuggerGetVariable, 0x0908, 0444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSADebuggerSetVariable, 0x0909, 0444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSADebuggerGetPreviousCallFrame, 0x090a, 044, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSADebuggerDisposeCallFrame, 0x090b, 04, "AppleScriptLib")
CARBON_TYPED_COMPONENT(ASInit, 0x1001, 04444444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(ASSetSourceStyles, 0x1002, 04, "AppleScriptLib")
CARBON_TYPED_COMPONENT(ASGetSourceStyles, 0x1003, 04, "AppleScriptLib")
CARBON_TYPED_COMPONENT(ASGetSourceStyleNames, 0x1004, 044, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAGetDefaultScriptingComponent, 0x1001, 04, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSASetDefaultScriptingComponent, 0x1002, 04, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAGetScriptingComponent, 0x1003, 044, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAGetScriptingComponentFromStored, 0x1004, 044, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAGenericToRealID, 0x1005, 044, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSARealToGenericID, 0x1006, 044, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSASetProperty, 0x1106, 04444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAGetProperty, 0x1107, 04444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAGetPropertyNames, 0x110c, 0444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSASetHandler, 0x1108, 04444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAGetHandler, 0x1109, 04444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAGetHandlerNames, 0x110d, 0444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAGetAppTerminology, 0x110a, 044244, "AppleScriptLib")
CARBON_TYPED_COMPONENT(OSAGetSysTerminology, 0x110b, 0424, "AppleScriptLib")
CARBON_TYPED_COMPONENT(ASSetProperty, 0x1101, 0444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(ASGetProperty, 0x1102, 0444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(ASSetHandler, 0x1103, 0444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(ASGetHandler, 0x1104, 0444, "AppleScriptLib")
CARBON_TYPED_COMPONENT(ASGetAppTerminology, 0x1105, 04244, "AppleScriptLib")

/* SoundLib */
CARBON_TYPED_COMPONENT(SoundComponentInitOutputDevice, 0x0001, 04, "SoundLib")
CARBON_TYPED_COMPONENT(SoundComponentSetSource, 0x0002, 044, "SoundLib")
CARBON_TYPED_COMPONENT(SoundComponentGetSource, 0x0003, 044, "SoundLib")
CARBON_TYPED_COMPONENT(SoundComponentGetSourceData, 0x0004, 04, "SoundLib")
CARBON_TYPED_COMPONENT(SoundComponentSetOutput, 0x0005, 044, "SoundLib")
CARBON_TYPED_COMPONENT(SoundComponentAddSource, 0x0101, 04, "SoundLib")
CARBON_TYPED_COMPONENT(SoundComponentRemoveSource, 0x0102, 04, "SoundLib")
CARBON_TYPED_COMPONENT(SoundComponentGetInfo, 0x0103, 0444, "SoundLib")
CARBON_TYPED_COMPONENT(SoundComponentSetInfo, 0x0104, 0444, "SoundLib")
CARBON_TYPED_COMPONENT(SoundComponentStartSource, 0x0105, 024, "SoundLib")
CARBON_TYPED_COMPONENT(SoundComponentStopSource, 0x0106, 024, "SoundLib")
CARBON_TYPED_COMPONENT(SoundComponentPauseSource, 0x0107, 024, "SoundLib")
CARBON_TYPED_COMPONENT(SoundComponentPlaySourceBuffer, 0x0108, 0444, "SoundLib")

/* TextServices */
CARBON_TYPED_COMPONENT(GetScriptLanguageSupport, 0x0001, 04, "CarbonLib")
CARBON_TYPED_COMPONENT(InitiateTextService, 0x0002, 0, "CarbonLib")
CARBON_TYPED_COMPONENT(TerminateTextService, 0x0003, 0, "CarbonLib")
CARBON_TYPED_COMPONENT(ActivateTextService, 0x0004, 0, "CarbonLib")
CARBON_TYPED_COMPONENT(DeactivateTextService, 0x0005, 0, "CarbonLib")
CARBON_TYPED_COMPONENT(FixTextService, 0x0009, 0, "CarbonLib")
CARBON_TYPED_COMPONENT(HidePaletteWindows, 0x000b, 0, "CarbonLib")
CARBON_TYPED_COMPONENT(GetTextServiceProperty, 0x000c, 044, "CarbonLib")
CARBON_TYPED_COMPONENT(SetTextServiceProperty, 0x000d, 044, "CarbonLib")

#undef CARBON_TYPED_COMPONENT

#endif
