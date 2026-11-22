/*+===================================================================
  File:      native_preferences_test.c

  Summary:   Exercise the resource-backed Windows preferences tabs and settings routing.

  Classes:   None.

  Functions: main, cygwin_sdlwindow, sdl_preferences_input_reset, ROMlib_preferences_redraw,
             ROMlib_preferences_field_label, ROMlib_preferences_check_label, ROMlib_preferences_check,
             ROMlib_preferences_toggle, ROMlib_extra_preferences_valid, ROMlib_extra_preferences_apply,
             ROMlib_default_preferences_init, ROMlib_profile_preferences_init, ROMlib_profile_preferences_save,
             saveprefvalues.

  Origin:    ROMAW Windows preferences regression fixture using the production dialog procedure.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

#define NDEBUG 1
#include "../../platform/os/cygwin32/win_preferences.c"
#undef main
#undef NDEBUG
#include <assert.h>

static int scenario, applied, saved, defaults_saved;
static ROMlib_extra_preferences result;
char ROMlib_configsection[800] = "APPL.TEST";

HWND cygwin_sdlwindow(void)
{
    return NULL;
}

void sdl_preferences_input_reset(void)
{
}

void ROMlib_preferences_redraw(void)
{
}

const char* ROMlib_preferences_field_label(unsigned field)
{
    return field < 19 && field != 0 && field != 2 && !(field >= 4 && field <= 9) && field != 15 ? "field" : NULL;
}

const char* ROMlib_preferences_check_label(unsigned check)
{
    return check < 21 ? "check" : NULL;
}

int ROMlib_preferences_check(ROMlib_extra_preferences* prefs, unsigned check)
{
    return !!(prefs->checks & (1u << check));
}

void ROMlib_preferences_toggle(ROMlib_extra_preferences* prefs, unsigned check)
{
    prefs->checks ^= 1u << check;
}

int ROMlib_extra_preferences_valid(ROMlib_extra_preferences* prefs, unsigned* bad)
{
    (void)prefs;
    (void)bad;
    return 1;
}

int ROMlib_extra_preferences_apply(ROMlib_extra_preferences* prefs)
{
    ++applied;
    result = *prefs;
    return 1;
}

void ROMlib_profile_preferences_init(ROMlib_extra_preferences* prefs, const char* filename, const char* section)
{
    (void)filename;
    (void)section;
    memset(prefs, 0, sizeof *prefs);
    strcpy(prefs->fields[12], "7.1.1");
    strcpy(prefs->fields[17], "8");
}

void ROMlib_default_preferences_init(ROMlib_extra_preferences* prefs, const char* filename)
{
    ROMlib_profile_preferences_init(prefs, filename, "Defaults");
}

int ROMlib_profile_preferences_save(ROMlib_extra_preferences* prefs, const ROMlib_extra_preferences* original,
                                    const char* filename, const char* section)
{
    (void)original;
    (void)filename;
    assert(section && !strcmp(section, scenario == 4 ? "APPL.OTHR" : "Defaults"));
    ++defaults_saved;
    result = *prefs;
    return 1;
}

int saveprefvalues(char* filename, int32_t x, int32_t y)
{
    (void)filename;
    (void)x;
    (void)y;
    ++saved;
    return 1;
}

static void CALLBACK exercise_dialog(HWND unused, UINT message, UINT_PTR timer, DWORD time)
{
    (void)unused;
    (void)message;
    (void)time;
    HWND dialog = NULL;
    while ((dialog = FindWindowExW(NULL, dialog, L"#32770", L"ROMAW Preferences"))) {
        DWORD process;
        GetWindowThreadProcessId(dialog, &process);
        if (process == GetCurrentProcessId())
            break;
    }
    if (!dialog)
        return;
    KillTimer(NULL, timer);
    native_preferences* state = (native_preferences*)GetWindowLongPtr(dialog, DWLP_USER);
    HWND tabs = GetDlgItem(dialog, IDC_PREF_TABS);
    assert(TabCtrl_GetItemCount(tabs) == (int)state->page_count);
    for (unsigned page = 0; page < state->page_count; ++page) {
        TabCtrl_SetCurSel(tabs, page);
        NMHDR notification = {tabs, IDC_PREF_TABS, TCN_SELCHANGE};
        SendMessage(dialog, WM_NOTIFY, IDC_PREF_TABS, (LPARAM)&notification);
        for (unsigned i = 0; i < state->page_count; ++i)
            assert(!!(GetWindowLongPtr(state->pages[i], GWL_STYLE) & WS_VISIBLE) == (i == page));
    }
    if (scenario == 6)
        assert(!IsWindowEnabled(state->pages[NATIVE_PREF_DRIVES]));
    if (scenario == 5) {
        collect_fields(dialog, state);
        assert(!strcmp(state->edited.fields[17], "0"));
    }
    for (unsigned field = 0; field < 19; ++field) {
        if (!ROMlib_preferences_field_label(field))
            continue;
        HWND page = preferences_container(dialog, 200 + field);
        assert(page == state->pages[field_page(field)]);
        if (field == 12) {
            SetDlgItemTextA(page, IDC_PREF_SYSTEM_VERSION, "9");
            SetDlgItemTextA(page, IDC_PREF_VERSION_MINOR, "2");
            SetDlgItemTextA(page, IDC_PREF_VERSION_PATCH, "2");
        }
        else if (field == 16 || field == 17 || field == 3) {
            SendDlgItemMessage(page, 200 + field, CB_SETCURSEL, 1, 0);
        }
        else
            SetDlgItemTextA(page, 200 + field, "edited");
    }
    for (unsigned check = 0; check < 21; ++check) {
        HWND page = preferences_container(dialog, 300 + check);
        assert(page == state->pages[check == 6 || check >= 15 ? 4 : 3]);
        SendDlgItemMessage(page, 300 + check, BM_SETCHECK, BST_CHECKED, 0);
    }
    HWND slider = GetDlgItem(state->pages[0], IDC_PREF_REFRESH_SLIDER);
    SendMessage(slider, TBM_SETPOS, TRUE, 10);
    SendMessage(state->pages[0], WM_HSCROLL, 0, (LPARAM)slider);
    char refresh[32];
    GetDlgItemTextA(state->pages[0], IDC_PREF_REFRESH, refresh, sizeof refresh);
    assert(!strcmp(refresh, "10"));
    SetDlgItemTextA(state->pages[0], IDC_PREF_REFRESH, "25");
    assert(SendMessage(slider, TBM_GETPOS, 0, 0) == 25);
    SendDlgItemMessage(state->pages[4], IDC_PREF_CPU_PPC, BM_CLICK, 0, 0);
    assert(SendDlgItemMessage(state->pages[4], IDC_PREF_PREFER_M68K, BM_GETCHECK, 0, 0) == BST_UNCHECKED);
    SendDlgItemMessage(state->pages[4], IDC_PREF_PREFER_M68K, BM_CLICK, 0, 0);
    if (state->page_count == NATIVE_PREF_PAGE_COUNT) {
        SendMessage(state->pages[NATIVE_PREF_APPLICATION], WM_COMMAND, IDC_PREF_APP_CURRENT, 0);
        assert(TabCtrl_GetCurSel(tabs) == 0);
        if (scenario < 2) {
            HWND drives = state->pages[NATIVE_PREF_DRIVES];
            SendDlgItemMessage(drives, 102, CB_SETCURSEL, 1, 0);
            SendMessage(drives, WM_COMMAND, MAKEWPARAM(102, CBN_SELCHANGE), (LPARAM)GetDlgItem(drives, 102));
            assert(state->drive_dirty);
        }
    }
    HWND general = state->pages[1];
    assert(SendDlgItemMessage(general, IDC_PREF_SCALE, CB_GETCOUNT, 0, 0) == 6);
    SendDlgItemMessage(general, IDC_PREF_SCALE, CB_SETCURSEL, 5, 0);
    SendMessage(general, WM_COMMAND, MAKEWPARAM(IDC_PREF_SCALE, CBN_SELCHANGE),
                (LPARAM)GetDlgItem(general, IDC_PREF_SCALE));
    char value[256];
    GetDlgItemTextA(general, IDC_PREF_SCALE, value, sizeof value);
    assert(!*value);
    SetDlgItemTextA(general, IDC_PREF_SCALE, "1.5x");
    if (scenario == 1) {
        assert(LOWORD(SendMessage(dialog, DM_GETDEFID, 0, 0)) == IDOK);
        GetDlgItemTextA(dialog, IDOK, value, sizeof value);
        assert(!strcmp(value, "Save"));
    }
    SendMessage(dialog, WM_COMMAND, scenario == 0 ? IDCANCEL : scenario == 2 ? IDC_PREF_SAVE : IDOK, 0);
}

int main(void)
{
    char fixture[L_tmpnam];
    assert(tmpnam(fixture));
    for (scenario = 0; scenario < 7; ++scenario) {
        ROMlib_extra_preferences prefs = {0};
        strcpy(prefs.fields[0], "original");
        strcpy(prefs.fields[12], "7.1.1");
        strcpy(prefs.fields[17], scenario == 5 ? "0" : "8");
        assert(SetTimer(NULL, 0, 50, exercise_dialog));
        int accepted;
        if (scenario == 4) {
            native_preferences profile = {.edited = prefs,
                                          .defaults = &prefs,
                                          .filename = fixture,
                                          .section = "APPL.OTHR",
                                          .page_count = ROMAW_PREF_PAGE_COUNT};
            accepted = show_preferences(NULL, &profile) == IDOK;
        }
        else
            accepted = scenario == 1 ? ROMlib_win_default_preferences(NULL, fixture)
                                     : ROMlib_win_preferences(&prefs, scenario == 6 ? NULL : fixture);
        assert(accepted == (scenario != 0));
        if (scenario == 0) {
            assert(!strcmp(prefs.fields[0], "original"));
            assert(!applied && !saved && !defaults_saved);
            FILE* cancelled = fopen(fixture, "rb");
            assert(!cancelled);
        }
        else {
            assert(!strcmp(result.fields[0], "original") || scenario == 1);
            assert(!strcmp(result.fields[12], "9.2.2"));
            assert(!strcmp(result.fields[16], "1"));
            assert(!strcmp(result.fields[17], "2"));
            assert(!strcmp(result.fields[18], "1.5x"));
            assert(result.checks == 0x1fffff);
        }
    }
    assert(applied == 4 && saved == 1 && defaults_saved == 2);
    assert(!remove(fixture));
    puts(
        "Native preferences: resource pages, tab selection, control routing, Custom scale, Cancel, defaults Save, application Save and OK passed");
    return 0;
}
