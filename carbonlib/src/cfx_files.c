/*+===================================================================
  File:      cfx_files.c

  Summary:   Implements CoreFoundation files service entries for the PowerPC bridge.

  Classes:   None.

  Functions: ROMlib_cfx_CFContentsOfDirectory, ROMlib_cfx_CFCopySystemVersionDictionary,
             ROMlib_cfx_CFCreateDirectory, ROMlib_cfx_CFCreateURLFromFSSpec, ROMlib_cfx_CFDeleteFile,
             ROMlib_cfx_CFGetFSSpecFromURL, ROMlib_cfx_CFGetFileProperties,
             ROMlib_cfx_CFReadBytesFromFile, ROMlib_cfx_CFRemoveDirectory,
             ROMlib_cfx_CFWriteBytesToFile

  Origin:    C reconstruction of the local CoreFoundationLib 1.4 disassembly; each entry verifies its reference offset.
             Public contracts use Universal Interfaces 3.4.2; private ABI notes are in cf-services-batch.json.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "cfx_internal.h"

static uint32 file_encoding(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_ppc_frame nested = *call_frame;
    return ROMlib_cfx_CFStringFileSystemEncoding(&nested) ? nested.words[0] : 0;
}

static int file_spec(ROMlib_carbon_ppc_frame* call_frame, uint32 allocator, uint32 url, FSSpec* spec)
{
    ROMlib_carbon_ppc_frame nested = *call_frame;
    nested.words[0] = allocator;
    nested.words[1] = url;
    nested.words[2] = US_TO_SYN68K(spec);
    return ROMlib_cfx_CFGetFSSpecFromURL(&nested) && (nested.words[0] & 255);
}

static uint32 file_string(const ROMlib_carbon_reference* reference, ROMlib_carbon_ppc_frame* call_frame, uint32 path)
{
    return ROMlib_cfstring_cstring_no_copy(0, path, file_encoding(call_frame), cfp_toc(reference, 0x316c));
}

static void catalog_parameters(Byte parameters[256], const FSSpec* spec)
{
    memset(parameters, 0, 256);
    uint32 name = CL(US_TO_SYN68K(spec->name));
    memcpy(parameters + 18, &name, 4);
    memcpy(parameters + 22, &spec->vRefNum, 2);
    memcpy(parameters + 48, &spec->parID, 4);
}

static uint32 local_word(const Byte* memory)
{
    return ((uint32)memory[0] << 24) | ((uint32)memory[1] << 16) | ((uint32)memory[2] << 8) | memory[3];
}

int ROMlib_cfx_CFContentsOfDirectory(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFContentsOfDirectory", 0x1897c, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 allocator = CFX_ARG(0), directory = CFX_ARG(1), supplied_spec = CFX_ARG(2);
    uint32 base = CFX_ARG(3), extension = CFX_ARG(4), result = 0;
    unsigned owns_base = !base;
    if (extension)
        CFW_CALL("CFRetain", extension);
    _Alignas(128) FSSpec local_spec = {0};
    if (!base) {
        if (directory) {
            uint32 length = 0;
            for (;;) {
                const Byte* byte = ROMlib_carbon_guest_memory(cfx_address(directory, length, 1), 1, 0);
                if (!byte)
                    goto cleanup;
                if (!*byte)
                    break;
                if (length == INT32_MAX) {
                    cfp_failure("Native Carbon directory path exceeds CFIndex");
                    goto cleanup;
                }
                ++length;
            }
            base = CFW_CALL("CFURLCreateFromFileSystemRepresentation", allocator, directory, length, 1);
        }
        else if (supplied_spec) {
            ROMlib_carbon_ppc_frame nested = *call_frame;
            nested.words[0] = allocator;
            nested.words[1] = supplied_spec;
            nested.words[2] = 1;
            if (!ROMlib_cfx_CFCreateURLFromFSSpec(&nested))
                goto cleanup;
            base = nested.words[0];
        }
    }
    if (!base)
        goto cleanup;
    if (!supplied_spec && !file_spec(call_frame, allocator, base, &local_spec))
        goto cleanup;
    const FSSpec* spec = supplied_spec ? ROMlib_carbon_guest_memory(supplied_spec, sizeof(FSSpec), 0) : &local_spec;
    if (!spec)
        goto cleanup;
    _Alignas(256) Byte parameters[256];
    catalog_parameters(parameters, spec);
    if (PBGetCatInfo((CInfoPBPtr)parameters, 0))
        goto cleanup;
    uint32 directory_id = local_word(parameters + 48);
    result = CFW_CALL("CFArrayCreateMutable", allocator, 0, cfx_address(US_TO_SYN68K(reference.toc), 0x3234, 16));
    if (!result)
        goto cleanup;
    _Alignas(256) Byte name[256];
    for (unsigned index = 1; index <= INT16_MAX; ++index) {
        uint32 name_address = CL(US_TO_SYN68K(name)), identifier = CL(directory_id);
        uint16 packed_index = CW((uint16)index);
        memcpy(parameters + 18, &name_address, 4);
        memcpy(parameters + 28, &packed_index, 2);
        memcpy(parameters + 48, &identifier, 4);
        if (PBGetCatInfo((CInfoPBPtr)parameters, 0))
            break;
        uint32 component = CFW_CALL("CFStringCreateMutable", allocator, 0);
        if (!component)
            continue;
        CFW_CALL("CFStringAppendCString", component, cfx_address(cfp_toc(&reference, 0x474), 0x3c, 1), 0x600);
        CFW_CALL("CFStringAppendPascalString", component, US_TO_SYN68K(name), file_encoding(call_frame));
        if (!extension || (CFW_CALL("CFStringHasSuffix", component, extension) & 255)) {
            uint32 child = CFW_CALL("CFURLCreateWithFileSystemPathRelativeToBase", allocator, component, 1,
                                    !!(parameters[30] & 0x10), base);
            if (child) {
                CFW_CALL("CFArrayAppendValue", result, child);
                CFW_CALL("CFRelease", child);
            }
        }
        CFW_CALL("CFRelease", component);
    }
cleanup:
    if (extension)
        CFW_CALL("CFRelease", extension);
    if (owns_base && base)
        CFW_CALL("CFRelease", base);
    return CFX_RESULT(result);
}

int ROMlib_cfx_CFCopySystemVersionDictionary(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFCopySystemVersionDictionary", 0x1a99c, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 allocator = cfp_toc(&reference, 0x3160), path = cfx_literal(&reference, 0x484, 0);
    uint32 url = US_TO_SYN68K(ROMlib_carbon_cf_url_create_file(allocator, SYN68K_TO_US(path), 0, 0));
    uint32 result = 0;
    if (url) {
        _Alignas(16) uint32 data = 0;
        if (CFW_CALL("CFURLCreateDataAndPropertiesFromResource", allocator, url, US_TO_SYN68K(&data), 0, 0, 0)) {
            result = CFW_CALL("CFPropertyListCreateFromXMLData", cfp_toc(&reference, 0x3164), CL(data), 0, 0);
            if (CL(data))
                CFW_CALL("CFRelease", CL(data));
        }
        CFW_CALL("CFRelease", url);
    }
    return CFX_RESULT(result);
}

int ROMlib_cfx_CFCreateDirectory(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFCreateDirectory", 0x184c0, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 path = file_string(&reference, call_frame, CFX_ARG(0));
    uint32 success = 0;
    if (path) {
        _Alignas(128) FSSpec spec = {0};
        if (CFX_PRIVATE(0x17cd4, 0, path, US_TO_SYN68K(&spec)) & 255) {
            int32 directory_id = 0;
            success = !C_FSpDirCreate(&spec, -1, &directory_id);
        }
        CFW_CALL("CFRelease", path);
    }
    return CFX_RESULT(success);
}

int ROMlib_cfx_CFCreateURLFromFSSpec(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFCreateURLFromFSSpec", 0xc48c, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 allocator = CFX_ARG(0), spec = CFX_ARG(1), directory = CFX_ARG(2);
    uint32 path = CFX_PRIVATE(0x180a4, allocator, spec), result = 0;
    if (path) {
        result = US_TO_SYN68K(ROMlib_carbon_cf_url_create_file(allocator, SYN68K_TO_US(path), 1, directory));
        CFW_CALL("CFRelease", path);
    }
    return CFX_RESULT(result);
}

int ROMlib_cfx_CFDeleteFile(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFDeleteFile", 0x18618, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 path = file_string(&reference, call_frame, CFX_ARG(0));
    uint32 success = 0;
    if (path) {
        _Alignas(128) FSSpec spec = {0};
        if (CFX_PRIVATE(0x17cd4, 0, path, US_TO_SYN68K(&spec)) & 255) {
            success = !C_FSpDelete(&spec);
        }
        CFW_CALL("CFRelease", path);
    }
    return CFX_RESULT(success);
}

int ROMlib_cfx_CFGetFSSpecFromURL(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFGetFSSpecFromURL", 0xc0f4, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 allocator = CFX_ARG(0), url = CFX_ARG(1), output = CFX_ARG(2);
    uint32 absolute = CFW_CALL("CFURLCopyAbsoluteURL", url);
    uint32 path = absolute ? CFW_CALL("CFURLCopyFileSystemPath", absolute, 1) : 0;
    if (absolute)
        CFW_CALL("CFRelease", absolute);
    uint32 result = 0;
    if (path) {
        result = CFX_PRIVATE(0x17cd4, allocator, path, output);
        CFW_CALL("CFRelease", path);
    }
    return CFX_RESULT(result);
}

int ROMlib_cfx_CFGetFileProperties(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFGetFileProperties", 0x18ce4, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 allocator = CFX_ARG(0), url = CFX_ARG(1);
    uint32 exists_out = CFX_ARG(2), mode_out = CFX_ARG(3), size_out = CFX_ARG(4), modification_out = CFX_ARG(5);
    uint32 owner_out = CFX_ARG(6), contents_out = CFX_ARG(7);
    if (!exists_out && !mode_out && !size_out && !modification_out && !owner_out && !contents_out)
        return CFX_RESULT(0);
    _Alignas(128) FSSpec spec = {0};
    if (!file_spec(call_frame, allocator, url, &spec))
        return CFX_RESULT(UINT32_MAX);
    _Alignas(256) Byte parameters[256];
    catalog_parameters(parameters, &spec);
    OSErr error = PBGetCatInfo((CInfoPBPtr)parameters, FALSE);
    if (error && error != -43)
        return CFX_RESULT(UINT32_MAX);
    unsigned exists = error == 0, directory = exists && (parameters[30] & 0x10);
    if (exists_out) {
        Byte* output = ROMlib_carbon_guest_memory(exists_out, 1, 1);
        if (!output)
            return 0;
        *output = exists;
    }
    if (mode_out && !cfp_write(mode_out, exists ? (directory ? 0x4000 : 0x8000) | 0x1ff : 0))
        return 0;
    if (size_out) {
        uint32 size = exists && !directory ? local_word(parameters + 54) : 0;
        if (!cfp_write(size_out, (int32)size < 0 ? UINT32_MAX : 0) || !cfp_write(cfx_address(size_out, 4, 4), size))
            return 0;
    }
    if (modification_out) {
        uint32 date = 0;
        if (exists) {
            ROMlib_carbon_ppc_frame nested = *call_frame;
            nested.words[0] = allocator;
            /* 1.4 adds the positive 1904/2001 interval here. Preserve this file
               timestamp convention for comparison with its existing bundle cache. */
            cfx_double_result(&nested, (double)local_word(parameters + 76) + 3061152000.0);
            if (!ROMlib_cfx_CFDateCreate(&nested))
                return 0;
            date = nested.words[0];
        }
        if (!cfp_write(modification_out, date))
            return 0;
    }
    if (owner_out && !cfp_write(owner_out, UINT32_MAX))
        return 0;
    if (contents_out) {
        uint32 contents = 0;
        if (directory) {
            ROMlib_carbon_ppc_frame nested = *call_frame;
            nested.words[0] = allocator;
            nested.words[1] = 0;
            nested.words[2] = US_TO_SYN68K(&spec);
            nested.words[3] = url;
            nested.words[4] = 0;
            if (!ROMlib_cfx_CFContentsOfDirectory(&nested))
                return 0;
            contents = nested.words[0];
        }
        if (!cfp_write(contents_out, contents))
            return 0;
    }
    return CFX_RESULT(0);
}

