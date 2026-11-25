/*+===================================================================
  File:      file_manager.c

  Summary:   Implements native CarbonLib File Manager register traps and convenience calls.

  Classes:   None.

  Functions: ROMlib_carbon_file_trap, ROMlib_carbon_file_parameter_block, ROMlib_carbon_file_io,
             ROMlib_carbon_file_hierarchy,
             ROMlib_carbon_file_spec, ROMlib_carbon_file_make_spec, ROMlib_carbon_PBWaitIOComplete,
             ROMlib_carbon_PBUnmountVol and ROMlib_carbon_UnmountVol.

  Origin:    System 9.2.1 InterfaceLib File Manager overlay (fovr 50),
             CarbonLib 1.4 FSSpec wrappers, Universal Interfaces 3.4.2 Files.h, and Executor's File Manager.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "file_manager.h"
#include "rsys/hfs.h"
#include <string.h>

_Static_assert(sizeof(CopyParam) == 52 && offsetof(CopyParam, ioCopyName) == 32,
               "HFS move-and-rename parameter block layout");
_Static_assert(sizeof(ForkAccessParam) == 52 && offsetof(ForkAccessParam, ioDenyModes) == 26,
               "HFS fork sharing parameter block layout");
_Static_assert(sizeof(FIDParam) == 58 && offsetof(FIDParam, ioFileID) == 54, "HFS file ID parameter block layout");

int ROMlib_carbon_file_trap(uint16_t trap_word, int selector, uint32_t address, uint32_t* result)
{
    ROMlib_carbon_ppc_frame arguments = {0};
    unsigned trap_slot = 0;
    uint32_t proc_info = 0x133822; /* D1 is the trap word, A0 is the parameter block. */
    if (selector >= 0) {
        arguments.words[0] = (uint32_t)selector;
        trap_slot = 1;
        proc_info = 0x2671822; /* Add the HFSDispatch selector in D0. */
    }
    arguments.words[trap_slot] = address;
    return ROMlib_carbon_call_register_vector(0x400 + 4 * (trap_word & 0xff), proc_info, trap_word, trap_slot,
                                              &arguments, result);
}

static int result_error(ROMlib_carbon_ppc_frame* call_frame, int16_t error)
{
    call_frame->words[0] = (uint32_t)(int32_t)error;
    return 1;
}

int ROMlib_carbon_PBWaitIOComplete(ROMlib_carbon_ppc_frame* call_frame)
{
    const uint32_t address = call_frame->words[0];
    if (!address)
        return result_error(call_frame, paramErr);

    const size_t result_offset = offsetof(ioParam, ioResult);
    if (address > UINT32_MAX - result_offset)
        return result_error(call_frame, paramErr);
    const void* field = ROMlib_carbon_guest_memory(address + result_offset, sizeof(INTEGER), 0);
    if (!field)
        return 0;
    INTEGER encoded;
    memcpy(&encoded, field, sizeof encoded);
    const int16_t result = CW(encoded);
    if (result != 1 || !call_frame->words[1])
        return result_error(call_frame, result);

    /* A patched driver can leave operations pending. Let CarbonLib perform
       the timed wait and service any cooperative guest driver hooks. */
    ROMlib_carbon_reference original;
    return ROMlib_carbon_reference_function("CarbonLib", "PBWaitIOComplete", &original) &&
           ROMlib_carbon_invoke_frame(original.function, call_frame);
}

static int check_name(uint32_t address);

