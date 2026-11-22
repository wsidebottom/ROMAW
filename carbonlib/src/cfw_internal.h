/*+===================================================================
  File:      cfw_internal.h

  Summary:   Internal declarations for the framework/preference worker cohort.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    C reconstruction from the user's local CarbonLib 1.4 disassembly; public contracts use Universal Interfaces
             3.4.2.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_CFW_INTERNAL_H
#define ROMAW_CARBON_CFW_INTERNAL_H

#include "cfc_cohort.h"

/* Each argument is a raw PPC parameter word, including explicit range words. */
#define CFW_CALL(name, ...) \
    cfc_public(name, (const uint32[]){__VA_ARGS__}, sizeof((const uint32[]){__VA_ARGS__}) / sizeof(uint32))

uint32 ROMlib_cfw_PreferencesStandardDomain(uint32 application, uint32 user, uint32 host);
uint32 ROMlib_cfw_SynchronizeDomainCache(void);
uint32 ROMlib_cfw_PreferencesCreateDomainList(uint32 user, uint32 host);

uint32 ROMlib_cfw_PreferencesDomainCreate(uint32 context, uint32 callbacks);
uint32 ROMlib_cfw_PreferencesDomainCreateValueForKey(uint32 domain, uint32 key);
void ROMlib_cfw_PreferencesDomainSet(uint32 domain, uint32 key, uint32 value);
uint32 ROMlib_cfw_PreferencesDomainSynchronize(uint32 domain);
void ROMlib_cfw_PreferencesDomainGetKeysAndValues(uint32 allocator, uint32 domain, uint32 keys, uint32 count);
void ROMlib_cfw_PreferencesDomainSetIsWorldReadable(uint32 domain, uint32 readable);
uint32 ROMlib_cfw_PreferencesAllocator(void);
uint32 ROMlib_cfw_PreferencesDomainCopyDictionary(uint32 domain);
void ROMlib_cfw_PreferencesDomainSetDictionary(uint32 domain, uint32 dictionary);
uint32 ROMlib_cfw_PreferencesDomainExists(uint32 application, uint32 user, uint32 host);

void ROMlib_cfw_ApplicationPreferencesUpdate(uint32 preferences);
uint32 ROMlib_cfw_ApplicationPreferencesCreateValueForKey(uint32 preferences, uint32 key);
void ROMlib_cfw_ApplicationPreferencesSet(uint32 preferences, uint32 key, uint32 value);
void ROMlib_cfw_ApplicationPreferencesRemove(uint32 preferences, uint32 key);
uint32 ROMlib_cfw_ApplicationPreferencesSynchronize(uint32 preferences);
uint32 ROMlib_cfw_ApplicationPreferencesCreateWithUser(uint32 user, uint32 application);
void ROMlib_cfw_ApplicationPreferencesSetStandardSearchList(uint32 preferences);
uint32 ROMlib_cfw_StandardApplicationPreferences(uint32 application);
void ROMlib_cfw_ApplicationPreferencesSetCacheForApp(uint32 preferences, uint32 application);
void ROMlib_cfw_DeallocateApplicationPreferences(uint32 preferences);
uint32 ROMlib_cfw_ApplicationPreferencesCopyRepresentationWithHint(uint32 preferences, uint32 hint);
uint32 ROMlib_cfw_ApplicationPreferencesCopyRepresentation(uint32 preferences);
void ROMlib_cfw_ApplicationPreferencesSetSearchList(uint32 preferences, uint32 list);
void ROMlib_cfw_ApplicationPreferencesAddDomain(uint32 preferences, uint32 value, uint32 front);
uint32 ROMlib_cfw_ApplicationPreferencesContainsDomain(uint32 preferences, uint32 value);
void ROMlib_cfw_ApplicationPreferencesRemoveDomain(uint32 preferences, uint32 value);
void ROMlib_cfw_ApplicationPreferencesRemoveSuitePreferences(uint32 preferences, uint32 suite);
void ROMlib_cfw_ApplicationPreferencesAddSuitePreferences(uint32 preferences, uint32 suite);

