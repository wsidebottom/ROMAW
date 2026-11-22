/*+===================================================================
  File:      windows_test.c

  Summary:   Link the real archive against a small guest-memory/Window Manager fixture.

  Classes:   None.

  Functions: C_AECreateDesc, C_AEDisposeDesc, C_CopyPixPat, C_CopyRgn, C_DiffRgn, C_DisposeRgn, C_EmptyRgn,
             C_CloseCPort, CToPascalCall, ROMlib_open_color_port, ROMlib_valid_qd_port,
             C_AECreateAppleEvent, C_AECreateList, C_AEDuplicateDesc, C_AEGetAttributeDesc,
             C_AEPutAttributeDesc, C_AEPutDesc, C_AEPutKeyDesc, config_prefs_get, config_prefs_set,
             C_BitMapToRegion, C_CopyBits, C_GetCPixel, C_MapRect, C_MapRgn, C_OffsetRect, C_PtInRgn,
             C_RectInRgn, C_SectRect, C_SetEmptyRgn, C_GetMHandle, C_LockPixels, C_UnlockPixels,
             C_InsMenuItem, C_NewRgn, C_OffsetRgn,
             C_RectRgn, C_SetPort, C_UnionRgn, GetDateTime, GetHandleSize, HandToHand, MemError, PtrToHand,
             ROMlib_carbon_native_ppc_callback, ROMlib_carbon_timezone_storage,
             ROMlib_get_local_clock, ROMlib_machine_gmt_delta, ROMlib_set_local_clock,
             C_CloseResFile, C_CurResFile, C_FSpOpenResFile, C_Get1Resource, C_SetResLoad, C_UseResFile,
             BlockMove_the_trap, DisposHandle, GetAlrtStage, HandleZone, HGetState, HLock, HSetState, ReallocHandle,
             ResetAlrtStage, SetDAFont, SetHandleSize, _NewHandle_flags,
             C_AddIconToSuite, C_DisposeIconSuite, C_GetIconFromSuite, C_IconSuiteToRgn, C_NewIconSuite,
             C_PlotIconSuite, C_PtInIconSuite, C_RectInIconSuite,
             _PurgeSpace_flags, ROMlib_component_call, ROMlib_component_can_dispatch, ROMlib_component_service, ROMlib_component_signature,
             ROMlib_carbon_canonical_cf_callbacks, ROMlib_carbon_call_romaw_service,
             ROMlib_carbon_call_register_vector,
             ROMlib_carbon_call_toolbox_vector, ROMlib_carbon_dispose_descriptor,
             ROMlib_carbon_is_cf_system_allocator_worker,
             ROMlib_carbon_invoke_mixed_upp, ROMlib_carbon_invoke_pascal, ROMlib_carbon_invoke_transition_vector,
             ROMlib_carbon_new_ppc_descriptor, ROMlib_QDGetPatternOrigin, ROMlib_QDSetPatternOrigin,
             ROMlib_change_menu_item_property, ROMlib_menu_hide_bar, ROMlib_menu_item_count, ROMlib_menu_item_enabled,
             ROMlib_menu_item_property, ROMlib_menu_show_bar, ROMlib_set_menu_item_enabled, ROMlib_valid_window,
             ROMlib_set_menu_items_enabled, ROMlib_get_menu_item_command, ROMlib_set_menu_item_command,
             ROMlib_get_menu_item_encoding, ROMlib_set_menu_item_encoding, ROMlib_menu_bar_item, ROMlib_mitemtop,
             ROMlib_control_activate, ROMlib_control_data, ROMlib_control_focus, ROMlib_control_idle,
             ROMlib_control_best_rect, ROMlib_control_region, ROMlib_ctlcall, ROMlib_get_control_features, ROMlib_get_dialog_control,
             ROMlib_abort_custom_control, ROMlib_alloc_custom_control, ROMlib_send_control_message,
             ROMlib_set_up_control_color, C_DisposeControl,
             ROMlib_live_control_aux, ROMlib_windcall, ROMlib_window_change_owners, ROMlib_window_get_metadata,
             ROMlib_window_first_all, ROMlib_window_next_all, _gui_fatal, main, syn68k_guest_to_host,
             syn68k_host_to_guest, TEFromScrap, TEToScrap, C_CharType, C_InfoScrap,
             ROMlib_type_select_clear, ROMlib_type_select_key, ROMlib_type_select_compare, ROMlib_type_select_find

  Origin:    Existing repository source in packages/carbonlib/tests; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Link the real archive against a small guest-memory/Window Manager fixture. */
#include "rsys/common.h"
#include "rsys/everything.h"
#include "rsys/wind.h"
#include "rsys/cquick.h"
#include "rsys/ctl.h"
#include "rsys/layers.h"
#include "rsys/menu.h"
#include "rsys/component.h"
#include "rsys/configprefs.h"
#include "rsys/pattern_origin.h"
#include "rsys/prefs.h"
#include "rsys/typeselect.h"
#include "carbonlib.h"
#include "encodings_test.h"
#include "bundle_state_test.h"
#include "bundle_lifecycle_test.h"
#include "urls_test.h"
#include "cfstrings_test.h"
#undef main
#undef NDEBUG
#include <assert.h>
#include "../../system/tests/guest_test_memory.h"

void ROMlib_carbon_host_tests(void);
void ROMlib_carbon_pascal_tests(void);
void ROMlib_carbon_corefoundation_tests(void);
void ROMlib_carbon_gworld_tests(void);
void ROMlib_carbon_classic_access_tests(void);
void ROMlib_carbon_values_tests(void);

uint64 ROMlib_offset;
CPUState cpu_state;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 0xe0000000, syn68k_host_alias_size = 0x10000000;
static WindowRecord application, floating;
static GrafPort offscreen;
static GrafPtr selected;
static Size state_size;
static int region_empty;
INTEGER ROMlib_qd_error;
static Region temporary_region;
static HIDDEN_RgnPtr temporary_master;
static int fail_region_allocation, disappear_on_allocation, window_present = 1;
static int temporary_allocations, temporary_disposals, validation;
static OSErr memory_error, combine_error;
static Rect damage;
static Rect* move_rectangle_on_allocation;
static int descriptor_disposals, descriptor_creations;
static OSErr descriptor_create_error;
static HIDDEN_Ptr descriptor_master;
static Byte descriptor_bytes[32];
uint32 ROMlib_carbon_test_mac_time;

OSErr TEFromScrap(void)
{
    return paramErr;
}

OSErr TEToScrap(void)
{
    return paramErr;
}

static Point test_pattern_origin;

/* The Toolbox application exercises the live Control and Window Managers. */
void ROMlib_type_select_clear(ROMlib_type_select_record* record)
{
    abort();
}

BOOLEAN ROMlib_type_select_key(EventRecord* event, ROMlib_type_select_record* record)
{
    abort();
}

INTEGER ROMlib_type_select_compare(const ROMlib_type_select_record* record, INTEGER script, StringPtr text)
{
    abort();
}

INTEGER ROMlib_type_select_find(const ROMlib_type_select_record* record, INTEGER count, INTEGER mode, ProcPtr proc,
                                void* context)
{
    abort();
}

INTEGER C_CharType(Ptr text, INTEGER offset)
{
    abort();
}

PScrapStuff C_InfoScrap(void)
{
    abort();
}

AuxCtlHandle ROMlib_live_control_aux(ControlHandle control)
{
    abort();
}

OSErr ROMlib_control_activate(ControlHandle control, boolean_t active)
{
    abort();
}

LONGINT ROMlib_control_data(ControlHandle control, INTEGER part, ResType tag, Size size, Byte* data, Size* actual,
                            enum ROMlib_control_data_op operation)
{
    abort();
}

OSErr ROMlib_control_focus(WindowPtr window, ControlHandle control, INTEGER part, HIDDEN_ControlHandle* output)
{
    abort();
}

void ROMlib_control_idle(WindowPtr window)
{
    abort();
}

OSErr ROMlib_get_control_features(ControlHandle control, uint32* features)
{
    abort();
}

int32 ROMlib_ctlcall(ControlHandle control, int16 message, intptr_t parameter)
{
    return paramErr;
}

ControlHandle ROMlib_alloc_custom_control(WindowPtr window, const Rect* bounds, uint32 definition)
{
    abort();
}

void ROMlib_abort_custom_control(ControlHandle control)
{
    abort();
}

void C_DisposeControl(ControlHandle control)
{
    abort();
}

int32 ROMlib_send_control_message(ControlHandle control, int16 message, uint32 parameter)
{
    abort();
}

int32 ROMlib_set_up_control_color(ControlHandle control, int16 depth, Byte color_device, boolean_t text)
{
    abort();
}

OSErr ROMlib_control_best_rect(ControlHandle control, Rect* bounds, INTEGER* baseline)
{
    abort();
}

OSErr ROMlib_control_region(ControlHandle control, INTEGER part, RgnHandle region)
{
    abort();
}

