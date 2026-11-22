/*+===================================================================
  File:      cfx_entries.h

  Summary:   Entry declarations for the 100-API CoreFoundation service cohort.

  Classes:   None.

  Functions: Interface declarations; implementations are tracked by cf-services-batch.json.

  Origin:    Native entry interfaces for the local CoreFoundationLib 1.4 service reconstruction.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_CFX_ENTRIES_H
#define ROMAW_CARBON_CFX_ENTRIES_H

int ROMlib_cfx_CFAbsoluteTimeAddGregorianUnits(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFAbsoluteTimeGetDayOfWeek(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFAbsoluteTimeGetDayOfYear(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFAbsoluteTimeGetDifferenceAsGregorianUnits(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFAbsoluteTimeGetGregorianDate(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFAbsoluteTimeGetWeekOfYear(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBSearch(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFCharacterSetCompact(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFCharacterSetFast(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFCopyTypeIDDescription(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFDateCreate(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFGregorianDateGetAbsoluteTime(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFGregorianDateIsValid(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFHashBytes(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFLog(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFMergeSortArray(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFQSortArray(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFShow(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFStringAppendFormat(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFStringAppendFormatAndArguments(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFStringCreateWithFormat(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFStringCreateWithFormatAndArguments(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFStringFileSystemEncoding(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFStringGetDoubleValue(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFURLCreateDataAndPropertiesFromResource(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFURLCreatePropertyFromResource(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFURLCreateStringByAddingPercentEscapes(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFURLCreateStringByReplacingPercentEscapes(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFURLDestroyResource(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFURLWriteDataAndPropertiesToResource(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFUUIDCreateWithBytes(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFUUIDGetConstantUUIDWithBytes(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFAppendPathComponent(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFAppendPathExtension(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFArgv(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFShowStr(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleCopyBuiltInPlugInsURL(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleCopyBundleRegionsArray(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleCopyBundleURLForExecutableURL(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleCopyExecutableURLInDirectory(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleCopyInfoDictionaryInResourceFork(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleCopyInfoPlistURL(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleCopyLanguageSearchListInDirectory(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleCopyPrivateFrameworksURL(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleCopyResourceForkURL(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleCopyResourceURLForLanguage(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleCopyResourceURLsOfTypeForLanguage(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleCopySharedFrameworksURL(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleCopySharedSupportURL(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleCreateIfLooksLikeBundle(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleCreateWithExecutableURLIfLooksLikeBundle(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleGetCFMFunctionPointerForName(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleGetCFMFunctionPointersForNames(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleGetHasChanged(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleGetLanguageAndRegionCodes(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleGetLanguageSearchList(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleGetLocalInfoDictionary(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleGetMainBundleIfLooksLikeBundle(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleGetPackageInfoInDirectory(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleGetValueForInfoKey(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleLayoutVersion(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleOpenBundleResourceFork(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFBundleURLLooksLikeBundle(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFContentsOfDirectory(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFCopyExtensionForAbstractType(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFCopySystemVersionDictionary(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFCreateDirectory(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFCreateURLFromFSSpec(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFDeleteFile(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFEnviron(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFGetEnvironmentVariable(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFGetFSSpecFromURL(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFGetFileProperties(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFIsAbsolutePath(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFLengthAfterDeletingLastPathComponent(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFLengthAfterDeletingPathExtension(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFProcessName(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFProcessNameString(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFProcessPath(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFPropertyListCreateFromXMLData(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFReadBytesFromFile(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFRemoveDirectory(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFStartOfLastPathComponent(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFStartOfPathExtension(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFStringCreateWithBytesNoCopy(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFStringCreateWithFormatAndArgumentsAux(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFStringEncodingSetForceASCIICompatibility(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFStringGetFileSystemRepresentation(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFStringIsLegalURLString(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFStripTrailingPathSlashes(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFTemporaryMemoryAllocator(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFTransmutePathSlashes(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFWriteBytesToFile(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFRangeMake(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFStorageGetCapacity(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFStorageGetValueSize(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFStringCheckMutableAndReplace(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFStringComputeEightBitStringEncoding(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFStringIsEightBit(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFStringIsMutable(ROMlib_carbon_ppc_frame* call_frame);

#endif
