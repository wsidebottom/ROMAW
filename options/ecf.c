/*+===================================================================
  File:      ecf.c

  Summary:   Manage application launch profiles, persistent image selections and process handoffs.

  Classes:   None.

  Functions: ROMlib_ecf_application_operand, ROMlib_ecf_apply_startup_profile, ROMlib_ecf_complete_startup,
             ROMlib_ecf_mounted_operand, ROMlib_ecf_mount_operand, ROMlib_ecf_parse_zone, ROMlib_ecf_remaining_arguments,
             ROMlib_ecf_remember_arguments, ROMlib_ecf_remember_mount, ROMlib_ecf_remember_mount_mode,
             ROMlib_ecf_forget_mount, ROMlib_ecf_restore_mounts, ROMlib_ecf_mount_operand_mode, ROMlib_ecf_restart_memory,
             ROMlib_ecf_restart_mode_memory, ROMlib_ecf_restart_system, ROMlib_ecf_require_system, ROMlib_ecf_startup_memory,
             ROMlib_ecf_zone_value, ROMlib_launch_continue, ROMlib_launch_desk_file, ROMlib_launch_desk_resource

  Origin:    Existing repository source in executor/src/options; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* ECF memory values describe the next application launch, never a live heap. */
#include "rsys/common.h"
#include "FileMgr.h"
#include "rsys/ecf.h"
#include "rsys/hfs.h"
#include "rsys/mman.h"
#include "rsys/memsize.h"
#include "rsys/parsenum.h"
#include "rsys/segment.h"
#include "rsys/file.h"
#include "rsys/launch.h"
#include "rsys/prefs.h"
#include "rsys/version.h"
#include "rsys/configprefs.h"
#include "rsys/system_version_request.h"
#include <limits.h>
#if !defined(CYGWIN32)
#include <unistd.h>
#include <spawn.h>
static char* host_executable;
#endif

int ROMlib_config_syszone_size = -1, ROMlib_config_applzone_size = -1;
static char** mounted_images;
static size_t mounted_image_count;
static const char** mounted_options;
static char* mount_preferences;
static boolean_t restoring_mounts;

static boolean_t save_mounts(void)
{
    if (!mount_preferences || restoring_mounts)
        return TRUE;
    FILE* settings = tmpfile();
    if (!settings)
        return FALSE;
    fprintf(settings, "Count=%zu\n", mounted_image_count);
    for (size_t i = 0; i < mounted_image_count; ++i) {
        fprintf(settings, "Image%zu=%s\nMode%zu=%s\n", i, mounted_images[i], i, mounted_options[i]);
    }
    boolean_t ok = !ferror(settings) && config_prefs_store_ini(mount_preferences, "MountedImages", settings);
    fclose(settings);
    return ok;
}

void ROMlib_ecf_restore_mounts(const char* preferences)
{
    free(mount_preferences);
    mount_preferences = preferences ? strdup(preferences) : NULL;
    if (!mount_preferences)
        return;
    char value[32768], key[40];
    config_prefs_get(preferences, "MountedImages", "Count", value, sizeof value);
    char* end;
    unsigned long count = strtoul(value, &end, 10);
    if (!*value || *end || count > 1024)
        return;
    restoring_mounts = TRUE;
    for (unsigned long i = 0; i < count; ++i) {
        snprintf(key, sizeof key, "Mode%lu", i);
        config_prefs_get(preferences, "MountedImages", key, value, sizeof value);
        int mode = !strcmp(value, "-mount-rw") ? 1 : !strcmp(value, "-mount-ro") ? -1 : 0;
        snprintf(key, sizeof key, "Image%lu", i);
        size_t length = config_prefs_get(preferences, "MountedImages", key, value, sizeof value);
        if (length && length < sizeof value - 1) {
            /* An explicit selection in this launch overrides the saved mode. */
            for (size_t j = 0;; ++j) {
                int requested_mode;
                const char* requested = ROMlib_ecf_mount_operand_mode(j, &requested_mode);
                if (!requested)
                    break;
#if defined(CYGWIN32)
                char* absolute = _fullpath(NULL, requested, 0);
#else
                char* absolute = realpath(requested, NULL);
#endif
                if (absolute && !strcasecmp(absolute, value))
                    mode = requested_mode;
                free(absolute);
            }
            OSErr error = ROMlib_mount_image_mode(value, mode);
            if (error && error != -128) /* userCanceledErr */
                fprintf(stderr, "Cannot restore disk image %s (error %d).\n", value, error);
        }
    }
    restoring_mounts = FALSE;
}

