/*+===================================================================
  File:      cfw_xml.c

  Summary:   XML node allocation, parser state and callbacks, and XML tree entry control flow.

  Classes:   None.

  Functions: ROMlib_cfw_XMLNodeCreate, ROMlib_cfw_XMLNodeCreateCopy, ROMlib_cfw_XMLNodeGetInfoPtr,
             ROMlib_cfw_XMLNodeGetString, ROMlib_cfw_XMLNodeGetTypeCode, ROMlib_cfw_XMLNodeGetTypeID,
             ROMlib_cfw_XMLNodeGetVersion, ROMlib_cfw_XMLParserAbort, ROMlib_cfw_XMLParserCopyErrorDescription,
             ROMlib_cfw_XMLParserCreate, ROMlib_cfw_XMLParserCreateWithDataFromURL, ROMlib_cfw_XMLParserGetCallBacks,
             ROMlib_cfw_XMLParserGetContext, ROMlib_cfw_XMLParserGetDocument, ROMlib_cfw_XMLParserGetLineNumber,
             ROMlib_cfw_XMLParserGetLocation, ROMlib_cfw_XMLParserGetSourceURL, ROMlib_cfw_XMLParserGetStatusCode,
             ROMlib_cfw_XMLParserGetTypeID, ROMlib_cfw_XMLParserParse, ROMlib_cfw_XMLTreeCreateFromData,
             ROMlib_cfw_XMLTreeCreateWithDataFromURL, ROMlib_cfw_XMLTreeCreateWithNode, ROMlib_cfw_XMLTreeCreateXMLData,
             ROMlib_cfw_XMLTreeGetNode

  Origin:    C reconstruction of local CoreFoundationLib 1.4 XML exports. Scanner, recursive serializer and
             additional-info workers remain PPC.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "cfw_internal.h"

static uint32 xml_private(const ROMlib_carbon_reference* reference, uint32 offset, const uint32* arguments,
                          unsigned count)
{
    uint32 result = 0;
    cfp_private(reference, offset, arguments, count, &result);
    return result;
}

uint32 ROMlib_cfw_XMLNodeGetTypeID(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (!cfp_reference("CFXMLNodeGetTypeID", 0x521a4, NULL, 0, &reference, &result))
        return result;
    return cfp_toc(&reference, 0xa58);
}

uint32 ROMlib_cfw_XMLParserGetTypeID(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (!cfp_reference("CFXMLParserGetTypeID", 0x2b878, NULL, 0, &reference, &result))
        return result;
    return cfp_toc(&reference, 0x968);
}

uint32 ROMlib_cfw_XMLNodeGetTypeCode(uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object}, result = 0;
    if (!cfp_reference("CFXMLNodeGetTypeCode", 0x525e8, arguments, 1, &reference, &result))
        return result;
    return cfp_read(object + 0xc);
}

uint32 ROMlib_cfw_XMLNodeGetString(uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object}, result = 0;
    if (!cfp_reference("CFXMLNodeGetString", 0x525f0, arguments, 1, &reference, &result))
        return result;
    return cfp_read(object + 0x10);
}

uint32 ROMlib_cfw_XMLNodeGetInfoPtr(uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object}, result = 0;
    if (!cfp_reference("CFXMLNodeGetInfoPtr", 0x525f8, arguments, 1, &reference, &result))
        return result;
    return cfp_read(object + 0x14);
}

uint32 ROMlib_cfw_XMLNodeGetVersion(uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object}, result = 0;
    if (!cfp_reference("CFXMLNodeGetVersion", 0x52600, arguments, 1, &reference, &result))
        return result;
    return cfp_read(object + 0x8);
}

uint32 ROMlib_cfw_XMLParserGetSourceURL(uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object}, result = 0;
    if (!cfp_reference("CFXMLParserGetSourceURL", 0x2b8f0, arguments, 1, &reference, &result))
        return result;
    return cfp_read(object + 0xc);
}

uint32 ROMlib_cfw_XMLParserGetStatusCode(uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object}, result = 0;
    if (!cfp_reference("CFXMLParserGetStatusCode", 0x2b968, arguments, 1, &reference, &result))
        return result;
    return cfp_read(object + 0x90);
}

uint32 ROMlib_cfw_XMLParserGetLocation(uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object}, result = 0;
    if (!cfp_reference("CFXMLParserGetLocation", 0x2b8f8, arguments, 1, &reference, &result))
        return result;
    return cfp_read(object + 0x30);
}

uint32 ROMlib_cfw_XMLParserGetLineNumber(uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object}, result = 0;
    if (!cfp_reference("CFXMLParserGetLineNumber", 0x2b920, arguments, 1, &reference, &result))
        return result;
    return cfp_read(object + 0x34);
}

void ROMlib_cfw_XMLParserGetContext(uint32 parser, uint32 output)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {parser, output}, result = 0;
    if (!cfp_reference("CFXMLParserGetContext", 0x2b880, arguments, 2, &reference, &result))
        return;
    if (!output)
        return;
    /* Preserve the original paired-load order when source and output overlap. */
    for (unsigned i = 0; i < 20; i += 8) {
        uint32 first, second = 0;
        if (!cfp_get(parser + 0x7c + i, &first) || (i + 4 < 20 && !cfp_get(parser + 0x7c + i + 4, &second)))
            return;
        if (!cfp_write(output + i, first) || (i + 4 < 20 && !cfp_write(output + i + 4, second)))
            return;
    }
}