uint32 ROMlib_cfw_XMLNodeGetTypeID(void);
uint32 ROMlib_cfw_XMLParserGetTypeID(void);
uint32 ROMlib_cfw_XMLNodeGetTypeCode(uint32 object);
uint32 ROMlib_cfw_XMLNodeGetString(uint32 object);
uint32 ROMlib_cfw_XMLNodeGetInfoPtr(uint32 object);
uint32 ROMlib_cfw_XMLNodeGetVersion(uint32 object);
uint32 ROMlib_cfw_XMLParserGetSourceURL(uint32 object);
uint32 ROMlib_cfw_XMLParserGetStatusCode(uint32 object);
uint32 ROMlib_cfw_XMLParserGetLocation(uint32 object);
uint32 ROMlib_cfw_XMLParserGetLineNumber(uint32 object);
void ROMlib_cfw_XMLParserGetContext(uint32 parser, uint32 output);
void ROMlib_cfw_XMLParserGetCallBacks(uint32 parser, uint32 output);
uint32 ROMlib_cfw_XMLParserGetDocument(uint32 parser);
uint32 ROMlib_cfw_XMLParserCopyErrorDescription(uint32 parser);
void ROMlib_cfw_XMLParserAbort(uint32 parser, uint32 code, uint32 description);
uint32 ROMlib_cfw_XMLNodeCreate(uint32 allocator, uint32 type, uint32 string, uint32 info, uint32 version);
uint32 ROMlib_cfw_XMLNodeCreateCopy(uint32 allocator, uint32 node);
uint32 ROMlib_cfw_XMLParserCreate(uint32 allocator, uint32 data, uint32 url, uint32 options, uint32 version,
                                  uint32 callbacks, uint32 context);
uint32 ROMlib_cfw_XMLParserCreateWithDataFromURL(uint32 allocator, uint32 url, uint32 options, uint32 version,
                                                 uint32 callbacks, uint32 context);
uint32 ROMlib_cfw_XMLParserParse(uint32 parser);
uint32 ROMlib_cfw_XMLTreeCreateWithNode(uint32 allocator, uint32 node);
uint32 ROMlib_cfw_XMLTreeGetNode(uint32 tree);
uint32 ROMlib_cfw_XMLTreeCreateXMLData(uint32 allocator, uint32 tree);
uint32 ROMlib_cfw_XMLTreeCreateWithDataFromURL(uint32 allocator, uint32 url, uint32 options, uint32 version);
uint32 ROMlib_cfw_XMLTreeCreateFromData(uint32 allocator, uint32 data, uint32 url, uint32 options, uint32 version);

uint32 ROMlib_cfw_PlugInGetTypeID(void);
uint32 ROMlib_cfw_PlugInCreate(uint32 allocator, uint32 url);
uint32 ROMlib_cfw_PlugInGetBundle(uint32 plugin);
uint32 ROMlib_cfw_PlugInInstanceGetTypeID(void);
uint32 ROMlib_cfw_PlugInInstanceGetInstanceData(uint32 instance);
uint32 ROMlib_cfw_PlugInInstanceGetFactoryName(uint32 instance);
uint32 ROMlib_cfw_PlugInInstanceGetInterfaceFunctionTable(uint32 instance, uint32 name, uint32 output);
uint32 ROMlib_cfw_PlugInInstanceCreateWithInstanceDataSize(uint32 allocator, uint32 size, uint32 deallocate,
                                                           uint32 name, uint32 get_interface);
uint32 ROMlib_cfw_PlugInIsLoadOnDemand(uint32 plugin);
void ROMlib_cfw_PlugInSetLoadOnDemand(uint32 plugin, uint32 flag);
uint32 ROMlib_cfw_PlugInFindFactoriesForPlugInType(uint32 type);
uint32 ROMlib_cfw_PlugInFindFactoriesForPlugInTypeInPlugIn(uint32 type, uint32 plugin);
uint32 ROMlib_cfw_PlugInRegisterFactoryFunction(uint32 uuid, uint32 procedure);
uint32 ROMlib_cfw_PlugInRegisterFactoryFunctionByName(uint32 uuid, uint32 plugin, uint32 name);
uint32 ROMlib_cfw_PlugInUnregisterFactory(uint32 uuid);
uint32 ROMlib_cfw_PlugInRegisterPlugInType(uint32 uuid, uint32 type);
uint32 ROMlib_cfw_PlugInUnregisterPlugInType(uint32 uuid, uint32 type);
void ROMlib_cfw_PlugInAddInstanceForFactory(uint32 uuid);
void ROMlib_cfw_PlugInRemoveInstanceForFactory(uint32 uuid);
uint32 ROMlib_cfw_PlugInInstanceCreate(uint32 allocator, uint32 uuid, uint32 type);
void ROMlib_cfw_AllocatorSetDefault(uint32 allocator);