void ROMlib_ecf_forget_mount(const char* path)
{
    if (!path)
        return;
    for (size_t i = 0; i < mounted_image_count; ++i)
        if (!strcasecmp(mounted_images[i], path)) {
            free(mounted_images[i]);
            --mounted_image_count;
            memmove(mounted_images + i, mounted_images + i + 1, (mounted_image_count - i) * sizeof *mounted_images);
            memmove(mounted_options + i, mounted_options + i + 1, (mounted_image_count - i) * sizeof *mounted_options);
            if (!save_mounts())
                fprintf(stderr, "Cannot save disk image removal.\n");
            return;
        }
}

boolean_t ROMlib_ecf_remember_mount_mode(const char* path, int mode)
{
    if (!path || !*path || strpbrk(path, "\r\n"))
        return FALSE;
#if defined(CYGWIN32)
    char* absolute = _fullpath(NULL, path, 0);
#else
    char* absolute = realpath(path, NULL);
#endif
    if (!absolute)
        return FALSE;
    for (size_t i = 0; i < mounted_image_count; ++i)
        if (!strcasecmp(mounted_images[i], absolute)) {
            free(absolute);
            return save_mounts();
        }
    if (mounted_image_count >= 1024 || mounted_image_count >= (INT_MAX - 2) / 2 ||
        mounted_image_count >= (SIZE_MAX / sizeof(char*) - 3) / 2) {
        free(absolute);
        return FALSE;
    }
    char** images = realloc(mounted_images, (mounted_image_count + 1) * sizeof *images);
    if (!images) {
        free(absolute);
        return FALSE;
    }
    mounted_images = images;
    const char** options = realloc(mounted_options, (mounted_image_count + 1) * sizeof *options);
    if (!options) {
        free(absolute);
        return FALSE;
    }
    mounted_options = options;
    mounted_options[mounted_image_count] = mode > 0 ? "-mount-rw" : mode < 0 ? "-mount-ro" : "-mount";
    mounted_images[mounted_image_count++] = absolute;
    return save_mounts();
}

boolean_t ROMlib_ecf_remember_mount(const char* path)
{
    return ROMlib_ecf_remember_mount_mode(path, 0);
}

static int append_mount_arguments(char** arguments, int count)
{
    for (size_t i = 0; i < mounted_image_count; ++i) {
        arguments[count++] = (char*)mounted_options[i];
        arguments[count++] = mounted_images[i];
    }
    return count;
}

const char* ROMlib_ecf_mounted_operand(size_t index, int* mode)
{
    if (index >= mounted_image_count)
        return NULL;
    if (mode)
        *mode = !strcmp(mounted_options[index], "-mount-rw")   ? 1
                : !strcmp(mounted_options[index], "-mount-ro") ? -1
                                                               : 0;
    return mounted_images[index];
}

static int append_child_arguments(char** arguments, char version[16])
{
    int count = append_mount_arguments(arguments, 1);
    snprintf(version, 16, "%u.%u.%u", (unsigned)(system_version >> 8), (unsigned)((system_version >> 4) & 15),
             (unsigned)(system_version & 15));
    arguments[count++] = "-system";
    arguments[count++] = version;
    return count;
}