void ROMlib_cfw_XMLParserGetCallBacks(uint32 parser, uint32 output)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {parser, output}, result = 0;
    if (!cfp_reference("CFXMLParserGetCallBacks", 0x2b8b4, arguments, 2, &reference, &result))
        return;
    if (!output)
        return;
    /* Preserve the original paired-load order when source and output overlap. */
    for (unsigned i = 0; i < 24; i += 8) {
        uint32 first, second = 0;
        if (!cfp_get(parser + 0x64 + i, &first) || (i + 4 < 24 && !cfp_get(parser + 0x64 + i + 4, &second)))
            return;
        if (!cfp_write(output + i, first) || (i + 4 < 24 && !cfp_write(output + i + 4, second)))
            return;
    }
}

uint32 ROMlib_cfw_XMLParserGetDocument(uint32 parser)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {parser}, result = 0;
    if (!cfp_reference("CFXMLParserGetDocument", 0x2b948, arguments, 1, &reference, &result))
        return result;
    return cfp_read(parser + 0x50) ? cfp_read(cfp_read(parser + 0x48)) : 0;
}

uint32 ROMlib_cfw_XMLParserCopyErrorDescription(uint32 parser)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {parser}, result = 0;
    if (!cfp_reference("CFXMLParserCopyErrorDescription", 0x2b970, arguments, 1, &reference, &result))
        return result;
    result = cfp_read(parser + 0x94);
    cfp_call("CFRetain", &result, 1, NULL);
    return result;
}

void ROMlib_cfw_XMLParserAbort(uint32 parser, uint32 code, uint32 description)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {parser, code, description}, result = 0;
    if (!cfp_reference("CFXMLParserAbort", 0x2b998, arguments, 3, &reference, &result))
        return;
    if (!cfp_write(parser + 0x90, code))
        return;
    uint32 old = cfp_read(parser + 0x94);
    if (old)
        cfp_call("CFRelease", &old, 1, NULL);
    uint32 copy = CFW_CALL("CFStringCreateCopy", 0, description);
    cfp_write(parser + 0x94, copy);
}

uint32 ROMlib_cfw_XMLNodeCreate(uint32 allocator, uint32 type, uint32 string, uint32 info, uint32 version)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, type, string, info, version}, result = 0;
    if (!cfp_reference("CFXMLNodeCreate", 0x524b8, arguments, 5, &reference, &result))
        return result;
    static const unsigned sizes[16] = {0, 8, 12, 0, 4, 0, 0, 0, 0, 20, 4, 8, 0, 8, 4, 8};
    unsigned extra = type < 16 ? sizes[type] : 0;
    result = cfc_allocate_instance(&reference, allocator, cfp_toc(&reference, 0xa58), 16 + extra);
    if (!result)
        return 0;
    allocator = CFW_CALL("CFGetAllocator", result);
    cfp_write(result + 8, version);
    cfp_write(result + 12, type);
    uint32 copy = string ? CFW_CALL("CFStringCreateCopy", allocator, string) : 0;
    cfp_write(result + 16, copy);
    cfp_write(result + 20, extra ? result + 24 : 0);
    if (extra) {
        uint32 copy_info[] = {allocator, type, info, result + 24};
        xml_private(&reference, 0x521e4, copy_info, 4);
    }
    return result;
}

