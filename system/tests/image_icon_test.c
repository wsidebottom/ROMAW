/*+===================================================================
  File:      image_icon_test.c

  Summary:   Test mounted-image icon lookup against intercepted resource operations.

  Classes:   None.

  Functions: main, ROMlib_finder_image_desktop; static fixtures intercept resource and volume operations.

  Origin:    Executor File Manager and native Finder regression fixture.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "finder_metadata/icons.h"
#include "finder/image_volumes.h"

typedef int16_t INTEGER;
typedef unsigned char* StringPtr;
typedef uint8_t BOOLEAN;
typedef unsigned char Str255[256];
typedef uint32_t ResType;
typedef int32_t Size;

typedef struct {
    unsigned char* bytes;
    int32_t size;
    int16_t id;
} Resource;

typedef Resource* Handle;

typedef struct {
    int valid;
} HVCB;

#define TRUE 1
#define FALSE 0
#define fsRdPerm 1
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))
#define STARH(h) ((h)->bytes)
#define CW(v) ((int16_t)__builtin_bswap16((uint16_t)(v)))
#define TICK(s) ((uint32_t)(s)[0] << 24 | (uint32_t)(s)[1] << 16 | (uint32_t)(s)[2] << 8 | (s)[3])
static INTEGER CurMap = 7, ResErr = -192;
static BOOLEAN ResLoad;
static HVCB volume;
static unsigned opens, closes, borrowed;
static int desktop_icons, packed_only;
static unsigned char packed_bytes[2296];
static Resource packed_icon = {packed_bytes, sizeof packed_bytes, -16455};
static int fail_open, bundle_count = 1, custom, missing_color;
static uint32_t expected_parent = 2;
static const char* expected_name = "Application";
static unsigned char fref_bytes[] = {'A', 'P', 'P', 'L', 0, 9};
static unsigned char bundle_bytes[] = {'T', 'E', 'S', 'T', 0, 0, 0, 0, 'I', 'C', 'N', '#', 0, 0, 0, 9, 2, 1};
static unsigned char plane_bytes[6][1024];
static Resource fref = {fref_bytes, sizeof fref_bytes, 5};
static Resource bundle = {bundle_bytes, sizeof bundle_bytes, 128};
static Resource planes[6];

static HVCB* ROMlib_findvcb(int16_t reference, void* name, void* directory, int use_default)
{
    (void)name;
    (void)directory;
    (void)use_default;
    return reference == -10 ? &volume : NULL;
}

static int ROMlib_image_volume_kind(const HVCB* value)
{
    return value == &volume;
}

static void SetResLoad(BOOLEAN load)
{
    ResLoad = load;
}

static INTEGER HOpenResFile(INTEGER reference, uint32_t parent, const unsigned char* name, int permission)
{
    if (name[0] == 7 && !memcmp(name + 1, "Desktop", 7))
        return -1;
    assert(reference == -10 && parent == expected_parent && permission == fsRdPerm && !ResLoad);
    assert(name[0] == strlen(expected_name) && !memcmp(name + 1, expected_name, name[0]));
    ++opens;
    CurMap = CW(19);
    ResErr = 0;
    return fail_open ? -1 : 19;
}

static int Count1Resources(ResType type)
{
    assert(ResLoad);
    return type == TICK("BNDL") ? bundle_count : type == TICK("FREF") || type == TICK("ICN#") ? 1 : 0;
}

static Handle borrow(Handle resource)
{
    assert(ResLoad);
    ++borrowed;
    return resource;
}

static Handle Get1IndResource(ResType type, int index)
{
    if (index != 1)
        return NULL;
    if (type == TICK("FREF"))
        return borrow(&fref);
    if (type == TICK("BNDL") && bundle_count)
        return borrow(&bundle);
    return type == TICK("ICN#") ? borrow(&planes[0]) : NULL;
}

static Handle Get1Resource(ResType type, int id)
{
    if (type == TICK("icns") && packed_only && id == -16455)
        return borrow(&packed_icon);
    if (packed_only)
        return NULL;
    if (id != (custom ? -16455 : 513))
        return NULL;
    for (unsigned i = 0; i < 6; ++i)
        if (type == finder_icon_types[i] && !(missing_color && i == 3))
            return borrow(&planes[i]);
    return NULL;
}

static int32_t GetHandleSize(Handle resource)
{
    return resource->size;
}

static void ReleaseResource(Handle resource)
{
    assert(resource && borrowed);
    --borrowed;
}

static void GetResInfo(Handle resource, INTEGER* id, ResType* type, unsigned char* name)
{
    *id = CW(resource->id);
    *type = TICK("ICN#");
    name[0] = 0;
}

static void CloseResFile(INTEGER file)
{
    assert(file == 19 && borrowed == 0);
    ++closes;
    CurMap = 0;
    ResErr = -1;
}

#include "rsys/desktop_database.h"

int ROMlib_finder_image_desktop(int16_t reference, int operation, unsigned char key[38], unsigned index, void* data,
                                size_t* size, uint32_t* tag)
{
    (void)index;
    (void)tag;
    assert(reference == -10 && operation == desktop_get && key[0] == 11 && *size == 4500);
    if (!desktop_icons)
        return -43;
    const unsigned char types[] = {1, 4, 2, 3, 5, 6};
    for (unsigned i = 0; i < 6; ++i)
        if (key[10] == types[i])
            *size = finder_icon_sizes[i];
    memset(data, 99, *size);
    return 0;
}

/* The runner extracts these unchanged functions from host/diskimages.c. */
#include "rsys/desktop_database.h"
#include "image_icon_impl.inc"