OSErr ROMlib_get_dialog_control(WindowPtr window, INTEGER item, HIDDEN_ControlHandle* control)
{
    abort();
}

LONGINT ROMlib_windcall(WindowPtr window, INTEGER message, intptr_t parameter)
{
    abort();
}

/* Live menus are exercised by the existing PPC Toolbox application. */
void ROMlib_menu_hide_bar(void)
{
    abort();
}

void ROMlib_menu_show_bar(void)
{
    abort();
}

int ROMlib_menu_item_count(MenuHandle menu)
{
    abort();
}

boolean_t ROMlib_menu_item_enabled(MenuHandle menu, INTEGER item)
{
    abort();
}

boolean_t ROMlib_set_menu_item_enabled(MenuHandle menu, INTEGER item, boolean_t enabled)
{
    abort();
}

void ROMlib_set_menu_items_enabled(MenuHandle menu, int count, boolean_t enabled)
{
    abort();
}

OSErr C_BitMapToRegion(RgnHandle region, const BitMap* bitmap)
{
    abort();
}

void C_CopyBits(BitMap* source, BitMap* destination, const Rect* source_rect, const Rect* destination_rect,
                INTEGER mode, RgnHandle mask)
{
    abort();
}

void C_GetCPixel(INTEGER h, INTEGER v, RGBColor* color)
{
    abort();
}

void C_MapRect(Rect* rect, Rect* source, Rect* destination)
{
    abort();
}

void C_MapRgn(RgnHandle region, Rect* source, Rect* destination)
{
    abort();
}

void C_OffsetRect(Rect* rect, INTEGER dh, INTEGER dv)
{
    abort();
}

BOOLEAN C_PtInRgn(Point point, RgnHandle region)
{
    abort();
}

BOOLEAN C_RectInRgn(Rect* rect, RgnHandle region)
{
    abort();
}

BOOLEAN C_SectRect(const Rect* first, const Rect* second, Rect* destination)
{
    abort();
}

void C_SetEmptyRgn(RgnHandle region)
{
    abort();
}

MenuHandle C_GetMHandle(INTEGER id)
{
    abort();
}

void C_InsMenuItem(MenuHandle menu, StringPtr text, INTEGER after)
{
    abort();
}

mextp ROMlib_mitemtop(MenuHandle menu, INTEGER item, StringPtr* text)
{
    abort();
}

MenuHandle ROMlib_menu_bar_item(unsigned index)
{
    abort();
}

LONGINT ROMlib_get_menu_item_command(MenuHandle menu, INTEGER item, ULONGINT* command)
{
    abort();
}

OSErr ROMlib_set_menu_item_command(MenuHandle menu, INTEGER item, ULONGINT command)
{
    abort();
}

OSErr ROMlib_get_menu_item_encoding(MenuHandle menu, INTEGER item, ULONGINT* encoding)
{
    abort();
}

OSErr ROMlib_set_menu_item_encoding(MenuHandle menu, INTEGER item, ULONGINT encoding)
{
    abort();
}

int ROMlib_menu_item_property(MenuHandle menu, INTEGER item, uint32_t creator, uint32_t tag, uint32_t* size,
                              uint32_t* attributes, const void** data)
{
    abort();
}

int ROMlib_change_menu_item_property(MenuHandle menu, INTEGER item, uint32_t creator, uint32_t tag,
                                     enum ROMlib_menu_property_change operation, uint32_t first, uint32_t second,
                                     const void* data)
{
    abort();
}

void ROMlib_QDGetPatternOrigin(Point* origin)
{
    ROMlib_qd_error = origin ? noErr : paramErr;
    if (origin)
        *origin = test_pattern_origin;
}

void ROMlib_QDSetPatternOrigin(Point origin)
{
    ROMlib_qd_error = noErr;
    test_pattern_origin = (Point){CW(origin.v), CW(origin.h)};
}

void GetDateTime(LONGINT* seconds)
{
    *seconds = CL(ROMlib_carbon_test_mac_time);
}

uint64 ROMlib_get_local_clock(void)
{
    return (uint64)ROMlib_carbon_test_mac_time << 16;
}

OSErr ROMlib_set_local_clock(uint64 value)
{
    ROMlib_carbon_test_mac_time = value >> 16;
    return noErr;
}

int32 ROMlib_machine_gmt_delta(void)
{
    return 0;
}

void* ROMlib_carbon_timezone_storage(uint32 size, uint32* address)
{
    return NULL;
}

int ROMlib_carbon_native_ppc_callback(const char* name, int (*function)(ROMlib_carbon_ppc_frame*), uint32* vector)
{
    return 0;
}

OSErr C_AEDisposeDesc(AEDesc* descriptor)
{
    assert(descriptor);
    ++descriptor_disposals;
    return noErr;
}

OSErr C_AECreateDesc(DescType type, Ptr data, Size size, AEDesc* descriptor)
{
    ++descriptor_creations;
    assert(DESC_TYPE(descriptor) == typeNull && !DESC_DATA(descriptor));
    if (descriptor_create_error)
        return descriptor_create_error;
    assert(size >= 0 && size <= sizeof descriptor_bytes);
    if (data)
        memmove(descriptor_bytes, data, size);
    else
        memset(descriptor_bytes, 0, size);
    HIDDEN_RAW_SET(descriptor_master, RM(&descriptor_bytes[0]));
    FIELD_ASSIGN_RAW(DESC_DATA_X(descriptor), RM(&descriptor_master));
    DESC_TYPE_X(descriptor) = CL(type);
    state_size = size;
    return noErr;
}

OSErr C_AECreateAppleEvent(AEEventClass event_class, AEEventID event_id, AEAddressDesc* target, int16 return_id,
                           int32 transaction_id, AppleEvent* output)
{
    abort();
}

OSErr C_AECreateList(Ptr prefix, Size prefix_size, Boolean is_record, AEDescList* output)
{
    abort();
}

OSErr C_AEDuplicateDesc(AEDesc* source, AEDesc* output)
{
    abort();
}

OSErr C_AEGetAttributeDesc(AppleEvent* event, AEKeyword key, DescType type, AEDesc* output)
{
    abort();
}

OSErr C_AEPutAttributeDesc(AppleEvent* event, AEKeyword key, AEDesc* descriptor)
{
    abort();
}

OSErr C_AEPutDesc(AERecord* list, int32 index, AEDesc* descriptor)
{
    abort();
}

OSErr C_AEPutKeyDesc(AERecord* record, AEKeyword key, AEDesc* descriptor)
{
    abort();
}

SignedByte HGetState(Handle handle)
{
    abort();
}

void HLock(Handle handle)
{
    abort();
}

void HSetState(Handle handle, SignedByte flags)
{
    abort();
}

void ReallocHandle(Handle handle, Size size)
{
    abort();
}

THz HandleZone(Handle handle)
{
    abort();
}

void BlockMove_the_trap(Ptr source, Ptr destination, Size length, boolean_t flush_code)
{
    abort();
}

INTEGER GetAlrtStage(void)
{
    abort();
}

void ResetAlrtStage(void)
{
    abort();
}

void SetDAFont(INTEGER font)
{
    abort();
}

BOOLEAN ROMlib_valid_window(WindowPtr window)
{
    return window_present && (window == (WindowPtr)&application || window == (WindowPtr)&floating);
}

OSErr ROMlib_window_change_owners(WindowPtr window, int change, uint32* owners)
{
    abort();
}

ROMlib_window_metadata* ROMlib_window_get_metadata(WindowPtr window)
{
    // Raw record fixtures have no live Window Manager auxiliary metadata.
    if (!ROMlib_valid_window(window))
        return NULL;
    abort();
}

char* ROMlib_configfilename;

size_t config_prefs_get(const char* path, const char* section, const char* key, char* value, size_t capacity)
{
    abort();
}

int config_prefs_set(const char* path, const char* section, const char* key, const char* value)
{
    abort();
}

RgnHandle C_NewRgn(void)
{
    if (fail_region_allocation) {
        memory_error = memFullErr;
        return NULL;
    }
    memory_error = noErr;
    ++temporary_allocations;
    if (move_rectangle_on_allocation)
        memset(move_rectangle_on_allocation, 0x55, sizeof(Rect));
    if (disappear_on_allocation)
        window_present = 0;
    memset(&temporary_region, 0, sizeof temporary_region);
    HIDDEN_RAW_SET(temporary_master, RM(&temporary_region));
    return &temporary_master;
}

void C_DisposeRgn(RgnHandle region)
{
    assert(region == &temporary_master);
    ++temporary_disposals;
}

OSErrRET HandToHand(Handle* handle)
{
    RgnHandle copy = C_NewRgn();
    if (!copy)
        return memFullErr;
    *STARH(copy) = *STARH((RgnHandle)*handle);
    *handle = (Handle)copy;
    return noErr;
}

OSErr MemError(void)
{
    return memory_error;
}

INTEGER C_CurResFile(void)
{
    abort();
}

INTEGER C_FSpOpenResFile(FSSpecPtr spec, SignedByte permissions)
{
    abort();
}