uint32 ROMlib_cfw_XMLNodeCreateCopy(uint32 allocator, uint32 node)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, node}, result = 0;
    if (!cfp_reference("CFXMLNodeCreateCopy", 0x521ac, arguments, 2, &reference, &result))
        return result;
    return ROMlib_cfw_XMLNodeCreate(allocator, cfp_read(node + 12), cfp_read(node + 16), cfp_read(node + 20),
                                    cfp_read(node + 8));
}

uint32 ROMlib_cfw_XMLParserCreate(uint32 allocator, uint32 data, uint32 url, uint32 options, uint32 version,
                                  uint32 callbacks, uint32 context)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, data, url, options, version, callbacks, context}, result = 0;
    if (!cfp_reference("CFXMLParserCreate", 0x2bbf4, arguments, 7, &reference, &result))
        return result;
    uint32 create[] = {allocator, url, options, data, version, callbacks, context};
    return xml_private(&reference, 0x2b9f8, create, 7);
}

uint32 ROMlib_cfw_XMLParserCreateWithDataFromURL(uint32 allocator, uint32 url, uint32 options, uint32 version,
                                                 uint32 callbacks, uint32 context)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, url, options, version, callbacks, context}, result = 0;
    if (!cfp_reference("CFXMLParserCreateWithDataFromURL", 0x2bc24, arguments, 6, &reference, &result))
        return result;
    uint32 create[] = {allocator, url, options, 0, version, callbacks, context};
    return xml_private(&reference, 0x2b9f8, create, 7);
}

uint32 ROMlib_cfw_XMLParserParse(uint32 parser)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {parser}, result = 0;
    if (!cfp_reference("CFXMLParserParse", 0x2bc5c, arguments, 1, &reference, &result))
        return result;
    if (cfp_read(parser + 0x90) != (uint32)-2)
        return 0;
    cfp_write(parser + 0x90, UINT32_MAX);
    uint32 stream = parser + 8;
    if (!(xml_private(&reference, 0x50028, &stream, 1) & 255)) {
        uint32 literals = cfp_toc(&reference, 0x574);
        uint32 allocator = CFW_CALL("CFGetAllocator", parser), error;
        if (!cfp_read(parser + 8)) {
            cfp_write(parser + 0x90, 15);
            uint32 url_string = CFW_CALL("CFURLGetString", cfp_read(parser + 12));
            uint32 format = CFW_CALL("__CFStringMakeConstantString", literals + 0x1f);
            error = CFW_CALL("CFStringCreateWithFormat", allocator, 0, format, url_string);
        }
        else {
            cfp_write(parser + 0x90, 2);
            error = CFW_CALL("CFStringCreateWithCString", allocator, literals + 0x33, 0x600);
        }
        cfp_write(parser + 0x94, error);
        uint32 procedure = cfp_read(parser + 0x78);
        if (procedure) {
            uint32 notify[] = {parser, cfp_read(parser + 0x90), cfp_read(parser + 0x80)};
            cfp_callback(procedure, notify, 3, NULL);
        }
        return 0;
    }
    uint32 allocator = CFW_CALL("CFGetAllocator", parser);
    uint32 stack = CFW_CALL("CFAllocatorAllocate", allocator, 64, 0);
    if (!stack)
        return 0;
    cfp_write(parser + 0x48, stack);
    cfp_write(parser + 0x50, 16);
    uint32 node = cfp_read(parser + 0x54);
    cfp_write(node + 12, 1);
    _Alignas(16) uint32 document[] = {CL(cfp_read(parser + 12)), CL(cfp_read(stream + 8))};
    cfp_write(node + 20, US_TO_SYN68K(document));
    uint32 create[] = {parser, node, cfp_read(parser + 0x80)}, tree = 0;
    if (!cfp_callback(cfp_read(parser + 0x68), create, 3, &tree) || !cfp_write(stack, tree))
        return 0;
    cfp_write(parser + 0x4c, stack);
    uint32 status = cfp_read(parser + 0x90);
    if (status != UINT32_MAX) {
        uint32 abort[] = {parser, status, 0};
        xml_private(&reference, 0x2f790, abort, 3);
        return 0;
    }
    return xml_private(&reference, 0x2f4e8, &parser, 1);
}

