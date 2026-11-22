/*+===================================================================
  File:      ecf_memory_test.c

  Summary:   Implements ecf memory test support for regression tests and test fixtures.

  Classes:   None.

  Functions: C_FSpGetFInfo, ROMlib_filename_from_fsspec, ROMlib_launch_native_app, ROMlib_launch_path_from_fsspec,
             ROMlib_set_system_version, ROMlib_win_relaunch, config_prefs_set_many, main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "rsys/common.h"
#include "../../options/parsenum.c"
#include "../../options/ecf.c"
#undef main
#include <assert.h>
int ROMlib_syszone_size, ROMlib_applzone_size, ROMlib_stack_size = 256 * 1024;
char ROMlib_exit;
static int launches, expected_system, expected_app, launch_result = 1;
static int expected_argc;
static const char* expected_arguments[32];
uint32 system_version = 0x711;
char* ROMlib_configfilename = "test prefs";
char ROMlib_configsection[800] = "APPL.TEST";
static int saved_profiles;

/* Persistence is exercised against the real INI implementation in integration
   tests; this fixture inspects the mount list passed to process launch. */
int config_prefs_store_ini(const char* path, const char* section, FILE* script)
{
    assert(path && !strcmp(section, "MountedImages") && script);
    return 1;
}

static OSErr info_error, path_error;
static int native_launches;
static OSType launch_type = TICK("APPL");

OSErr C_FSpGetFInfo(FSSpec* file, FInfo* info)
{
    assert(file);
    memset(info, 0, sizeof *info);
    info->fdType = CL(launch_type);
    return info_error;
}

int ROMlib_launch_native_app(int argc, char** argv)
{
    assert(argc == 3 && !strcmp(argv[0], "D:/new app") && !strcmp(argv[1], "D:/document one") &&
           !strcmp(argv[2], "D:/document two") && !argv[3]);
    ++native_launches;
    return noErr;
}

void ROMlib_set_system_version(uint32 value)
{
    system_version = value;
}

int config_prefs_set_many(const char* path, const char* section, const char* const* keys, const char* const* values,
                          size_t count)
{
    assert(!strcmp(path, "test prefs") && !strcmp(section, "APPL.TEST"));
    assert(count == 3 && !strcmp(keys[0], "SystemVersion") && !strcmp(keys[1], "SysZone") &&
           !strcmp(keys[2], "ApplZone"));
    assert(!strcmp(values[0], "9.0.4") && !strcmp(values[1], "16777216") && !strcmp(values[2], "67108864"));
    ++saved_profiles;
    return 1;
}

OSErr ROMlib_filename_from_fsspec(char** path, FSSpec* file)
{
    assert(file);
    if (path_error)
        return path_error;
    *path = strdup(file->name[0] == 1 ? "D:/document one" : file->name[0] == 2 ? "D:/document two" : "D:/new app");
    return *path ? noErr : memFullErr;
}

OSErr ROMlib_launch_path_from_fsspec(char** path, FSSpec* file)
{
    return ROMlib_filename_from_fsspec(path, file);
}

int ROMlib_win_relaunch(int argc, char** argv, int system, int app)
{
    int i;
    assert(argc == expected_argc && system == expected_system && app == expected_app);
    for (i = 0; i < argc; ++i)
        assert(!strcmp(argv[i], expected_arguments[i]));
    assert(!argv[argc]);
    ++launches;
    return launch_result;
}

static void baseline(void)
{
    ROMlib_syszone_size = 2 * 1024 * 1024;
    ROMlib_applzone_size = 6 * 1024 * 1024;
}