void C_CloseResFile(INTEGER file)
{
    abort();
}

Handle C_Get1Resource(ResType type, INTEGER id)
{
    abort();
}

void C_SetResLoad(BOOLEAN load)
{
    abort();
}

void C_UseResFile(INTEGER file)
{
    abort();
}

Handle _NewHandle_flags(Size size, boolean_t system_heap, boolean_t clear)
{
    abort();
}

void DisposHandle(Handle handle)
{
    abort();
}

void SetHandleSize(Handle handle, Size size)
{
    abort();
}

OSErr C_NewIconSuite(Handle* suite)
{
    abort();
}

OSErr C_AddIconToSuite(Handle icon, Handle suite, ResType type)
{
    abort();
}

OSErr C_DisposeIconSuite(Handle suite, Boolean dispose_data)
{
    abort();
}

OSErr C_GetIconFromSuite(Handle* icon, Handle suite, ResType type)
{
    abort();
}

OSErr C_PlotIconSuite(const Rect* rect, IconAlignmentType align, IconTransformType transform, Handle suite)
{
    abort();
}

OSErr C_IconSuiteToRgn(RgnHandle region, const Rect* rect, IconAlignmentType align, Handle suite)
{
    abort();
}

Boolean C_PtInIconSuite(Point point, const Rect* rect, IconAlignmentType align, Handle suite)
{
    abort();
}

Boolean C_RectInIconSuite(const Rect* test, const Rect* rect, IconAlignmentType align, Handle suite)
{
    abort();
}

void C_RectRgn(RgnHandle region, Rect* rect)
{
    STARH(region)->rgnBBox = *rect;
}

void C_OffsetRgn(RgnHandle region, INTEGER h, INTEGER v)
{
    Rect* r = &STARH(region)->rgnBBox;
    r->top = CW(CW(r->top) + v);
    r->bottom = CW(CW(r->bottom) + v);
    r->left = CW(CW(r->left) + h);
    r->right = CW(CW(r->right) + h);
}

void C_UnionRgn(RgnHandle source, RgnHandle update, RgnHandle output)
{
    assert(update == output);
    damage = STARH(source)->rgnBBox;
    validation = 0;
    memory_error = combine_error;
    if (!memory_error)
        *STARH(output) = *STARH(source);
}

void C_DiffRgn(RgnHandle update, RgnHandle source, RgnHandle output)
{
    assert(update == output);
    damage = STARH(source)->rgnBBox;
    validation = 1;
    memory_error = combine_error;
    if (!memory_error)
        memset(STARH(output), 0, sizeof(Region));
}

void _gui_fatal(const char* file, int line, const char* fn, const char* fmt, ...)
{
    abort();
}

uint32 ROMlib_resource_map_generation = 1;

/* The consolidated PPC Toolbox app exercises these calls with the real manager. */
toolstuff_t toolstuff[NTOOLENTRIES];
void* tooltraptable[NTOOLENTRIES];

intptr_t CToPascalCall(void* routine, unsigned long flags, ...)
{
    (void)routine;
    (void)flags;
    abort();
}

Boolean ROMlib_open_color_port(CGrafPtr port)
{
    (void)port;
    abort();
}

Boolean ROMlib_valid_qd_port(GrafPtr port)
{
    (void)port;
    abort();
}

void C_CloseCPort(CGrafPtr port)
{
    (void)port;
    abort();
}

void C_CopyPixPat(PixPatHandle source, PixPatHandle output)
{
    (void)source;
    (void)output;
    abort();
}

Boolean C_LockPixels(PixMapHandle pixels)
{
    (void)pixels;
    abort();
}

void C_UnlockPixels(PixMapHandle pixels)
{
    (void)pixels;
    abort();
}

void C_CopyRgn(RgnHandle source, RgnHandle output)
{
    *STARH(output) = *STARH(source);
}