uint32 ROMlib_cfw_XMLTreeCreateWithNode(uint32 allocator, uint32 node)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, node}, result = 0;
    if (!cfp_reference("CFXMLTreeCreateWithNode", 0x52608, arguments, 2, &reference, &result))
        return result;
    _Alignas(32) uint32 context[] = {0, CL(node), CL(cfp_toc(&reference, 0x29c)), CL(cfp_toc(&reference, 0x298)),
                                     CL(cfp_toc(&reference, 0x294))};
    return ROMlib_cfc_CFTreeCreate(allocator, US_TO_SYN68K(context));
}

uint32 ROMlib_cfw_XMLTreeGetNode(uint32 tree)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {tree}, result = 0;
    if (!cfp_reference("CFXMLTreeGetNode", 0x52654, arguments, 1, &reference, &result))
        return result;
    _Alignas(32) uint32 context[5] = {0};
    ROMlib_cfc_CFTreeGetContext(tree, US_TO_SYN68K(context));
    return CL(context[1]);
}

uint32 ROMlib_cfw_XMLTreeCreateXMLData(uint32 allocator, uint32 tree)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, tree}, result = 0;
    if (!cfp_reference("CFXMLTreeCreateXMLData", 0x52688, arguments, 2, &reference, &result))
        return result;
    uint32 text = CFW_CALL("CFStringCreateMutable", allocator, 0);
    uint32 serialize[] = {text, tree};
    xml_private(&reference, 0x52754, serialize, 2);
    uint32 node = ROMlib_cfw_XMLTreeGetNode(tree), encoding = 0x08000100;
    if (ROMlib_cfw_XMLNodeGetTypeCode(node) == 1) {
        uint32 info = ROMlib_cfw_XMLNodeGetInfoPtr(node);
        if (info)
            encoding = cfp_read(info + 4);
    }
    result = CFW_CALL("CFStringCreateExternalRepresentation", allocator, text, encoding, 0);
    cfp_call("CFRelease", &text, 1, NULL);
    return result;
}

uint32 ROMlib_cfw_XMLTreeCreateWithDataFromURL(uint32 allocator, uint32 url, uint32 options, uint32 version)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, url, options, version}, result = 0;
    if (!cfp_reference("CFXMLTreeCreateWithDataFromURL", 0x2faa4, arguments, 4, &reference, &result))
        return result;
    _Alignas(32) uint32 callbacks[] = {
        0, CL(cfp_toc(&reference, 0x524)), CL(cfp_toc(&reference, 0x520)), CL(cfp_toc(&reference, 0x51c)), 0, 0};
    uint32 parser =
        ROMlib_cfw_XMLParserCreateWithDataFromURL(allocator, url, options, version, US_TO_SYN68K(callbacks), 0);
    if (ROMlib_cfw_XMLParserParse(parser) & 255)
        result = ROMlib_cfw_XMLParserGetDocument(parser);
    else {
        uint32 document = ROMlib_cfw_XMLParserGetDocument(parser);
        if (document)
            cfp_call("CFRelease", &document, 1, NULL);
    }
    cfp_call("CFRelease", &parser, 1, NULL);
    return result;
}

uint32 ROMlib_cfw_XMLTreeCreateFromData(uint32 allocator, uint32 data, uint32 url, uint32 options, uint32 version)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, data, url, options, version}, result = 0;
    if (!cfp_reference("CFXMLTreeCreateFromData", 0x2fb4c, arguments, 5, &reference, &result))
        return result;
    _Alignas(32) uint32 callbacks[] = {
        0, CL(cfp_toc(&reference, 0x524)), CL(cfp_toc(&reference, 0x520)), CL(cfp_toc(&reference, 0x51c)), 0, 0};
    uint32 parser = ROMlib_cfw_XMLParserCreate(allocator, data, url, options, version, US_TO_SYN68K(callbacks), 0);
    if (ROMlib_cfw_XMLParserParse(parser) & 255)
        result = ROMlib_cfw_XMLParserGetDocument(parser);
    else {
        uint32 document = ROMlib_cfw_XMLParserGetDocument(parser);
        if (document)
            cfp_call("CFRelease", &document, 1, NULL);
    }
    cfp_call("CFRelease", &parser, 1, NULL);
    return result;
}
