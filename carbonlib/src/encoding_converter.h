/*+===================================================================
  File:      encoding_converter.h

  Summary:   Declares native Text Encoding Converter operations.

  Classes:   None.

  Functions: Declares ROMlib_carbon_tec and its export wrappers.

  Origin:    Mac OS 9.2.1 Text Encoding Converter and Universal Interfaces 3.4.2.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_ENCODING_CONVERTER_H
#define ROMAW_CARBON_ENCODING_CONVERTER_H

#include "internal.h"

enum {
    tec_count_available,
    tec_count_direct,
    tec_count_sniffers,
    tec_get_available,
    tec_get_direct,
    tec_get_sniffers,
    tec_get_internet_name,
    tec_from_internet_name,
    tec_create,
    tec_create_path,
    tec_dispose,
    tec_convert,
    tec_flush,
    tec_clear,
    tec_encoding_list,
    tec_count_sub,
    tec_get_sub,
    tec_count_mail,
    tec_get_mail,
    tec_count_web,
    tec_get_web,
    tec_count_destinations,
    tec_get_destinations,
    tec_create_sniffer,
    tec_clear_sniffer,
    tec_dispose_sniffer,
    tec_sniff,
    tec_create_multiple,
    tec_convert_multiple,
    tec_flush_multiple,
    tec_info,
};

int ROMlib_carbon_tec(const char* name, uint32_t offset, int operation, ROMlib_carbon_ppc_frame* call);

#ifdef CARBON_TEC_EXPORTS
#define CARBON_TEC(name, offset, operation)                       \
    static int carbon_tec_##name(ROMlib_carbon_ppc_frame* call)   \
    {                                                             \
        return ROMlib_carbon_tec(#name, offset, operation, call); \
    }

CARBON_TEC(TECCountAvailableTextEncodings, 0x1fac, tec_count_available)
CARBON_TEC(TECCountDirectTextEncodingConversions, 0x21a4, tec_count_direct)
CARBON_TEC(TECCountAvailableSniffers, 0x301c, tec_count_sniffers)
CARBON_TEC(TECGetAvailableTextEncodings, 0x2054, tec_get_available)
CARBON_TEC(TECGetDirectTextEncodingConversions, 0x224c, tec_get_direct)
CARBON_TEC(TECGetAvailableSniffers, 0x30c4, tec_get_sniffers)
CARBON_TEC(TECGetTextEncodingInternetName, 0x2a80, tec_get_internet_name)
CARBON_TEC(TECGetTextEncodingFromInternetName, 0x2b34, tec_from_internet_name)
CARBON_TEC(TECCreateConverter, 0x1d3c, tec_create)
CARBON_TEC(TECCreateConverterFromPath, 0x1b5c, tec_create_path)
CARBON_TEC(TECDisposeConverter, 0x1518, tec_dispose)
CARBON_TEC(TECConvertText, 0x53ac, tec_convert)
CARBON_TEC(TECFlushText, 0x549c, tec_flush)
CARBON_TEC(TECClearConverterContextInfo, 0x56ec, tec_clear)
CARBON_TEC(TECGetEncodingList, 0x3d7c, tec_encoding_list)

CARBON_TEC(TECCountSubTextEncodings, 0x28d0, tec_count_sub)
CARBON_TEC(TECGetSubTextEncodings, 0x2988, tec_get_sub)
CARBON_TEC(TECCountMailTextEncodings, 0x2df0, tec_count_mail)
CARBON_TEC(TECGetMailTextEncodings, 0x2ee8, tec_get_mail)
CARBON_TEC(TECCountWebTextEncodings, 0x2bc4, tec_count_web)
CARBON_TEC(TECGetWebTextEncodings, 0x2cbc, tec_get_web)
CARBON_TEC(TECCountDestinationTextEncodings, 0x2494, tec_count_destinations)
CARBON_TEC(TECGetDestinationTextEncodings, 0x266c, tec_get_destinations)
CARBON_TEC(TECCreateSniffer, 0x1068, tec_create_sniffer)
CARBON_TEC(TECClearSnifferContextInfo, 0xfcc, tec_clear_sniffer)
CARBON_TEC(TECDisposeSniffer, 0x1208, tec_dispose_sniffer)
CARBON_TEC(TECSniffTextEncoding, 0x12b0, tec_sniff)

CARBON_TEC(TECCreateOneToManyConverter, 0x18a4, tec_create_multiple)
CARBON_TEC(TECConvertTextToMultipleEncodings, 0x5504, tec_convert_multiple)
CARBON_TEC(TECFlushMultipleEncodings, 0x5638, tec_flush_multiple)
CARBON_TEC(TECGetInfo, 0x2b58, tec_info)

#undef CARBON_TEC
#endif

#endif
