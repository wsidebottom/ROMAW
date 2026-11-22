/*+===================================================================
  File:      code_fragments.h

  Summary:   Native Carbon Code Fragment Manager argument and result handling.

  Classes:   None.

  Functions: Private frame adapters for the Carbon export registry.

  Origin:    Universal Interfaces 3.4.2 CodeFragments.h and CarbonLib 1.4 InterfaceLib re-exports.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_CODE_FRAGMENTS_H
#define ROMAW_CARBON_CODE_FRAGMENTS_H

#include "cf_batch.h"
#include "rsys/cfm.h"

static int carbon_cfm_result(ROMlib_carbon_ppc_frame* call, int32_t error)
{
    call->words[0] = error;
    return 1;
}

static const unsigned char* carbon_cfm_pascal(uint32_t address, unsigned maximum)
{
    const unsigned char* length = address ? ROMlib_carbon_try_guest_memory(address, 1, 0) : NULL;
    return length && *length <= maximum ? ROMlib_carbon_try_guest_memory(address, *length + 1, 0) : NULL;
}

static void* carbon_cfm_output(uint32_t address, size_t size)
{
    return address ? ROMlib_carbon_try_guest_memory(address, size, 1) : NULL;
}

static int carbon_cfm_load_outputs(ROMlib_carbon_ppc_frame* call, const char* operation, unsigned connection,
                                   const unsigned char* name)
{
    if (!carbon_cfm_output(call->words[connection], 4) ||
        (call->words[connection + 1] && !carbon_cfm_output(call->words[connection + 1], 4)))
        return carbon_cfm_result(call, paramErr);
    const uint32_t message = call->words[connection + 2];
    unsigned char detail[64] = {0};
    if (name)
        memcpy(detail, name, *name + 1);
    unsigned char* output = message ? carbon_cfm_output(message, detail[0] + 1) : NULL;
    if (message && !output)
        return carbon_cfm_result(call, paramErr);

    if (!ROMlib_carbon_call_cfm(operation, call))
        return 0;
    if (output && (int32_t)call->words[0] == fragLibNotFound)
        memcpy(output, detail, detail[0] + 1);
    return 1;
}

static int carbon_GetSharedLibrary(ROMlib_carbon_ppc_frame* call)
{
    const unsigned char* name = carbon_cfm_pascal(call->words[0], 63);
    return name && *name ? carbon_cfm_load_outputs(call, "GetSharedLibrary", 3, name)
                         : carbon_cfm_result(call, paramErr);
}

static int carbon_GetDiskFragment(ROMlib_carbon_ppc_frame* call)
{
    const unsigned char* name = carbon_cfm_pascal(call->words[3], 63);
    if ((call->words[3] && !name) || !call->words[0] ||
        !ROMlib_carbon_try_guest_memory(call->words[0], sizeof(FSSpec), 0))
        return carbon_cfm_result(call, paramErr);
    return carbon_cfm_load_outputs(call, "GetDiskFragment", 5, name);
}

static int carbon_GetMemFragment(ROMlib_carbon_ppc_frame* call)
{
    const unsigned char* name = carbon_cfm_pascal(call->words[2], 63);
    const uint32_t extent = call->words[1] ? call->words[1] : 40;
    const void* bytes = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], extent, 0) : NULL;
    if ((call->words[2] && !name) || !bytes)
        return carbon_cfm_result(call, paramErr);
    if (extent < 40 || memcmp(bytes, "Joy!peff", 8))
        return carbon_cfm_result(call, -2806); /* cfragFragmentFormatErr */
    return carbon_cfm_load_outputs(call, "GetMemFragment", 4, name);
}

static int carbon_CloseConnection(ROMlib_carbon_ppc_frame* call)
{
    if (!carbon_cfm_output(call->words[0], 4))
        return carbon_cfm_result(call, paramErr);
    return ROMlib_carbon_call_cfm("CloseConnection", call);
}

static int carbon_FindSymbol(ROMlib_carbon_ppc_frame* call)
{
    const unsigned char* name = carbon_cfm_pascal(call->words[1], 255);
    void* address = carbon_cfm_output(call->words[2], 4);
    unsigned char* symbol_class = carbon_cfm_output(call->words[3], 1);
    if (!name || !address || (call->words[3] && !symbol_class))
        return carbon_cfm_result(call, paramErr);
    uint32_t value = 0;
    unsigned kind;
    const int32_t error = ROMlib_carbon_find_symbol(call->words[0], name, &value, &kind);
    value = CL(value);
    memcpy(address, &value, 4);
    if (!error && symbol_class)
        *symbol_class = kind;
    return carbon_cfm_result(call, error);
}

static int carbon_CountSymbols(ROMlib_carbon_ppc_frame* call)
{
    if (!carbon_cfm_output(call->words[1], 4))
        return carbon_cfm_result(call, paramErr);
    return ROMlib_carbon_call_cfm("CountSymbols", call);
}

static int carbon_GetIndSymbol(ROMlib_carbon_ppc_frame* call)
{
    if (!carbon_cfm_output(call->words[2], 256) || !carbon_cfm_output(call->words[3], 4) ||
        !carbon_cfm_output(call->words[4], 1))
        return carbon_cfm_result(call, paramErr);
    return ROMlib_carbon_call_cfm("GetIndSymbol", call);
}

#endif