int ROMlib_cfx_CFReadBytesFromFile(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFReadBytesFromFile", 0x186c0, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 allocator = CFX_ARG(0), url = CFX_ARG(1), bytes_out = CFX_ARG(2), length_out = CFX_ARG(3),
           maximum = CFX_ARG(4);
    _Alignas(128) FSSpec spec = {0};
    if (!file_spec(call_frame, allocator, url, &spec))
        return CFX_RESULT(0);
    INTEGER reference_number = CWC(-1);
    if (C_FSpOpenDF(&spec, 1, &reference_number))
        return CFX_RESULT(0);
    INTEGER number = (int16)CW(reference_number);
    LONGINT eof = 0;
    uint32 success = 0;
    if (SetFPos(number, 1, 0) || GetEOF(number, &eof))
        goto read_done;
    uint32 size = eof;
    if ((int32)size < 0 || (int32)maximum < 0) {
        cfp_failure("Native Carbon file read length is outside the supported CFIndex range");
        goto read_done;
    }
    if (!size) {
        uint32 bytes = CFW_CALL("CFAllocatorAllocate", allocator, 4, 0);
        success = cfp_write(bytes_out, bytes) && cfp_write(length_out, 0);
        goto read_done;
    }
    uint32 requested = maximum && maximum < size ? maximum : size;
    uint32 bytes = CFW_CALL("CFAllocatorAllocate", allocator, requested, 0);
    if (!bytes)
        goto read_done;
    void* output = ROMlib_carbon_guest_memory(bytes, requested, 1);
    LONGINT amount = requested;
    if (!output || FSRead(number, &amount, output)) {
        CFW_CALL("CFAllocatorDeallocate", allocator, bytes);
        cfp_write(bytes_out, 0);
        goto read_done;
    }
    success = cfp_write(bytes_out, bytes) && cfp_write(length_out, amount);
read_done:
    FSClose(number);
    return CFX_RESULT(success);
}

