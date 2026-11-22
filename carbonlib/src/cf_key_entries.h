/*+===================================================================
  File:      cf_key_entries.h

  Summary:   Reference export indices, guest slots and values of CoreFoundation string keys.

  Classes:   None.

  Functions: Macro expansion data for the export registry and key initializers.

  Origin:    CarbonLib 1.4 import table and CoreFoundationLib 1.4 key initializers.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

/* Preferences. */
CARBON_CF_KEY("kCFPreferencesAnyApplication", 0x1ba, 0x217c, "kCFPreferencesAnyApplication")
CARBON_CF_KEY("kCFPreferencesAnyHost", 0xfa, 0x2180, "kCFPreferencesAnyHost")
CARBON_CF_KEY("kCFPreferencesAnyUser", 0xa2, 0x2184, "kCFPreferencesAnyUser")
CARBON_CF_KEY("kCFPreferencesCurrentApplication", 0xfb, 0x2188, "kCFPreferencesCurrentApplication")
CARBON_CF_KEY("kCFPreferencesCurrentHost", 0x1d0, 0x218c, "kCFPreferencesCurrentHost")
CARBON_CF_KEY("kCFPreferencesCurrentUser", 0x4c, 0x2190, "kCFPreferencesCurrentUser")

/* URL properties. */
CARBON_CF_KEY("kCFURLFileExists", 0x1ce, 0x21f0, "kCFURLFileExists")
CARBON_CF_KEY("kCFURLFilePOSIXMode", 0x1d9, 0x21f4, "kCFURLFilePOSIXMode")
CARBON_CF_KEY("kCFURLFileDirectoryContents", 0x10d, 0x21f8, "kCFURLFileDirectoryContents")
CARBON_CF_KEY("kCFURLFileLength", 0x144, 0x21fc, "kCFURLFileLength")
CARBON_CF_KEY("kCFURLFileLastModificationTime", 0x152, 0x2200, "kCFURLFileLastModificationTime")
CARBON_CF_KEY("kCFURLFileOwnerID", 0x1a8, 0x2204, "kCFURLFileOwnerID")
CARBON_CF_KEY("kCFURLHTTPStatusCode", 0x54, 0x2208, "kCFURLHTTPStatusCode")
CARBON_CF_KEY("kCFURLHTTPStatusLine", 0x1b7, 0x220c, "kCFURLHTTPStatusLine")
CARBON_CF_KEY_ALIAS("kCFFileURLExists", 0x9f, 0x2210, 0x21f0)
CARBON_CF_KEY_ALIAS("kCFFileURLPOSIXMode", 0x52, 0x2214, 0x21f4)
CARBON_CF_KEY_ALIAS("kCFFileURLSize", 0x91, 0x2218, 0x21fc)
CARBON_CF_KEY_ALIAS("kCFFileURLDirectoryContents", 0x77, 0x221c, 0x21f8)
CARBON_CF_KEY_ALIAS("kCFFileURLLastModificationTime", 0x1ae, 0x2220, 0x2200)
CARBON_CF_KEY_ALIAS("kCFHTTPURLStatusCode", 0x1dc, 0x2224, 0x2208)
CARBON_CF_KEY_ALIAS("kCFHTTPURLStatusLine", 0x20, 0x2228, 0x220c)

