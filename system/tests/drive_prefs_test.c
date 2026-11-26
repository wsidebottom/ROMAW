/*+===================================================================
  File:      drive_prefs_test.c

  Summary:   Implements drive prefs test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "rsys/configprefs.h"
#include "rsys/mac_storage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

static void make_directory(const char* path)
{
#ifdef _WIN32
    assert(!_mkdir(path));
#else
    assert(!mkdir(path, 0700));
#endif
}

int main(void)
{
    make_directory("drive-format-detect");
    assert(config_drive_detect_format("drive-format-detect") == CONFIG_DRIVE_WINDOWS);
    FILE* marker = fopen("drive-format-detect/%Marker", "wb");
    assert(marker && fwrite("ordinary", 1, 8, marker) == 8 && !fclose(marker));
    assert(config_drive_detect_format("drive-format-detect") == CONFIG_DRIVE_WINDOWS);
    unsigned char double_file[82] = {0, 5, 22, 7, 0, 2, 0, 0};
    double_file[25] = 1;
    ROMAWMacStorageEntry info = {9, 50, 32, 0};
    assert(romaw_mac_storage_store(double_file, sizeof double_file, sizeof double_file, 0, &info));
    marker = fopen("drive-format-detect/%Marker", "wb");
    assert(marker && fwrite(double_file, 1, sizeof double_file, marker) == sizeof double_file && !fclose(marker));
    assert(config_drive_detect_format("drive-format-detect") == CONFIG_DRIVE_ROMAW);
    make_directory("drive-format-detect/.rsrc");
    assert(config_drive_detect_format("drive-format-detect") == CONFIG_DRIVE_BASILISKII);
    make_directory("drive-format-detect/.AppleDouble");
    assert(config_drive_detect_format("drive-format-detect") == CONFIG_DRIVE_NETATALK);
    marker = fopen("drive-format-detect/.DS_Store", "wb");
    assert(marker && !fclose(marker));
    assert(config_drive_detect_format("drive-format-detect") == CONFIG_DRIVE_OSX);
    assert(!remove("drive-format-detect/%Marker") && !remove("drive-format-detect/.DS_Store"));
    assert(!rmdir("drive-format-detect/.rsrc") && !rmdir("drive-format-detect/.AppleDouble"));
    assert(!rmdir("drive-format-detect"));
    const char* file = "drive-prefs.ini";
    remove(file);
    assert(config_drive_prefs_load(file, "C:/Users/William/.romaw/Apps/") == 0);
    assert(!config_drive_sidecars("C:/"));
    assert(!config_drive_sidecars("Y:/Games"));
    assert(!config_drive_sidecars("\\\\server\\share"));
    assert(config_drive_sidecars("c:\\users\\william\\.romaw\\APPS"));
    assert(config_drive_sidecars("C:/Users/William/.romaw/Apps/Kid Pix"));
    assert(!config_drive_sidecars("C:/Users/William/.romaw/Apps2"));
    assert(!config_drive_sidecars("C:/Users/William/.romaw/Apps/../Other"));
    assert(config_drive_sidecars("/home/example"));
    assert(!config_drive_sidecars(NULL));
    assert(config_prefs_set(file, "Other", "Keep", "unchanged"));
    assert(config_drive_prefs_save(file, 0));
    assert(config_drive_prefs_load(file, "C:/Users/William/.romaw/Apps") == 1);
    assert(!config_drive_prefs_mask());
    char value[64];
    assert(config_prefs_get(file, "HostDrives", "SidecarDrives", value, sizeof value));
    assert(!strcmp(value, "none"));
    assert(config_drive_prefs_save(file, (1u << 12) | (1u << 24)));
    assert(config_drive_prefs_load(file, "C:/Users/William/.romaw/Apps") == 1);
    assert(config_drive_sidecars("M:/"));
    assert(config_drive_sidecars("y:\\Software"));
    assert(!config_drive_sidecars("C:/Users"));
    assert(config_prefs_get(file, "Other", "Keep", value, sizeof value));
    assert(!strcmp(value, "unchanged"));
    const char* invalid[] = {"", "C,Y,", "all", "C,Y,1", "C/", "C;Y", "C,,Y", "C,C"};
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
        assert(config_prefs_set(file, "HostDrives", "SidecarDrives", invalid[i]));
        assert(config_drive_prefs_load(file, "C:/Users/William/.romaw/Apps") == 0);
        assert(!config_drive_prefs_mask());
        assert(config_drive_sidecars("C:/Users/William/.romaw/Apps"));
    }
    assert(config_prefs_set(file, "HostDrives", "SidecarDrives", "c:, m:, Z"));
    assert(config_drive_prefs_load(file, NULL) == 1);
    assert(config_drive_sidecars("c:/"));
    assert(config_drive_sidecars("Z:/"));
    assert(!config_drive_prefs_save("missing-parent/no.ini", 1u << 3));
    assert(config_drive_sidecars("C:/"));
    assert(!config_drive_sidecars("D:/"));
    const char* folders[] = {"C:\\Games\\Classic\\.", "//server/share/Mac"};
    assert(config_drive_prefs_save_all(file, 0, folders, 2));
    assert(config_drive_directory_count() == 2);
    assert(config_drive_sidecars("c:/games/Classic/Child"));
    assert(config_drive_sidecars("//server/share/Mac/Child"));
    assert(!config_drive_sidecars("C:/Games/Classic2"));
    assert(!config_drive_sidecars("C:/Games/Classic/../PC"));
    assert(!config_drive_sidecars("C:/Games"));
    assert(config_drive_rename("C:/", "Macintosh HD"));
    assert(config_drive_rename("C:/Games/Classic", "Classic Games"));
    assert(config_drive_rename("C:/Equals=Percent%", "Special path"));
    assert(config_drive_prefs_load(file, "C:/Users/William/.romaw/Apps") == 1);
    assert(config_drive_directory_count() == 2);
    assert(config_drive_label("c:\\", value, sizeof value));
    assert(!strcmp(value, "Macintosh HD"));
    assert(config_drive_label("C:/Games/Classic/", value, sizeof value));
    assert(!strcmp(value, "Classic Games"));
    assert(config_drive_label("C:/Equals=Percent%", value, sizeof value));
    assert(!strcmp(value, "Special path"));
    assert(!config_drive_rename("C:/", "bad:name"));
    assert(!config_drive_rename("C:/", ""));
    assert(!config_drive_rename("C:/", "bad\nname"));
    assert(!config_drive_rename("C:/", " trailing "));
    const char* invalid_folders[] = {"C:/Good", "relative/path"};
    assert(!config_drive_prefs_save_all(file, 0, invalid_folders, 2));
    assert(config_drive_directory_count() == 2);
    assert(config_drive_prefs_save_all(file, 0, NULL, 0));
    assert(!config_drive_sidecars("C:/Games/Classic"));
    assert(config_drive_prefs_load(file, "C:/Users/William/.romaw/Apps") == 1);
    assert(!config_drive_directory_count());
    assert(config_drive_sidecars("C:/Users/William/.romaw/Apps"));
    {
        int formats[26] = {0};
        formats[0] = CONFIG_DRIVE_WINDOWS;
        formats[1] = CONFIG_DRIVE_OSX;
        formats[2] = CONFIG_DRIVE_NETATALK;
        formats[3] = CONFIG_DRIVE_BASILISKII;
        formats[4] = CONFIG_DRIVE_ROMAW;
        assert(config_drive_prefs_save_formats(file, formats, NULL, 0));
        assert(config_drive_format("A:/") == CONFIG_DRIVE_WINDOWS);
        assert(config_drive_format("b:/Folder") == CONFIG_DRIVE_OSX);
        assert(config_drive_format("C:/Folder") == CONFIG_DRIVE_NETATALK);
        assert(config_drive_set_windows_desktop("C:/Users/William/Desktop"));
        assert(config_drive_format("c:\\users\\william\\Desktop\\Nested") == CONFIG_DRIVE_WINDOWS);
        assert(!config_drive_sidecars("C:/Users/William/Desktop/Nested"));
        assert(config_drive_format("C:/Users/William/Desktop Backup") == CONFIG_DRIVE_NETATALK);
        assert(config_drive_sidecars("C:/Users/William/Desktop Backup"));
        assert(config_drive_set_windows_desktop(NULL));
        assert(config_drive_format("C:/Users/William/Desktop") == CONFIG_DRIVE_NETATALK);
        assert(config_drive_format("D:/") == CONFIG_DRIVE_BASILISKII);
        assert(config_drive_format("E:/") == CONFIG_DRIVE_ROMAW);
        assert(config_drive_format("F:/") == CONFIG_DRIVE_UNDECLARED);
        assert(config_drive_format("C:/Users/William/.romaw/Apps") == CONFIG_DRIVE_OSX);
        assert(!config_drive_sidecars("A:/") && config_drive_sidecars("E:/"));
        assert(!strcmp(config_drive_format_name(CONFIG_DRIVE_ROMAW), "Executor"));
        assert(config_prefs_set(file, "HostDrives", "FormatE", "ROMAW"));
        assert(config_drive_prefs_load(file, "C:/Users/William/.romaw/Apps"));
        assert(config_drive_format("E:/") == CONFIG_DRIVE_ROMAW);
        assert(config_prefs_set(file, "HostDrives", "FormatE", "Executor"));
        formats[4] = CONFIG_DRIVE_ROMAW + 1;
        assert(!config_drive_prefs_save_formats(file, formats, NULL, 0));
        assert(config_drive_format("E:/") == CONFIG_DRIVE_ROMAW);
        assert(config_drive_prefs_load(file, "C:/Users/William/.romaw/Apps") == 1);
        assert(config_drive_format("E:/") == CONFIG_DRIVE_ROMAW);
        assert(config_drive_format("C:/Folder") == CONFIG_DRIVE_NETATALK);
    }
    remove(file);
    puts("DOS defaults, sidecar selection, Apps override and persistence passed.");
    return 0;
}