void* syn68k_guest_to_host(uint32 address)
{
    return fixture_guest_to_host(address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint pointer)
{
    return fixture_host_to_guest(pointer);
}

WindowPeek ROMlib_window_first_all(void)
{
    return &floating;
}

WindowPeek ROMlib_window_next_all(WindowPeek window)
{
    return window == &floating ? &application : NULL;
}

void C_SetPort(GrafPtr port)
{
    selected = port;
}

Size GetHandleSize(Handle handle)
{
    assert(handle);
    return state_size;
}

BOOLEAN C_EmptyRgn(RgnHandle region)
{
    assert(region);
    return region_empty;
}

static void* function(const char* name)
{
    const ROMlib_carbon_export* entry = ROMlib_carbon_find_export(name, 2);
    assert(entry && entry->reference_offset);
    assert(ROMlib_carbon_find_export(name, 0) == entry);
    assert(!ROMlib_carbon_find_export(name, 1));
    assert(!ROMlib_carbon_find_export(name, 3));
    return entry->function;
}

static void quickdraw_accessors(void)
{
    Rect sentinel = {CWC(-31), CWC(17), CWC(203), CWC(401)};

    struct {
        uint32 before;
        Rect bounds;
        uint32 after;
    } guarded = {0x12345678, {0}, 0x87654321};

    GrafPtr mono = (GrafPtr)&application, color = (GrafPtr)&floating;
    Rect* (*bounds)(GrafPtr, Rect*) = function("GetPortBounds");
    PixMapHandle (*pixmap)(GrafPtr) = function("GetPortPixMap");
    INTEGER (*font)(GrafPtr) = function("GetPortTextFont");
    INTEGER (*size)(GrafPtr) = function("GetPortTextSize");
    LONGINT (*rowbytes)(PixMapHandle) = function("GetPixRowBytes");
    GrafPtr (*current)(void) = function("GetQDGlobalsThePort");
    void (*error)(INTEGER) = function("SetQDError");
    PixMap pixels = {0};
    HIDDEN_PixMapPtr pixel_master;
    HIDDEN_RAW_SET(pixel_master, RM(&pixels));
    HIDDEN_RAW_SET(((CGrafPtr)color)->portPixMap, RM(&pixel_master));
    PORT_RECT(mono) = sentinel;
    PORT_TX_FONT_X(mono) = CWC(-13);
    PORT_TX_SIZE_X(mono) = CWC(19);
    PORT_TX_FONT_X(color) = CWC(23);
    PORT_TX_SIZE_X(color) = CWC(-2);
    SET_THEPORT_RAW(RM(mono));
    error(-177);
    assert(current() == mono);
    assert(bounds(NULL, &guarded.bounds) == &guarded.bounds);
    assert(!memcmp(&guarded.bounds, &sentinel, sizeof sentinel));
    assert(guarded.before == 0x12345678 && guarded.after == 0x87654321);
    assert(font(NULL) == -13 && size(NULL) == 19);
    assert(font(color) == 23 && size(color) == -2);
    assert(ROMlib_qd_error == -177); /* Successful getters retain the error. */
    assert(!pixmap(mono) && ROMlib_qd_error == paramErr);
    error(-178);
    assert(pixmap(color) == &pixel_master && ROMlib_qd_error == -178);
    SET_THEPORT_RAW(RM(color));
    assert(current() == color && pixmap(NULL) == &pixel_master);
    assert(bounds(NULL, &guarded.bounds) == &guarded.bounds);
    assert(!memcmp(&guarded.bounds, &PORT_RECT(color), sizeof(Rect)));
    for (unsigned i = 0; i < 65536; ++i) {
        pixels.rowBytes = CW(i);
        assert(rowbytes(&pixel_master) == (i & 0x7fff));
    }
    assert(!rowbytes(NULL) && ROMlib_qd_error == -178);
    HIDDEN_RAW_SET(pixel_master, 0);
    assert(!rowbytes(&pixel_master) && ROMlib_qd_error == paramErr);

    Region visible = {0}, clip = {0}, output = {0};
    HIDDEN_RgnPtr visible_master, clip_master, output_master;
    HIDDEN_RAW_SET(visible_master, RM(&visible));
    HIDDEN_RAW_SET(clip_master, RM(&clip));
    HIDDEN_RAW_SET(output_master, RM(&output));
    visible.rgnBBox = sentinel;
    clip.rgnBBox = PORT_RECT(color);
    HIDDEN_RAW_SET(PORT_VIS_REGION_X(color), RM(&visible_master));
    HIDDEN_RAW_SET(PORT_CLIP_REGION_X(color), RM(&clip_master));
    RgnHandle (*get_visible)(GrafPtr, RgnHandle) = function("GetPortVisibleRegion");
    RgnHandle (*get_clip)(GrafPtr, RgnHandle) = function("GetPortClipRegion");
    error(-179);
    assert(get_visible(NULL, &output_master) == &output_master);
    assert(!memcmp(&output, &visible, sizeof output));
    assert(get_clip(color, &output_master) == &output_master);
    assert(!memcmp(&output, &clip, sizeof output));
    assert(get_clip(color, &clip_master) == &clip_master);
    assert(ROMlib_qd_error == -179);
    assert(!get_visible(color, NULL) && ROMlib_qd_error == paramErr);
    HIDDEN_RAW_SET(PORT_VIS_REGION_X(color), 0);
    assert(get_visible(color, &output_master) == &output_master);
    assert(!memcmp(&output, &clip, sizeof output));

    SET_THEPORT_RAW(0);
    guarded.bounds = sentinel;
    error(0);
    assert(!current() && !font(NULL) && ROMlib_qd_error == paramErr);
    error(0);
    assert(!size(NULL) && ROMlib_qd_error == paramErr);
    error(0);
    assert(!pixmap(NULL) && ROMlib_qd_error == paramErr);
    error(0);
    assert(bounds(NULL, &guarded.bounds) == &guarded.bounds && ROMlib_qd_error == paramErr);
    assert(!memcmp(&guarded.bounds, &sentinel, sizeof sentinel));
    error(0);
    assert(!bounds(color, NULL) && ROMlib_qd_error == paramErr);
    error(0);
    get_clip(NULL, &output_master);
    assert(ROMlib_qd_error == paramErr && !memcmp(&output, &clip, sizeof output));
}

static void quickdraw_patterns(void)
{
    const char* names[] = {"GetQDGlobalsDarkGray", "GetQDGlobalsLightGray", "GetQDGlobalsGray", "GetQDGlobalsBlack",
                           "GetQDGlobalsWhite"};
    uint32 saved_a5 = EM_A5;

    struct {
        Pattern patterns[5];
        HIDDEN_GrafPtr port;
    } worlds[2] = {0};

    HIDDEN_HIDDEN_GrafPtr_Ptr pointers[2];
    for (unsigned world = 0; world < 2; ++world) {
        HIDDEN_RAW_SET(pointers[world], RM(&worlds[world].port));
        EM_A5 = US_TO_SYN68K(&pointers[world]);
        for (unsigned i = 0; i < 5; ++i)
            for (unsigned j = 0; j < 8; ++j)
                worlds[world].patterns[i][j] = world * 71 + i * 13 + j;
        for (unsigned i = 0; i < 5; ++i) {
            struct {
                uint32 before;
                Pattern pattern;
                uint32 after;
            } output = {0x12345678, {0}, 0x87654321};

            Pattern* (*get)(Pattern*) = function(names[i]);
            ROMlib_qd_error = -177;
            assert(get(&output.pattern) == &output.pattern);
            assert(!memcmp(output.pattern, worlds[world].patterns[i], sizeof(Pattern)));
            assert(output.before == 0x12345678 && output.after == 0x87654321);
            assert(ROMlib_qd_error == -177);
            output.pattern[0] ^= 0xff;
            assert(output.pattern[0] != worlds[world].patterns[i][0]);
            assert(get(&worlds[world].patterns[i]) == &worlds[world].patterns[i]);
            assert(!get(NULL) && ROMlib_qd_error == paramErr);
        }
    }
    EM_A5 = saved_a5;
}

static void dialog_accessors(void)
{
    DialogRecord dialog = {0};
    TERec text = {0};
    HIDDEN_TEPtr text_master;
    HIDDEN_RAW_SET(text_master, RM(&text));
    HIDDEN_RAW_SET(dialog.textH, RM(&text_master));
    dialog.aDefItem = CWC(-13);
    WindowPtr (*window)(DialogPtr) = function("GetDialogWindow");
    DialogPtr (*from_window)(WindowPtr) = function("GetDialogFromWindow");
    TEHandle (*edit)(DialogPtr) = function("GetDialogTextEditHandle");
    INTEGER (*default_item)(DialogPtr) = function("GetDialogDefaultItem");
    void (*set_port)(DialogPtr) = function("SetPortDialogPort");
    assert(window((DialogPtr)&dialog) == (WindowPtr)&dialog);
    assert(from_window((WindowPtr)&dialog) == (DialogPtr)&dialog);
    assert(!window(NULL) && !from_window(NULL));
    assert(edit((DialogPtr)&dialog) == &text_master);
    assert(!edit(NULL));
    HIDDEN_RAW_SET(dialog.textH, 0);
    assert(!edit((DialogPtr)&dialog));
    assert(default_item((DialogPtr)&dialog) == -13 && !default_item(NULL));
    dialog.aDefItem = CWC(7);
    assert(default_item((DialogPtr)&dialog) == 7);
    set_port((DialogPtr)&dialog);
    assert(selected == (GrafPtr)&dialog);
    set_port(NULL);
    assert(selected == (GrafPtr)&dialog);

    ControlRecord first = {0}, moved = {0};
    HIDDEN_ControlPtr control;
    HIDDEN_RAW_SET(control, RM(&first));
    first.contrlRect = (Rect){CWC(-11), CWC(7), CWC(31), CWC(203)};
    moved.contrlRect = (Rect){CWC(5), CWC(-14), CWC(301), CWC(41)};

    struct {
        uint32 before;
        Rect bounds;
        uint32 after;
    } guarded = {0x12345678, {0}, 0x87654321};

    Rect* (*bounds)(ControlHandle, Rect*) = function("GetControlBounds");
    ROMlib_qd_error = -177;
    assert(bounds(&control, &guarded.bounds) == &guarded.bounds);
    assert(!memcmp(&guarded.bounds, &first.contrlRect, sizeof(Rect)));
    HIDDEN_RAW_SET(control, RM(&moved));
    bounds(&control, &guarded.bounds);
    assert(!memcmp(&guarded.bounds, &moved.contrlRect, sizeof(Rect)));
    assert(bounds(NULL, &guarded.bounds) == &guarded.bounds);
    HIDDEN_RAW_SET(control, 0);
    assert(bounds(&control, &guarded.bounds) == &guarded.bounds);
    assert(!memcmp(&guarded.bounds, &moved.contrlRect, sizeof(Rect)));
    assert(!bounds(&control, NULL));
    assert(ROMlib_qd_error == -177);
    assert(guarded.before == 0x12345678 && guarded.after == 0x87654321);
}

static void window_invalidation(void)
{
    Region update = {0}, source = {0};
    HIDDEN_RgnPtr update_master, source_master;
    HIDDEN_RAW_SET(update_master, RM(&update));
    HIDDEN_RAW_SET(source_master, RM(&source));
    memset(&application, 0, sizeof application);
    HIDDEN_RAW_SET(application.updateRgn, RM(&update_master));
    application.port.portBits.bounds = (Rect){CWC(-70), CWC(-40), CWC(410), CWC(600)};
    WindowPtr window = (WindowPtr)&application;
    Rect rectangle = {CWC(4), CWC(5), CWC(14), CWC(25)};
    Rect saved = rectangle;
    Rect expected = {CWC(74), CWC(45), CWC(84), CWC(65)};
    LONGINT (*invalidate)(WindowPtr, const Rect*) = function("InvalWindowRect");
    LONGINT (*validate)(WindowPtr, const Rect*) = function("ValidWindowRect");
    LONGINT (*invalidate_region)(WindowPtr, RgnHandle) = function("InvalWindowRgn");
    LONGINT (*validate_region)(WindowPtr, RgnHandle) = function("ValidWindowRgn");
    SET_THEPORT_RAW(RM(&offscreen));
    assert(invalidate(window, &rectangle) == noErr && !validation);
    assert(!memcmp(&damage, &expected, sizeof damage));
    assert(!memcmp(&rectangle, &saved, sizeof saved));
    assert(thePort == &offscreen);
    assert(validate(window, &rectangle) == noErr && validation);
    source.rgnBBox = rectangle;
    Region original = source;
    assert(invalidate_region(window, &source_master) == noErr && !validation);
    assert(!memcmp(&damage, &expected, sizeof damage));
    assert(validate_region(window, &source_master) == noErr && validation);
    assert(!memcmp(&source, &original, sizeof source));
    assert(thePort == &offscreen);
    assert(invalidate(NULL, &rectangle) == -5600);
    assert(validate((WindowPtr)(uintptr_t)0x1234, &rectangle) == -5600);
    assert(invalidate(window, NULL) == paramErr && validate(window, NULL) == paramErr);
    assert(invalidate_region(window, NULL) == paramErr);
    HIDDEN_RgnPtr empty = {0};
    assert(validate_region(window, &empty) == paramErr);
    int allocations = temporary_allocations, disposals = temporary_disposals;
    fail_region_allocation = 1;
    assert(invalidate(window, &rectangle) == memFullErr);
    assert(validate_region(window, &source_master) == memFullErr);
    assert(temporary_allocations == allocations && temporary_disposals == disposals);
    assert(!memcmp(&source, &original, sizeof source));
    fail_region_allocation = 0;
    move_rectangle_on_allocation = &rectangle;
    assert(invalidate(window, &rectangle) == noErr);
    assert(!memcmp(&damage, &expected, sizeof damage));
    move_rectangle_on_allocation = NULL;
    rectangle = saved;
    disappear_on_allocation = 1;
    assert(invalidate(window, &rectangle) == -5600);
    disappear_on_allocation = 0;
    window_present = 1;
    combine_error = memFullErr;
    assert(validate(window, &rectangle) == memFullErr);
    combine_error = 0;
    assert(temporary_allocations == temporary_disposals);

    PixMap pixels = {0};
    HIDDEN_PixMapPtr pixel_master;
    HIDDEN_RAW_SET(pixel_master, RM(&pixels));
    memset(&floating, 0, sizeof floating);
    ((CGrafPtr)&floating)->portVersion = CWC(0xc000);
    HIDDEN_RAW_SET(((CGrafPtr)&floating)->portPixMap, RM(&pixel_master));
    HIDDEN_RAW_SET(floating.updateRgn, RM(&update_master));
    pixels.bounds = (Rect){CWC(-100), CWC(-80), CWC(380), CWC(560)};
    expected = (Rect){CWC(104), CWC(85), CWC(114), CWC(105)};
    assert(invalidate((WindowPtr)&floating, &rectangle) == noErr);
    assert(!memcmp(&damage, &expected, sizeof damage));
    assert(temporary_allocations == temporary_disposals && thePort == &offscreen);
}

static void string_conversions(void)
{
    void (*to_c)(const Byte*, char*) = function("CopyPascalStringToC");
    void (*to_pascal)(const char*, Byte*) = function("CopyCStringToPascal");
    void (*c2p)(Byte*, const char*) = function("c2pstrcpy");
    void (*p2c)(char*, const Byte*) = function("p2cstrcpy");
    Byte pascal_bytes[256], text[1024], expected[256];

    struct {
        uint32 before;
        Byte bytes[256];
        uint32 after;
    } guarded;

    for (unsigned length = 0; length <= 255; ++length) {
        for (unsigned i = 0; i < length; ++i)
            pascal_bytes[i + 1] = (Byte)(i * 71 + 3); /* Includes embedded NUL bytes. */
        pascal_bytes[0] = length;
        guarded.before = 0x12345678;
        guarded.after = 0x87654321;
        memset(guarded.bytes, 0xa5, sizeof guarded.bytes);
        to_c(pascal_bytes, (char*)guarded.bytes);
        assert(!memcmp(guarded.bytes, pascal_bytes + 1, length));
        assert(!guarded.bytes[length]);
        if (length < 255)
            assert(guarded.bytes[length + 1] == 0xa5);
        assert(guarded.before == 0x12345678 && guarded.after == 0x87654321);
        memcpy(expected, guarded.bytes, sizeof expected);
        memcpy(guarded.bytes, pascal_bytes, sizeof pascal_bytes);
        to_c(guarded.bytes, (char*)guarded.bytes);
        assert(!memcmp(guarded.bytes, expected, length + 1));
        memset(guarded.bytes, 0xa5, sizeof guarded.bytes);
        p2c((char*)guarded.bytes, pascal_bytes);
        assert(!memcmp(guarded.bytes, expected, length + 1));
    }
    for (unsigned length = 0; length < sizeof text; ++length) {
        memset(text, 0x9f, sizeof text);
        text[length] = 0;
        unsigned copied = length < 255 ? length : 255;
        memset(guarded.bytes, 0xa5, sizeof guarded.bytes);
        to_pascal((char*)text, guarded.bytes);
        assert(guarded.bytes[0] == copied);
        assert(!memcmp(guarded.bytes + 1, text, copied));
        if (copied < 255)
            assert(guarded.bytes[copied + 1] == 0xa5);
        assert(guarded.before == 0x12345678 && guarded.after == 0x87654321);
        memcpy(expected, guarded.bytes, sizeof expected);
        memcpy(guarded.bytes, text, sizeof guarded.bytes);
        c2p(guarded.bytes, (char*)guarded.bytes);
        assert(!memcmp(guarded.bytes, expected, copied + 1));
    }
    Byte overlap[300];
    memcpy(overlap + 10, "Overlap", 8);
    to_pascal((char*)overlap + 10, overlap + 12);
    assert(overlap[12] == 7 && !memcmp(overlap + 13, "Overlap", 7));
    to_c(overlap + 12, (char*)overlap + 14);
    assert(!memcmp(overlap + 14, "Overlap\0", 8));
    to_c(NULL, (char*)guarded.bytes);
    assert(!guarded.bytes[0]);
    to_pascal(NULL, guarded.bytes);
    assert(!guarded.bytes[0]);
    to_c(pascal_bytes, NULL);
    to_pascal((char*)text, NULL);
}

static void atomic_helpers(void)
{
    LONGINT (*increment)(volatile LONGINT*) = function("IncrementAtomic");
    LONGINT (*decrement)(volatile LONGINT*) = function("DecrementAtomic");
    BOOLEAN (*set)(uint32, volatile Byte*) = function("TestAndSet");
    BOOLEAN (*clear)(uint32, volatile Byte*) = function("TestAndClear");
    Byte memory[16];
    const uint32 values[] = {0, 1, UINT32_MAX, 0x7fffffff, 0x80000000, 0x12345678};
    for (unsigned offset = 0; offset < 4; ++offset)
        for (unsigned i = 0; i < sizeof values / sizeof values[0]; ++i) {
            memset(memory, 0xa5, sizeof memory);
            uint32 encoded = CL(values[i]);
            memcpy(memory + 4 + offset, &encoded, sizeof encoded);
            volatile LONGINT* word = (volatile LONGINT*)(memory + 4 + offset);
            assert((uint32)increment(word) == values[i]);
            memcpy(&encoded, memory + 4 + offset, sizeof encoded);
            assert(CL(encoded) == values[i] + 1);
            assert((uint32)decrement(word) == values[i] + 1);
            assert((uint32)decrement(word) == values[i]);
            memcpy(&encoded, memory + 4 + offset, sizeof encoded);
            assert(CL(encoded) == values[i] - 1);
            for (unsigned j = 0; j < sizeof memory; ++j)
                if (j < 4 + offset || j >= 8 + offset)
                    assert(memory[j] == 0xa5);
        }
    for (unsigned initial = 0; initial < 256; ++initial)
        for (unsigned bit = 0; bit < 256; ++bit) {
            memset(memory, 0xa5, sizeof memory);
            Byte mask = 0x80u >> (bit & 7);
            memory[5] = initial;
            assert(set(bit, memory + 5) == ((initial & mask) != 0));
            assert(memory[5] == (initial | mask));
            assert(set(bit, memory + 5) == 1);
            assert(clear(bit, memory + 5) == 1);
            assert(memory[5] == (initial & ~mask));
            assert(clear(bit, memory + 5) == 0);
            assert(memory[4] == 0xa5 && memory[6] == 0xa5);
        }
    assert(!increment(NULL) && !decrement(NULL));
    assert(!set(0, NULL) && !clear(0, NULL));
}

static void quickdraw_record_accessors(void)
{
    GrafPort mono = {0};
    CGrafPort color = {0};
    color.portVersion = CWC(0xc000);
    const char* scalars[] = {"GetPortTextMode", "GetPortPenMode", "GetPortPenVisibility"};
    for (unsigned i = 0; i < 2; ++i) {
        GrafPtr port = i ? (GrafPtr)&color : &mono;
        PORT_TX_FACE_X(port) = -1;
        PORT_TX_MODE_X(port) = CWC(-4);
        PORT_PEN_MODE_X(port) = CWC(37);
        PORT_PEN_VIS_X(port) = CWC(-7);
        PORT_SP_EXTRA_X(port) = CLC(0x81234567);
        PORT_PEN_SIZE(port) = (Point){CWC(3), CWC(5)};
        PORT_PEN_LOC(port) = (Point){CWC(-13), CWC(27)};
        Byte procedures[64];
        HIDDEN_RAW_SET(PORT_GRAF_PROCS_X(port), RM(&procedures[0]));
        SET_THEPORT_RAW(RM(port));
        ROMlib_qd_error = -177;
        LONGINT (*face)(GrafPtr) = function("GetPortTextFace");
        Fixed (*extra)(GrafPtr) = function("GetPortSpExtra");
        void* (*procs)(GrafPtr) = function("GetPortGrafProcs");
        const BitMap* (*bitmap)(GrafPtr) = function("GetPortBitMapForCopyBits");
        assert(face(NULL) == 255 && face(port) == 255);
        assert((uint32)extra(NULL) == 0x81234567);
        assert(procs(NULL) == procedures);
        assert(bitmap(NULL) == (const BitMap*)((Byte*)port + 2));
        const INTEGER expected[] = {-4, 37, -7};
        for (unsigned j = 0; j < 3; ++j) {
            INTEGER (*get)(GrafPtr) = function(scalars[j]);
            assert(get(NULL) == expected[j] && get(port) == expected[j]);
        }
        const char* points[] = {"GetPortPenSize", "GetPortPenLocation"};
        for (unsigned j = 0; j < 2; ++j) {
            Point* (*get)(GrafPtr, Point*) = function(points[j]);

            struct {
                uint32 before;
                Point point;
                uint32 after;
            } output = {0x12345678, {0}, 0x87654321};

            assert(get(NULL, &output.point) == &output.point);
            Point wanted = j ? PORT_PEN_LOC(port) : PORT_PEN_SIZE(port);
            assert(!memcmp(&output.point, &wanted, sizeof wanted));
            assert(output.before == 0x12345678 && output.after == 0x87654321);
        }
        assert(ROMlib_qd_error == -177);
    }
    color.chExtra = CWC(-17);
    color.pnLocHFrac = CWC(-1234);
    INTEGER (*ch)(GrafPtr) = function("GetPortChExtra");
    INTEGER (*fraction)(GrafPtr) = function("GetPortFracHPenLocation");
    assert(ch((GrafPtr)&color) == -17 && fraction((GrafPtr)&color) == -1234);
    assert(ch(&mono) == 0 && ROMlib_qd_error == paramErr);
    assert(fraction(&mono) == INT16_MIN);

    GrafVars variables = {0};
    HIDDEN_GrafVarsPtr variable_master;
    HIDDEN_RAW_SET(variable_master, RM(&variables));
    HIDDEN_RAW_SET(color.grafVars, RM(&variable_master));
    color.rgbFgColor = (RGBColor){CWC(0x1234), CWC(0x5678), CWC(0xabcd)};
    color.rgbBkColor = (RGBColor){CWC(0xfedc), CWC(0x4321), CWC(0x8765)};
    variables.rgbOpColor = (RGBColor){CWC(0x1111), CWC(0x2222), CWC(0x3333)};
    variables.rgbHiliteColor = (RGBColor){CWC(0x4444), CWC(0x5555), CWC(0x6666)};
    const char* colors[] = {"GetPortForeColor", "GetPortBackColor", "GetPortOpColor", "GetPortHiliteColor"};
    const RGBColor* expected_colors[] = {&color.rgbFgColor, &color.rgbBkColor, &variables.rgbOpColor,
                                         &variables.rgbHiliteColor};
    SET_THEPORT_RAW(RM(&color));
    for (unsigned i = 0; i < 4; ++i) {
        struct {
            uint16 before;
            RGBColor color;
            uint16 after;
        } output = {0x1234, {0}, 0x5678};

        RGBColor* (*get)(GrafPtr, RGBColor*) = function(colors[i]);
        ROMlib_qd_error = -177;
        assert(get(NULL, &output.color) == &output.color);
        assert(!memcmp(&output.color, expected_colors[i], sizeof(RGBColor)));
        assert(output.before == 0x1234 && output.after == 0x5678 && ROMlib_qd_error == -177);
        RGBColor saved = output.color;
        get(&mono, &output.color);
        assert(!memcmp(&output.color, &saved, sizeof saved) && ROMlib_qd_error == paramErr);
        assert(!get((GrafPtr)&color, NULL));
    }
    HIDDEN_RAW_SET(variable_master, 0);
    RGBColor untouched = color.rgbFgColor;
    RGBColor* (*op)(GrafPtr, RGBColor*) = function("GetPortOpColor");
    op((GrafPtr)&color, &untouched);
    assert(!memcmp(&untouched, &color.rgbFgColor, sizeof untouched));

    PixMap pixels = {0};
    HIDDEN_PixMapPtr pixel_master;
    HIDDEN_RAW_SET(pixel_master, RM(&pixels));
    pixels.bounds = (Rect){CWC(-10), CWC(2), CWC(234), CWC(567)};
    pixels.pixelSize = CWC(-19);
    Rect output = {0};
    Rect* (*pix_bounds)(PixMapHandle, Rect*) = function("GetPixBounds");
    INTEGER (*depth)(PixMapHandle) = function("GetPixDepth");
    assert(pix_bounds(&pixel_master, &output) == &output && !memcmp(&output, &pixels.bounds, sizeof output));
    assert(depth(&pixel_master) == -19);
    HIDDEN_RAW_SET(pixel_master, 0);
    assert(!depth(&pixel_master));
    assert(pix_bounds(NULL, &output) == &output && !memcmp(&output, &pixels.bounds, sizeof output));
    Region region = {0};
    region.rgnBBox = pixels.bounds;
    HIDDEN_RgnPtr master;
    HIDDEN_RAW_SET(master, RM(&region));
    Rect* (*region_bounds)(RgnHandle, Rect*) = function("GetRegionBounds");
    ROMlib_qd_error = -177;
    assert(region_bounds(&master, &output) == &output && !memcmp(&output, &region.rgnBBox, sizeof output));
    assert(ROMlib_qd_error == -177);
    assert(region_bounds(NULL, &output) == &output && ROMlib_qd_error == paramErr);
    assert(!region_bounds(&master, NULL));
    SET_THEPORT_RAW(0);
    INTEGER (*mode)(GrafPtr) = function("GetPortTextMode");
    assert(mode(NULL) == 1 && ROMlib_qd_error == paramErr);
    assert(fraction(NULL) == INT16_MIN && ch(NULL) == 0);
}

static void quickdraw_remaining_globals(void)
{
    Byte worlds[2][130];
    HIDDEN_HIDDEN_GrafPtr_Ptr roots[2];
    uint32 saved_a5 = EM_A5;
    LONGINT (*get_seed)(void) = function("GetQDGlobalsRandomSeed");
    void (*set_seed)(LONGINT) = function("SetQDGlobalsRandomSeed");
    BitMap* (*get_bits)(BitMap*) = function("GetQDGlobalsScreenBits");
    Cursor* (*get_arrow)(Cursor*) = function("GetQDGlobalsArrow");
    _Static_assert(sizeof(BitMap) == 14 && sizeof(Cursor) == 68, "Classic global record sizes");
    for (unsigned world = 0; world < 2; ++world) {
        for (unsigned i = 0; i < sizeof worlds[world]; ++i)
            worlds[world][i] = world * 71 + i;
        HIDDEN_RAW_SET(roots[world], RM(&worlds[world][126]));
        EM_A5 = US_TO_SYN68K(&roots[world]);
        ROMlib_qd_error = -177;
        uint32 seed = CL(0x81234567 + world);
        memcpy(worlds[world], &seed, sizeof seed);
        assert((uint32)get_seed() == 0x81234567 + world);
        set_seed((LONGINT)(0xfedcba98 + world));
        memcpy(&seed, worlds[world], sizeof seed);
        assert(CL(seed) == 0xfedcba98 + world);

        struct {
            uint16 before;
            BitMap bits;
            uint16 after;
        } bits = {0x1234, {0}, 0x5678};

        struct {
            uint16 before;
            Cursor cursor;
            uint16 after;
        } cursor = {0x1234, {0}, 0x5678};

        assert(get_bits(&bits.bits) == &bits.bits);
        assert(!memcmp(&bits.bits, worlds[world] + 4, sizeof(BitMap)));
        assert(bits.before == 0x1234 && bits.after == 0x5678);
        assert(get_arrow(&cursor.cursor) == &cursor.cursor);
        assert(!memcmp(&cursor.cursor, worlds[world] + 18, sizeof(Cursor)));
        assert(cursor.before == 0x1234 && cursor.after == 0x5678 && ROMlib_qd_error == -177);
        memset(&bits.bits, 0, sizeof bits.bits);
        assert(memcmp(&bits.bits, worlds[world] + 4, sizeof bits.bits));
        assert(!get_bits(NULL) && !get_arrow(NULL) && ROMlib_qd_error == paramErr);
    }
    EM_A5 = saved_a5;
}

static uint32 fixture_read_number(const Byte* bytes, unsigned width)
{
    uint32 value = 0;
    for (unsigned i = 0; i < width; ++i)
        value = (value << 8) | bytes[i];
    return value;
}

static void fixture_write_number(Byte* bytes, unsigned width, uint32 value)
{
    for (unsigned i = 0; i < width; ++i)
        bytes[width - 1 - i] = value >> (8 * i);
}

static void atomic_family(void)
{
    const char* suffixes[] = {"8", "16", ""};
    const unsigned widths[] = {1, 2, 4};
    const LONGINT amounts[] = {0, 1, -1, 0x10203, INT32_MIN, INT32_MAX};
    Byte bytes[16];
    for (unsigned size = 0; size < 3; ++size) {
        unsigned width = widths[size];
        uint32 mask = width == 4 ? UINT32_MAX : (1u << (8 * width)) - 1;
        char name[40];
        snprintf(name, sizeof name, "AddAtomic%s", suffixes[size]);
        LONGINT (*add)(LONGINT, volatile void*) = function(name);
        snprintf(name, sizeof name, "IncrementAtomic%s", suffixes[size]);
        LONGINT (*increment)(volatile void*) = function(name);
        snprintf(name, sizeof name, "DecrementAtomic%s", suffixes[size]);
        LONGINT (*decrement)(volatile void*) = function(name);
        for (unsigned offset = 0; offset < 4; ++offset) {
            for (unsigned value = 0; value <= 256; ++value) {
                uint32 initial = value == 256 ? mask : (0x81234500u | value) & mask;
                LONGINT signed_initial = width == 1 ? (int8)initial : width == 2 ? (int16)initial : (int32)initial;
                for (unsigned a = 0; a < sizeof amounts / sizeof amounts[0]; ++a) {
                    memset(bytes, 0xa5, sizeof bytes);
                    fixture_write_number(bytes + 4 + offset, width, initial);
                    assert(add(amounts[a], bytes + 4 + offset) == signed_initial);
                    assert(fixture_read_number(bytes + 4 + offset, width) ==
                           (initial + amounts[a]) % ((uint64)mask + 1));
                    for (unsigned j = 0; j < sizeof bytes; ++j)
                        if (j < 4 + offset || j >= 4 + offset + width)
                            assert(bytes[j] == 0xa5);
                }
                fixture_write_number(bytes + 4 + offset, width, initial);
                assert(increment(bytes + 4 + offset) == signed_initial);
                assert(fixture_read_number(bytes + 4 + offset, width) == ((initial + 1) & mask));
                fixture_write_number(bytes + 4 + offset, width, initial);
                assert(decrement(bytes + 4 + offset) == signed_initial);
                assert(fixture_read_number(bytes + 4 + offset, width) == ((initial - 1) & mask));
                const char* operations[] = {"And", "Or", "Xor"};
                for (unsigned op = 0; op < 3; ++op) {
                    snprintf(name, sizeof name, "Bit%sAtomic%s", operations[op], suffixes[size]);
                    uint32 (*bitwise)(uint32, volatile void*) = function(name);
                    fixture_write_number(bytes + 4 + offset, width, initial);
                    uint32 operand = 0xa55a3c81;
                    uint32 expected = op == 0 ? initial & operand : op == 1 ? initial | operand : initial ^ operand;
                    assert(bitwise(operand, bytes + 4 + offset) == initial);
                    assert(fixture_read_number(bytes + 4 + offset, width) == (expected & mask));
                }
            }
        }
    }
    BOOLEAN (*compare)(uint32, uint32, volatile uint32*) = function("CompareAndSwap");
    for (unsigned offset = 0; offset < 4; ++offset) {
        memset(bytes, 0xa5, sizeof bytes);
        Byte* at = bytes + 4 + offset;
        fixture_write_number(at, 4, 0x81234567);
        assert(!compare(0, UINT32_MAX, (volatile uint32*)at));
        assert(fixture_read_number(at, 4) == 0x81234567);
        assert(compare(0x81234567, UINT32_MAX, (volatile uint32*)at));
        assert(fixture_read_number(at, 4) == UINT32_MAX);
        assert(compare(UINT32_MAX, 0, (volatile uint32*)at));
        assert(!fixture_read_number(at, 4));
    }
    assert(!compare(0, 1, NULL));
}

static void descriptor_data_helpers(void)
{
    OSErr (*copy)(const AEDesc*, void*, Size) = function("AEGetDescData");
    Size (*size)(const AEDesc*) = function("AEGetDescDataSize");
    OSErr (*replace)(DescType, const void*, Size, AEDesc*) = function("AEReplaceDescData");
    const DescType text_type = 0x54455854;
    AEDesc descriptor = {0};
    descriptor.descriptorType = CL(typeNull);
    Byte output[40], input[] = {0x80, 0, 0xff, 0x34, 0x12};
    assert(size(&descriptor) == 0 && size(NULL) == 0);
    memset(output, 0x5a, sizeof output);
    assert(copy(&descriptor, output, sizeof output) == noErr && output[0] == 0x5a);
    assert(copy(NULL, output, 1) == paramErr);
    assert(copy(&descriptor, output, -1) == paramErr);
    assert(replace(text_type, input, sizeof input, &descriptor) == noErr);
    assert(descriptor_disposals == 1 && descriptor_creations == 1);
    assert(size(&descriptor) == sizeof input && DESC_TYPE(&descriptor) == text_type);
    for (Size maximum = 0; maximum < 12; ++maximum) {
        memset(output, 0x5a, sizeof output);
        Size copied = maximum < sizeof input ? maximum : sizeof input;
        assert(copy(&descriptor, output + 1, maximum) == noErr);
        assert(!memcmp(output + 1, input, copied));
        assert(output[0] == 0x5a && output[copied + 1] == 0x5a);
    }
    assert(copy(&descriptor, NULL, 0) == noErr);
    assert(copy(&descriptor, NULL, 1) == paramErr);
    assert(copy(&descriptor, descriptor_bytes + 1, 4) == noErr);
    assert(!memcmp(descriptor_bytes + 1, input, 4));
    descriptor.descriptorType = CL(typeNull);
    assert(size(&descriptor) == 0);
    descriptor.descriptorType = CL(text_type);
    HIDDEN_RAW_SET(descriptor_master, 0);
    assert(copy(&descriptor, output, 1) == nilHandleErr);
    assert(replace(text_type, NULL, 3, &descriptor) == noErr);
    assert(size(&descriptor) == 3 && !memcmp(descriptor_bytes, "\0\0\0", 3));
    descriptor_create_error = memFullErr;
    assert(replace(text_type, input, sizeof input, &descriptor) == memFullErr);
    assert(DESC_TYPE(&descriptor) == typeNull && !DESC_DATA(&descriptor));
    assert(descriptor_disposals == 3 && descriptor_creations == 3);
    assert(replace(text_type, input, -1, &descriptor) == paramErr);
    assert(replace(text_type, input, 1, NULL) == paramErr);
    assert(descriptor_disposals == 3 && descriptor_creations == 3);
    descriptor_create_error = noErr;
}

static uint32_t expected_slot, expected_info, expected_selector;
static int vector_ok, vector_calls;

int ROMlib_carbon_is_cf_system_allocator_worker(unsigned index, uint32_t address)
{
    return 0;
}

uint32_t ROMlib_carbon_canonical_cf_callbacks(uint32_t record, const void* reference_toc)
{
    return record;
}

int ROMlib_carbon_call_romaw_service(const char* name, ROMlib_carbon_ppc_frame* call_frame)
{
    return 0;
}

int ROMlib_carbon_call_toolbox_vector(uint32_t address, uint32_t info, uint32_t selector,
                                      const ROMlib_carbon_ppc_frame* call_frame, uint32_t* result)
{
    assert(address == expected_slot && info == expected_info && selector == expected_selector &&
           call_frame->words[0] == 0x98765432);
    ++vector_calls;
    *result = 0xabcdef87;
    return vector_ok;
}

uint32_t ROMlib_carbon_new_ppc_descriptor(uint32_t procedure, uint32_t info)
{
    assert(!"Routine descriptor allocation in the isolated host harness");
    return 0;
}

void ROMlib_carbon_dispose_descriptor(uint32_t address)
{
    assert(!"Routine descriptor release in the isolated host harness");
}

int ROMlib_carbon_invoke_pascal(uint32_t procedure, uint32_t info, const ROMlib_carbon_ppc_frame* call_frame,
                                uint32_t* result)
{
    assert(!"Pascal callback invocation in the isolated host harness");
    return 0;
}

int ROMlib_carbon_invoke_mixed_upp(uint32_t procedure, uint32_t info, const ROMlib_carbon_ppc_frame* call_frame,
                                   uint32_t* result)
{
    assert(!"Mixed callback invocation in the isolated host harness");
    return 0;
}

int ROMlib_carbon_call_register_vector(uint32_t address, uint32_t info, uint32_t trap, unsigned trap_slot,
                                       const ROMlib_carbon_ppc_frame* call_frame, uint32_t* result)
{
    assert(!"Register trap invocation in the isolated host harness");
    return 0;
}

int ROMlib_carbon_invoke_transition_vector(uint32_t vector, ROMlib_carbon_ppc_frame* call_frame)
{
    assert(!"Raw transition vector invocation in the isolated host harness");
    return 0;
}

static void toolbox_vector_results(void)
{
    static const struct {
        const char* name;
        uint32_t slot, info, result, selector;
    } cases[] = {{"EmptyRgn", 0x1188, 0xd0, 0x87},
                 {"CurResFile", 0x1450, 0x20, 0xffffef87},
                 {"GetResource", 0x1480, 0x2f0, 0xabcdef87},
                 {"GetCurrentProcess", 0x103c, 0x3ae, 0xffffef87, 0x37},
                 {"OffscreenVersion", 0x1a74, 0xf8, 0xabcdef87, 0x14}};

    for (unsigned i = 0; i < NELEM(cases); ++i) {
        const ROMlib_carbon_export* entry = ROMlib_carbon_find_export(cases[i].name, 2);
        assert(entry && entry->signature == ROMLIB_CARBON_PPC_FRAME && entry->flags == ROMLIB_CARBON_PPC_ONLY);
        expected_slot = cases[i].slot;
        expected_info = cases[i].info;
        expected_selector = cases[i].selector;
        for (vector_ok = 0; vector_ok <= 1; ++vector_ok) {
            ROMlib_carbon_ppc_frame expected, actual;
            memset(&expected, 0xa5, sizeof expected);
            expected.words[0] = 0x98765432;
            memcpy(&actual, &expected, sizeof actual);
            vector_calls = 0;
            assert(((ROMlib_carbon_frame_entry)entry->function)(&actual) == vector_ok && vector_calls == 1);
            if (vector_ok)
                expected.words[0] = cases[i].result;
            assert(!memcmp(&actual, &expected, sizeof actual));
        }
    }
    puts("CarbonLib Toolbox vectors: return widths, preserved registers and failed calls passed");
}

/* The grouped ROMAW app exercises the installed memory and component managers. */
OSErrRET PtrToHand(Ptr source, Handle* destination, LONGINT size)
{
    (void)source;
    (void)destination;
    (void)size;
    abort();
}

void _PurgeSpace_flags(Size* total, Size* contiguous, boolean_t system)
{
    (void)total;
    (void)contiguous;
    (void)system;
    abort();
}

const char* ROMlib_component_signature(unsigned selector)
{
    (void)selector;
    abort();
}

int ROMlib_component_service(unsigned selector, const uint32* arguments, uint32* result)
{
    (void)selector;
    (void)arguments;
    (void)result;
    abort();
}

int ROMlib_component_can_dispatch(uint32 instance)
{
    (void)instance;
    assert(!"Unexpected component dispatch from the isolated CarbonLib archive test");
    return 0;
}

int32 ROMlib_component_call(uint32 instance, const void* parameters)
{
    (void)instance;
    (void)parameters;
    abort();
}

int main(void)
{
    toolbox_vector_results();
    HIDDEN_GrafPtr current_port;
    HIDDEN_HIDDEN_GrafPtr_Ptr port_pointer;
    HIDDEN_RAW_SET(port_pointer, RM(&current_port));
    EM_A5 = US_TO_SYN68K(&port_pointer);
    HIDDEN_RAW_SET(current_port, 0);
    size_t count;
    const ROMlib_carbon_export* exports = ROMlib_carbon_exports(&count);
    assert(count == 4448 && ROMlib_carbon_exports(NULL) == exports);
    assert(!ROMlib_carbon_find_export(NULL, 2));
    assert(ROMlib_carbon_find_export("WaitNextEvent", 2));
    assert(ROMlib_carbon_find_export("CreateThreadPool", 2)->signature == ROMLIB_CARBON_PPC_THREAD);
    assert(ROMlib_carbon_find_export("CFArrayCreateMutable", 2));
    assert(ROMlib_carbon_find_export("CFArraySortValues", 2));
    assert(!ROMlib_carbon_find_library_export(NULL, "PLstrcmp", 2));
    assert(!ROMlib_carbon_find_library_export("StdCLib", "GetWindowPort", 2));
    assert(!ROMlib_carbon_find_library_export("Other", "PLstrcmp", 2));
    assert(!ROMlib_carbon_find_library_export("StdCLib", "PLstrcmp", 1));
    for (size_t i = 0; i < count; ++i) {
        assert(exports[i].function && exports[i].signature);
        const ROMlib_carbon_export* main_entry = exports[i].flags & ROMLIB_CARBON_COMPANION_ONLY ? NULL : &exports[i];
        assert(ROMlib_carbon_find_export(exports[i].name, 2) == main_entry);
        assert(ROMlib_carbon_find_library_export("CarbonLib", exports[i].name, 2) == main_entry);
        if (exports[i].reference_library)
            assert(ROMlib_carbon_find_library_export(exports[i].reference_library, exports[i].name, 2) == &exports[i]);
        const ROMlib_carbon_export* source = ROMlib_carbon_find_library_export("StdCLib", exports[i].name, 2);
        assert(source ==
               (exports[i].reference_library && !strcmp(exports[i].reference_library, "StdCLib") ? &exports[i] : NULL));
        for (size_t j = 0; j < i; ++j)
            assert(strcmp(exports[i].name, exports[j].name));
    }

    GrafPtr (*get_port)(WindowPtr) = function("GetWindowPort");
    assert(get_port((WindowPtr)&application) == (GrafPtr)&application);
    assert(!get_port(NULL));
    BOOLEAN (*hilited)(WindowPtr) = function("IsWindowHilited");
    assert(!hilited((WindowPtr)&application));
    application.hilited = 0x80;
    assert(hilited((WindowPtr)&application) == 1);
    assert(!hilited(NULL));

    BOOLEAN (*pending)(WindowPtr) = function("IsWindowUpdatePending");
    assert(!pending((WindowPtr)&application));
    HIDDEN_RAW_SET(application.updateRgn, RM((RgnHandle)&offscreen));
    region_empty = 1;
    assert(!pending((WindowPtr)&application));
    region_empty = 0;
    assert(pending((WindowPtr)&application) == 1);

    Rect user = {CWC(-7), CWC(11), CWC(230), CWC(410)};
    Rect standard = {CWC(20), CWC(-12), CWC(480), CWC(640)};
    Rect output = user;
    WStateData state = {0};
    HIDDEN_Ptr master;
    HIDDEN_RAW_SET(master, RM(&state));
    HIDDEN_RAW_SET(application.dataHandle, RM(&master));
    state_size = sizeof state;
    void (*set_user)(WindowPtr, const Rect*) = function("SetWindowUserState");
    void (*set_standard)(WindowPtr, const Rect*) = function("SetWindowStandardState");
    Rect* (*get_user)(WindowPtr, Rect*) = function("GetWindowUserState");
    Rect* (*get_standard)(WindowPtr, Rect*) = function("GetWindowStandardState");
    set_user((WindowPtr)&application, &user);
    set_standard((WindowPtr)&application, &standard);
    assert(!memcmp(&state.userState, &user, sizeof user));
    assert(!memcmp(&state.stdState, &standard, sizeof standard));
    assert(get_standard((WindowPtr)&application, &output) == &output);
    assert(!memcmp(&output, &standard, sizeof output));
    assert(get_user((WindowPtr)&application, &output) == &output);
    assert(!memcmp(&output, &user, sizeof output));
    state_size = sizeof state - 1;
    set_standard((WindowPtr)&application, &user);
    get_standard((WindowPtr)&application, &output);
    assert(!memcmp(&output, &user, sizeof output));
    assert(!memcmp(&state.stdState, &standard, sizeof standard));
    assert(get_user(NULL, &output) == &output);
    assert(!get_user((WindowPtr)&application, NULL));
    set_user(NULL, &user);
    set_user((WindowPtr)&application, NULL);
    HIDDEN_RAW_SET(application.dataHandle, 0);
    assert(get_standard((WindowPtr)&application, &output) == &output);
    assert(!memcmp(&output, &user, sizeof output));

    Rect* (*bounds)(WindowPtr, Rect*) = function("GetWindowPortBounds");
    application.port.portRect = standard;
    assert(bounds((WindowPtr)&application, &output) == &output);
    assert(!memcmp(&output, &standard, sizeof output));
    /* A color port's rectangle is at a different offset from a mono port. */
    CGrafPtr color = (CGrafPtr)&floating;
    color->portVersion = CWC(0xc000);
    color->portRect = user;
    bounds((WindowPtr)&floating, &output);
    assert(!memcmp(&output, &user, sizeof output));
    assert(bounds(NULL, &output) == &output);
    assert(!memcmp(&output, &user, sizeof output));
    assert(!bounds((WindowPtr)&floating, NULL));

    void (*set_port)(WindowPtr) = function("SetPortWindowPort");
    set_port((WindowPtr)&floating);
    assert(selected == (GrafPtr)&floating);
    set_port(NULL);
    assert(selected == (GrafPtr)&floating);
    WindowPtr (*from_port)(GrafPtr) = function("GetWindowFromPort");
    assert(from_port((GrafPtr)&floating) == (WindowPtr)&floating);
    assert(from_port((GrafPtr)&application) == (WindowPtr)&application);
    assert(!from_port(&offscreen));
    SET_THEPORT_RAW(RM(&application));
    assert(from_port(NULL) == (WindowPtr)&application);
    SET_THEPORT_RAW(RM(&offscreen));
    assert(!from_port(NULL));
    quickdraw_accessors();
    quickdraw_patterns();
    dialog_accessors();
    window_invalidation();
    string_conversions();
    atomic_helpers();
    ROMlib_carbon_host_tests();
    quickdraw_record_accessors();
    quickdraw_remaining_globals();
    atomic_family();
    descriptor_data_helpers();
    ROMlib_carbon_pascal_tests();
    ROMlib_carbon_corefoundation_tests();
    ROMlib_carbon_encoding_tests();
    ROMlib_carbon_bundle_state_tests();
    ROMlib_carbon_bundle_lifecycle_tests();
    ROMlib_carbon_url_tests();
    ROMlib_carbon_cfstrings_tests();
    ROMlib_carbon_classic_access_tests();
    ROMlib_carbon_values_tests();
    ROMlib_carbon_gworld_tests();
    puts("Native Carbon accessors and update-region tests passed");
    return 0;
}
