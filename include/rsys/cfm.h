/*+===================================================================
  File:      cfm.h

  Summary:   Declares cfm support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_cfm_h_)
#define _cfm_h_

#include "FileMgr.h"

/*
 * Copyright 2000 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: cfm.h 63 2004-12-24 18:19:43Z ctm $
 */

enum { kUnresolvedCFragSymbolAddress = 0 };

typedef struct PACKED {
    uint32 reserved0;
    uint32 reserved1;
    uint32 version;
    uint32 reserved2;
    uint32 reserved3;
    uint32 reserved4;
    uint32 reserved5;
    int32 n_descripts;
} cfrg_resource_t;

#define CFRG_VERSION_X(cfrg) ((cfrg)->version)
#define CFRG_VERSION(cfrg) (CL(CFRG_VERSION_X(cfrg)))

#define CFRG_N_DESCRIPTS_X(cfrg) ((cfrg)->n_descripts)
#define CFRG_N_DESCRIPTS(cfrg) (CL(CFRG_N_DESCRIPTS_X(cfrg)))

typedef struct PACKED {
    OSType isa;
    uint32 update_level;
    uint32 current_version;
    uint32 oldest_definition_version;
    uint32 stack_size;
    int16 appl_library_dir;
    uint8 fragment_type;
    uint8 fragment_location;
    int32 offset_to_fragment;
    int32 fragment_length;
    uint32 reserved0;
    uint32 reserved1;
    uint16 cfir_length;
    unsigned char name[1];
} cfir_t;

#define CFIR_ISA_X(cfir) ((cfir)->isa)
#define CFIR_ISA(cfir) (CL(CFIR_ISA_X(cfir)))

#define CFIR_TYPE_X(cfir) ((cfir)->fragment_type)
#define CFIR_TYPE(cfir) (CB(CFIR_TYPE_X(cfir)))

#define CFIR_LOCATION_X(cfir) ((cfir)->fragment_location)
#define CFIR_LOCATION(cfir) (CB(CFIR_LOCATION_X(cfir)))

#define CFIR_LENGTH_X(cfir) ((cfir)->cfir_length)
#define CFIR_LENGTH(cfir) (CW(CFIR_LENGTH_X(cfir)))

#define CFIR_OFFSET_TO_FRAGMENT_X(cfir) ((cfir)->offset_to_fragment)
#define CFIR_OFFSET_TO_FRAGMENT(cfir) (CL(CFIR_OFFSET_TO_FRAGMENT_X(cfir)))

#define CFIR_FRAGMENT_LENGTH_X(cfir) ((cfir)->fragment_length)
#define CFIR_FRAGMENT_LENGTH(cfir) (CL(CFIR_FRAGMENT_LENGTH_X(cfir)))

#define CFIR_NAME(cfir) ((cfir)->name)

enum {
    kImportLibraryCFrag,
    kApplicationCFrag,
    kDropInAdditionCFrag,
    kStubLibraryCFrag,
    kWeakStubLibraryCFrag,
};

enum { kWholeFork = 0 };

enum {
    kInMem,
    kOnDiskFlat,
    kOnDiskSegmented,
};

enum {
    kPowerPCArch = T('p', 'w', 'p', 'c'),
    kMotorola68KArch = T('m', '6', '8', 'k'),
};

typedef enum {
    kLoadLib = 1, /* deprecated */
    kReferenceCFrag = 1,
    kFindLib = 2,
    kLoadNewCopy = 5,
} LoadFlags;

typedef struct CFragConnection* ConnectionID;

extern OSErr C_GetDiskFragment(FSSpecPtr fsp, LONGINT offset, LONGINT length, Str63 fragname, LoadFlags flags,
                               ConnectionID* connp, Ptr* mainAddrp, Str255 errname);

typedef uint8 SymClass;

extern OSErr C_FindSymbol(ConnectionID connID, Str255 symName, Ptr* symAddr, SymClass* symClass);

enum { fragConnectionIDNotFound = -2801, fragSymbolNotFound = -2802, fragLibNotFound = -2804, fragNoMem = -2809 };

extern cfir_t* ROMlib_find_cfrg(Handle cfrg, OSType arch, uint8 type, StringPtr name);

extern OSErr C_CloseConnection(ConnectionID* cidp);

extern OSErr C_GetSharedLibrary(Str63 library, OSType arch, LoadFlags loadflags, ConnectionID* cidp, Ptr* mainaddrp,
                                Str255 errName);

extern OSErr C_GetMemFragment(void* addr, uint32 length, Str63 fragname, LoadFlags flags, ConnectionID* connp,
                              Ptr* mainAddrp, Str255 errname);

extern OSErr C_CountSymbols(ConnectionID id, LONGINT* countp);

extern OSErr C_GetIndSymbol(ConnectionID id, LONGINT index, Str255 name, Ptr* addrp, SymClass* classp);

#endif