static int lookup(uint32_t type, int directory, unsigned flags, FinderIcon* icon)
{
    unsigned previous_opens = opens, previous_closes = closes;
    INTEGER previous_map = CurMap, previous_error = ResErr;
    BOOLEAN previous_load = ResLoad;
    int found =
        ROMlib_finder_image_icon(-10, expected_parent, "Application", directory, flags, type, TICK("TEST"), icon);
    assert(CurMap == previous_map && ResErr == previous_error && ResLoad == previous_load && borrowed == 0);
    assert(opens == previous_opens + !desktop_icons && closes == previous_closes + (!desktop_icons && !fail_open));
    return found;
}

int main(void)
{
    FinderIcon icon;
    for (unsigned i = 0; i < 6; ++i) {
        memset(plane_bytes[i], i + 1, sizeof plane_bytes[i]);
        planes[i] = (Resource){plane_bytes[i], finder_icon_sizes[i], 513};
    }
    assert(lookup(TICK("APPL"), 0, 0, &icon) && icon.present == 63);
    assert(icon.mono[0] == 1 && icon.color8[1023] == 4 && icon.small8[255] == 6);
    assert(!lookup(TICK("TEXT"), 0, 0, &icon) && icon.present == 0);
    desktop_icons = 1;
    assert(lookup(TICK("TEXT"), 0, 0, &icon) && icon.present == 63 && icon.mono[0] == 99);
    desktop_icons = 0;
    custom = 1;
    ResLoad = TRUE;
    assert(lookup(TICK("TEXT"), 0, 0x400, &icon) && icon.present == 63);
    expected_name = "Icon\r";
    expected_parent = 900;
    assert(lookup(0, 1, 0, &icon) && icon.present == 63);
    // A mounted root uses Icon<CR>, including packed ICNS resources.
    FinderIcon family = {0};
    unsigned char* family_planes[] = {family.mono,   family.small,  family.color4,
                                      family.color8, family.small4, family.small8};
    for (unsigned i = 0; i < 6; ++i)
        memcpy(family_planes[i], plane_bytes[i], finder_icon_sizes[i]);
    family.present = 63;
    assert(finder_icon_icns_encode(&family, packed_bytes, sizeof packed_bytes) == sizeof packed_bytes);
    expected_parent = 2;
    packed_only = 1;
    assert(ROMlib_finder_image_icon(-10, 2, "", 1, 0, 0, 0, &icon) && icon.present == 63);
    assert(icon.mono[0] == 1 && icon.color8[1023] == 4 && icon.small8[255] == 6 && !borrowed);
    packed_only = 0;
    expected_name = "Application";
    custom = 0;
    bundle_count = 0;
    assert(lookup(TICK("INIT"), 0, 0, &icon) && icon.present == 63);
    missing_color = 1;
    assert(lookup(TICK("INIT"), 0, 0, &icon) && icon.present == 55);
    fail_open = 1;
    assert(!lookup(TICK("APPL"), 0, 0, &icon) && icon.present == 0);
    assert(!ROMlib_finder_image_icon(-999, 2, "Application", 0, 0, 0, 0, &icon));
    assert(!ROMlib_finder_image_icon(-10, 2, NULL, 0, 0, 0, 0, &icon));
    puts("Mounted-image icon selection, resource lifetime and state checks passed.");
    return 0;
}
