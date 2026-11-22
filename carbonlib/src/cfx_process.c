/*+===================================================================
  File:      cfx_process.c

  Summary:   Implements CoreFoundation process service entries for the PowerPC bridge.

  Classes:   None.

  Functions: ROMlib_cfx_CFProcessName, ROMlib_cfx_CFProcessNameString, ROMlib_cfx_CFProcessPath

  Origin:    C reconstruction of the local CoreFoundationLib 1.4 disassembly; each entry verifies its reference offset.
             Public contracts use Universal Interfaces 3.4.2; private ABI notes are in cf-services-batch.json.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "cfx_internal.h"

static uint32 process_toc_address(const ROMlib_carbon_reference* reference, unsigned offset)
{
    return cfx_address(US_TO_SYN68K(reference->toc), offset, 4);
}

static uint32 process_copy_cstring(uint32 allocator, uint32 input)
{
    if (!input)
        return 0;
    uint32 length = 0;
    for (;;) {
        const Byte* byte = ROMlib_carbon_guest_memory(cfx_address(input, length, 1), 1, 0);
        if (!byte)
            return 0;
        if (!*byte)
            break;
        if (length == INT32_MAX - 1) {
            cfp_failure("Native Carbon process string exceeds CFIndex");
            return 0;
        }
        ++length;
    }
    uint32 output = CFW_CALL("CFAllocatorAllocate", allocator, length + 1, 0);
    if (output && !cfp_move(output, input, length + 1)) {
        CFW_CALL("CFAllocatorDeallocate", allocator, output);
        return 0;
    }
    return output;
}

int ROMlib_cfx_CFProcessName(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFProcessName", 0x197e4, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 slot = process_toc_address(&reference, 0x8b8);
    if (!cfp_read(slot)) {
        CFW_CALL("CFRetain", CFW_CALL("CFAllocatorGetDefault"));
        ROMlib_carbon_ppc_frame nested = *call_frame;
        if (!ROMlib_cfx_CFProcessPath(&nested))
            return 0;
        if (!cfp_read(slot) && !cfp_write(slot, cfx_address(cfp_toc(&reference, 0x47c), 0x19, 1)))
            return 0;
    }
    return CFX_RESULT(cfp_read(slot));
}

int ROMlib_cfx_CFProcessNameString(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFProcessNameString", 0x1983c, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 slot = process_toc_address(&reference, 0x8b4), value = cfp_read(slot);
    if (!value) {
        ROMlib_carbon_ppc_frame nested = *call_frame;
        if (!ROMlib_cfx_CFProcessName(&nested))
            return 0;
        value = CFW_CALL("CFStringCreateWithCString", CFW_CALL("CFAllocatorGetDefault"), nested.words[0],
                         ROMlib_cfstring_system_encoding());
        if (!cfp_write(slot, value))
            return 0;
    }
    return CFX_RESULT(value);
}

int ROMlib_cfx_CFProcessPath(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFProcessPath", 0x19520, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 path_slot = process_toc_address(&reference, 0x8bc);
    uint32 name_slot = process_toc_address(&reference, 0x8b8);
    uint32 path = cfp_read(path_slot);
    if (path)
        return CFX_RESULT(path);
    uint32 empty = cfx_address(cfp_toc(&reference, 0x47c), 0x19, 1);
    uint32 allocator = CFW_CALL("CFAllocatorGetDefault");
    /* The cached bytes outlive a later change of the default allocator. */
    CFW_CALL("CFRetain", allocator);
    ROMlib_carbon_ppc_frame nested = *call_frame;
    nested.words[0] = cfx_address(cfp_toc(&reference, 0x47c), 0xb, 1);
    if (!ROMlib_cfx_CFGetEnvironmentVariable(&nested))
        return 0;
    path = process_copy_cstring(allocator, nested.words[0]);
    if (!path) {
        _Alignas(256) Byte name[256] = {0};
        _Alignas(128) FSSpec spec = {0};
        _Alignas(64) ProcessInfoRec info = {0};
        _Alignas(16) ProcessSerialNumber serial = {0};
        info.processInfoLength = CL(sizeof info);
        PP_ASSIGN(info.processName, (Byte*)name);
        PP_ASSIGN(info.processAppSpec, &spec);
        if (!C_GetCurrentProcess(&serial) && !C_GetProcessInformation(&serial, &info)) {
            nested = *call_frame;
            nested.words[0] = allocator;
            nested.words[1] = US_TO_SYN68K(&spec);
            nested.words[2] = 0;
            if (!ROMlib_cfx_CFCreateURLFromFSSpec(&nested))
                return 0;
            uint32 url = nested.words[0];
            uint32 text = url ? CFW_CALL("CFURLCopyFileSystemPath", url, 0) : 0;
            if (url)
                CFW_CALL("CFRelease", url);
            if (text && (int32)CFW_CALL("CFStringGetLength", text) > 0) {
                path = CFW_CALL("CFAllocatorAllocate", allocator, 256, 0);
                nested = *call_frame;
                if (!ROMlib_cfx_CFStringFileSystemEncoding(&nested)) {
                    if (path)
                        CFW_CALL("CFAllocatorDeallocate", allocator, path);
                    CFW_CALL("CFRelease", text);
                    return 0;
                }
                if (path && !(CFW_CALL("CFStringGetCString", text, path, 256, nested.words[0]) & 255)) {
                    CFW_CALL("CFAllocatorDeallocate", allocator, path);
                    path = 0;
                }
            }
            if (text)
                CFW_CALL("CFRelease", text);
            uint32 process_name = empty;
            if (name[0]) {
                process_name = CFW_CALL("CFAllocatorAllocate", allocator, (uint32)name[0] + 1, 0);
                if (process_name) {
                    if (!cfp_move(process_name, US_TO_SYN68K(name + 1), name[0]) ||
                        !cfc_byte(cfx_address(process_name, name[0], 1), 0)) {
                        CFW_CALL("CFAllocatorDeallocate", allocator, process_name);
                        process_name = empty;
                    }
                }
                else
                    process_name = empty;
            }
            if (!cfp_write(name_slot, process_name))
                return 0;
            /* Normal lookup failure caches the reference's empty literal. The argv
               recovery is reserved for allocation failure, not an empty path. */
            if (!path)
                path = empty;
        }
        else {
            path = empty;
            if (!cfp_write(name_slot, empty))
                return 0;
        }
    }
    if (!path) {
        uint32 argv = cfp_toc(&reference, 0x31a4);
        path = process_copy_cstring(allocator, argv);
    }
    if (!path)
        path = empty;
    if (!cfp_write(path_slot, path))
        return 0;
    return CFX_RESULT(path);
}