static int file_unmount_volume(ROMlib_carbon_ppc_frame* call, ioParam* block, uint32 address)
{
    const uint32 name = CL(PP_RAW(block->ioNamePtr));
    if (!check_name(name))
        return 0;
    LONGINT directory;
    char* remainder;
    VCB* volume = ROMlib_breakoutioname((ParmBlkPtr)block, &directory, &remainder, NULL, FALSE);
    if (!volume)
        return result_error(call, nsvErr);

    const INTEGER drive = CW(volume->vcbDrvNum);
    const DrvQExtra* queue_entry = drive ? ROMlib_dqbydrive(drive) : NULL;
    /* Carbon ejects optical media after a successful unmount. Record the
       drive type before the unmount changes the volume or the drive queue. */
    const bool optical = queue_entry && ((CL(queue_entry->flags) >> 16) & 0xff) == 8;
    uint32 result;
    if (!ROMlib_carbon_file_trap(0xa00e, -1, address, &result))
        return 0;
    if (!(int16)result && optical) {
        const INTEGER previous = block->ioVRefNum;
        block->ioVRefNum = CW(drive);
        uint32 ignored;
        const int invoked = ROMlib_carbon_file_trap(0xa017, -1, address, &ignored);
        block->ioVRefNum = previous;
        if (!invoked)
            return 0;
    }
    return result_error(call, result);
}

int ROMlib_carbon_PBUnmountVol(ROMlib_carbon_ppc_frame* call)
{
    const uint32 address = call->words[0];
    ioParam* block = address ? ROMlib_carbon_try_guest_memory(address, sizeof *block, 1) : NULL;
    return block ? file_unmount_volume(call, block, address) : result_error(call, paramErr);
}

int ROMlib_carbon_UnmountVol(ROMlib_carbon_ppc_frame* call)
{
    _Alignas(64) ioParam block = {0};
    PP_RAW(block.ioNamePtr) = CL(call->words[0]);
    block.ioVRefNum = CW((INTEGER)call->words[1]);
    return file_unmount_volume(call, &block, US_TO_SYN68K(&block));
}

int ROMlib_carbon_file_parameter_block(ROMlib_carbon_ppc_frame* call_frame, uint16_t trap_word, int selector,
                                       size_t extent)
{
    const uint32_t address = call_frame->words[0];
    if (!address)
        return result_error(call_frame, paramErr);
    const HParamBlockRec* block = ROMlib_carbon_guest_memory(address, extent, 1);
    if (!block)
        return 0;
    if (selector == 0x1b) {
        const uint32_t name = CL(PP_RAW(block->ioParam.ioNamePtr));
        const uint32_t spec = CL(block->ioParam.ioMisc);
        if (!check_name(name) || (spec && !ROMlib_carbon_guest_memory(spec, sizeof(FSSpec), 1)))
            return 0;
    }
    if (selector >= 0x14 && selector <= 0x16) {
        const uint32_t name = CL(PP_RAW(block->ioParam.ioNamePtr));
        if (!check_name(name) || (selector == 0x16 && name && !ROMlib_carbon_guest_memory(name, 32, 1)))
            return 0;
    }
    if (selector == 0x36 || selector == 0x37) {
        const CopyParam* transfer = (const CopyParam*)block;
        if (!check_name(CL(PP_RAW(transfer->ioNamePtr))) || !check_name(CL(PP_RAW(transfer->ioNewName))) ||
            !check_name(CL(PP_RAW(transfer->ioCopyName))))
            return 0;
    }
    if ((selector == 0x38 || selector == 0x39) && !check_name(CL(PP_RAW(block->ioParam.ioNamePtr))))
        return 0;
    return ROMlib_carbon_file_trap(trap_word, selector, address, call_frame->words);
}

