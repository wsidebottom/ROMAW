/*+===================================================================
  File:      cfc_cohort.h

  Summary:   Declarations for the 100-entry collection/runtime migration cohort and its internal helpers.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Native C control flow reconstructed from the local CarbonLib 1.4 CoreFoundationLib disassembly; public
             declarations checked against Universal Interfaces 3.4.2. Reference binaries remain local assets.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_CFC_COHORT_H
#define ROMAW_CARBON_CFC_COHORT_H

#include "cf_batch.h"

uint32 cfc_public(const char* name, const uint32* arguments, unsigned count);
uint32 cfc_allocate_instance(const ROMlib_carbon_reference* reference, uint32 allocator, uint32 type, uint32 extra);
int cfc_byte(uint32 address, unsigned value);
int cfc_product(uint32 first, uint32 second, uint32* result);

void ROMlib_cfc_CFBagAddValue(uint32 object, uint32 key);
void ROMlib_cfc_CFBagApplyFunction(uint32 object, uint32 procedure, uint32 context);
uint32 ROMlib_cfc_CFBagContainsValue(uint32 object, uint32 value);
uint32 ROMlib_cfc_CFBagCreate(uint32 allocator, uint32 values, uint32 count, uint32 callbacks);
uint32 ROMlib_cfc_CFBagCreateCopy(uint32 allocator, uint32 source);
uint32 ROMlib_cfc_CFBagCreateMutable(uint32 allocator, uint32 capacity, uint32 callbacks);
uint32 ROMlib_cfc_CFBagCreateMutableCopy(uint32 allocator, uint32 capacity, uint32 source);
uint32 ROMlib_cfc_CFBagGetCount(uint32 object);
uint32 ROMlib_cfc_CFBagGetCountOfValue(uint32 object, uint32 value);
uint32 ROMlib_cfc_CFBagGetTypeID(void);
uint32 ROMlib_cfc_CFBagGetValue(uint32 object, uint32 key);
uint32 ROMlib_cfc_CFBagGetValueIfPresent(uint32 object, uint32 key, uint32 output);
void ROMlib_cfc_CFBagGetValues(uint32 object, uint32 output);
void ROMlib_cfc_CFBagRemoveAllValues(uint32 object);
void ROMlib_cfc_CFBagRemoveValue(uint32 object, uint32 key);
void ROMlib_cfc_CFBagReplaceValue(uint32 object, uint32 key);
void ROMlib_cfc_CFBagSetValue(uint32 object, uint32 key);
void ROMlib_cfc_CFDictionaryAddValue(uint32 object, uint32 key, uint32 value);
void ROMlib_cfc_CFDictionaryApplyFunction(uint32 object, uint32 procedure, uint32 context);
uint32 ROMlib_cfc_CFDictionaryContainsKey(uint32 object, uint32 key);
uint32 ROMlib_cfc_CFDictionaryContainsValue(uint32 object, uint32 value);
uint32 ROMlib_cfc_CFDictionaryCreate(uint32 allocator, uint32 keys, uint32 values, uint32 count, uint32 callbacks,
                                     uint32 value_callbacks);
uint32 ROMlib_cfc_CFDictionaryCreateCopy(uint32 allocator, uint32 source);
uint32 ROMlib_cfc_CFDictionaryCreateMutable(uint32 allocator, uint32 capacity, uint32 callbacks,
                                            uint32 value_callbacks);
uint32 ROMlib_cfc_CFDictionaryCreateMutableCopy(uint32 allocator, uint32 capacity, uint32 source);
uint32 ROMlib_cfc_CFDictionaryGetCount(uint32 object);
uint32 ROMlib_cfc_CFDictionaryGetCountOfKey(uint32 object, uint32 key);
uint32 ROMlib_cfc_CFDictionaryGetCountOfValue(uint32 object, uint32 value);
void ROMlib_cfc_CFDictionaryGetKeysAndValues(uint32 object, uint32 keys, uint32 values);
uint32 ROMlib_cfc_CFDictionaryGetTypeID(void);
uint32 ROMlib_cfc_CFDictionaryGetValue(uint32 object, uint32 key);
uint32 ROMlib_cfc_CFDictionaryGetValueIfPresent(uint32 object, uint32 key, uint32 output);
void ROMlib_cfc_CFDictionaryRemoveAllValues(uint32 object);
void ROMlib_cfc_CFDictionaryRemoveValue(uint32 object, uint32 key);
void ROMlib_cfc_CFDictionaryReplaceValue(uint32 object, uint32 key, uint32 value);
void ROMlib_cfc_CFDictionarySetValue(uint32 object, uint32 key, uint32 value);
void ROMlib_cfc_CFSetAddValue(uint32 object, uint32 key);
void ROMlib_cfc_CFSetApplyFunction(uint32 object, uint32 procedure, uint32 context);
uint32 ROMlib_cfc_CFSetCreate(uint32 allocator, uint32 values, uint32 count, uint32 callbacks);
uint32 ROMlib_cfc_CFSetCreateCopy(uint32 allocator, uint32 source);
uint32 ROMlib_cfc_CFSetCreateMutable(uint32 allocator, uint32 capacity, uint32 callbacks);
uint32 ROMlib_cfc_CFSetCreateMutableCopy(uint32 allocator, uint32 capacity, uint32 source);
uint32 ROMlib_cfc_CFSetGetValueIfPresent(uint32 object, uint32 key, uint32 output);
void ROMlib_cfc_CFSetRemoveAllValues(uint32 object);
void ROMlib_cfc_CFSetRemoveValue(uint32 object, uint32 key);
void ROMlib_cfc_CFSetReplaceValue(uint32 object, uint32 key);
void ROMlib_cfc_CFSetSetValue(uint32 object, uint32 key);
uint32 ROMlib_cfc_CFPreferencesCopyAppValue(uint32 key, uint32 application);
uint32 ROMlib_cfc_CFPreferencesAppBooleanValue(uint32 key, uint32 application, uint32 output);
uint32 ROMlib_cfc_CFPreferencesGetAppBooleanValue(uint32 key, uint32 application, uint32 output);
uint32 ROMlib_cfc_CFPreferencesGetAppIntegerValue(uint32 key, uint32 application, uint32 output);
void ROMlib_cfc_CFPreferencesSetAppValue(uint32 key, uint32 value, uint32 application);
uint32 ROMlib_cfc_CFPreferencesAppSynchronize(uint32 application);
void ROMlib_cfc_CFPreferencesAddSuitePreferencesToApp(uint32 application, uint32 suite);
void ROMlib_cfc_CFPreferencesRemoveSuitePreferencesFromApp(uint32 application, uint32 suite);
uint32 ROMlib_cfc_CFPreferencesCopyValue(uint32 key, uint32 application, uint32 user, uint32 host);
uint32 ROMlib_cfc_CFPreferencesCopyMultiple(uint32 keys, uint32 application, uint32 user, uint32 host);
void ROMlib_cfc_CFPreferencesSetValue(uint32 key, uint32 value, uint32 application, uint32 user, uint32 host);
void ROMlib_cfc_CFPreferencesSetMultiple(uint32 values, uint32 removed, uint32 application, uint32 user, uint32 host);
uint32 ROMlib_cfc_CFPreferencesSynchronize(uint32 application, uint32 user, uint32 host);
uint32 ROMlib_cfc_CFPreferencesCopyApplicationList(uint32 user, uint32 host);
uint32 ROMlib_cfc_CFPreferencesCopyKeyList(uint32 application, uint32 user, uint32 host);
uint32 ROMlib_cfc_CFPropertyListCreateDeepCopy(uint32 allocator, uint32 object, uint32 options);
uint32 ROMlib_cfc_CFPropertyListCreateXMLData(uint32 allocator, uint32 object);
uint32 ROMlib_cfc_CFPropertyListCreateFromXMLData(uint32 allocator, uint32 data, uint32 options, uint32 error);
uint32 ROMlib_cfc_CFRuntimeRegisterClass(uint32 klass);
uint32 ROMlib_cfc_CFRuntimeGetClassWithTypeID(uint32 type);
uint32 ROMlib_cfc_CFRuntimeCreateInstance(uint32 allocator, uint32 type, uint32 extra, uint32 category);
void ROMlib_cfc_CFRuntimeSetInstanceTypeID(uint32 object, uint32 type);
uint32 ROMlib_cfc_CFStorageCreate(uint32 allocator, uint32 size);
uint32 ROMlib_cfc_CFStorageGetTypeID(void);
uint32 ROMlib_cfc_CFStorageGetCount(uint32 object);
uint32 ROMlib_cfc_CFStorageGetValueAtIndex(uint32 object, uint32 index, uint32 range);
void ROMlib_cfc_CFStorageInsertValues(uint32 object, uint32 location, uint32 length);
void ROMlib_cfc_CFStorageDeleteValues(uint32 object, uint32 location, uint32 length);
void ROMlib_cfc_CFStorageGetValues(uint32 object, uint32 location, uint32 length, uint32 output);
void ROMlib_cfc_CFStorageReplaceValues(uint32 object, uint32 location, uint32 length, uint32 input);
void ROMlib_cfc_CFStorageApplyFunction(uint32 object, uint32 location, uint32 length, uint32 procedure, uint32 context);
uint32 ROMlib_cfc_CFTreeGetTypeID(void);
uint32 ROMlib_cfc_CFTreeCreate(uint32 allocator, uint32 context);
uint32 ROMlib_cfc_CFTreeGetChildCount(uint32 tree);
uint32 ROMlib_cfc_CFTreeGetParent(uint32 tree);
uint32 ROMlib_cfc_CFTreeGetNextSibling(uint32 tree);
uint32 ROMlib_cfc_CFTreeGetFirstChild(uint32 tree);
uint32 ROMlib_cfc_CFTreeFindRoot(uint32 tree);
void ROMlib_cfc_CFTreeGetContext(uint32 tree, uint32 output);
void ROMlib_cfc_CFTreeSetContext(uint32 tree, uint32 context);
void ROMlib_cfc_CFTreeSortChildren(uint32 tree, uint32 comparator, uint32 context);
uint32 ROMlib_cfc_CFTreeGetChildAtIndex(uint32 tree, uint32 index);
void ROMlib_cfc_CFTreeGetChildren(uint32 tree, uint32 output);
void ROMlib_cfc_CFTreeApplyFunctionToChildren(uint32 tree, uint32 procedure, uint32 context);
void ROMlib_cfc_CFTreePrependChild(uint32 tree, uint32 child);
void ROMlib_cfc_CFTreeAppendChild(uint32 tree, uint32 child);
void ROMlib_cfc_CFTreeInsertSibling(uint32 tree, uint32 sibling);
void ROMlib_cfc_CFTreeRemove(uint32 tree);
void ROMlib_cfc_CFTreeRemoveAllChildren(uint32 tree);
uint32 ROMlib_cfc_CFUUIDGetTypeID(void);
uint32 ROMlib_cfc_CFUUIDCreate(uint32 allocator);
uint32 ROMlib_cfc_CFUUIDCreateFromUUIDBytes(uint32 allocator, uint32 first, uint32 second, uint32 third, uint32 fourth);
uint32 ROMlib_cfc_CFUUIDGetUUIDBytes(uint32 output, uint32 object);
uint32 ROMlib_cfc_CFUUIDCreateFromString(uint32 allocator, uint32 string);
uint32 ROMlib_cfc_CFUUIDCreateString(uint32 allocator, uint32 object);

#endif