/* Bundle properties. */
CARBON_CF_KEY("kCFBundleInfoDictionaryVersionKey", 0x9c, 0x2274, "CFBundleInfoDictionaryVersion")
CARBON_CF_KEY("kCFBundleExecutableKey", 0x153, 0x2278, "CFBundleExecutable")
CARBON_CF_KEY("kCFBundleIdentifierKey", 0x155, 0x227c, "CFBundleIdentifier")
CARBON_CF_KEY("kCFBundleVersionKey", 0x73, 0x2280, "CFBundleVersion")
CARBON_CF_KEY("kCFBundleDevelopmentRegionKey", 0x17e, 0x2284, "CFBundleDevelopmentRegion")
CARBON_CF_KEY("_kCFBundlePackageTypeKey", 0x1fa, 0x2288, "CFBundlePackageType")
CARBON_CF_KEY("_kCFBundleSignatureKey", 0x201, 0x228c, "CFBundleSignature")
CARBON_CF_KEY("_kCFBundleIconFileKey", 0x20c, 0x2290, "CFBundleIconFile")
CARBON_CF_KEY("_kCFBundleDocumentTypesKey", 0x21e, 0x2294, "CFBundleDocumentTypes")
CARBON_CF_KEY("_kCFBundleURLTypesKey", 0x210, 0x2298, "CFBundleURLTypes")
CARBON_CF_KEY("kCFBundleNameKey", 0x169, 0x229c, "CFBundleName")
CARBON_CF_KEY("_kCFBundleShortVersionStringKey", 0x1f1, 0x22a0, "CFBundleShortVersionString")
CARBON_CF_KEY("_kCFBundleGetInfoStringKey", 0x20b, 0x22a4, "CFBundleGetInfoString")
CARBON_CF_KEY("_kCFBundleGetInfoHTMLKey", 0x1fc, 0x22a8, "CFBundleGetInfoHTML")
CARBON_CF_KEY("_kCFBundleTypeNameKey", 0x21c, 0x22ac, "CFBundleTypeName")
CARBON_CF_KEY("_kCFBundleTypeRoleKey", 0x212, 0x22b0, "CFBundleTypeRole")
CARBON_CF_KEY("_kCFBundleTypeIconFileKey", 0x1f4, 0x22b4, "CFBundleTypeIconFile")
CARBON_CF_KEY("_kCFBundleTypeOSTypesKey", 0x218, 0x22b8, "CFBundleTypeOSTypes")
CARBON_CF_KEY("_kCFBundleTypeExtensionsKey", 0x213, 0x22bc, "CFBundleTypeExtensions")
CARBON_CF_KEY("_kCFBundleURLNameKey", 0x200, 0x22c0, "CFBundleURLName")
CARBON_CF_KEY("_kCFBundleURLIconFileKey", 0x217, 0x22c4, "CFBundleURLIconFile")
CARBON_CF_KEY("_kCFBundleURLSchemesKey", 0x1f3, 0x22c8, "CFBundleURLSchemes")
CARBON_CF_KEY("_kCFBundleOldExecutableKey", 0x215, 0x22cc, "NSExecutable")
CARBON_CF_KEY("_kCFBundleOldInfoDictionaryVersionKey", 0x1f0, 0x22d0, "NSInfoPlistVersion")
CARBON_CF_KEY("_kCFBundleOldNameKey", 0x202, 0x22d4, "NSHumanReadableName")
CARBON_CF_KEY("_kCFBundleOldIconFileKey", 0x219, 0x22d8, "NSIcon")
CARBON_CF_KEY("_kCFBundleOldDocumentTypesKey", 0x1f5, 0x22dc, "NSTypes")
CARBON_CF_KEY("_kCFBundleOldShortVersionStringKey", 0x221, 0x22e0, "NSAppVersion")
CARBON_CF_KEY("_kCFBundleOldTypeNameKey", 0x203, 0x22e4, "NSName")
CARBON_CF_KEY("_kCFBundleOldTypeRoleKey", 0x21a, 0x22e8, "NSRole")
CARBON_CF_KEY("_kCFBundleOldTypeIconFileKey", 0x1f7, 0x22ec, "NSIcon")
CARBON_CF_KEY("_kCFBundleOldTypeExtensions1Key", 0x20a, 0x22f0, "NSUnixExtensions")
CARBON_CF_KEY("_kCFBundleOldTypeExtensions2Key", 0x21b, 0x22f4, "NSDOSExtensions")
CARBON_CF_KEY("_kCFBundleOldTypeOSTypesKey", 0x222, 0x22f8, "NSMacOSType")
CARBON_CF_LOCAL_KEY(0x22fc, "CFBundleExecutablePath")
CARBON_CF_LOCAL_KEY(0x2300, "CFBundleInfoPlistURL")
CARBON_CF_LOCAL_KEY(0x2304, "CFBundleNumericVersion")
CARBON_CF_LOCAL_KEY(0x2308, "CSResourcesFileMapped")

/* Plug-in properties. */
CARBON_CF_KEY("kCFPlugInDynamicRegistrationKey", 0xdf, 0x305c, "CFPlugInDynamicRegistration")
CARBON_CF_KEY("kCFPlugInDynamicRegisterFunctionKey", 0x16b, 0x3060, "CFPlugInDynamicRegisterFunction")
CARBON_CF_KEY("kCFPlugInUnloadFunctionKey", 0xb1, 0x3064, "CFPlugInUnloadFunction")
CARBON_CF_KEY("kCFPlugInFactoriesKey", 0x199, 0x3068, "CFPlugInFactories")
CARBON_CF_KEY("kCFPlugInTypesKey", 0x172, 0x306c, "CFPlugInTypes")