static int base_syszone, base_applzone;
static boolean_t explicit_system, explicit_application;
static int saved_argc;
static char** saved_arguments;
static boolean_t application_operand = TRUE;
static int document_count;
static char** documents;
static uint32 retry_version;

const char* ROMlib_ecf_mount_operand_mode(size_t index, int* mode)
{
    for (int i = 1; i + 1 < saved_argc; ++i)
        if (saved_arguments[i] &&
            (!strcmp(saved_arguments[i], "-mount") || !strcmp(saved_arguments[i], "-mount-ro") ||
             !strcmp(saved_arguments[i], "-mount-rw")) &&
            saved_arguments[i + 1] && saved_arguments[i + 1][0] != '-') {
            if (!index--) {
                if (mode)
                    *mode = !strcmp(saved_arguments[i], "-mount-rw")   ? 1
                            : !strcmp(saved_arguments[i], "-mount-ro") ? -1
                                                                       : 0;
                return saved_arguments[i + 1];
            }
            ++i;
        }
    return NULL;
}

const char* ROMlib_ecf_mount_operand(size_t index)
{
    return ROMlib_ecf_mount_operand_mode(index, NULL);
}

boolean_t ROMlib_ecf_zone_value(long value, boolean_t system, int* out)
{
    int min = system ? MIN_SYSZONE_SIZE : MIN_APPLZONE_SIZE;
    int max = system ? MAX_SYSZONE_SIZE : MAX_APPLZONE_SIZE;
    if (!out || value < 0 || (value && (value < min || value > max)))
        return FALSE;
    *out = (value + ZONE_ALIGN_SIZE - 1) & ~(ZONE_ALIGN_SIZE - 1);
    return TRUE;
}

boolean_t ROMlib_ecf_parse_zone(const char* text, boolean_t system, int* value)
{
    int32 parsed;
    if (!text || !value)
        return FALSE;
    if (!*text)
        return ROMlib_ecf_zone_value(0, system, value);
    return parse_number(text, &parsed, 1) && ROMlib_ecf_zone_value(parsed, system, value);
}

void ROMlib_ecf_remember_arguments(int argc, char** argv)
{
    if (argc <= 0 || (size_t)argc > SIZE_MAX / sizeof *saved_arguments - 2)
        return;
    saved_arguments = malloc((size_t)argc * sizeof *saved_arguments);
    if (!saved_arguments)
        return;
    memcpy(saved_arguments, argv, (size_t)argc * sizeof *saved_arguments);
    saved_argc = argc;
    int retry = 0;
    uint32 requested = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-ppc-startup-retry"))
            retry = 1;
        if (!strcmp(argv[i], "-system") && i + 1 < argc) {
            const char* value = argv[++i];
            size_t consumed = 0;
            requested = ROMlib_required_version_parse(value, strlen(value), &consumed);
            if (consumed != strlen(value))
                requested = 0;
        }
    }
    retry_version = retry ? requested : 0;
#if !defined(CYGWIN32)
#if defined(__linux__)
    {
        char path[PATH_MAX];
        ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
        if (n > 0 && n < sizeof path - 1) {
            path[n] = 0;
            host_executable = strdup(path);
        }
    }
#endif
    if (!host_executable)
        host_executable = realpath(argv[0], NULL);
#endif
}

void ROMlib_ecf_remaining_arguments(int argc, char** argv)
{
    int i, j;
    document_count = argc > 1 ? argc - 1 : 0;
    documents = argv + (argc > 1 ? 1 : argc);
    for (i = 1; i < saved_argc; ++i)
        for (j = 1; j < argc; ++j)
            if (saved_arguments[i] == argv[j]) {
                saved_arguments[i] = NULL;
                break;
            }
}

void ROMlib_ecf_application_operand(boolean_t is_application)
{
    application_operand = is_application;
}

