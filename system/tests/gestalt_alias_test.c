/*+===================================================================
  File:      gestalt_alias_test.c

  Summary:   Implements gestalt alias test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#define NDEBUG 1
#include "../../runtime/gestalt.c"
#undef main
#undef NDEBUG
#include <assert.h>

uint32 system_version;
int ROMlib_pretend_help, ROMlib_pretend_script, ROMlib_pretend_edition;
int ROMlib_pretend_alias;
// Built-in Gestalt entries retain the trap-table addresses in this fixture.
void* tooltraptable[NTOOLENTRIES];
void* ostraptable[NOSENTRIES];

int main(void)
{
    static const uint32 versions[] = {0x607, 0x700, 0x711, 0x755, 0x922};
    for (unsigned i = 0; i < NELEM(versions); ++i)
        for (int enabled = 0; enabled < 2; ++enabled) {
            system_version = versions[i];
            ROMlib_pretend_alias = enabled;
            gestaltentry_t* entry = find_selector_in_table(gestaltAliasMgrAttr, gtable, NELEM(gtable));
            assert(!!entry == (enabled || system_version >= 0x700));
            if (entry)
                assert(entry->value == 1);
            assert(!find_selector_in_table(gestaltHelpMgrAttr, gtable, NELEM(gtable)));
            assert(!find_selector_in_table(gestaltScriptMgrVersion, gtable, NELEM(gtable)));
            assert(!find_selector_in_table(gestaltEditionMgrAttr, gtable, NELEM(gtable)));
        }
    puts("Alias Manager discovery: System 6 opt-in and System 7 availability passed");
    return 0;
}