int ROMlib_carbon_file_io(ROMlib_carbon_ppc_frame* call_frame, uint16_t trap_word)
{
    /* The live 68K/PPC vector receives a guest address. The aligned native
       block stays within one host alias slot during the synchronous call. */
    _Alignas(64) ioParam block = {0};
    _Static_assert(sizeof block <= 64, "File I/O block must fit its alias alignment");
    const uint32_t* arguments = call_frame->words;
    block.ioRefNum = CW(arguments[0]);
    void* output = NULL;
    const void* result_field = NULL;
    const int transfer = trap_word == 0xa002 || trap_word == 0xa003;
    const int allocate = trap_word == 0xa010 || trap_word == 0xa210;
    if (transfer || allocate || trap_word == 0xa011 || trap_word == 0xa018) {
        if (!arguments[1])
            return result_error(call_frame, paramErr);
        output = ROMlib_carbon_guest_memory(arguments[1], 4, 1);
        if (!output)
            return 0;
    }
    if (transfer || allocate) {
        memcpy(&block.ioReqCount, output, 4);
        int32_t length = (int32_t)CL(block.ioReqCount);
        if (length < 0 || (transfer && length && !arguments[2])) {
            uint32_t zero = 0;
            memcpy(output, &zero, 4);
            return result_error(call_frame, paramErr);
        }
        if (transfer && length && !ROMlib_carbon_guest_memory(arguments[2], length, trap_word == 0xa002)) {
            uint32_t zero = 0;
            memcpy(output, &zero, 4);
            return 0;
        }
        if (transfer)
            PP_RAW(block.ioBuffer) = CL(arguments[2]);
        block.ioPosMode = CWC(fsAtMark);
        result_field = &block.ioActCount;
    }
    else if (trap_word == 0xa011)
        result_field = &block.ioMisc;
    else if (trap_word == 0xa018)
        result_field = &block.ioPosOffset;
    else if (trap_word == 0xa012)
        block.ioMisc = CL(arguments[1]);
    else if (trap_word == 0xa044) {
        block.ioPosMode = CW(arguments[1]);
        block.ioPosOffset = CL(arguments[2]);
    }

    uint32_t result;
    if (!ROMlib_carbon_file_trap(trap_word, -1, US_TO_SYN68K(&block), &result))
        return 0;
    if (transfer || allocate || (output && !result))
        memcpy(output, result_field, 4);
    call_frame->words[0] = result;
    return 1;
}

static int check_name(uint32_t address)
{
    if (!address)
        return 1;
    const unsigned char* length = ROMlib_carbon_guest_memory(address, 1, 0);
    return length && ROMlib_carbon_guest_memory(address, (size_t)*length + 1, 0);
}

int ROMlib_carbon_file_hierarchy(ROMlib_carbon_ppc_frame* call_frame, uint16_t trap_word, int selector)
{
    _Alignas(128) union {
        HParamBlockRec hierarchical;
        WDPBRec working_directory;
    } block = {0};

    _Static_assert(sizeof block <= 128, "Hierarchical block must fit its alias alignment");
    const uint32_t* arguments = call_frame->words;
    const int volume_call = trap_word == 0xa214 || trap_word == 0xa215;
    const int opening = trap_word == 0xa200 || trap_word == 0xa20a || selector == 0x1a;
    const uint32_t name = arguments[volume_call ? 0 : 2];
    void* output = NULL;
    void* directory = NULL;
    size_t extent = 0;
    const void* result_field = NULL;
    if (trap_word == 0xa214) {
        if (!arguments[1] || !arguments[2])
            return result_error(call_frame, paramErr);
        output = ROMlib_carbon_guest_memory(arguments[1], 2, 1);
        directory = ROMlib_carbon_guest_memory(arguments[2], 4, 1);
        if (!output || !directory || (name && !ROMlib_carbon_guest_memory(name, 28, 1)))
            return 0;
        block.working_directory.ioWDIndex = 0;
        PP_RAW(block.working_directory.ioNamePtr) = CL(name);
    }
    else if (!check_name(name))
        return 0;

    if (trap_word == 0xa215) {
        PP_RAW(block.working_directory.ioNamePtr) = CL(name);
        block.working_directory.ioVRefNum = CW(arguments[1]);
        block.working_directory.ioWDDirID = CL(arguments[2]);
    }
    else if (!volume_call) {
        HFileParam* file = &block.hierarchical.fileParam;
        HIoParam* io = &block.hierarchical.ioParam;
        PP_RAW(file->ioNamePtr) = CL(name);
        file->ioVRefNum = CW(arguments[0]);
        file->ioDirID = CL(arguments[1]);
        unsigned output_word = 3;
        if (opening) {
            io->ioPermssn = (SignedByte)arguments[3];
            output_word = 4;
            extent = 2;
            result_field = &io->ioRefNum;
        }
        else if (trap_word == 0xa20c) {
            extent = sizeof(FInfo);
            result_field = &file->ioFlFndrInfo;
        }
        else if (selector == 6) {
            extent = 4;
            result_field = &file->ioDirID;
        }
        else if (trap_word == 0xa20b) {
            if (!check_name(arguments[3]))
                return 0;
            io->ioMisc = CL(arguments[3]);
        }
        if (extent) {
            if (!arguments[output_word])
                return result_error(call_frame, paramErr);
            output = ROMlib_carbon_guest_memory(arguments[output_word], extent, 1);
            if (!output)
                return 0;
        }
    }

    const uint32_t address = US_TO_SYN68K(&block);
    uint32_t result;
    if (!ROMlib_carbon_file_trap(trap_word, selector, address, &result))
        return 0;
    if (!result && trap_word == 0xa208) {
        HFileParam* file = &block.hierarchical.fileParam;
        file->ioFDirIndex = 0;
        if (!ROMlib_carbon_file_trap(0xa20c, -1, address, &result))
            return 0;
        if (!result) {
            file->ioFlFndrInfo.fdCreator = CL(arguments[3]);
            file->ioFlFndrInfo.fdType = CL(arguments[4]);
            file->ioDirID = CL(arguments[1]);
            if (!ROMlib_carbon_file_trap(0xa20d, -1, address, &result))
                return 0;
        }
    }
    if (!result && output) {
        if (directory) {
            memcpy(output, &block.working_directory.ioVRefNum, 2);
            memcpy(directory, &block.working_directory.ioWDDirID, 4);
        }
        else
            memcpy(output, result_field, extent);
    }
    call_frame->words[0] = result;
    return 1;
}