void ROMlib_ecf_startup_memory(boolean_t sys_explicit, boolean_t app_explicit)
{
    const char* text;
    int value, system = ROMlib_syszone_size, app = ROMlib_applzone_size;
    explicit_system = sys_explicit;
    explicit_application = app_explicit;
    ROMlib_ecf_zone_value(ROMlib_syszone_size, TRUE, &base_syszone);
    ROMlib_ecf_zone_value(ROMlib_applzone_size, FALSE, &base_applzone);
    text = getenv("ROMAW_ECF_SYSZONE");
    if (!sys_explicit && text && ROMlib_ecf_parse_zone(text, TRUE, &value) && value)
        system = value;
    text = getenv("ROMAW_ECF_APPLZONE");
    if (!app_explicit && text && ROMlib_ecf_parse_zone(text, FALSE, &value) && value)
        app = value;
    if ((uint64)system + app + ROMlib_stack_size + 32 * 1024 * 1024 <= INT_MAX) {
        ROMlib_syszone_size = system;
        ROMlib_applzone_size = app;
    }
#if defined(CYGWIN32)
    _putenv("ROMAW_ECF_SYSZONE=");
    _putenv("ROMAW_ECF_APPLZONE=");
    _putenv("ROMAW_ECF_BROWSER=");
#else
    unsetenv("ROMAW_ECF_SYSZONE");
    unsetenv("ROMAW_ECF_APPLZONE");
    unsetenv("ROMAW_ECF_BROWSER");
#endif
}

#if !defined(CYGWIN32)
static int relaunch_posix(char** arguments, int system, int app)
{
    extern char** environ;
    char** environment = environ;
    char** child_environment;
    char sysvalue[64], appvalue[64];
    size_t count = 0, i, used = 0;
    pid_t child;
    int error;
    while (environment[count])
        ++count;
    if (count > SIZE_MAX / sizeof *child_environment - 4)
        return 0;
    child_environment = malloc((count + 4) * sizeof *child_environment);
    if (!child_environment)
        return 0;
    for (i = 0; i < count; ++i)
        if (strncmp(environment[i], "ROMAW_ECF_SYSZONE=", sizeof "ROMAW_ECF_SYSZONE=" - 1) &&
            strncmp(environment[i], "ROMAW_ECF_APPLZONE=", sizeof "ROMAW_ECF_APPLZONE=" - 1) &&
            strncmp(environment[i], "ROMAW_ECF_BROWSER=", sizeof "ROMAW_ECF_BROWSER=" - 1))
            child_environment[used++] = environment[i];
    snprintf(sysvalue, sizeof sysvalue, "ROMAW_ECF_SYSZONE=%d", system);
    snprintf(appvalue, sizeof appvalue, "ROMAW_ECF_APPLZONE=%d", app);
    child_environment[used++] = sysvalue;
    child_environment[used++] = appvalue;
    child_environment[used] = NULL;
    error = posix_spawnp(&child, host_executable ? host_executable : arguments[0], NULL, NULL, arguments,
                         child_environment);
    free(child_environment);
    return error == 0;
}
#endif

/* An accessory owns its SDL surface and event loop in a separate process. */
static OSErr launch_desk_file(const FSSpec* file, const INTEGER* resource)
{
    char* path = NULL;
    int result;
    if (!file || !saved_argc)
        return paramErr;
    OSErr error = ROMlib_launch_path_from_fsspec(&path, (FSSpec*)file);
    if (error) {
        free(path);
        return error;
    }
    if (mounted_image_count > (INT_MAX - 7) / 2 || mounted_image_count > (SIZE_MAX / sizeof(char*) - 7) / 2) {
        free(path);
        return memFullErr;
    }
    char** arguments = malloc((7 + mounted_image_count * 2) * sizeof *arguments);
    if (!arguments) {
        free(path);
        return memFullErr;
    }
    arguments[0] = saved_arguments[0];
    char version[16];
    int count = append_child_arguments(arguments, version);
    char resource_id[12];
    if (resource) {
        snprintf(resource_id, sizeof resource_id, "%d", (int)*resource);
        arguments[count++] = "-desk-resource";
        arguments[count++] = resource_id;
    }
    arguments[count++] = path;
    arguments[count] = NULL;
#if defined(CYGWIN32)
    extern int ROMlib_win_relaunch(int, char**, int, int);
    result = ROMlib_win_relaunch(count, arguments, ROMlib_syszone_size, ROMlib_applzone_size);
#else
    result = relaunch_posix(arguments, ROMlib_syszone_size, ROMlib_applzone_size);
#endif
    free(arguments);
    free(path);
    return result ? noErr : paramErr;
}

