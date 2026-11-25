/*+===================================================================
  File:      timezone_icu.c

  Summary:   Loads the host system's time-zone provider for CarbonLib.

  Classes:   None.

  Functions: ROMlib_carbon_timezone_icu.

  Origin:    Microsoft Windows ICU ABI and ICU4C ucal.h and uenum.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "timezone_icu.h"

#ifdef _WIN32
#include <stdbool.h>
#include <string.h>
#include <windows.h>

/* Windows ICU exposes stable, unversioned C symbols since Windows 10 1903.
   Load it from System32 so the emulator can still run without it. */
const zone_icu* ROMlib_carbon_timezone_icu(void)
{
    static zone_icu api;
    static bool attempted;
    if (attempted)
        return api.open ? &api : NULL;
    attempted = true;
    HMODULE module = LoadLibraryExW(L"icu.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module)
        return NULL;
#define LOAD(field, symbol)                                \
    do {                                                   \
        FARPROC address = GetProcAddress(module, #symbol); \
        if (!address) {                                    \
            memset(&api, 0, sizeof api);                   \
            FreeLibrary(module);                           \
            return NULL;                                   \
        }                                                  \
        memcpy(&api.field, &address, sizeof address);      \
    } while (0)
    LOAD(open, ucal_open);
    LOAD(close, ucal_close);
    LOAD(canonical, ucal_getCanonicalTimeZoneID);
    LOAD(set_date, ucal_setMillis);
    LOAD(field, ucal_get);
    LOAD(next_transition, ucal_getTimeZoneTransitionDate);
    LOAD(display_name, ucal_getTimeZoneDisplayName);
    LOAD(open_names, ucal_openTimeZones);
    LOAD(next_name, uenum_next);
    LOAD(close_names, uenum_close);
#undef LOAD
    return &api;
}

#else

const zone_icu* ROMlib_carbon_timezone_icu(void)
{
    return 0;
}
#endif