int main(void)
{
    int value = 123, i;
    FSSpec app = {0};
    char* arguments[] = {"romaw", "-debug", "trace", "original app", "document one", "tail\\"};
    char* remaining[] = {arguments[0], arguments[3], arguments[4], arguments[5]};
    const char* invalid[] = {"1", "127k", "-8m", "2048m", "999999999999999999999999", "8mb", "8m ", " 8m"};
    assert(ROMlib_ecf_parse_zone("8m", TRUE, &value) && value == 8388608);
    assert(ROMlib_ecf_parse_zone("16M", FALSE, &value) && value == 16777216);
    assert(ROMlib_ecf_parse_zone("512k", FALSE, &value) && value == 524288);
    assert(ROMlib_ecf_parse_zone("131073", TRUE, &value) && value == 139264);
    assert(ROMlib_ecf_parse_zone("", TRUE, &value) && !value);
    assert(ROMlib_ecf_parse_zone("0", FALSE, &value) && !value);
    assert(!ROMlib_ecf_parse_zone("128k", FALSE, &value));
    for (i = 0; i < NELEM(invalid); ++i) {
        value = 123;
        assert(!ROMlib_ecf_parse_zone(invalid[i], TRUE, &value) && value == 123);
    }
    ROMlib_ecf_remember_arguments(NELEM(arguments), arguments);
    ROMlib_ecf_remaining_arguments(NELEM(remaining), remaining);
    baseline();
    ROMlib_ecf_startup_memory(FALSE, FALSE);
    ROMlib_exit = 1;
    ROMlib_config_syszone_size = expected_system = 8388608;
    ROMlib_config_applzone_size = expected_app = 16777216;
    expected_argc = 6;
    expected_arguments[0] = "romaw";
    expected_arguments[1] = "-debug";
    expected_arguments[2] = "trace";
    expected_arguments[3] = "D:/new app";
    expected_arguments[4] = "document one";
    expected_arguments[5] = "tail\\";
    assert(ROMlib_ecf_restart_memory(&app) == 1 && launches == 1);
    assert(ROMlib_syszone_size == 2097152 && ROMlib_applzone_size == 6291456);
    ROMlib_ecf_application_operand(FALSE);
    expected_argc = 7;
    expected_arguments[4] = "original app";
    expected_arguments[5] = "document one";
    expected_arguments[6] = "tail\\";
    assert(ROMlib_ecf_restart_memory(&app) == 1);
    ROMlib_ecf_application_operand(TRUE);
    expected_argc = 6;
    expected_arguments[4] = "document one";
    expected_arguments[5] = "tail\\";
    launch_result = 0;
    assert(ROMlib_ecf_restart_memory(&app) == -1);
    launch_result = 1;
    _putenv("ROMAW_ECF_SYSZONE=8388608");
    _putenv("ROMAW_ECF_APPLZONE=16777216");
    _putenv("ROMAW_ECF_BROWSER=1");
    baseline();
    ROMlib_ecf_startup_memory(FALSE, FALSE);
    assert(ROMlib_syszone_size == 8388608 && ROMlib_applzone_size == 16777216 && ROMlib_exit == 1);
    assert(!getenv("ROMAW_ECF_SYSZONE") && !ROMlib_ecf_restart_memory(&app));
    ROMlib_config_syszone_size = ROMlib_config_applzone_size = -1;
    expected_system = 2097152;
    expected_app = 6291456;
    expected_argc = 6;
    assert(ROMlib_ecf_restart_memory(&app) == 1);
    baseline();
    _putenv("ROMAW_ECF_SYSZONE=8388608");
    _putenv("ROMAW_ECF_APPLZONE=16777216");
    ROMlib_ecf_startup_memory(TRUE, FALSE);
    ROMlib_config_syszone_size = 8388608;
    ROMlib_config_applzone_size = 16777216;
    assert(ROMlib_syszone_size == 2097152 && ROMlib_applzone_size == 16777216 && !ROMlib_ecf_restart_memory(&app));
    baseline();
    _putenv("ROMAW_ECF_SYSZONE=8388608");
    _putenv("ROMAW_ECF_APPLZONE=16777216");
    ROMlib_ecf_startup_memory(FALSE, TRUE);
    assert(ROMlib_syszone_size == 8388608 && ROMlib_applzone_size == 6291456 && !ROMlib_ecf_restart_memory(&app));
    baseline();
    ROMlib_ecf_startup_memory(TRUE, TRUE);
    assert(!ROMlib_ecf_restart_memory(&app));
    baseline();
    _putenv("ROMAW_ECF_SYSZONE=2146435072");
    _putenv("ROMAW_ECF_APPLZONE=2146435072");
    ROMlib_ecf_startup_memory(FALSE, FALSE);
    assert(ROMlib_syszone_size == 2097152 && ROMlib_applzone_size == 6291456);
    ROMlib_config_syszone_size = ROMlib_config_applzone_size = MAX_SYSZONE_SIZE;
    assert(ROMlib_ecf_restart_memory(&app) == -1);
    /* Accessories inherit heaps and the System version, but neither the
     parent's application, documents or debug flags. */
    baseline();
    launch_result = 1;
    expected_argc = 4;
    expected_arguments[0] = "romaw";
    expected_arguments[1] = "-system";
    expected_arguments[2] = "7.1.1";
    expected_arguments[3] = "D:/new app";
    expected_system = ROMlib_syszone_size;
    expected_app = ROMlib_applzone_size;
    int before = launches;
    assert(ROMlib_launch_desk_file(&app) == noErr && launches == before + 1);
    assert(ROMlib_syszone_size == expected_system && ROMlib_applzone_size == expected_app);
    expected_argc = 6;
    expected_arguments[3] = "-desk-resource";
    expected_arguments[4] = "-32768";
    expected_arguments[5] = "D:/new app";
    assert(ROMlib_launch_desk_resource(&app, -32768) == noErr);
    expected_argc = 4;
    expected_arguments[3] = "D:/new app";
    launch_result = 0;
    assert(ROMlib_launch_desk_file(&app) == paramErr);
    assert(ROMlib_launch_desk_file(NULL) == paramErr);
    launch_result = 1;
    assert(ROMlib_launch_continue(&app, NULL) == noErr);
    assert(!native_launches);
    launch_result = 0;
    assert(ROMlib_launch_continue(&app, NULL) == paramErr);
    launch_result = 1;
    ROMlib_AppParameters_t* parameters = calloc(1, sizeof *parameters + 2 * sizeof(FSSpec));
    assert(parameters);
    parameters->magic = CLC(APP_PARAMS_MAGIC);
    parameters->n_fsspec = CWC(2);
    parameters->fsspec[0].name[0] = 1;
    parameters->fsspec[1].name[0] = 2;
    expected_argc = 6;
    expected_arguments[4] = "D:/document one";
    expected_arguments[5] = "D:/document two";
    assert(ROMlib_launch_continue(&app, parameters) == noErr);
    assert(!native_launches);
    launch_type = TICK("TEXT");
    assert(ROMlib_launch_continue(&app, parameters) == noErr);
    assert(native_launches == 1);
    launch_type = TICK("APPL");
    before = launches;
    parameters->magic = 0;
    assert(ROMlib_launch_continue(&app, parameters) == paramErr);
    parameters->magic = CLC(APP_PARAMS_MAGIC);
    parameters->n_fsspec = CWC(-1);
    assert(ROMlib_launch_continue(&app, parameters) == paramErr);
    info_error = fnfErr;
    assert(ROMlib_launch_continue(&app, NULL) == fnfErr);
    info_error = noErr;
    path_error = fnfErr;
    assert(ROMlib_launch_continue(&app, NULL) == fnfErr);
    path_error = noErr;
    assert(ROMlib_launch_continue(NULL, NULL) == paramErr);
    assert(launches == before && native_launches == 1);
    free(parameters);
    /* Session mounts survive heap-size process replacement, use absolute
     paths, and do not accumulate duplicate selections. */
    assert(!ROMlib_ecf_remember_mount(NULL));
    assert(!ROMlib_ecf_remember_mount(""));
    assert(ROMlib_ecf_remember_mount("D:/test image.toast"));
    assert(!ROMlib_ecf_remember_mount(NULL));
    assert(!ROMlib_ecf_remember_mount(""));
    assert(ROMlib_ecf_remember_mount("D:/test image.toast"));
    assert(ROMlib_ecf_remember_mount("D:/another.iso"));
    assert(mounted_image_count == 2);
    baseline();
    explicit_system = explicit_application = FALSE;
    ROMlib_config_syszone_size = expected_system = 8388608;
    ROMlib_config_applzone_size = expected_app = 16777216;
    ROMlib_exit = 0;
    expected_argc = 8;
    expected_arguments[0] = "romaw";
    expected_arguments[1] = "-debug";
    expected_arguments[2] = "trace";
    expected_arguments[3] = "-mount";
    expected_arguments[4] = mounted_images[0];
    expected_arguments[5] = "-mount";
    expected_arguments[6] = mounted_images[1];
    expected_arguments[7] = "D:/new app";
    launch_result = 1;
    assert(ROMlib_ecf_restart_memory(&app) == 1);
    /* Version retries advance monotonically, retain mounted images and heaps, and never modify the current process or saved arguments. */
    expected_system = 8388608;
    expected_app = 16777216;
    expected_argc = 15;
    expected_arguments[3] = "-system";
    expected_arguments[4] = "8.6";
    expected_arguments[5] = "-syszone";
    expected_arguments[6] = "8388608";
    expected_arguments[7] = "-applzone";
    expected_arguments[8] = "16777216";
    expected_arguments[9] = "-ppc-startup-retry";
    expected_arguments[10] = "-mount";
    expected_arguments[11] = mounted_images[0];
    expected_arguments[12] = "-mount";
    expected_arguments[13] = mounted_images[1];
    expected_arguments[14] = "D:/new app";
    assert(ROMlib_ecf_restart_system(&app, 0x711) == 1);
    assert(ROMlib_syszone_size == 2097152 && ROMlib_applzone_size == 6291456);
    ROMlib_syszone_size = 8388608;
    ROMlib_applzone_size = 16777216;
    expected_arguments[4] = "9.0.4";
    expected_app = 33554432;
    expected_arguments[8] = "33554432";
    assert(ROMlib_ecf_restart_system(&app, 0x860) == 1);
    ROMlib_applzone_size = 33554432;
    expected_arguments[4] = "9.2.2";
    expected_system = 16777216;
    expected_app = 67108864;
    expected_arguments[6] = "16777216";
    expected_arguments[8] = "67108864";
    assert(ROMlib_ecf_restart_system(&app, 0x904) == 1);
    ROMlib_syszone_size = expected_system;
    ROMlib_applzone_size = expected_app;
    assert(ROMlib_ecf_restart_system(&app, 0x921) == 1);
    before = launches;
    assert(!ROMlib_ecf_restart_system(&app, 0x922));
    assert(!ROMlib_ecf_restart_system(&app, 0x1000));
    assert(launches == before);
    baseline();
    assert(ROMlib_ecf_restart_system(&app, 0x922) == 1);
    ROMlib_syszone_size = expected_system;
    ROMlib_applzone_size = expected_app;
    launch_result = 0;
    expected_arguments[4] = "8.6";
    assert(ROMlib_ecf_restart_system(&app, 0x711) == -1);
    launch_result = 1;
    assert(ROMlib_ecf_restart_system(NULL, 0x711) == -1);
    system_version = 0x711;
    expected_arguments[4] = "8.6.0";
    assert(ROMlib_ecf_require_system(&app, 0x860) == 1);
    before = launches;
    system_version = 0x860;
    assert(!ROMlib_ecf_require_system(&app, 0x860));
    assert(!ROMlib_ecf_require_system(&app, 0x711));
    assert(!ROMlib_ecf_require_system(&app, 0x1000));
    assert(launches == before);
    expected_arguments[4] = "8.6";
    char* version_arguments[] = {"romaw",        "-system",      "7.1.1",  "-syszone", "2m",
                                 "-applzone",    "6m",           "-debug", "trace",    "-ppc-startup-retry",
                                 "original app", "document one", "tail\\"};
    char* version_remaining[] = {version_arguments[0], version_arguments[10], version_arguments[11],
                                 version_arguments[12]};
    free(saved_arguments);
    ROMlib_ecf_remember_arguments(NELEM(version_arguments), version_arguments);
    ROMlib_ecf_remaining_arguments(NELEM(version_remaining), version_remaining);
    ROMlib_exit = 1;
    expected_argc = 17;
    expected_arguments[4] = "9.0.4";
    expected_arguments[15] = "document one";
    expected_arguments[16] = "tail\\";
    assert(ROMlib_ecf_restart_system(&app, 0x860) == 1);
    assert(!strcmp(saved_arguments[2], "7.1.1"));
    assert(ROMlib_syszone_size == expected_system && ROMlib_applzone_size == expected_app);
    char* mount_arguments[] = {"romaw",  "-mount", mounted_images[0], "-system",      "7.1.1",
                               "-debug", "trace",  "original app",    "document one", "tail\\"};
    char* mount_remaining[] = {mount_arguments[0], mount_arguments[7], mount_arguments[8], mount_arguments[9]};
    free(saved_arguments);
    ROMlib_ecf_remember_arguments(NELEM(mount_arguments), mount_arguments);
    ROMlib_ecf_remaining_arguments(NELEM(mount_remaining), mount_remaining);
    assert(ROMlib_ecf_restart_system(&app, 0x860) == 1);
    retry_version = 0x904;
    ROMlib_ecf_apply_startup_profile();
    assert(system_version == 0x904);
    ROMlib_ecf_complete_startup(FALSE);
    assert(!saved_profiles && !retry_version);
    retry_version = 0x904;
    ROMlib_ecf_complete_startup(TRUE);
    assert(saved_profiles == 1 && !retry_version);
    ROMlib_ecf_complete_startup(TRUE);
    assert(saved_profiles == 1);
    expected_argc = 8;
    expected_arguments[0] = "romaw";
    expected_arguments[1] = "-mount";
    expected_arguments[2] = mounted_images[0];
    expected_arguments[3] = "-mount";
    expected_arguments[4] = mounted_images[1];
    expected_arguments[5] = "-system";
    expected_arguments[6] = "9.0.4";
    expected_arguments[7] = "D:/new app";
    expected_system = ROMlib_syszone_size;
    expected_app = ROMlib_applzone_size;
    assert(ROMlib_launch_continue(&app, NULL) == noErr);
    free(saved_arguments);
    ROMlib_ecf_remember_arguments(NELEM(arguments), arguments);
    ROMlib_ecf_remaining_arguments(NELEM(remaining), remaining);
    mounted_image_count = 0;
    expected_argc = 6;
    expected_arguments[0] = "romaw";
    expected_arguments[1] = "-debug";
    expected_arguments[2] = "trace";
    expected_arguments[3] = "D:/new app";
    expected_arguments[4] = "document one";
    expected_arguments[5] = "tail\\";
    baseline();
    ROMlib_ecf_startup_memory(FALSE, FALSE);
    ROMlib_config_syszone_size = ROMlib_config_applzone_size = -1;
    expected_system = 8 * 1024 * 1024;
    expected_app = 64 * 1024 * 1024;
    assert(ROMlib_ecf_restart_mode_memory(&app, TRUE, 0, 0, 0) == 1);
    ROMlib_syszone_size = expected_system;
    ROMlib_applzone_size = expected_app;
    assert(!ROMlib_ecf_restart_mode_memory(&app, TRUE, 0, 0, 0));
    expected_system = 2 * 1024 * 1024;
    expected_app = 8 * 1024 * 1024;
    assert(ROMlib_ecf_restart_mode_memory(&app, FALSE, 0, 0, 0) == 1);
    baseline();
    expected_app = 4 * 1024 * 1024;
    assert(ROMlib_ecf_restart_mode_memory(&app, FALSE, 18 * 1024 * 1024, 0, 0) == 1);
    ROMlib_applzone_size = expected_app;
    assert(!ROMlib_ecf_restart_mode_memory(&app, FALSE, 16 * 1024 * 1024, 0, 0));
    assert(ROMlib_ecf_restart_mode_memory(&app, FALSE, 32 * 1024 * 1024, 0, 0) == -1);
    assert(base_applzone == 6 * 1024 * 1024 && base_syszone == 2 * 1024 * 1024);
    baseline();
    ROMlib_ecf_startup_memory(TRUE, TRUE);
    assert(!ROMlib_ecf_restart_mode_memory(&app, TRUE, 0, 0, 0));
    assert(!ROMlib_ecf_restart_mode_memory(&app, FALSE, 32 * 1024 * 1024, 0, 0));
    ROMlib_ecf_startup_memory(FALSE, FALSE);
    ROMlib_config_syszone_size = expected_system = 4 * 1024 * 1024;
    ROMlib_config_applzone_size = expected_app = 16 * 1024 * 1024;
    assert(ROMlib_ecf_restart_mode_memory(&app, TRUE, 0, 0, 0) == 1);
    /* SIZE is the automatic partition, including values below the old default.
       Explicit per-app and command-line choices still override the resource. */
    ROMlib_config_syszone_size = ROMlib_config_applzone_size = -1;
    baseline();
    ROMlib_ecf_startup_memory(FALSE, FALSE);
    expected_system = 8 * 1024 * 1024;
    expected_app = 66048 * 1024;
    assert(ROMlib_ecf_restart_mode_memory(&app, TRUE, 0, expected_app, 1024 * 1024) == 1);
    ROMlib_syszone_size = expected_system;
    ROMlib_applzone_size = expected_app;
    assert(!ROMlib_ecf_restart_mode_memory(&app, TRUE, 0, expected_app, 1024 * 1024));
    expected_app = 2 * 1024 * 1024;
    assert(ROMlib_ecf_restart_mode_memory(&app, TRUE, 0, expected_app - 1, 1024 * 1024) == 1);
    expected_app = MIN_APPLZONE_SIZE;
    assert(ROMlib_ecf_restart_mode_memory(&app, TRUE, 0, 128 * 1024, 64 * 1024) == 1);
    assert(ROMlib_ecf_restart_mode_memory(&app, TRUE, 0, INT_MAX, 1024) == -1);
    expected_app = 16 * 1024 * 1024;
    ROMlib_config_applzone_size = expected_app;
    assert(ROMlib_ecf_restart_mode_memory(&app, TRUE, 0, 66048 * 1024, 1024) == 1);
    ROMlib_ecf_startup_memory(FALSE, TRUE);
    expected_app = ROMlib_applzone_size;
    assert(!ROMlib_ecf_restart_mode_memory(&app, TRUE, 0, 66048 * 1024, 1024));
    ROMlib_config_applzone_size = -1;
    baseline();
    ROMlib_ecf_startup_memory(FALSE, FALSE);
    expected_system = 2 * 1024 * 1024;
    expected_app = 4 * 1024 * 1024;
    assert(ROMlib_ecf_restart_mode_memory(&app, FALSE, 18 * 1024 * 1024, 6 * 1024 * 1024, 4 * 1024 * 1024) == 1);
    assert(ROMlib_ecf_restart_mode_memory(&app, FALSE, 18 * 1024 * 1024, 6 * 1024 * 1024, 5 * 1024 * 1024) == -1);
    /* Explicit access modes survive child launch arguments; Put Away removes
       exactly one source and preserves the remaining source's access mode. */
    assert(ROMlib_ecf_remember_mount("D:/discard.hfv"));
    assert(ROMlib_ecf_remember_mount_mode("D:/rw.toast", 1));
    assert(!strcmp(mounted_options[mounted_image_count - 1], "-mount-rw"));
    int mode = 0;
    assert(ROMlib_ecf_mounted_operand(mounted_image_count, &mode) == NULL);
    assert(ROMlib_ecf_mounted_operand(mounted_image_count - 1, &mode) == mounted_images[mounted_image_count - 1]);
    assert(mode == 1);
    size_t old_count = mounted_image_count;
    ROMlib_ecf_forget_mount("D:/not mounted");
    assert(mounted_image_count == old_count);
    ROMlib_ecf_forget_mount(mounted_images[0]);
    assert(mounted_image_count == old_count - 1);
    char* forwarded[32];
    int forwarded_count = append_mount_arguments(forwarded, 0);
    assert(forwarded_count == 2 * mounted_image_count);
    assert(!strcmp(forwarded[forwarded_count - 2], "-mount-rw"));
    puts("ECF memory: deferred heaps, retries, mounts and native/Mac launchContinue routing passed");
    return 0;
}