static int set_catalog_metadata(CInfoPBRec* block, uint32_t directory, int create_file, const uint32_t* arguments,
                                uint32_t* result)
{
    HFileInfo* info = &block->hFileInfo;
    const uint32_t address = US_TO_SYN68K(block);
    info->ioFDirIndex = 0;
    info->ioDirID = directory;
    if (!ROMlib_carbon_file_trap(0xa260, 9, address, result))
        return 0;
    if (*result)
        return 1;

    /* The high byte of the fourth reserved Finder word is the script byte
       of both extended catalog records in the CarbonLib 1.4 disassembly. */
    _Static_assert(offsetof(HFileInfo, ioFlXFndrInfo) == offsetof(DirInfo, ioDrFndrInfo),
                   "Finder extension must occupy the same catalog bytes");
    int16_t script = arguments[create_file ? 3 : 1];
    ((uint8_t*)&info->ioFlXFndrInfo)[8] = script >= 0 ? (uint8_t)script | 0x80 : 0;
    if (create_file) {
        info->ioFlFndrInfo.fdCreator = CL(arguments[1]);
        info->ioFlFndrInfo.fdType = CL(arguments[2]);
    }
    info->ioDirID = directory;
    return ROMlib_carbon_file_trap(0xa260, 0xa, address, result);
}

