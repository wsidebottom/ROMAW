/*+===================================================================
  File:      cfw_plugins.c

  Summary:   Plug-in factory entry flow, instance layout and callback handling, and default allocator ownership.

  Classes:   None.

  Functions: ROMlib_cfw_AllocatorSetDefault, ROMlib_cfw_PlugInAddInstanceForFactory, ROMlib_cfw_PlugInCreate,
             ROMlib_cfw_PlugInFindFactoriesForPlugInType, ROMlib_cfw_PlugInFindFactoriesForPlugInTypeInPlugIn,
             ROMlib_cfw_PlugInGetBundle, ROMlib_cfw_PlugInGetTypeID, ROMlib_cfw_PlugInInstanceCreate,
             ROMlib_cfw_PlugInInstanceCreateWithInstanceDataSize, ROMlib_cfw_PlugInInstanceGetFactoryName,
             ROMlib_cfw_PlugInInstanceGetInstanceData, ROMlib_cfw_PlugInInstanceGetInterfaceFunctionTable,
             ROMlib_cfw_PlugInInstanceGetTypeID, ROMlib_cfw_PlugInIsLoadOnDemand,
             ROMlib_cfw_PlugInRegisterFactoryFunction, ROMlib_cfw_PlugInRegisterFactoryFunctionByName,
             ROMlib_cfw_PlugInRegisterPlugInType, ROMlib_cfw_PlugInRemoveInstanceForFactory,
             ROMlib_cfw_PlugInSetLoadOnDemand, ROMlib_cfw_PlugInUnregisterFactory, ROMlib_cfw_PlugInUnregisterPlugInType

  Origin:    C reconstruction of local CoreFoundationLib 1.4 exports 0x153d8..0x173f4. Factory registry and dynamic
             load/unload workers remain PPC.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "cfw_internal.h"

static uint32 plugin_private(const ROMlib_carbon_reference* reference, uint32 offset, const uint32* arguments,
                             unsigned count)
{
    uint32 result = 0;
    cfp_private(reference, offset, arguments, count, &result);
    return result;
}

static unsigned plugin_byte(uint32 address)
{
    const unsigned char* memory = ROMlib_carbon_guest_memory(address, 1, 0);
    return memory ? *memory : 0;
}

static uint32 factory(const ROMlib_carbon_reference* reference, uint32 uuid)
{
    uint32 lookup[] = {uuid, 1};
    return plugin_private(reference, 0x158d0, lookup, 2);
}

static uint32 factories(const ROMlib_carbon_reference* reference, uint32 type, uint32 plugin, int filter)
{
    uint32 list = plugin_private(reference, 0x15fe4, &type, 1);
    if (!list)
        return 0;
    uint32 count = CFW_CALL("CFArrayGetCount", list);
    uint32 result = CFW_CALL("CFArrayCreateMutable", 0, 0, US_TO_SYN68K(reference->toc) + 0x3234);
    for (int32 i = 0; i < (int32)count; ++i) {
        uint32 item = CFW_CALL("CFArrayGetValueAtIndex", list, (uint32)i);
        if (!filter || cfp_read(item + 20) == plugin)
            CFW_CALL("CFArrayAppendValue", result, cfp_read(item + 4));
    }
    return result;
}

uint32 ROMlib_cfw_PlugInGetTypeID(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (!cfp_reference("CFPlugInGetTypeID", 0x169ac, NULL, 0, &reference, &result))
        return result;
    return cfc_public("CFBundleGetTypeID", NULL, 0);
}

uint32 ROMlib_cfw_PlugInCreate(uint32 allocator, uint32 url)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, url}, result = 0;
    if (!cfp_reference("CFPlugInCreate", 0x169d0, arguments, 2, &reference, &result))
        return result;
    return CFW_CALL("CFBundleCreate", allocator, url);
}

uint32 ROMlib_cfw_PlugInGetBundle(uint32 plugin)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {plugin}, result = 0;
    if (!cfp_reference("CFPlugInGetBundle", 0x169f4, arguments, 1, &reference, &result))
        return result;
    return plugin;
}

uint32 ROMlib_cfw_PlugInInstanceGetTypeID(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (!cfp_reference("CFPlugInInstanceGetTypeID", 0x1619c, NULL, 0, &reference, &result))
        return result;
    return cfp_toc(&reference, 0x894);
}

uint32 ROMlib_cfw_PlugInInstanceGetInstanceData(uint32 instance)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {instance}, result = 0;
    if (!cfp_reference("CFPlugInInstanceGetInstanceData", 0x162d8, arguments, 1, &reference, &result))
        return result;
    return instance + 20;
}

uint32 ROMlib_cfw_PlugInInstanceGetFactoryName(uint32 instance)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {instance}, result = 0;
    if (!cfp_reference("CFPlugInInstanceGetFactoryName", 0x162b0, arguments, 1, &reference, &result))
        return result;
    return cfp_read(cfp_read(instance + 8) + 4);
}

uint32 ROMlib_cfw_PlugInInstanceGetInterfaceFunctionTable(uint32 instance, uint32 name, uint32 output)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {instance, name, output}, result = 0;
    if (!cfp_reference("CFPlugInInstanceGetInterfaceFunctionTable", 0x16244, arguments, 3, &reference, &result))
        return result;
    uint32 procedure = cfp_read(instance + 12);
    _Alignas(16) uint32 table = 0;
    if (procedure) {
        uint32 callback[] = {instance, name, US_TO_SYN68K(&table)};
        cfp_callback(procedure, callback, 3, &result);
        result &= 255;
    }
    if (output)
        cfp_write(output, result ? CL(table) : 0);
    return result;
}

uint32 ROMlib_cfw_PlugInInstanceCreateWithInstanceDataSize(uint32 allocator, uint32 size, uint32 deallocate,
                                                           uint32 name, uint32 get_interface)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, size, deallocate, name, get_interface}, result = 0;
    if (!cfp_reference("CFPlugInInstanceCreateWithInstanceDataSize", 0x161a4, arguments, 5, &reference, &result))
        return result;
    if ((int32)size < 0 || size > INT32_MAX - 12) {
        cfp_failure("CFPlugInInstance data extent is invalid");
        return 0;
    }
    result = cfc_allocate_instance(&reference, allocator, cfp_toc(&reference, 0x894), size + 12);
    if (!result)
        return 0;
    uint32 item = factory(&reference, name);
    cfp_write(result + 8, item);
    if (item)
        plugin_private(&reference, 0x16050, &item, 1);
    cfp_write(result + 12, get_interface);
    cfp_write(result + 16, deallocate);
    return result;
}

uint32 ROMlib_cfw_PlugInIsLoadOnDemand(uint32 plugin)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {plugin}, result = 0;
    if (!cfp_reference("CFPlugInIsLoadOnDemand", 0x16acc, arguments, 1, &reference, &result))
        return result;
    return plugin_byte(plugin + 0x4c) ? plugin_byte(plugin + 0x4d) : 0;
}

void ROMlib_cfw_PlugInSetLoadOnDemand(uint32 plugin, uint32 flag)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {plugin, flag}, result = 0;
    if (!cfp_reference("CFPlugInSetLoadOnDemand", 0x169f8, arguments, 2, &reference, &result))
        return;
    if (!plugin_byte(plugin + 0x4c))
        return;
    cfc_byte(plugin + 0x4d, flag);
    if ((flag & 255) && !plugin_byte(plugin + 0x4e) && !cfp_read(plugin + 0x50))
        plugin_private(&reference, 0xf2c0, &plugin, 1);
    else if (!(flag & 255))
        plugin_private(&reference, 0xf18c, &plugin, 1);
}

uint32 ROMlib_cfw_PlugInFindFactoriesForPlugInType(uint32 type)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {type}, result = 0;
    if (!cfp_reference("CFPlugInFindFactoriesForPlugInType", 0x153d8, arguments, 1, &reference, &result))
        return result;
    return factories(&reference, type, 0, 0);
}

uint32 ROMlib_cfw_PlugInFindFactoriesForPlugInTypeInPlugIn(uint32 type, uint32 plugin)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {type, plugin}, result = 0;
    if (!cfp_reference("CFPlugInFindFactoriesForPlugInTypeInPlugIn", 0x15490, arguments, 2, &reference, &result))
        return result;
    return factories(&reference, type, plugin, 1);
}

uint32 ROMlib_cfw_PlugInRegisterFactoryFunction(uint32 uuid, uint32 procedure)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {uuid, procedure}, result = 0;
    if (!cfp_reference("CFPlugInRegisterFactoryFunction", 0x1561c, arguments, 2, &reference, &result))
        return result;
    uint32 create[] = {0, uuid, procedure};
    plugin_private(&reference, 0x15b28, create, 3);
    return 1;
}

uint32 ROMlib_cfw_PlugInRegisterFactoryFunctionByName(uint32 uuid, uint32 plugin, uint32 name)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {uuid, plugin, name}, result = 0;
    if (!cfp_reference("CFPlugInRegisterFactoryFunctionByName", 0x15654, arguments, 3, &reference, &result))
        return result;
    uint32 allocator = CFW_CALL("CFGetAllocator", plugin);
    uint32 create[] = {allocator, uuid, plugin, name};
    plugin_private(&reference, 0x15b64, create, 4);
    return 1;
}

uint32 ROMlib_cfw_PlugInUnregisterFactory(uint32 uuid)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {uuid}, result = 0;
    if (!cfp_reference("CFPlugInUnregisterFactory", 0x156b8, arguments, 1, &reference, &result))
        return result;
    uint32 item = factory(&reference, uuid);
    if (item) {
        uint32 update[] = {item};
        plugin_private(&reference, 0x15cf0, update, 1);
    }
    return 1;
}

uint32 ROMlib_cfw_PlugInRegisterPlugInType(uint32 uuid, uint32 type)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {uuid, type}, result = 0;
    if (!cfp_reference("CFPlugInRegisterPlugInType", 0x156f4, arguments, 2, &reference, &result))
        return result;
    uint32 item = factory(&reference, uuid);
    if (item) {
        uint32 update[] = {item, type};
        plugin_private(&reference, 0x15d30, update, 2);
    }
    return 1;
}

uint32 ROMlib_cfw_PlugInUnregisterPlugInType(uint32 uuid, uint32 type)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {uuid, type}, result = 0;
    if (!cfp_reference("CFPlugInUnregisterPlugInType", 0x15740, arguments, 2, &reference, &result))
        return result;
    uint32 item = factory(&reference, uuid);
    if (item) {
        uint32 update[] = {item, type};
        plugin_private(&reference, 0x15e18, update, 2);
    }
    return 1;
}

void ROMlib_cfw_PlugInAddInstanceForFactory(uint32 uuid)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {uuid}, result = 0;
    if (!cfp_reference("CFPlugInAddInstanceForFactory", 0x1578c, arguments, 1, &reference, &result))
        return;
    uint32 item = factory(&reference, uuid);
    if (item) {
        uint32 update[] = {item};
        plugin_private(&reference, 0x16050, update, 1);
    }
}

void ROMlib_cfw_PlugInRemoveInstanceForFactory(uint32 uuid)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {uuid}, result = 0;
    if (!cfp_reference("CFPlugInRemoveInstanceForFactory", 0x157c4, arguments, 1, &reference, &result))
        return;
    uint32 item = factory(&reference, uuid);
    if (item) {
        uint32 update[] = {item};
        plugin_private(&reference, 0x1608c, update, 1);
    }
}

uint32 ROMlib_cfw_PlugInInstanceCreate(uint32 allocator, uint32 uuid, uint32 type)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, uuid, type}, result = 0;
    if (!cfp_reference("CFPlugInInstanceCreate", 0x1554c, arguments, 3, &reference, &result))
        return result;
    uint32 item = factory(&reference, uuid);
    uint32 literals = cfp_toc(&reference, 0x42c);
    if (!item) {
        uint32 message = CFW_CALL("__CFStringMakeConstantString", literals + 0x73);
        CFW_CALL("CFLog", 22, message, uuid);
        return 0;
    }
    uint32 check[] = {item, type};
    if (!(plugin_private(&reference, 0x15f58, check, 2) & 255)) {
        uint32 message = CFW_CALL("__CFStringMakeConstantString", literals + 0x8a);
        CFW_CALL("CFLog", 22, message, uuid, type);
        return 0;
    }
    uint32 create[] = {allocator, item, type};
    return plugin_private(&reference, 0x15c10, create, 3);
}

void ROMlib_cfw_AllocatorSetDefault(uint32 allocator)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator}, result = 0;
    if (!cfp_reference("CFAllocatorSetDefault", 0x1737c, arguments, 1, &reference, &result))
        return;
    uint32 slot = US_TO_SYN68K(reference.toc) + 0x6f28;
    uint32 old = cfp_read(slot);
    if (!allocator || allocator == old)
        return;
    if (old)
        cfp_call("CFRelease", &old, 1, NULL);
    /* The original takes two retains when replacing the process default. */
    if (cfp_call("CFRetain", &allocator, 1, NULL) && cfp_call("CFRetain", &allocator, 1, NULL))
        cfp_write(slot, allocator);
}
