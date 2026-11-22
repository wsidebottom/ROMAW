/*+===================================================================
  File:      alias_file_test.c

  Summary:   Exercise resource-backed Finder alias traversal through the real resolver.

  Classes:   None.

  Functions: C_CloseResFile, C_CurResFile, C_FSpOpenResFile, C_Get1IndResource, C_ResError, C_SetResLoad, C_UseResFile,
             main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Exercise resource-backed Finder alias traversal through the real resolver. */
#define ALIAS_RELATIVE_NO_MAIN
#include "alias_relative_test.c"

static INTEGER active_resource = 7;
static unsigned opens, closes, links, chain_length;
static int missing_resource, open_failure, broken_target, folder_target;
static Handle resource_alias;
static Boolean initial_load;

INTEGER
C_CurResFile(void)
{
    return active_resource;
}

void C_UseResFile(INTEGER file)
{
    active_resource = file;
}

INTEGER
C_FSpOpenResFile(FSSpecPtr file, SignedByte permissions)
{
    assert(permissions == fsRdPerm);
    ++opens;
    return open_failure ? -1 : 12;
}

OSErr C_ResError(void)
{
    return permErr;
}

void C_CloseResFile(INTEGER file)
{
    assert(file == 12);
    ++closes;
    active_resource = 0;
}

void C_SetResLoad(BOOLEAN load)
{
    ResLoad = load;
}

Handle C_Get1IndResource(ResType type, INTEGER index)
{
    assert(active_resource == 12 && ResLoad);
    assert(type == TICK("alis") && index == 1);
    return missing_resource ? NULL : resource_alias;
}

static OSErr catalog(CInfoPBPtr pb)
{
    if (broken_target && links)
        return fnfErr;
    if (folder_target && links)
        pb->hFileInfo.ioFlAttrib = ATTRIB_ISADIR;
    pb->hFileInfo.ioFlFndrInfo.fdFlags = links < chain_length ? CWC(0x8000) : 0;
    return noErr;
}

static OSErr next_spec(FSSpecPtr out)
{
    *out = spec(-5, 30 + ++links, "Target");
    return noErr;
}

static OSErr run(unsigned count, Boolean all, Boolean* folder, Boolean* aliased)
{
    FSSpec original = spec(-5, 20, "Alias"), file = original;
    chain_length = count;
    opens = closes = links = 0;
    active_resource = 7;
    ResLoad = initial_load;
    OSErr error = C_ResolveAliasFile(&file, all, folder, aliased);
    assert(active_resource == 7 && ResLoad == initial_load);
    assert(closes == (open_failure ? 0 : opens));
    if (error)
        assert(!memcmp(&file, &original, sizeof file));
    else
        assert(CL(file.parID) == (links ? 30 + links : 20));
    return error;
}

int main(void)
{
    void* low = calloc(1, 65536);
    assert(low);
    ROMlib_offset = (uintptr_t)low;
    FSSpec target = spec(-5, 30, "File");
    HIDDEN_AliasHandle hidden;
    Boolean folder, aliased;
    assert(!C_NewAliasMinimal(&target, &hidden));
    resource_alias = HIDDEN_NATIVE(hidden);
    catalog_override = catalog;
    spec_override = next_spec;
    assert(!run(0, TRUE, &folder, &aliased) && !folder && !aliased);
    assert(!run(1, TRUE, &folder, &aliased) && !folder && aliased);
    assert(!run(10, TRUE, &folder, &aliased) && links == 10);
    assert(run(11, TRUE, &folder, &aliased) == fnfErr && links == 10);
    assert(!run(11, FALSE, &folder, &aliased) && links == 1);
    initial_load = TRUE;
    assert(!run(1, TRUE, &folder, &aliased));
    alias_head_t* head = (alias_head_t*)STARH(resource_alias);
    INTEGER length = head->length;
    head->length = 0;
    assert(run(1, TRUE, &folder, &aliased) == paramErr && aliased);
    head->length = length;
    missing_resource = 1;
    assert(run(1, TRUE, &folder, &aliased) == resNotFound && aliased);
    missing_resource = 0;
    open_failure = 1;
    assert(run(1, TRUE, &folder, &aliased) == permErr);
    open_failure = 0;
    broken_target = 1;
    assert(run(2, TRUE, &folder, &aliased) == fnfErr);
    broken_target = 0;
    /* A folder with bit 15 set is a folder, not another alias link. */
    folder_target = 1;
    head->usually_0 = CWC(1);
    assert(!run(2, TRUE, &folder, &aliased) && folder && links == 1);
    assert(C_ResolveAliasFile(NULL, TRUE, &folder, &aliased) == paramErr);
    free(low);
    puts("PASS: Finder alias files, bounded chains and resource restoration");
    return 0;
}