int ROMlib_carbon_file_spec(ROMlib_carbon_ppc_frame* call_frame, uint16_t trap_word, int selector)
{
    _Alignas(128) union {
        HParamBlockRec file;
        CInfoPBRec catalog;
        CMovePBRec move;
    } block = {0};

    _Static_assert(sizeof block <= 128, "FSSpec parameter block must fit its alias alignment");
    const uint32_t* arguments = call_frame->words;
    const int get_info = trap_word == 0xa20c;
    const int set_info = trap_word == 0xa20d;
    const int opening = selector == 0x1a || trap_word == 0xa20a;
    const int create_directory = selector == 6;
    const int create_file = trap_word == 0xa208;
    if (!arguments[0] || (get_info && !arguments[1]))
        return result_error(call_frame, get_info ? nsvErr : paramErr);
    const FSSpec* spec = ROMlib_carbon_guest_memory(arguments[0], sizeof *spec, 0);
    if (!spec)
        return 0;
    if (spec->name[0] >= sizeof spec->name)
        return result_error(call_frame, bdNamErr);

    const uint32_t directory = spec->parID;
    const uint32_t name = CL(arguments[0] + offsetof(FSSpec, name));
    PP_RAW(block.file.fileParam.ioNamePtr) = name;
    block.file.fileParam.ioVRefNum = spec->vRefNum;
    block.file.fileParam.ioDirID = directory;
    if (selector == 5) {
        if (!arguments[1])
            return result_error(call_frame, paramErr);
        const FSSpec* destination = ROMlib_carbon_guest_memory(arguments[1], sizeof *destination, 0);
        if (!destination)
            return 0;
        if (spec->vRefNum != destination->vRefNum)
            return result_error(call_frame, paramErr);
        if (destination->name[0] >= sizeof destination->name)
            return result_error(call_frame, bdNamErr);
        block.move.ioNewDirID = destination->parID;
        PP_RAW(block.move.ioNewName) = CL(arguments[1] + offsetof(FSSpec, name));
    }
    else if (trap_word == 0xa20b) {
        if (!check_name(arguments[1]))
            return 0;
        block.file.ioParam.ioMisc = CL(arguments[1]);
    }

    void* buffer = NULL;
    size_t extent = opening ? 2 : create_directory ? 4 : get_info || set_info ? sizeof(FInfo) : 0;
    if (extent) {
        const uint32_t address = arguments[opening || create_directory ? 2 : 1];
        if (!address)
            return result_error(call_frame, paramErr);
        buffer = ROMlib_carbon_guest_memory(address, extent, !set_info);
        if (!buffer)
            return 0;
    }
    if (opening)
        block.file.ioParam.ioPermssn = (SignedByte)arguments[1];

    const uint32_t parameter_block = US_TO_SYN68K(&block);
    uint32_t result;
    if (!ROMlib_carbon_file_trap(set_info ? 0xa20c : trap_word, selector, parameter_block, &result))
        return 0;
    if (opening)
        memcpy(buffer, &block.file.ioParam.ioRefNum, extent);
    else if (get_info)
        memcpy(buffer, &block.file.fileParam.ioFlFndrInfo, extent);
    else if (create_directory)
        memcpy(buffer, &block.file.fileParam.ioDirID, extent);
    if (!result && set_info) {
        block.file.fileParam.ioDirID = directory;
        memcpy(&block.file.fileParam.ioFlFndrInfo, buffer, extent);
        if (!ROMlib_carbon_file_trap(trap_word, selector, parameter_block, &result))
            return 0;
    }
    if (!result && (create_file || create_directory) &&
        !set_catalog_metadata(&block.catalog, directory, create_file, arguments, &result))
        return 0;
    call_frame->words[0] = result;
    return 1;
}

int ROMlib_carbon_file_make_spec(ROMlib_carbon_ppc_frame* call_frame)
{
    const uint32_t filename = call_frame->words[2];
    const uint32_t spec = call_frame->words[3];
    if (!filename || !spec)
        return result_error(call_frame, paramErr);
    const uint8_t* length = ROMlib_carbon_guest_memory(filename, 1, 0);
    if (!length || !ROMlib_carbon_guest_memory(spec, sizeof(FSSpec), 1))
        return 0;
    const size_t extent = (size_t)*length + 1;
    const void* input = ROMlib_carbon_guest_memory(filename, extent, 0);
    if (!input)
        return 0;

    /* Preserve the input name when the caller builds the result over it. */
    ROMlib_carbon_ppc_frame arguments = *call_frame;
    _Alignas(256) uint8_t saved[256];
    if ((uint64_t)filename < (uint64_t)spec + sizeof(FSSpec) && (uint64_t)spec < (uint64_t)filename + extent) {
        memcpy(saved, input, extent);
        arguments.words[2] = US_TO_SYN68K(saved);
    }
    return ROMlib_carbon_call_toolbox_vector(0x1748, 0xfea8, 1, &arguments, call_frame->words);
}
