/*+===================================================================
  File:      cf_keys.c

  Summary:   Builds the shared CoreFoundation preference, URL, bundle and plug-in keys.

  Classes:   None.

  Functions: ROMlib_carbon_cf_preference_keys, ROMlib_carbon_cf_url_keys,
             ROMlib_carbon_cf_bundle_keys, ROMlib_carbon_cf_plugin_keys

  Origin:    CoreFoundationLib 1.4 initializers at offsets 0x1f08, 0x3de8, 0xd4b8 and 0x1534c.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "cfc_cohort.h"

static const struct {
    uint16 slot, source;
    const char* text;
} keys[] = {

#define CARBON_CF_KEY(name, index, slot, text) {slot, 0, text},
#define CARBON_CF_LOCAL_KEY(slot, text) {slot, 0, text},
#define CARBON_CF_KEY_ALIAS(name, index, slot, source) {slot, source, NULL},
#include "cf_key_entries.h"
#undef CARBON_CF_KEY
#undef CARBON_CF_LOCAL_KEY
#undef CARBON_CF_KEY_ALIAS
};

static int initialize_keys(uint16 first, uint16 last, uint16 class_slot, uint16 type_slot)
{
    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", "__CFStringMakeConstantString", &reference))
        return 0;
    uint32 toc = US_TO_SYN68K(reference.toc);
    if (class_slot) {
        uint32 type = ROMlib_cfc_CFRuntimeRegisterClass(toc + class_slot);
        if (!type || !cfp_write(toc + type_slot, type))
            return 0;
    }
    for (size_t index = 0; index < sizeof keys / sizeof *keys; ++index) {
        uint16 slot = keys[index].slot;
        if (slot < first || slot > last)
            continue;
        uint32 object = 0;
        if (keys[index].source) {
            if (!cfp_get(toc + keys[index].source, &object))
                return 0;
        }
        else {
            _Alignas(64) Byte literal[64];
            size_t length = strlen(keys[index].text) + 1;
            if (length > sizeof literal)
                return cfp_failure("CoreFoundation key exceeds literal buffer");
            memcpy(literal, keys[index].text, length);
            object = US_TO_SYN68K_CHECK0(ROMlib_carbon_cf_string_constant(literal));
        }
        if (!object || !cfp_write(toc + slot, object))
            return 0;
    }
    return 1;
}

int ROMlib_carbon_cf_preference_keys(ROMlib_carbon_ppc_frame* call)
{
    return initialize_keys(0x217c, 0x2190, 0x2194, 0x7dc);
}

int ROMlib_carbon_cf_url_keys(ROMlib_carbon_ppc_frame* call)
{
    return initialize_keys(0x21f0, 0x2228, 0, 0);
}

int ROMlib_carbon_cf_bundle_keys(ROMlib_carbon_ppc_frame* call)
{
    return initialize_keys(0x2274, 0x2308, 0x230c, 0x834);
}

int ROMlib_carbon_cf_plugin_keys(ROMlib_carbon_ppc_frame* call)
{
    return initialize_keys(0x305c, 0x306c, 0, 0);
}
