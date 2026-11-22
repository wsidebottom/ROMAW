/*+===================================================================
  File:      cf_set_queries.c

  Summary:   Set query control flow. The shared open-addressed hash lookup worker remains an explicit real dependency
             until its generic bucket engine is migrated.

  Classes:   None.

  Functions: ROMlib_cfset_contains, ROMlib_cfset_count, ROMlib_cfset_count_value, ROMlib_cfset_value,
             ROMlib_cfset_values

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Set query control flow. The shared open-addressed hash lookup worker remains
   an explicit real dependency until its generic bucket engine is migrated. */
#include "cf_batch.h"

static int set_lookup(const ROMlib_carbon_reference* reference, uint32 set, uint32 value, uint32* bucket)
{
    *bucket = 0;
    if (!cfp_read(set + 12))
        return 1;
    uint32 hash = value, procedure = cfp_read(set + 44);
    if (procedure) {
        uint32 arguments[] = {value, cfp_read(set + 8)};
        if (!cfp_callback(procedure, arguments, 2, &hash))
            return 0;
    }
    _Alignas(16) uint32 found = 0;
    uint32 arguments[] = {cfp_read(set + 48), cfp_read(set + 40),  8, cfp_read(set + 20), cfp_read(set + 8), value,
                          hash & 0x3fffffff,  US_TO_SYN68K(&found)};
    if (!cfp_private(reference, 0x2299c, arguments, 8, NULL))
        return 0;
    *bucket = CL(found);
    return 1;
}

uint32 ROMlib_cfset_count(uint32 set)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (cfp_reference("CFSetGetCount", 0x1f128, &set, 1, &reference, &result))
        result = cfp_read(set + 12);
    return result;
}

uint32 ROMlib_cfset_count_value(uint32 set, uint32 value)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {set, value}, result = 0, bucket;
    if (cfp_reference("CFSetGetCountOfValue", 0x1f130, arguments, 2, &reference, &result))
        result = set_lookup(&reference, set, value, &bucket) && bucket != 0;
    return result;
}

uint32 ROMlib_cfset_contains(uint32 set, uint32 value)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {set, value}, result = 0, bucket;
    if (cfp_reference("CFSetContainsValue", 0x1f1d8, arguments, 2, &reference, &result))
        result = set_lookup(&reference, set, value, &bucket) && bucket != 0;
    return result;
}

uint32 ROMlib_cfset_value(uint32 set, uint32 value)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {set, value}, result = 0, bucket;
    if (cfp_reference("CFSetGetValue", 0x1f210, arguments, 2, &reference, &result)) {
        if (!set_lookup(&reference, set, value, &bucket))
            return 0;
        result = bucket ? cfp_read(bucket + 4) : 0;
    }
    return result;
}

void ROMlib_cfset_values(uint32 set, uint32 output)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {set, output}, ignored = 0;
    if (!cfp_reference("CFSetGetValues", 0x1f37c, arguments, 2, &reference, &ignored))
        return;
    uint32 buckets = cfp_read(set + 48), count = cfp_read(set + 20);
    if ((int32)count <= 0)
        return;
    for (uint32 i = 0; i < count; ++i) {
        uint32 bucket = buckets + i * 8;
        uint32 state, value;
        if (!cfp_get(bucket, &state))
            return;
        if ((state >> 30) == 3) {
            if (!cfp_get(bucket + 4, &value) || !cfp_write(output, value))
                return;
            output += 4;
        }
    }
}