uint32 ROMlib_cfw_BundleGetMainBundle(void);
uint32 ROMlib_cfw_BundleGetAllBundles(void);
uint32 ROMlib_cfw_BundleGetBundleWithIdentifier(uint32 identifier);
uint32 ROMlib_cfw_BundleCopyInfoDictionaryInDirectory(uint32 url);
uint32 ROMlib_cfw_BundleCreateBundlesFromDirectory(uint32 allocator, uint32 directory, uint32 type);
uint32 ROMlib_cfw_BundleCopySupportFilesDirectoryURL(uint32 bundle);
uint32 ROMlib_cfw_BundleCopyResourcesDirectoryURL(uint32 bundle);
uint32 ROMlib_cfw_BundleCopyExecutableURL(uint32 bundle);
uint32 ROMlib_cfw_BundleCopyAuxiliaryExecutableURL(uint32 bundle, uint32 name);
uint32 ROMlib_cfw_BundleCopyPrivateFrameworksURL(uint32 bundle);
uint32 ROMlib_cfw_BundleCopySharedFrameworksURL(uint32 bundle);
uint32 ROMlib_cfw_BundleCopySharedSupportURL(uint32 bundle);
uint32 ROMlib_cfw_BundleCopyBuiltInPlugInsURL(uint32 bundle);
uint32 ROMlib_cfw_BundleCopyResourceURL(uint32 bundle, uint32 name, uint32 type, uint32 subdirectory);
uint32 ROMlib_cfw_BundleCopyResourceURLsOfType(uint32 bundle, uint32 type, uint32 subdirectory);
uint32 ROMlib_cfw_BundleCopyResourceURLForLocalization(uint32 bundle, uint32 name, uint32 type, uint32 subdirectory,
                                                       uint32 localization);
uint32 ROMlib_cfw_BundleCopyResourceURLsOfTypeForLocalization(uint32 bundle, uint32 type, uint32 subdirectory,
                                                              uint32 localization);
uint32 ROMlib_cfw_BundleCopyResourceURLInDirectory(uint32 url, uint32 name, uint32 type, uint32 subdirectory);
uint32 ROMlib_cfw_BundleCopyResourceURLsOfTypeInDirectory(uint32 url, uint32 type, uint32 subdirectory);
uint32 ROMlib_cfw_BundleGetPackageInfoInDirectory(uint32 url, uint32 type, uint32 creator);
void ROMlib_cfw_BundleGetPackageInfo(uint32 bundle, uint32 type, uint32 creator);
uint32 ROMlib_cfw_BundleOpenBundleResourceFiles(uint32 bundle, uint32 reference_number, uint32 localized_number);
uint32 ROMlib_cfw_BundleOpenBundleResourceMap(uint32 bundle);
void ROMlib_cfw_BundleCloseBundleResourceMap(uint32 bundle, uint32 number);

uint32 ROMlib_cfw_BundleGetLocalizationInfoForLocalization(uint32 localization, uint32 language_out, uint32 region_out,
                                                           uint32 script_out, uint32 encoding_out);
uint32 ROMlib_cfw_BundleCopyLocalizationForLocalizationInfo(uint32 language, uint32 region, uint32 script,
                                                            uint32 encoding);
uint32 ROMlib_cfw_BundleCopyBundleLocalizations(uint32 bundle);
uint32 ROMlib_cfw_BundleCopyLocalizedString(uint32 bundle, uint32 key, uint32 value, uint32 table);
uint32 ROMlib_cfw_BundleCopyPreferredLocalizationsFromArray(uint32 localizations);

#endif