OSErr ROMlib_launch_desk_file(const FSSpec* file)
{
    return launch_desk_file(file, NULL);
}

OSErr ROMlib_launch_desk_resource(const FSSpec* file, INTEGER resource)
{
    return launch_desk_file(file, &resource);
}

/* launchContinue preserves the caller. Macintosh applications need another
   ROMAW process; only host applications go through the native launcher. */
OSErr ROMlib_launch_continue(const FSSpec* file, const ROMlib_AppParameters_t* parameters)
{
    if (!file)
        return paramErr;
    FSSpec spec = *file;
    FInfo info;
    OSErr error = FSpGetFInfo(&spec, &info);
    if (error)
        return error;
    int mac = info.fdType == TICKX("APPL");
    int documents = 0;
    if (parameters) {
        /* Only ROMAW's existing document-vector ABI is understood here.
         Do not interpret a classic event parameter block as a file count. */
        if (parameters->magic != CLC(APP_PARAMS_MAGIC) || CW(parameters->n_fsspec) < 0)
            return paramErr;
        documents = CW(parameters->n_fsspec);
    }
    if (mac && !saved_argc)
        return paramErr;
    if (mac && mounted_image_count > (INT_MAX - 5 - documents) / 2)
        return memFullErr;
    size_t slots = (size_t)documents + 5 + (mac ? mounted_image_count * 2 : 0);
    if (slots > INT_MAX || slots > SIZE_MAX / sizeof(char*))
        return memFullErr;
    char** arguments = calloc(slots, sizeof *arguments);
    if (!arguments)
        return memFullErr;
    int first = 0;
    char version[16];
    if (mac) {
        arguments[0] = saved_arguments[0];
        first = append_child_arguments(arguments, version);
    }
    int count = first;
    for (int i = 0; i <= documents; ++i) {
        FSSpec target = i ? parameters->fsspec[i - 1] : spec;
        error = mac ? ROMlib_launch_path_from_fsspec(&arguments[count], &target)
                    : ROMlib_filename_from_fsspec(&arguments[count], &target);
        if (error)
            break;
        ++count;
    }
    if (!error && mac) {
        int started;
#if defined(CYGWIN32)
        extern int ROMlib_win_relaunch(int, char**, int, int);
        started = ROMlib_win_relaunch(count, arguments, ROMlib_syszone_size, ROMlib_applzone_size);
#else
        started = relaunch_posix(arguments, ROMlib_syszone_size, ROMlib_applzone_size);
#endif
        error = started ? noErr : paramErr;
    }
    else if (!error)
        error = ROMlib_launch_native_app(count, arguments);
    /* Mount operands and argv[0] are borrowed. Converted file paths are owned,
     including any path a failed conversion may have allocated. */
    for (int i = first; i <= count; ++i)
        free(arguments[i]);
    free(arguments);
    return error;
}

