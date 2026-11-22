/*+===================================================================
  File:      config_prefs_test.c

  Summary:   Implements config prefs test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "rsys/configprefs.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void write_text(const char* path, const char* text)
{
    FILE* file = fopen(path, "wb");
    assert(file && fputs(text, file) >= 0 && !fclose(file));
}

static void expect_script(const char* path, const char* section, const char* text)
{
    FILE* file = config_prefs_open(path, section);
    char bytes[4096];
    assert(file);
    size_t n = fread(bytes, 1, sizeof bytes - 1, file);
    bytes[n] = 0;
    if (strcmp(bytes, text))
        fprintf(stderr, "Expected script:\n%sActual script:\n%s", text, bytes);
    assert(!strcmp(bytes, text));
    assert(!fclose(file));
}

static int collect_application(const char* section, void* context)
{
    unsigned* count = context;
    assert(!strcmp(section, "APPL.TEST") || !strcmp(section, "APPL.OTHR"));
    ++*count;
    return 1;
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    assert(config_prefs_validate_line("DisplayScale=100"));
    assert(config_prefs_validate_line("DisplayScale=150"));
    assert(config_prefs_validate_line("DisplayScale=25"));
    assert(config_prefs_validate_line("DisplayScale=1600"));
    assert(!config_prefs_validate_line("DisplayScale=0"));
    assert(!config_prefs_validate_line("DisplayScale=1601"));
    assert(!config_prefs_validate_line("DisplayScale=NaN"));
    assert(config_prefs_validate_line("PreferM68K=0"));
    assert(config_prefs_validate_line("PreferM68K=1"));
    assert(!config_prefs_validate_line("PreferM68K=2"));
    assert(config_prefs_set("scale.ini", "APPL.TEST", "DisplayScale", "150"));
    assert(config_prefs_set("scale.ini", "APPL.OTHR", "DisplayScale", "200"));
    expect_script("scale.ini", "APPL.TEST", "DisplayScale=150;\n");
    expect_script("scale.ini", "APPL.OTHR", "DisplayScale=200;\n");
    const char* switches[] = {"StickyMenus", "ForwardDelete", "AutoRefresh", "SubstituteFonts"};
    for (unsigned i = 0; i < sizeof switches / sizeof switches[0]; ++i) {
        char line[128];
        snprintf(line, sizeof line, "%s=1", switches[i]);
        assert(config_prefs_validate_line(line));
        snprintf(line, sizeof line, "%s=2", switches[i]);
        assert(!config_prefs_validate_line(line));
        assert(config_prefs_set("boolean.ini", "APPL.TEST", switches[i], "1"));
    }
    expect_script("boolean.ini", "APPL.TEST", "SubstituteFonts=1;\nAutoRefresh=1;\nForwardDelete=1;\nStickyMenus=1;\n");
    write_text("applications.ini", "[Defaults]\n[HostDrives]\n[APPL.TEST]\n[invalid]\n[0x4150504c.0x4f544852]\n");
    unsigned applications = 0;
    assert(config_prefs_visit_applications("applications.ini", collect_application, &applications));
    assert(applications == 2);
    const char* prefs = "ROMAW.prefs";
    assert(config_prefs_seed(prefs, "missing-master"));
    write_text(
        "obsolete.ini",
        "[ROMAW]\r\nStartup=ROMAW Finder\r\nOther=keep\r\nBrowser=old app\r\n[Elsewhere]\r\nStartup=unchanged\r\n");
    assert(config_prefs_seed("obsolete.ini", argv[1]));
    char obsolete[256];
    assert(!config_prefs_get("obsolete.ini", "ROMAW", "Startup", obsolete, sizeof obsolete));
    assert(!config_prefs_get("obsolete.ini", "ROMAW", "Browser", obsolete, sizeof obsolete));
    assert(config_prefs_get("obsolete.ini", "ROMAW", "Other", obsolete, sizeof obsolete) && !strcmp(obsolete, "keep"));
    assert(config_prefs_get("obsolete.ini", "Elsewhere", "Startup", obsolete, sizeof obsolete) &&
           !strcmp(obsolete, "unchanged"));
    assert(config_prefs_remove_startup("obsolete.ini"));
    FILE* script = tmpfile();
    assert(script);
    fputs("SoundOn=1\nCloseIsQuit=1\n"
          "Rule000000=CRC('CODE',1):0x1234{SysZone=8m;};\n",
          script);
    write_text(prefs, "[APPL.TEST]\r\nSize=800x600\r\n"
                      "Location=-100,20\r\n[Unrelated]\r\nData000000=0102\r\n");
    assert(config_prefs_store(prefs, "APPL.TEST", script));
    expect_script(prefs, "APPL.TEST",
                  "Options={};\nSoundOn=1;\nCloseIsQuit=1;\n"
                  "CRC('CODE',1):0x1234{SysZone=8m;};\n");
    char bytes[4096], section[CONFIG_PREFS_SECTION_SIZE];
    FILE* file = fopen(prefs, "rb");
    assert(file);
    size_t n = fread(bytes, 1, sizeof bytes - 1, file);
    bytes[n] = 0;
    fclose(file);
    assert(strstr(bytes, "Size=800x600\r\nLocation=-100,20"));
    assert(strstr(bytes, "[Unrelated]\r\nData000000=0102"));
    assert(!strstr(bytes, "ConfigFormat=") && !strstr(bytes, "Config000"));
    assert(config_prefs_seed(prefs, argv[1]));
    expect_script(prefs, "APPL.TEST",
                  "Options={};\nSoundOn=1;\nCloseIsQuit=1;\n"
                  "CRC('CODE',1):0x1234{SysZone=8m;};\n");
    assert(!config_prefs_store(prefs, "bad]\n[section", script));
    fclose(script);
    assert(config_prefs_seed("first-start.ini", argv[1]));
    file = fopen(argv[1], "rb");
    assert(file);
    unsigned count = 0;
    while (fgets(bytes, sizeof bytes, file))
        if (!strncmp(bytes, "[APPL.", 6)) {
            char* end = strchr(bytes, ']');
            assert(end);
            *end = 0;
            FILE* restored = config_prefs_open("first-start.ini", bytes + 1);
            assert(restored);
            assert(!fseek(restored, 0, SEEK_END) && ftell(restored) > 0);
            fclose(restored);
            ++count;
        }
    fclose(file);
    assert(count == 123);
    config_prefs_app_section(0x4150504c, 0x3842494d, section);
    assert(!strcmp(section, "APPL.8BIM"));
    config_prefs_app_section(0x4150504c, 0x005bff20, section);
    assert(!strcmp(section, "APPL.0x005bff20"));
    assert(config_prefs_set("case.ini", "APPL.RSED", "Size", "800x600"));
    assert(config_prefs_set("case.ini", "APPL.rsed", "Size", "900x700"));
    assert(config_prefs_get("case.ini", "APPL.RSED", "Size", bytes, sizeof bytes));
    assert(!strcmp(bytes, "800x600"));
    assert(config_prefs_get("case.ini", "APPL.rsed", "Size", bytes, sizeof bytes));
    assert(!strcmp(bytes, "900x700"));
    config_prefs_app_section(0x20202020, 0x54455320, section);
    assert(!strcmp(section, "    .TES "));
    assert(config_prefs_set("case.ini", section, "Size", "512x342"));
    assert(config_prefs_get("case.ini", section, "Size", bytes, sizeof bytes));
    assert(!strcmp(bytes, "512x342"));
    script = tmpfile();
    assert(script);
    fputs("SysZone=0\nApplZone=0\nBitsPerPixel=0\nNoClock=0\nRightButtonModifier=0\nDelay=0\n", script);
    assert(config_prefs_store("zero.ini", "APPL.TEST", script));
    fclose(script);
    expect_script("zero.ini", "APPL.TEST", "Options={};\nNoClock=0;\nRightButtonModifier=0;\nDelay=0;\n");
    script = tmpfile();
    assert(script);
    fputs("[Injected]\n", script);
    assert(!config_prefs_store(prefs, "APPL.TEST", script));
    fclose(script);
    expect_script(prefs, "APPL.TEST",
                  "Options={};\nSoundOn=1;\nCloseIsQuit=1;\n"
                  "CRC('CODE',1):0x1234{SysZone=8m;};\n");
    write_text("gestalt.ini", "[APPL.TEST]\n"
                              "Gestalt('fs  ')={0,1};\n"
                              "Gestalt('a=b ')={-5551,0};\n"
                              "Gestalt(0x00667320) = {0,0x12345678};\n");
    expect_script("gestalt.ini", "APPL.TEST",
                  "Gestalt('fs  ')={0,1};\n"
                  "Gestalt('a=b ')={-5551,0};\n"
                  "Gestalt(0x00667320) = {0,0x12345678};\n");
    script = tmpfile();
    assert(script);
    fputs("SystemVersion=7.1.1\n", script);
    assert(config_prefs_store("gestalt.ini", "APPL.TEST", script));
    fclose(script);
    expect_script("gestalt.ini", "APPL.TEST",
                  "SystemVersion=7.1.1;\n"
                  "Gestalt('fs  ')={0,1};\n"
                  "Gestalt('a=b ')={-5551,0};\n"
                  "Gestalt(0x00667320) = {0,0x12345678};\n");
    write_text("hex.ini", "[0x4150504c.0X54455354]\nSoundOn=1\n");
    assert(config_prefs_get("hex.ini", "APPL.TEST", "SoundOn", bytes, sizeof bytes));
    assert(!strcmp(bytes, "1"));
    assert(!config_prefs_get("hex.ini", "APPL.test", "SoundOn", bytes, sizeof bytes));
    assert(config_prefs_set("hex.ini", "APPL.TEST", "SoundOn", "0"));
    assert(config_prefs_get("hex.ini", "0x4150504c.TEST", "SoundOn", bytes, sizeof bytes));
    assert(!strcmp(bytes, "0"));
    assert(config_prefs_validate_line("Gestalt('fs  ')={0,0xffffffff};"));
    assert(!config_prefs_validate_line("Gestalt('fs  ')={banana,1};"));
    assert(config_prefs_validate_line("ScreenLocation=-10,0"));
    assert(!config_prefs_validate_line("ScreenSize=wide,tall"));
    assert(!config_prefs_validate_line("SysZone=999999999999999999999m"));
    write_text("launch.ini", "[APPL.TEST]\nSystemVersion=7.1.1\nSysZone=2m\n"
                             "ApplZone=6m\nSoundOn=1\nCustom=retain\n"
                             "[APPL.OTHR]\nSysZone=1m\n");
    const char* keys[] = {"SystemVersion", "SysZone", "ApplZone"};
    const char* values[] = {"9.2.2", "16777216", "67108864"};
    assert(config_prefs_set_many("launch.ini", "APPL.TEST", keys, values, 3));
    for (size_t i = 0; i < 3; ++i) {
        assert(config_prefs_get("launch.ini", "APPL.TEST", keys[i], bytes, sizeof bytes));
        assert(!strcmp(bytes, values[i]));
    }
    assert(config_prefs_get("launch.ini", "APPL.TEST", "Custom", bytes, sizeof bytes) && !strcmp(bytes, "retain"));
    assert(config_prefs_get("launch.ini", "APPL.OTHR", "SysZone", bytes, sizeof bytes) && !strcmp(bytes, "1m"));
    const char* invalid_values[] = {"8.6", "8m", "bad\nvalue"};
    assert(!config_prefs_set_many("launch.ini", "APPL.TEST", keys, invalid_values, 3));
    assert(config_prefs_get("launch.ini", "APPL.TEST", "SystemVersion", bytes, sizeof bytes) &&
           !strcmp(bytes, "9.2.2"));
    printf(
        "Configuration prefs: %u master sections, first-start seeding, readable saves, case-sensitive identities and preservation passed\n",
        count);
    return 0;
}
