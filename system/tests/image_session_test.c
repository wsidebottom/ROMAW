/*+===================================================================
  File:      image_session_test.c

  Summary:   Check persistent image selections, access modes and Put Away removal.

  Classes:   None.

  Functions: ROMlib_mount_image_mode, main

  Origin:    Regression fixture for Executor image-session persistence.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "rsys/common.h"
#include "../../options/ecf.c"
#undef main
#include <assert.h>

static unsigned restored;

OSErr ROMlib_mount_image_mode(const char* path, int mode)
{
    ++restored;
    return ROMlib_ecf_remember_mount_mode(path, mode) ? noErr : memFullErr;
}

static void clear_session(void)
{
    for (size_t i = 0; i < mounted_image_count; ++i)
        free(mounted_images[i]);
    mounted_image_count = 0;
}

int main(void)
{
    char preferences[L_tmpnam];
    assert(tmpnam(preferences));
    FILE* original = fopen(preferences, "wb");
    assert(original);
    fputs("[Defaults]\nComments=preserve me\n", original);
    assert(!fclose(original));
    ROMlib_ecf_restore_mounts(preferences);
    assert(!restored);
    assert(ROMlib_ecf_remember_mount_mode("D:/Temp/default.img", 0));
    assert(ROMlib_ecf_remember_mount_mode("D:/Temp/read only.image", -1));
    assert(ROMlib_ecf_remember_mount_mode("D:/Temp/optical.toast", 1));
    assert(ROMlib_ecf_remember_mount_mode("d:/TEMP/OPTICAL.toast", 1));
    assert(mounted_image_count == 3);
    assert(!ROMlib_ecf_remember_mount("D:/bad\npath"));
    clear_session();
    char* arguments[] = {"romaw", "-mount-ro", "D:/Temp/optical.toast"};
    ROMlib_ecf_remember_arguments(3, arguments);
    ROMlib_ecf_restore_mounts(preferences);
    assert(restored == 3 && mounted_image_count == 3);
    assert(!strcmp(mounted_options[0], "-mount"));
    assert(!strcmp(mounted_options[1], "-mount-ro"));
    assert(!strcmp(mounted_options[2], "-mount-ro"));
    ROMlib_ecf_forget_mount(mounted_images[1]);
    assert(mounted_image_count == 2);
    char value[256];
    config_prefs_get(preferences, "MountedImages", "Count", value, sizeof value);
    assert(!strcmp(value, "2"));
    config_prefs_get(preferences, "Defaults", "Comments", value, sizeof value);
    assert(!strcmp(value, "preserve me"));
    clear_session();
    restored = 0;
    ROMlib_ecf_restore_mounts(preferences);
    assert(restored == 2 && mounted_image_count == 2);
    ROMlib_ecf_forget_mount(mounted_images[0]);
    ROMlib_ecf_forget_mount(mounted_images[0]);
    clear_session();
    restored = 0;
    ROMlib_ecf_restore_mounts(preferences);
    assert(!restored && !mounted_image_count);
    assert(!remove(preferences));
    puts("Image sessions: real INI persistence, access modes, CLI override, deduplication and Put Away removal passed");
    return 0;
}