static int restart_application(const FSSpec* application, int system, int app, const char* version)
{
#if defined(CYGWIN32)
    extern int ROMlib_win_relaunch(int, char**, int, int);
#endif
    char *path = NULL, **arguments;
    char system_text[32], app_text[32];
    int count = 0, i, result;
    if (!application || !saved_argc || !system || !app ||
        (uint64)system + app + ROMlib_stack_size + 32 * 1024 * 1024 > INT_MAX)
        return -1;
    if (ROMlib_launch_path_from_fsspec(&path, (FSSpec*)application) != noErr) {
        free(path);
        return -1;
    }
    /* Removed operands leave enough slots for the new application and original
     document operands only on a direct launch. */
    if ((size_t)saved_argc + 9 + mounted_image_count * 2 > INT_MAX ||
        (size_t)saved_argc + 9 + mounted_image_count * 2 > SIZE_MAX / sizeof *arguments) {
        free(path);
        return -1;
    }
    arguments = malloc(((size_t)saved_argc + 9 + mounted_image_count * 2) * sizeof *arguments);
    if (!arguments) {
        free(path);
        return -1;
    }
    for (i = 0; i < saved_argc; ++i)
        if (saved_arguments[i]) {
            if ((!strcmp(saved_arguments[i], "-mount") || !strcmp(saved_arguments[i], "-mount-ro") ||
                 !strcmp(saved_arguments[i], "-mount-rw"))) {
                ++i;
                continue;
            }
            if (version && (!strcmp(saved_arguments[i], "-system") || !strcmp(saved_arguments[i], "-syszone") ||
                            !strcmp(saved_arguments[i], "-applzone")))
                ++i;
            else if (version && !strcmp(saved_arguments[i], "-ppc-startup-retry"))
                continue;
            else
                arguments[count++] = saved_arguments[i];
        }
    if (version) {
        arguments[count++] = "-system";
        arguments[count++] = (char*)version;
        snprintf(system_text, sizeof system_text, "%d", system);
        snprintf(app_text, sizeof app_text, "%d", app);
        arguments[count++] = "-syszone";
        arguments[count++] = system_text;
        arguments[count++] = "-applzone";
        arguments[count++] = app_text;
        arguments[count++] = "-ppc-startup-retry";
    }
    count = append_mount_arguments(arguments, count);
    arguments[count++] = path;
    if (ROMlib_exit)
        for (i = application_operand ? 1 : 0; i < document_count; ++i)
            arguments[count++] = documents[i];
    arguments[count] = NULL;
#if defined(CYGWIN32)
    result = ROMlib_win_relaunch(count, arguments, system, app);
#else
    result = relaunch_posix(arguments, system, app);
#endif
    free(arguments);
    free(path);
    return result ? 1 : -1;
}

int ROMlib_ecf_restart_memory(const FSSpec* application)
{
    int system = explicit_system                  ? ROMlib_syszone_size
                 : ROMlib_config_syszone_size > 0 ? ROMlib_config_syszone_size
                                                  : base_syszone;
    int app = explicit_application              ? ROMlib_applzone_size
              : ROMlib_config_applzone_size > 0 ? ROMlib_config_applzone_size
                                                : base_applzone;
    if (system == ROMlib_syszone_size && app == ROMlib_applzone_size)
        return 0;
    return restart_application(application, system, app, NULL);
}

int ROMlib_ecf_restart_mode_memory(const FSSpec* application, boolean_t mode32, uint64 framebuffer_end,
                                   int32 preferred_size, int32 minimum_size)
{
    int saved_system = base_syszone, saved_application = base_applzone;
#if defined(CYGWIN32) && defined(SDL)
    if (!explicit_system)
        base_syszone = (mode32 ? 8 : 2) * 1024 * 1024;
    if (!explicit_application) {
        base_applzone = (mode32 ? 64 : 8) * 1024 * 1024;
        if (ROMlib_config_applzone_size <= 0 && preferred_size > 0 && minimum_size >= 0 &&
            preferred_size >= minimum_size) {
            int resource_size;
            if (!ROMlib_ecf_zone_value(MAX(preferred_size, MIN_APPLZONE_SIZE), FALSE, &resource_size)) {
                base_syszone = saved_system;
                base_applzone = saved_application;
                return -1;
            }
            base_applzone = resource_size;
        }
        if (!mode32 && framebuffer_end && ROMlib_config_applzone_size <= 0) {
            int system = ROMlib_config_syszone_size > 0 ? ROMlib_config_syszone_size
                         : explicit_system              ? ROMlib_syszone_size
                                                        : base_syszone;
            /* The current framebuffer base already includes the callback
             window, heap slop, stack and alignment. Adjust only the heap
             sizes that will differ in the replacement process. */
            int64 available =
                (int64)ROMlib_applzone_size + 0x1000000 - (int64)framebuffer_end + ROMlib_syszone_size - system;
            if (available < MAX(MIN_APPLZONE_SIZE, minimum_size)) {
                base_syszone = saved_system;
                base_applzone = saved_application;
                return -1;
            }
            if (available < base_applzone)
                base_applzone = available & ~(ZONE_ALIGN_SIZE - 1);
        }
    }
#else
    (void)framebuffer_end;
#endif
    int result = ROMlib_ecf_restart_memory(application);
    base_syszone = saved_system;
    base_applzone = saved_application;
    return result;
}