int ROMlib_cfx_CFRemoveDirectory(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFRemoveDirectory", 0x18570, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 path = file_string(&reference, call_frame, CFX_ARG(0));
    uint32 success = 0;
    if (path) {
        _Alignas(128) FSSpec spec = {0};
        if (CFX_PRIVATE(0x17cd4, 0, path, US_TO_SYN68K(&spec)) & 255) {
            success = !C_FSpDelete(&spec);
        }
        CFW_CALL("CFRelease", path);
    }
    return CFX_RESULT(success);
}

int ROMlib_cfx_CFWriteBytesToFile(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFWriteBytesToFile", 0x18828, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 url = CFX_ARG(0), bytes = CFX_ARG(1), length = CFX_ARG(2);
    if ((int32)length < 0 || (!bytes && length))
        return CFX_RESULT(0);
    _Alignas(128) FSSpec spec = {0};
    uint32 allocator = CFW_CALL("CFGetAllocator", url);
    if (!file_spec(call_frame, allocator, url, &spec))
        return CFX_RESULT(0);
    INTEGER reference_number = CWC(-1);
    OSErr error = C_FSpOpenDF(&spec, 2, &reference_number);
    if (error == -43) {
        if (C_FSpCreate(&spec, 0, 0, -1))
            return CFX_RESULT(0);
        error = C_FSpOpenDF(&spec, 2, &reference_number);
    }
    if (error)
        return CFX_RESULT(0);
    INTEGER number = (int16)CW(reference_number);
    LONGINT amount = length;
    const void* input = length ? ROMlib_carbon_guest_memory(bytes, length, 0) : SYN68K_TO_US_CHECK0(bytes);
    uint32 success = (!length || input) && !SetFPos(number, 1, 0) && !FSWrite(number, &amount, (Ptr)input) &&
                     !SetEOF(number, amount);
    FSClose(number);
    return CFX_RESULT(success);
}
