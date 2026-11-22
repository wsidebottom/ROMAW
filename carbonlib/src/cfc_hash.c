/*+===================================================================
  File:      cfc_hash.c

  Summary:   Native Set, Bag and Dictionary entry control flow; original factory, lookup and growth workers remain
             dependencies.

  Classes:   None.

  Functions: ROMlib_cfc_CFBagAddValue, ROMlib_cfc_CFBagApplyFunction, ROMlib_cfc_CFBagContainsValue,
             ROMlib_cfc_CFBagCreate, ROMlib_cfc_CFBagCreateCopy, ROMlib_cfc_CFBagCreateMutable,
             ROMlib_cfc_CFBagCreateMutableCopy, ROMlib_cfc_CFBagGetCount, ROMlib_cfc_CFBagGetCountOfValue,
             ROMlib_cfc_CFBagGetTypeID, ROMlib_cfc_CFBagGetValue, ROMlib_cfc_CFBagGetValueIfPresent,
             ROMlib_cfc_CFBagGetValues, ROMlib_cfc_CFBagRemoveAllValues, ROMlib_cfc_CFBagRemoveValue,
             ROMlib_cfc_CFBagReplaceValue, ROMlib_cfc_CFBagSetValue, ROMlib_cfc_CFDictionaryAddValue,
             ROMlib_cfc_CFDictionaryApplyFunction, ROMlib_cfc_CFDictionaryContainsKey,
             ROMlib_cfc_CFDictionaryContainsValue, ROMlib_cfc_CFDictionaryCreate, ROMlib_cfc_CFDictionaryCreateCopy,
             ROMlib_cfc_CFDictionaryCreateMutable, ROMlib_cfc_CFDictionaryCreateMutableCopy,
             ROMlib_cfc_CFDictionaryGetCount, ROMlib_cfc_CFDictionaryGetCountOfKey,
             ROMlib_cfc_CFDictionaryGetCountOfValue, ROMlib_cfc_CFDictionaryGetKeysAndValues,
             ROMlib_cfc_CFDictionaryGetTypeID, ROMlib_cfc_CFDictionaryGetValue,
             ROMlib_cfc_CFDictionaryGetValueIfPresent, ROMlib_cfc_CFDictionaryRemoveAllValues,
             ROMlib_cfc_CFDictionaryRemoveValue, ROMlib_cfc_CFDictionaryReplaceValue, ROMlib_cfc_CFDictionarySetValue,
             ROMlib_cfc_CFSetAddValue, ROMlib_cfc_CFSetApplyFunction, ROMlib_cfc_CFSetCreate,
             ROMlib_cfc_CFSetCreateCopy, ROMlib_cfc_CFSetCreateMutable, ROMlib_cfc_CFSetCreateMutableCopy,
             ROMlib_cfc_CFSetGetValueIfPresent, ROMlib_cfc_CFSetRemoveAllValues, ROMlib_cfc_CFSetRemoveValue,
             ROMlib_cfc_CFSetReplaceValue, ROMlib_cfc_CFSetSetValue

  Origin:    Native C reconstruction of the local CoreFoundationLib 1.4 collection disassembly and Universal Interfaces
             3.4.2 declarations.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "cfc_cohort.h"

enum hash_kind { hash_set, hash_bag, hash_dictionary };

enum hash_operation { hash_add, hash_replace, hash_set_value, hash_remove };

static unsigned bucket_size(enum hash_kind kind)
{
    return kind == hash_set ? 8 : 12;
}

static unsigned bucket_field(enum hash_kind kind)
{
    return kind == hash_dictionary ? 68 : 48;
}

static unsigned count_field(enum hash_kind kind)
{
    return kind == hash_bag ? 8 : 12;
}

static unsigned unique_field(enum hash_kind kind)
{
    return kind == hash_bag ? 16 : 12;
}

static unsigned capacity_field(enum hash_kind kind)
{
    return kind == hash_bag ? 12 : 16;
}

static uint32 hash_context(enum hash_kind kind, uint32 object)
{
    return kind == hash_bag ? 0 : cfp_read(object + 8);
}

static int hash_find(const ROMlib_carbon_reference* reference, enum hash_kind kind, uint32 object, uint32 key,
                     int inserting, uint32* found, uint32* vacant, uint32* hash)
{
    uint32 procedure;
    *hash = key;
    *found = *vacant = 0;
    if (!cfp_get(object + 44, &procedure))
        return 0;
    uint32 callback[] = {key, hash_context(kind, object)};
    if (procedure && !cfp_callback(procedure, callback, kind == hash_bag ? 1 : 2, hash))
        return 0;
    *hash &= 0x3fffffff;
    _Alignas(16) uint32 match = 0, empty = 0;
    uint32 arguments[] = {cfp_read(object + bucket_field(kind)),
                          cfp_read(object + 40),
                          bucket_size(kind),
                          cfp_read(object + 20),
                          hash_context(kind, object),
                          key,
                          *hash,
                          US_TO_SYN68K(&match),
                          US_TO_SYN68K(&empty)};
    if (!cfp_private(reference, inserting ? 0x22a74 : 0x2299c, arguments, inserting ? 9 : 8, NULL))
        return 0;
    *found = CL(match);
    *vacant = CL(empty);
    return 1;
}

static int hash_query(const ROMlib_carbon_reference* reference, enum hash_kind kind, uint32 object, uint32 key,
                      uint32* found)
{
    *found = 0;
    uint32 count, empty, hash;
    if (!cfp_get(object + count_field(kind), &count))
        return 0;
    return !count || hash_find(reference, kind, object, key, 0, found, &empty, &hash);
}

static int hash_collect(enum hash_kind kind, uint32 object, uint32 keys, uint32 values)
{
    uint32 buckets, count;
    if (!cfp_get(object + bucket_field(kind), &buckets) || !cfp_get(object + 20, &count))
        return 0;
    for (uint32 i = 0; (int32)i < (int32)count; ++i) {
        uint32 bucket = buckets + i * bucket_size(kind), state;
        if (!cfp_get(bucket, &state))
            return 0;
        if ((state >> 30) != 3)
            continue;
        uint32 repetitions = kind == hash_bag ? cfp_read(bucket + 8) : 1;
        for (uint32 j = 0; j < repetitions; ++j) {
            uint32 value;
            if (keys) {
                if (!cfp_get(bucket + 4, &value) || !cfp_write(keys, value))
                    return 0;
                keys += 4;
            }
            if (values || kind != hash_dictionary) {
                if (!cfp_get(bucket + (kind == hash_dictionary ? 8 : 4), &value) || !cfp_write(values, value))
                    return 0;
                values += 4;
            }
        }
    }
    return 1;
}

static void hash_apply(enum hash_kind kind, uint32 object, uint32 procedure, uint32 context)
{
    uint32 buckets, count;
    if (!cfp_get(object + bucket_field(kind), &buckets) || !cfp_get(object + 20, &count))
        return;
    for (uint32 i = 0; (int32)i < (int32)count; ++i) {
        uint32 bucket = buckets + i * bucket_size(kind), state;
        if (!cfp_get(bucket, &state))
            return;
        if ((state >> 30) != 3)
            continue;
        uint32 repetitions = kind == hash_bag ? cfp_read(bucket + 8) : 1;
        for (uint32 j = 0; j < repetitions; ++j) {
            uint32 arguments[] = {cfp_read(bucket + 4), context, context};
            if (kind == hash_dictionary)
                arguments[1] = cfp_read(bucket + 8);
            if (!cfp_callback(procedure, arguments, kind == hash_dictionary ? 3 : 2, NULL))
                return;
        }
    }
}

static uint32 hash_create(const ROMlib_carbon_reference* reference, enum hash_kind kind, uint32 allocator,
                          uint32 mutability, uint32 capacity, uint32 keys, uint32 values, uint32 count,
                          uint32 callbacks, uint32 value_callbacks)
{
    uint32 ignored;
    if (!cfc_product(capacity, 1, &ignored) || !cfc_product(count, 4, &ignored))
        return 0;
    uint32 result = 0;
    callbacks = ROMlib_carbon_canonical_cf_callbacks(callbacks, reference->toc);
    if (kind == hash_dictionary) {
        value_callbacks = ROMlib_carbon_canonical_cf_callbacks(value_callbacks, reference->toc);
        uint32 arguments[] = {allocator, mutability, capacity, keys, values, count, callbacks, value_callbacks};
        cfp_private(reference, 0x215c4, arguments, 8, &result);
    }
    else {
        uint32 arguments[] = {allocator, mutability, capacity, values, count, callbacks};
        cfp_private(reference, kind == hash_set ? 0x1ebf0 : 0x20004, arguments, 6, &result);
    }
    return result;
}

static uint32 hash_copy(const ROMlib_carbon_reference* reference, enum hash_kind kind, uint32 allocator, uint32 source,
                        int mutable, uint32 capacity)
{
    uint32 count, bytes;
    if (!cfp_get(source + count_field(kind), &count) || !cfc_product(count, 4, &bytes))
        return 0;
    _Alignas(1024) uint32 stack_keys[128], stack_values[256];
    uint32 values = US_TO_SYN68K(stack_values), keys = 0;
    int large = count > (kind == hash_dictionary ? 128U : 256U);
    if (large) {
        uint32 arguments[] = {allocator, bytes, 0};
        values = cfc_public("CFAllocatorAllocate", arguments, 3);
        if (!values)
            return 0;
    }
    if (kind == hash_dictionary) {
        keys = US_TO_SYN68K(stack_keys);
        if (large) {
            uint32 arguments[] = {allocator, bytes, 0};
            keys = cfc_public("CFAllocatorAllocate", arguments, 3);
            if (!keys) {
                uint32 cleanup[] = {allocator, values};
                cfp_call("CFAllocatorDeallocate", cleanup, 2, NULL);
                return 0;
            }
        }
    }
    uint32 result = 0;
    if (hash_collect(kind, source, keys, values))
        result = hash_create(reference, kind, allocator, mutable ? (capacity ? 3 : 1) : 0, mutable ? capacity : count,
                             keys, values, count, source + 24, source + 48);
    if (large) {
        uint32 cleanup[] = {allocator, values};
        cfp_call("CFAllocatorDeallocate", cleanup, 2, NULL);
        if (keys) {
            cleanup[1] = keys;
            cfp_call("CFAllocatorDeallocate", cleanup, 2, NULL);
        }
    }
    return result;
}

static int hash_owned_callback(enum hash_kind kind, uint32 object, unsigned field, uint32 value, uint32* result)
{
    uint32 procedure;
    if (!cfp_get(object + field, &procedure))
        return 0;
    if (!procedure) {
        if (result)
            *result = value;
        return 1;
    }
    uint32 allocator = cfc_public("CFGetAllocator", &object, 1);
    uint32 arguments[] = {allocator, value, hash_context(kind, object)};
    return cfp_callback(procedure, arguments, kind == hash_bag ? 2 : 3, result);
}

static int hash_mutable(uint32 object, uint32* mutability)
{
    uint32 flags;
    if (!cfp_get(object + 4, &flags))
        return 0;
    *mutability = (flags >> 2) & 3;
    if (*mutability != 1 && *mutability != 3)
        return cfp_failure("Native Carbon hash mutation requires a mutable collection");
    return 1;
}

static void hash_mutate(const ROMlib_carbon_reference* reference, enum hash_kind kind, enum hash_operation operation,
                        uint32 object, uint32 key, uint32 value)
{
    uint32 mutability, count, capacity;
    if (!hash_mutable(object, &mutability) || !cfp_get(object + unique_field(kind), &count) ||
        !cfp_get(object + capacity_field(kind), &capacity))
        return;
    if (operation == hash_add || operation == hash_set_value) {
        if (mutability == 1 && count == capacity) {
            uint32 arguments[] = {object, 1};
            uint32 worker = kind == hash_set ? 0x1f42c : kind == hash_bag ? 0x208b0 : 0x21fec;
            if (!cfp_private(reference, worker, arguments, 2, NULL))
                return;
        }
        else if (mutability == 3 && operation == hash_add && cfp_read(object + count_field(kind)) == capacity) {
            cfp_failure("Native Carbon hash insertion exceeds fixed capacity");
            return;
        }
    }
    uint32 found, vacant, hash;
    if (!hash_find(reference, kind, object, key, operation == hash_add || operation == hash_set_value, &found, &vacant,
                   &hash))
        return;
    if (operation == hash_remove) {
        if (!found)
            return;
        if (kind == hash_bag) {
            uint32 repetitions;
            if (!cfp_get(found + 8, &repetitions) || !cfp_write(found + 8, repetitions - 1) ||
                !cfp_write(object + 8, cfp_read(object + 8) - 1))
                return;
            if (repetitions != 1)
                return;
        }
        if (!cfp_write(found, 0x40000000) || !hash_owned_callback(kind, object, 32, cfp_read(found + 4), NULL) ||
            !cfp_write(found + 4, 0))
            return;
        if (kind == hash_dictionary &&
            (!hash_owned_callback(kind, object, 56, cfp_read(found + 8), NULL) || !cfp_write(found + 8, 0)))
            return;
        cfp_write(object + unique_field(kind), cfp_read(object + unique_field(kind)) - 1);
        return;
    }
    if (found) {
        if (operation == hash_add) {
            if (kind == hash_bag && cfp_write(found + 8, cfp_read(found + 8) + 1))
                cfp_write(object + 8, cfp_read(object + 8) + 1);
            return;
        }
        unsigned field = kind == hash_dictionary ? 8 : 4;
        unsigned retain = kind == hash_dictionary ? 52 : 28;
        unsigned release = kind == hash_dictionary ? 56 : 32;
        uint32 replacement = kind == hash_dictionary ? value : key;
        if (!hash_owned_callback(kind, object, retain, replacement, &replacement) ||
            !hash_owned_callback(kind, object, release, cfp_read(found + field), NULL))
            return;
        cfp_write(found + field, replacement);
        return;
    }
    if (operation == hash_replace)
        return;
    if (mutability == 3 && cfp_read(object + count_field(kind)) == cfp_read(object + capacity_field(kind))) {
        cfp_failure("Native Carbon hash insertion exceeds fixed capacity");
        return;
    }
    if (!vacant) {
        cfp_failure("Native Carbon hash insertion has no vacant bucket");
        return;
    }
    uint32 retained;
    if (!cfp_write(vacant, 0xc0000000 | hash) || !hash_owned_callback(kind, object, 28, key, &retained) ||
        !cfp_write(vacant + 4, retained))
        return;
    if (kind == hash_dictionary &&
        (!hash_owned_callback(kind, object, 52, value, &retained) || !cfp_write(vacant + 8, retained)))
        return;
    if (kind == hash_bag && (!cfp_write(vacant + 8, 1) || !cfp_write(object + 8, cfp_read(object + 8) + 1)))
        return;
    cfp_write(object + unique_field(kind), cfp_read(object + unique_field(kind)) + 1);
}

static void hash_clear(enum hash_kind kind, uint32 object)
{
    uint32 mutability, buckets, count;
    if (!hash_mutable(object, &mutability) || !cfp_get(object + bucket_field(kind), &buckets) ||
        !cfp_get(object + 20, &count))
        return;
    for (uint32 i = 0; (int32)i < (int32)count; ++i) {
        uint32 bucket = buckets + i * bucket_size(kind), state;
        if (!cfp_get(bucket, &state))
            return;
        if ((state >> 30) == 3) {
            if (!hash_owned_callback(kind, object, 32, cfp_read(bucket + 4), NULL) || !cfp_write(bucket + 4, 0))
                return;
            if (kind == hash_dictionary && !hash_owned_callback(kind, object, 56, cfp_read(bucket + 8), NULL))
                return;
            if (kind != hash_set && !cfp_write(bucket + 8, 0))
                return;
        }
        if (!cfp_write(bucket, 0))
            return;
    }
    if (cfp_write(object + unique_field(kind), 0) && kind == hash_bag)
        cfp_write(object + 8, 0);
}

static uint32 dictionary_count_value(uint32 object, uint32 value)
{
    uint32 buckets, count, equal, result = 0;
    if (!cfp_get(object + 68, &buckets) || !cfp_get(object + 20, &count) || !cfp_get(object + 64, &equal))
        return 0;
    for (uint32 i = 0; (int32)i < (int32)count; ++i) {
        uint32 bucket = buckets + i * 12, state, candidate;
        if (!cfp_get(bucket, &state))
            return 0;
        if ((state >> 30) != 3)
            continue;
        if (!cfp_get(bucket + 8, &candidate))
            return 0;
        if (candidate == value)
            ++result;
        else if (equal) {
            uint32 arguments[] = {value, candidate, cfp_read(object + 8)}, same = 0;
            if (!cfp_callback(equal, arguments, 3, &same))
                return 0;
            result += same != 0;
        }
    }
    return result;
}

void ROMlib_cfc_CFBagAddValue(uint32 object, uint32 key)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, key}, result = 0;
    if (!cfp_reference("CFBagAddValue", 0x20a84, arguments, 2, &reference, &result))
        return;
    hash_mutate(&reference, hash_bag, hash_add, object, key, 0);
}

void ROMlib_cfc_CFBagApplyFunction(uint32 object, uint32 procedure, uint32 context)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, procedure, context}, result = 0;
    if (!cfp_reference("CFBagApplyFunction", 0x2082c, arguments, 3, &reference, &result))
        return;
    hash_apply(hash_bag, object, procedure, context);
}

uint32 ROMlib_cfc_CFBagContainsValue(uint32 object, uint32 value)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, value}, result = 0;
    if (!cfp_reference("CFBagContainsValue", 0x205dc, arguments, 2, &reference, &result))
        return result;
    uint32 found;
    if (!hash_query(&reference, hash_bag, object, value, &found))
        return 0;
    return found && cfp_read(found + 8) != 0;
}

uint32 ROMlib_cfc_CFBagCreate(uint32 allocator, uint32 values, uint32 count, uint32 callbacks)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, values, count, callbacks}, result = 0;
    if (!cfp_reference("CFBagCreate", 0x2033c, arguments, 4, &reference, &result))
        return result;
    return hash_create(&reference, hash_bag, allocator, 0, count, 0, values, count, callbacks, 0);
}

uint32 ROMlib_cfc_CFBagCreateCopy(uint32 allocator, uint32 source)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, source}, result = 0;
    if (!cfp_reference("CFBagCreateCopy", 0x203b4, arguments, 2, &reference, &result))
        return result;
    return hash_copy(&reference, hash_bag, allocator, source, 0, 0);
}

uint32 ROMlib_cfc_CFBagCreateMutable(uint32 allocator, uint32 capacity, uint32 callbacks)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, capacity, callbacks}, result = 0;
    if (!cfp_reference("CFBagCreateMutable", 0x20370, arguments, 3, &reference, &result))
        return result;
    return hash_create(&reference, hash_bag, allocator, capacity ? 3 : 1, capacity, 0, 0, 0, callbacks, 0);
}

uint32 ROMlib_cfc_CFBagCreateMutableCopy(uint32 allocator, uint32 capacity, uint32 source)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, capacity, source}, result = 0;
    if (!cfp_reference("CFBagCreateMutableCopy", 0x20474, arguments, 3, &reference, &result))
        return result;
    return hash_copy(&reference, hash_bag, allocator, source, 1, capacity);
}

uint32 ROMlib_cfc_CFBagGetCount(uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object}, result = 0;
    if (!cfp_reference("CFBagGetCount", 0x20530, arguments, 1, &reference, &result))
        return result;
    return cfp_read(object + count_field(hash_bag));
}

uint32 ROMlib_cfc_CFBagGetCountOfValue(uint32 object, uint32 value)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, value}, result = 0;
    if (!cfp_reference("CFBagGetCountOfValue", 0x20538, arguments, 2, &reference, &result))
        return result;
    uint32 found;
    if (!hash_query(&reference, hash_bag, object, value, &found))
        return 0;
    return found ? cfp_read(found + 8) : 0;
}

uint32 ROMlib_cfc_CFBagGetTypeID(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (!cfp_reference("CFBagGetTypeID", 0x1fffc, NULL, 0, &reference, &result))
        return result;
    return cfp_toc(&reference, 0x8cc);
}

uint32 ROMlib_cfc_CFBagGetValue(uint32 object, uint32 key)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, key}, result = 0;
    if (!cfp_reference("CFBagGetValue", 0x20614, arguments, 2, &reference, &result))
        return result;
    uint32 found;
    if (!hash_query(&reference, hash_bag, object, key, &found))
        return 0;
    return found ? cfp_read(found + 4) : 0;
}

uint32 ROMlib_cfc_CFBagGetValueIfPresent(uint32 object, uint32 key, uint32 output)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, key, output}, result = 0;
    if (!cfp_reference("CFBagGetValueIfPresent", 0x206b8, arguments, 3, &reference, &result))
        return result;
    uint32 found;
    if (!hash_query(&reference, hash_bag, object, key, &found))
        return 0;
    if (found && output && !cfp_write(output, cfp_read(found + 4)))
        return 0;
    return found != 0;
}

void ROMlib_cfc_CFBagGetValues(uint32 object, uint32 output)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, output}, result = 0;
    if (!cfp_reference("CFBagGetValues", 0x20778, arguments, 2, &reference, &result))
        return;
    hash_collect(hash_bag, object, 0, output);
}

void ROMlib_cfc_CFBagRemoveAllValues(uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object}, result = 0;
    if (!cfp_reference("CFBagRemoveAllValues", 0x210b8, arguments, 1, &reference, &result))
        return;
    hash_clear(hash_bag, object);
}

void ROMlib_cfc_CFBagRemoveValue(uint32 object, uint32 key)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, key}, result = 0;
    if (!cfp_reference("CFBagRemoveValue", 0x20f70, arguments, 2, &reference, &result))
        return;
    hash_mutate(&reference, hash_bag, hash_remove, object, key, 0);
}

void ROMlib_cfc_CFBagReplaceValue(uint32 object, uint32 key)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, key}, result = 0;
    if (!cfp_reference("CFBagReplaceValue", 0x20c38, arguments, 2, &reference, &result))
        return;
    hash_mutate(&reference, hash_bag, hash_replace, object, key, 0);
}

void ROMlib_cfc_CFBagSetValue(uint32 object, uint32 key)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, key}, result = 0;
    if (!cfp_reference("CFBagSetValue", 0x20d54, arguments, 2, &reference, &result))
        return;
    hash_mutate(&reference, hash_bag, hash_set_value, object, key, 0);
}

void ROMlib_cfc_CFDictionaryAddValue(uint32 object, uint32 key, uint32 value)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, key, value}, result = 0;
    if (!cfp_reference("CFDictionaryAddValue", 0x221c0, arguments, 3, &reference, &result))
        return;
    hash_mutate(&reference, hash_dictionary, hash_add, object, key, value);
}

void ROMlib_cfc_CFDictionaryApplyFunction(uint32 object, uint32 procedure, uint32 context)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, procedure, context}, result = 0;
    if (!cfp_reference("CFDictionaryApplyFunction", 0x21f78, arguments, 3, &reference, &result))
        return;
    hash_apply(hash_dictionary, object, procedure, context);
}

uint32 ROMlib_cfc_CFDictionaryContainsKey(uint32 object, uint32 key)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, key}, result = 0;
    if (!cfp_reference("CFDictionaryContainsKey", 0x21d40, arguments, 2, &reference, &result))
        return result;
    uint32 found;
    if (!hash_query(&reference, hash_dictionary, object, key, &found))
        return 0;
    return found != 0;
}

uint32 ROMlib_cfc_CFDictionaryContainsValue(uint32 object, uint32 value)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, value}, result = 0;
    if (!cfp_reference("CFDictionaryContainsValue", 0x21d78, arguments, 2, &reference, &result))
        return result;
    return dictionary_count_value(object, value) != 0;
}

uint32 ROMlib_cfc_CFDictionaryCreate(uint32 allocator, uint32 keys, uint32 values, uint32 count, uint32 callbacks,
                                     uint32 value_callbacks)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, keys, values, count, callbacks, value_callbacks}, result = 0;
    if (!cfp_reference("CFDictionaryCreate", 0x21948, arguments, 6, &reference, &result))
        return result;
    return hash_create(&reference, hash_dictionary, allocator, 0, count, keys, values, count, callbacks,
                       value_callbacks);
}

uint32 ROMlib_cfc_CFDictionaryCreateCopy(uint32 allocator, uint32 source)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, source}, result = 0;
    if (!cfp_reference("CFDictionaryCreateCopy", 0x219d8, arguments, 2, &reference, &result))
        return result;
    return hash_copy(&reference, hash_dictionary, allocator, source, 0, 0);
}

uint32 ROMlib_cfc_CFDictionaryCreateMutable(uint32 allocator, uint32 capacity, uint32 callbacks, uint32 value_callbacks)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, capacity, callbacks, value_callbacks}, result = 0;
    if (!cfp_reference("CFDictionaryCreateMutable", 0x2198c, arguments, 4, &reference, &result))
        return result;
    return hash_create(&reference, hash_dictionary, allocator, capacity ? 3 : 1, capacity, 0, 0, 0, callbacks,
                       value_callbacks);
}

uint32 ROMlib_cfc_CFDictionaryCreateMutableCopy(uint32 allocator, uint32 capacity, uint32 source)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, capacity, source}, result = 0;
    if (!cfp_reference("CFDictionaryCreateMutableCopy", 0x21adc, arguments, 3, &reference, &result))
        return result;
    return hash_copy(&reference, hash_dictionary, allocator, source, 1, capacity);
}

uint32 ROMlib_cfc_CFDictionaryGetCount(uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object}, result = 0;
    if (!cfp_reference("CFDictionaryGetCount", 0x21bf4, arguments, 1, &reference, &result))
        return result;
    return cfp_read(object + count_field(hash_dictionary));
}

uint32 ROMlib_cfc_CFDictionaryGetCountOfKey(uint32 object, uint32 key)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, key}, result = 0;
    if (!cfp_reference("CFDictionaryGetCountOfKey", 0x21bfc, arguments, 2, &reference, &result))
        return result;
    uint32 found;
    if (!hash_query(&reference, hash_dictionary, object, key, &found))
        return 0;
    return found != 0;
}

uint32 ROMlib_cfc_CFDictionaryGetCountOfValue(uint32 object, uint32 value)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, value}, result = 0;
    if (!cfp_reference("CFDictionaryGetCountOfValue", 0x21ca4, arguments, 2, &reference, &result))
        return result;
    return dictionary_count_value(object, value);
}

void ROMlib_cfc_CFDictionaryGetKeysAndValues(uint32 object, uint32 keys, uint32 values)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, keys, values}, result = 0;
    if (!cfp_reference("CFDictionaryGetKeysAndValues", 0x21f1c, arguments, 3, &reference, &result))
        return;
    hash_collect(hash_dictionary, object, keys, values);
}

uint32 ROMlib_cfc_CFDictionaryGetTypeID(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (!cfp_reference("CFDictionaryGetTypeID", 0x215bc, NULL, 0, &reference, &result))
        return result;
    return cfp_toc(&reference, 0x8c8);
}

uint32 ROMlib_cfc_CFDictionaryGetValue(uint32 object, uint32 key)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, key}, result = 0;
    if (!cfp_reference("CFDictionaryGetValue", 0x21e74, arguments, 2, &reference, &result))
        return result;
    uint32 found;
    if (!hash_query(&reference, hash_dictionary, object, key, &found))
        return 0;
    return found ? cfp_read(found + 8) : 0;
}

uint32 ROMlib_cfc_CFDictionaryGetValueIfPresent(uint32 object, uint32 key, uint32 output)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, key, output}, result = 0;
    if (!cfp_reference("CFDictionaryGetValueIfPresent", 0x21db0, arguments, 3, &reference, &result))
        return result;
    uint32 found;
    if (!hash_query(&reference, hash_dictionary, object, key, &found))
        return 0;
    if (found && output && !cfp_write(output, cfp_read(found + 8)))
        return 0;
    return found != 0;
}

void ROMlib_cfc_CFDictionaryRemoveAllValues(uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object}, result = 0;
    if (!cfp_reference("CFDictionaryRemoveAllValues", 0x2286c, arguments, 1, &reference, &result))
        return;
    hash_clear(hash_dictionary, object);
}

void ROMlib_cfc_CFDictionaryRemoveValue(uint32 object, uint32 key)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, key}, result = 0;
    if (!cfp_reference("CFDictionaryRemoveValue", 0x22708, arguments, 2, &reference, &result))
        return;
    hash_mutate(&reference, hash_dictionary, hash_remove, object, key, 0);
}

void ROMlib_cfc_CFDictionaryReplaceValue(uint32 object, uint32 key, uint32 value)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, key, value}, result = 0;
    if (!cfp_reference("CFDictionaryReplaceValue", 0x22394, arguments, 3, &reference, &result))
        return;
    hash_mutate(&reference, hash_dictionary, hash_replace, object, key, value);
}

void ROMlib_cfc_CFDictionarySetValue(uint32 object, uint32 key, uint32 value)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, key, value}, result = 0;
    if (!cfp_reference("CFDictionarySetValue", 0x224c8, arguments, 3, &reference, &result))
        return;
    hash_mutate(&reference, hash_dictionary, hash_set_value, object, key, value);
}

void ROMlib_cfc_CFSetAddValue(uint32 object, uint32 key)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, key}, result = 0;
    if (!cfp_reference("CFSetAddValue", 0x1f5f4, arguments, 2, &reference, &result))
        return;
    hash_mutate(&reference, hash_set, hash_add, object, key, 0);
}

void ROMlib_cfc_CFSetApplyFunction(uint32 object, uint32 procedure, uint32 context)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, procedure, context}, result = 0;
    if (!cfp_reference("CFSetApplyFunction", 0x1f3bc, arguments, 3, &reference, &result))
        return;
    hash_apply(hash_set, object, procedure, context);
}

uint32 ROMlib_cfc_CFSetCreate(uint32 allocator, uint32 values, uint32 count, uint32 callbacks)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, values, count, callbacks}, result = 0;
    if (!cfp_reference("CFSetCreate", 0x1ef1c, arguments, 4, &reference, &result))
        return result;
    return hash_create(&reference, hash_set, allocator, 0, count, 0, values, count, callbacks, 0);
}

uint32 ROMlib_cfc_CFSetCreateCopy(uint32 allocator, uint32 source)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, source}, result = 0;
    if (!cfp_reference("CFSetCreateCopy", 0x1ef94, arguments, 2, &reference, &result))
        return result;
    return hash_copy(&reference, hash_set, allocator, source, 0, 0);
}

uint32 ROMlib_cfc_CFSetCreateMutable(uint32 allocator, uint32 capacity, uint32 callbacks)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, capacity, callbacks}, result = 0;
    if (!cfp_reference("CFSetCreateMutable", 0x1ef50, arguments, 3, &reference, &result))
        return result;
    return hash_create(&reference, hash_set, allocator, capacity ? 3 : 1, capacity, 0, 0, 0, callbacks, 0);
}

uint32 ROMlib_cfc_CFSetCreateMutableCopy(uint32 allocator, uint32 capacity, uint32 source)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, capacity, source}, result = 0;
    if (!cfp_reference("CFSetCreateMutableCopy", 0x1f060, arguments, 3, &reference, &result))
        return result;
    return hash_copy(&reference, hash_set, allocator, source, 1, capacity);
}

uint32 ROMlib_cfc_CFSetGetValueIfPresent(uint32 object, uint32 key, uint32 output)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, key, output}, result = 0;
    if (!cfp_reference("CFSetGetValueIfPresent", 0x1f2b8, arguments, 3, &reference, &result))
        return result;
    uint32 found;
    if (!hash_query(&reference, hash_set, object, key, &found))
        return 0;
    if (found && output && !cfp_write(output, cfp_read(found + 4)))
        return 0;
    return found != 0;
}

void ROMlib_cfc_CFSetRemoveAllValues(uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object}, result = 0;
    if (!cfp_reference("CFSetRemoveAllValues", 0x1fbe0, arguments, 1, &reference, &result))
        return;
    hash_clear(hash_set, object);
}

void ROMlib_cfc_CFSetRemoveValue(uint32 object, uint32 key)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, key}, result = 0;
    if (!cfp_reference("CFSetRemoveValue", 0x1fab8, arguments, 2, &reference, &result))
        return;
    hash_mutate(&reference, hash_set, hash_remove, object, key, 0);
}

void ROMlib_cfc_CFSetReplaceValue(uint32 object, uint32 key)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, key}, result = 0;
    if (!cfp_reference("CFSetReplaceValue", 0x1f77c, arguments, 2, &reference, &result))
        return;
    hash_mutate(&reference, hash_set, hash_replace, object, key, 0);
}

void ROMlib_cfc_CFSetSetValue(uint32 object, uint32 key)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, key}, result = 0;
    if (!cfp_reference("CFSetSetValue", 0x1f8a4, arguments, 2, &reference, &result))
        return;
    hash_mutate(&reference, hash_set, hash_set_value, object, key, 0);
}