int ROMlib_ecf_require_system(const FSSpec* application, uint32 required_version)
{
    if (required_version <= system_version || required_version < 0x600 || required_version > 0x922 ||
        ((required_version >> 4) & 15) > 9 || (required_version & 15) > 9)
        return 0;
    char version[16];
    snprintf(version, sizeof version, "%u.%u.%u", (unsigned)(required_version >> 8),
             (unsigned)((required_version >> 4) & 15), (unsigned)(required_version & 15));
    fprintf(stderr, "trace: application requires System %s; restarting before event processing\n", version);
    return restart_application(application, MAX(ROMlib_syszone_size, 8 * 1024 * 1024),
                               MAX(ROMlib_applzone_size, 16 * 1024 * 1024), version);
}

int ROMlib_ecf_restart_system(const FSSpec* application, uint32 current_version)
{
    static const struct {
        uint32 number;
        const char* text;
        int system, app;
    } versions[] = {{0x860, "8.6", 8 * 1024 * 1024, 16 * 1024 * 1024},
                    {0x904, "9.0.4", 8 * 1024 * 1024, 32 * 1024 * 1024},
                    {0x922, "9.2.2", 16 * 1024 * 1024, 64 * 1024 * 1024}};

    for (size_t i = 0; i < NELEM(versions); ++i)
        if (current_version <= versions[i].number &&
            (current_version < versions[i].number || ROMlib_syszone_size < versions[i].system ||
             ROMlib_applzone_size < versions[i].app)) {
            int system = MAX(ROMlib_syszone_size, versions[i].system);
            int app = MAX(ROMlib_applzone_size, versions[i].app);
            fprintf(stderr,
                    "trace: retrying library startup with System %s, "
                    "SysZone %d, ApplZone %d\n",
                    versions[i].text, system, app);
            return restart_application(application, system, app, versions[i].text);
        }
    return 0;
}

void ROMlib_ecf_apply_startup_profile(void)
{
    /* A saved per-app SystemVersion must not reset the trial on every child
     launch. Heap sizes are explicit CLI arguments for the same reason. */
    if (retry_version)
        ROMlib_set_system_version(retry_version);
}

void ROMlib_ecf_complete_startup(boolean_t success)
{
    uint32 version = retry_version;
    retry_version = 0;
    if (!success || !version || !ROMlib_configfilename || !ROMlib_configsection[0])
        return;
    char os[32], system[32], app[32];
    snprintf(os, sizeof os, "%u.%u.%u", (unsigned)(version >> 8), (unsigned)((version >> 4) & 15),
             (unsigned)(version & 15));
    snprintf(system, sizeof system, "%d", ROMlib_syszone_size);
    snprintf(app, sizeof app, "%d", ROMlib_applzone_size);
    const char* keys[] = {"SystemVersion", "SysZone", "ApplZone"};
    const char* values[] = {os, system, app};
    if (!config_prefs_set_many(ROMlib_configfilename, ROMlib_configsection, keys, values, NELEM(keys)))
        fprintf(stderr, "Unable to save successful PowerPC startup settings\n");
}
