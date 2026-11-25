/*+===================================================================
  File:      internal.h

  Summary:   ifndef ROMAW_CARBON_INTERNAL_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_INTERNAL_H
#define ROMAW_CARBON_INTERNAL_H

#include "rsys/common.h"
#include "rsys/everything.h"
#include "carbonlib.h"

GrafPtr ROMlib_carbon_window_port(WindowPtr window);

enum carbon_property_operation {
    property_get,
    property_size,
    property_set,
    property_remove,
    property_attributes,
    property_change,
    property_button_get,
    property_button_set,
    property_title_get,
    property_title_set,
    property_metadata_get,
    property_scope_set,
    property_class_set,
    property_window_attributes_change,
    property_window_class_search,
    property_sheet_parent,
    property_latent_visibility,
    property_scroll_region,
    property_scroll_rect,
    property_find_window_of_class,
    property_class_valid,
    property_available_attributes
};

int ROMlib_carbon_window_property(const char* name, uint32_t offset, enum carbon_property_operation operation,
                                  uint32_t argument, ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_control_property(const char* name, uint32_t offset, enum carbon_property_operation operation,
                                   uint32_t system, ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_clear_window_title(uint32_t window);
int ROMlib_carbon_window_group(const char* name, uint32_t offset, ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_window_activation(const char* name, uint32_t offset, ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_windows_compatibility(const char* name, uint32_t offset, ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_window_saved_screen_bits(const char* name, uint32_t offset, ROMlib_carbon_ppc_frame* call);
void ROMlib_carbon_get_gworld(HIDDEN_CGrafPtr* port, HIDDEN_GDHandle* device);
int ROMlib_carbon_new_gworld_from_ptr(ROMlib_carbon_ppc_frame* call);
GDHandle ROMlib_carbon_gworld_device(GWorldPtr world);
PixMapHandle ROMlib_carbon_gworld_pixmap(GWorldPtr world);
Ptr ROMlib_carbon_pix_base_addr(PixMapHandle pixels);
GWorldFlags ROMlib_carbon_pixels_state(PixMapHandle pixels);
int ROMlib_carbon_nqd_get_gworld(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_nqd_gworld_device(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_nqd_gworld_pixmap(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_nqd_pix_base_addr(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_nqd_pix_rowbytes(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_nqd_pixmap_32bit(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_nqd_pixels_state(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_nqd_ctab_changed(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_nqd_pixpat_changed(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_nqd_clear_error(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_nqd_is_done(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_nqd_offscreen_version(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_nqd_put_pic_byte(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_nqd_put_pic_data(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_nqd_put_pic_long(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_nqd_put_pic_op(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_nqd_put_pic_word(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_nqd_check_picture(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_nqd_hilite_rect(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_nqd_hilite_region(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_nqd_unflatten_pixpat(ROMlib_carbon_ppc_frame* call);
BOOLEAN ROMlib_carbon_window_hilited(WindowPtr window);
BOOLEAN ROMlib_carbon_window_update_pending(WindowPtr window);
Rect* ROMlib_carbon_window_standard_state(WindowPtr window, Rect* bounds);
Rect* ROMlib_carbon_window_user_state(WindowPtr window, Rect* bounds);
void ROMlib_carbon_set_window_standard_state(WindowPtr window, const Rect* bounds);
void ROMlib_carbon_set_window_user_state(WindowPtr window, const Rect* bounds);
void ROMlib_carbon_set_port_window_port(WindowPtr window);
Rect* ROMlib_carbon_window_port_bounds(WindowPtr window, Rect* bounds);
WindowPtr ROMlib_carbon_window_from_port(GrafPtr port);

OSErr ROMlib_carbon_lock_port_bits(GrafPtr port);
OSErr ROMlib_carbon_unlock_port_bits(GrafPtr port);
BOOLEAN ROMlib_carbon_port_is_color(GrafPtr port);
BOOLEAN ROMlib_carbon_port_is_offscreen(GrafPtr port);
BOOLEAN ROMlib_carbon_port_defining_region(GrafPtr port);
BOOLEAN ROMlib_carbon_port_defining_picture(GrafPtr port);
BOOLEAN ROMlib_carbon_port_defining_polygon(GrafPtr port);
void ROMlib_carbon_set_port_bounds(GrafPtr port, const Rect* bounds);
void ROMlib_carbon_set_port_clip(GrafPtr port, RgnHandle region);
void ROMlib_carbon_set_port_visible(GrafPtr port, RgnHandle region);
void ROMlib_carbon_set_port_fractional_pen(GrafPtr port, INTEGER fraction);
void ROMlib_carbon_set_port_graf_procs(GrafPtr port, QDProcsPtr procs);
void ROMlib_carbon_set_port_op_color(GrafPtr port, const RGBColor* color);
void ROMlib_carbon_set_port_pen_mode(GrafPtr port, INTEGER mode);
void ROMlib_carbon_set_port_pen_size(GrafPtr port, uint32 size);
PixPatHandle ROMlib_carbon_get_port_back_pixpat_direct(GrafPtr port);
PixPatHandle ROMlib_carbon_get_port_pen_pixpat_direct(GrafPtr port);
PixPatHandle ROMlib_carbon_get_port_back_pixpat(GrafPtr port, PixPatHandle output);
PixPatHandle ROMlib_carbon_get_port_pen_pixpat(GrafPtr port, PixPatHandle output);
PixPatHandle ROMlib_carbon_get_port_fill_pixpat(GrafPtr port, PixPatHandle output);
void ROMlib_carbon_set_port_back_pixpat(GrafPtr port, PixPatHandle input);
void ROMlib_carbon_set_port_pen_pixpat(GrafPtr port, PixPatHandle input);
void ROMlib_carbon_set_port_fill_pixpat(GrafPtr port, PixPatHandle input);
void ROMlib_carbon_set_port_back_pixpat_direct(GrafPtr port, PixPatHandle input);
void ROMlib_carbon_set_port_pen_pixpat_direct(GrafPtr port, PixPatHandle input);
void ROMlib_carbon_set_port_fill_pixpat_direct(GrafPtr port, PixPatHandle input);
void ROMlib_carbon_set_qd_arrow(const Cursor* cursor);
Rect* ROMlib_carbon_port_bounds(GrafPtr port, Rect* bounds);
PixMapHandle ROMlib_carbon_port_pixmap(GrafPtr port);
RgnHandle ROMlib_carbon_port_visible_region(GrafPtr port, RgnHandle output);
RgnHandle ROMlib_carbon_port_clip_region(GrafPtr port, RgnHandle output);
INTEGER ROMlib_carbon_port_text_font(GrafPtr port);
INTEGER ROMlib_carbon_port_text_size(GrafPtr port);
LONGINT ROMlib_carbon_pix_rowbytes(PixMapHandle pixmap);
GrafPtr ROMlib_carbon_qd_globals_port(void);
void ROMlib_carbon_set_qd_error(INTEGER error);
int ROMlib_carbon_convert_region_handle(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_region_bits(const char* name, uint32_t offset, ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_create_port(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_dispose_port(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_valid_port(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_get_port_printing_reference(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_set_port_printing_reference(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_region_to_rects(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_intersect_rects(ROMlib_carbon_ppc_frame* call);
Pattern* ROMlib_carbon_qd_black(Pattern* output);
Pattern* ROMlib_carbon_qd_white(Pattern* output);
Pattern* ROMlib_carbon_qd_gray(Pattern* output);
Pattern* ROMlib_carbon_qd_light_gray(Pattern* output);
Pattern* ROMlib_carbon_qd_dark_gray(Pattern* output);

WindowPtr ROMlib_carbon_dialog_window(DialogPtr dialog);
DialogPtr ROMlib_carbon_dialog_from_window(WindowPtr window);
TEHandle ROMlib_carbon_dialog_text_edit(DialogPtr dialog);
INTEGER ROMlib_carbon_dialog_default_item(DialogPtr dialog);
int ROMlib_carbon_get_param_text(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_insert_dialog_item(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_remove_dialog_items(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_std_filter_proc(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_set_dialog_default_item(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_set_dialog_cancel_item(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_set_dialog_tracks_cursor(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_move_dialog_item(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_size_dialog_item(ROMlib_carbon_ppc_frame* call);
void ROMlib_carbon_set_port_dialog_port(DialogPtr dialog);
Rect* ROMlib_carbon_control_bounds(ControlHandle control, Rect* output);

LONGINT ROMlib_carbon_inval_window_rect(WindowPtr window, const Rect* rectangle);
LONGINT ROMlib_carbon_inval_window_region(WindowPtr window, RgnHandle region);
LONGINT ROMlib_carbon_valid_window_rect(WindowPtr window, const Rect* rectangle);
LONGINT ROMlib_carbon_valid_window_region(WindowPtr window, RgnHandle region);

void ROMlib_carbon_copy_pascal_to_c(const Byte* source, char* destination);
void ROMlib_carbon_copy_c_to_pascal(const char* source, Byte* destination);
void ROMlib_carbon_c2pstrcpy(Byte* destination, const char* source);
void ROMlib_carbon_p2cstrcpy(char* destination, const Byte* source);

LONGINT ROMlib_carbon_increment_atomic(volatile LONGINT* value);
LONGINT ROMlib_carbon_decrement_atomic(volatile LONGINT* value);
BOOLEAN ROMlib_carbon_test_and_set(uint32 bit, volatile Byte* value);
BOOLEAN ROMlib_carbon_test_and_clear(uint32 bit, volatile Byte* value);

LONGINT ROMlib_carbon_port_text_face(GrafPtr port);
INTEGER ROMlib_carbon_port_text_mode(GrafPtr port);
INTEGER ROMlib_carbon_port_pen_mode(GrafPtr port);
INTEGER ROMlib_carbon_port_pen_visibility(GrafPtr port);
Fixed ROMlib_carbon_port_space_extra(GrafPtr port);
INTEGER ROMlib_carbon_port_char_extra(GrafPtr port);
INTEGER ROMlib_carbon_port_fractional_pen(GrafPtr port);
Point* ROMlib_carbon_port_pen_size(GrafPtr port, Point* output);
Point* ROMlib_carbon_port_pen_location(GrafPtr port, Point* output);
void* ROMlib_carbon_port_graf_procs(GrafPtr port);
const BitMap* ROMlib_carbon_port_bitmap(GrafPtr port);
Rect* ROMlib_carbon_pix_bounds(PixMapHandle pixels, Rect* output);
INTEGER ROMlib_carbon_pix_depth(PixMapHandle pixels);
Rect* ROMlib_carbon_region_bounds(RgnHandle region, Rect* output);
RGBColor* ROMlib_carbon_port_fore_color(GrafPtr port, RGBColor* output);
RGBColor* ROMlib_carbon_port_back_color(GrafPtr port, RGBColor* output);
RGBColor* ROMlib_carbon_port_op_color(GrafPtr port, RGBColor* output);
RGBColor* ROMlib_carbon_port_hilite_color(GrafPtr port, RGBColor* output);
LONGINT ROMlib_carbon_qd_random_seed(void);
void ROMlib_carbon_set_qd_random_seed(LONGINT seed);
BitMap* ROMlib_carbon_qd_screen_bits(BitMap* output);
Cursor* ROMlib_carbon_qd_arrow(Cursor* output);

LONGINT ROMlib_carbon_add_atomic(LONGINT amount, volatile LONGINT* value);
LONGINT ROMlib_carbon_add_atomic8(LONGINT amount, volatile int8* value);
LONGINT ROMlib_carbon_add_atomic16(LONGINT amount, volatile INTEGER* value);
LONGINT ROMlib_carbon_increment_atomic8(volatile int8* value);
LONGINT ROMlib_carbon_decrement_atomic8(volatile int8* value);
LONGINT ROMlib_carbon_increment_atomic16(volatile INTEGER* value);
LONGINT ROMlib_carbon_decrement_atomic16(volatile INTEGER* value);
uint32 ROMlib_carbon_bit_and_atomic(uint32 mask, volatile uint32* value);
uint32 ROMlib_carbon_bit_or_atomic(uint32 mask, volatile uint32* value);
uint32 ROMlib_carbon_bit_xor_atomic(uint32 mask, volatile uint32* value);
uint32 ROMlib_carbon_bit_and_atomic8(uint32 mask, volatile Byte* value);
uint32 ROMlib_carbon_bit_or_atomic8(uint32 mask, volatile Byte* value);
uint32 ROMlib_carbon_bit_xor_atomic8(uint32 mask, volatile Byte* value);
uint32 ROMlib_carbon_bit_and_atomic16(uint32 mask, volatile uint16* value);
uint32 ROMlib_carbon_bit_or_atomic16(uint32 mask, volatile uint16* value);
uint32 ROMlib_carbon_bit_xor_atomic16(uint32 mask, volatile uint16* value);
BOOLEAN ROMlib_carbon_compare_and_swap(uint32 expected, uint32 replacement, volatile uint32* value);

OSErr ROMlib_carbon_get_desc_data(const AEDesc* descriptor, void* output, Size maximum);
int ROMlib_carbon_AECheckIsRecord(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_AESizeOfFlattenedDesc(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_AEFlattenDesc(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_AEUnflattenDesc(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_AEPrintDescToHandle(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_AEGetArray(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_AEPutArray(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_CreateCompDescriptor(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_CreateLogicalDescriptor(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_CreateObjSpecifier(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_CreateOffsetDescriptor(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_CreateRangeDescriptor(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_ae_build(const char* name, int event_kind, Boolean indirect, ROMlib_carbon_ppc_frame* call);

enum carbon_ae_stream_operation {
    ae_stream_open,
    ae_stream_close,
    ae_stream_open_desc,
    ae_stream_close_desc,
    ae_stream_write_data,
    ae_stream_write_desc,
    ae_stream_write_aedesc,
    ae_stream_open_list,
    ae_stream_close_list,
    ae_stream_open_record,
    ae_stream_close_record,
    ae_stream_record_type,
    ae_stream_write_key,
    ae_stream_open_key_desc,
    ae_stream_write_key_desc,
    ae_stream_create_event,
    ae_stream_open_event,
    ae_stream_optional_param,
};

int ROMlib_carbon_ae_stream(enum carbon_ae_stream_operation operation, const char* name, ROMlib_carbon_ppc_frame* call);
Size ROMlib_carbon_get_desc_data_size(const AEDesc* descriptor);
OSErr ROMlib_carbon_replace_desc_data(DescType type, const void* data, Size size, AEDesc* descriptor);

INTEGER ROMlib_carbon_pl_length(const Byte* string);
INTEGER ROMlib_carbon_pl_compare(const Byte* first, const Byte* second);
INTEGER ROMlib_carbon_pl_compare_n(const Byte* first, const Byte* second, INTEGER maximum);
Byte* ROMlib_carbon_pl_copy(Byte* destination, const Byte* source);
Byte* ROMlib_carbon_pl_copy_n(Byte* destination, const Byte* source, INTEGER maximum);
Byte* ROMlib_carbon_pl_append(Byte* destination, const Byte* source);
Byte* ROMlib_carbon_pl_append_n(Byte* destination, const Byte* source, INTEGER maximum);
const Byte* ROMlib_carbon_pl_find_char(const Byte* string, INTEGER character);
const Byte* ROMlib_carbon_pl_find_last_char(const Byte* string, INTEGER character);
const Byte* ROMlib_carbon_pl_find_any(const Byte* string, const Byte* characters);
INTEGER ROMlib_carbon_pl_span(const Byte* string, const Byte* characters);
const Byte* ROMlib_carbon_pl_find_string(const Byte* string, const Byte* search);
INTEGER ROMlib_carbon_pl_position(const Byte* search, const Byte* string);

LONGINT ROMlib_carbon_cf_array_count(const void* array);
void* ROMlib_carbon_cf_allocator_default(void);
uint32 ROMlib_carbon_cf_allocator_type_id(void);
void* ROMlib_carbon_cf_allocator_create(uint32 allocator, const void* context);
void ROMlib_carbon_cf_allocator_context(const void* allocator, void* context);
LONGINT ROMlib_carbon_cf_allocator_preferred(const void* allocator, LONGINT size, uint32 hints);
void* ROMlib_carbon_cf_allocator_allocate(const void* allocator, LONGINT size, uint32 hints);
void* ROMlib_carbon_cf_allocator_reallocate(const void* allocator, void* pointer, LONGINT size, uint32 hints);
void ROMlib_carbon_cf_allocator_deallocate(const void* allocator, void* pointer);
void* ROMlib_carbon_cf_retain(void* object);
void ROMlib_carbon_cf_release(void* object);
LONGINT ROMlib_carbon_cf_retain_count(const void* object);
void* ROMlib_carbon_cf_get_allocator(const void* object);
uint32 ROMlib_carbon_cf_array_type_id(void);
void* ROMlib_carbon_cf_array_create(const void* allocator, const void* values, LONGINT count, const void* callbacks);
void ROMlib_carbon_cf_array_apply(const void* array, LONGINT location, LONGINT length, void* function, uint32 context);
LONGINT ROMlib_carbon_cf_array_search(const void* array, LONGINT location, LONGINT length, uint32 value, void* function,
                                      uint32 context);
double ROMlib_carbon_cf_absolute_time(void);
double ROMlib_carbon_cf_date_time(const void* date);
double ROMlib_carbon_cf_date_interval(const void* date, const void* other);
LONGINT ROMlib_carbon_cf_date_compare(const void* date, const void* other, uint32 context);
LONGINT ROMlib_carbon_cf_string_length(void* string);
const void* ROMlib_carbon_cf_string_characters(const void* string);
uint32 ROMlib_carbon_cf_string_character(const void* string, LONGINT index);
uint32 ROMlib_carbon_cf_unichar_member(uint32 character, uint32 charset);
uint32 ROMlib_carbon_cf_string_pascal(void* string, void* output, LONGINT capacity, uint32 encoding);
uint32 ROMlib_carbon_cf_string_c(void* string, void* output, LONGINT capacity, uint32 encoding);
LONGINT ROMlib_carbon_cf_string_bytes(void* string, LONGINT location, LONGINT length, uint32 encoding, uint32 loss_byte,
                                      uint32 external, void* output, LONGINT capacity, void* used);
uint32 ROMlib_carbon_cf_string_fastest(const void* string);
void* ROMlib_carbon_cf_bundle_create(uint32 allocator, const void* url);
void* ROMlib_carbon_cf_bundle_local_info(void* bundle);
uint32 ROMlib_carbon_cf_bundle_version(void* bundle);
void* ROMlib_carbon_cf_bundle_development(void* bundle);
void ROMlib_carbon_cf_bundle_unload(void* bundle);
void ROMlib_carbon_cf_bundle_functions(void* bundle, const void* names, void* output);
void ROMlib_carbon_cf_bundle_data_pointers(void* bundle, const void* names, void* output);
uint32 ROMlib_carbon_cf_url_type_id(void);
uint32 ROMlib_carbon_cf_url_decomposable(void* url);
void* ROMlib_carbon_cf_url_string(void* url);
void* ROMlib_carbon_cf_url_base(void* url);
uint32 ROMlib_carbon_cf_url_directory(void* url);
void* ROMlib_carbon_cf_url_scheme(void* url);
void* ROMlib_carbon_cf_url_net_location(void* url);
void* ROMlib_carbon_cf_url_path(void* url);
void* ROMlib_carbon_cf_url_strict_path(void* url, Byte* absolute);
void* ROMlib_carbon_cf_url_resource(void* url);
void* ROMlib_carbon_cf_url_last_component(void* url);
void* ROMlib_carbon_cf_url_append(uint32 allocator, void* url, const void* component, uint32 directory);
uint32 ROMlib_carbon_cf_bundle_type_id(void);
void* ROMlib_carbon_cf_bundle_url(void* bundle);
void* ROMlib_carbon_cf_bundle_info(void* bundle);
void* ROMlib_carbon_cf_bundle_info_value(void* bundle, const void* key);
void* ROMlib_carbon_cf_bundle_identifier(void* bundle);
uint32 ROMlib_carbon_cf_bundle_loaded(void* bundle);
uint32 ROMlib_carbon_cf_bundle_load(void* bundle);
void* ROMlib_carbon_cf_bundle_plugin(void* bundle);
LONGINT ROMlib_carbon_cf_maximum_encoding_size(LONGINT length, uint32 encoding);
uint32 ROMlib_carbon_cf_encoding_to_ns(uint32 encoding);
uint32 ROMlib_carbon_cf_ns_to_encoding(uint32 encoding);
uint32 ROMlib_carbon_cf_encoding_to_windows(uint32 encoding);
uint32 ROMlib_carbon_cf_windows_to_encoding(uint32 page);
uint32 ROMlib_carbon_cf_encoding_available(uint32 encoding);
uint32 ROMlib_carbon_cf_compatible_mac_encoding(uint32 encoding);
const void* ROMlib_carbon_cf_string_c_pointer(void* string, uint32 encoding);
const void* ROMlib_carbon_cf_string_pascal_pointer(void* string, uint32 encoding);
int ROMlib_carbon_cf_encode_stream(const uint32 arguments[9], uint32* result);
void* ROMlib_carbon_cf_bundle_function(void* bundle, const void* name);
void* ROMlib_carbon_cf_bundle_data(void* bundle, const void* name);
void ROMlib_carbon_cf_string_get_characters(const void* string, LONGINT location, LONGINT length, void* output);
uint32 ROMlib_carbon_cf_array_value(const void* array, LONGINT index);
void ROMlib_carbon_cf_array_values(const void* array, LONGINT location, LONGINT length, void* output);
uint32 ROMlib_carbon_cf_type_id(const void* object);
LONGINT ROMlib_carbon_cf_data_length(const void* data);
Byte* ROMlib_carbon_cf_data_bytes(const void* data);
void ROMlib_carbon_cf_data_get_bytes(const void* data, LONGINT location, LONGINT length, void* output);

void* ROMlib_carbon_cf_url_host(void* url);
void* ROMlib_carbon_cf_url_user(void* url);
void* ROMlib_carbon_cf_url_password(void* url);
void* ROMlib_carbon_cf_url_parameters(void* url, const void* leave_escaped);
void* ROMlib_carbon_cf_url_query(void* url, const void* leave_escaped);
void* ROMlib_carbon_cf_url_fragment(void* url, const void* leave_escaped);
uint32 ROMlib_carbon_cf_url_port(void* url);
void* ROMlib_carbon_cf_url_reserved(void* url);
void ROMlib_carbon_cf_url_set_reserved(void* url, void* value);
void* ROMlib_carbon_cf_url_extension(void* url);
void* ROMlib_carbon_cf_url_from_bytes(uint32 allocator, const Byte* bytes, uint32 length, uint32 directory);
void* ROMlib_carbon_cf_url_from_bytes_base(uint32 allocator, const Byte* bytes, uint32 length, uint32 directory,
                                           void* base);
void* ROMlib_carbon_cf_url_from_fsref(uint32 allocator, const void* fsref);
uint32 ROMlib_carbon_cf_url_get_fsref(void* url, void* fsref);

void* ROMlib_carbon_cf_url_alloc(uint32 allocator);
void ROMlib_carbon_cf_url_initialize(void* url, const void* text, void* base);
void* ROMlib_carbon_cf_url_create_string(uint32 allocator, const void* text, void* base);
void* ROMlib_carbon_cf_url_create_bytes(uint32 allocator, const Byte* bytes, uint32 length, uint32 encoding,
                                        void* base);
void* ROMlib_carbon_cf_url_create_data(uint32 allocator, void* url, uint32 encoding, uint32 escape_whitespace);
void* ROMlib_carbon_cf_url_create_file(uint32 allocator, const void* text, uint32 style, uint32 directory);
void* ROMlib_carbon_cf_url_create_file_base(uint32 allocator, const void* text, uint32 style, uint32 directory,
                                            void* base);
void* ROMlib_carbon_cf_url_delete_last(uint32 allocator, void* url);
void* ROMlib_carbon_cf_url_append_extension(uint32 allocator, void* url, const void* extension);
void* ROMlib_carbon_cf_url_delete_extension(uint32 allocator, void* url);

void* ROMlib_carbon_cf_url_absolute(void* url);
void* ROMlib_carbon_cf_url_filesystem_path(void* url, uint32 style);
uint32 ROMlib_carbon_cf_url_filesystem_bytes(void* url, uint32 resolve, Byte* buffer, uint32 capacity);

uint32 ROMlib_carbon_cf_string_compare_options(const void* first, const void* second, uint32 location, uint32 length,
                                               uint32 options);
uint32 ROMlib_carbon_cf_string_compare(const void* first, const void* second, uint32 options);
uint32 ROMlib_carbon_cf_string_prefix(const void* string, const void* pattern);
uint32 ROMlib_carbon_cf_string_suffix(const void* string, const void* pattern);
void ROMlib_carbon_cf_string_find(void* range, const void* string, const void* pattern, uint32 options);
uint32 ROMlib_carbon_cf_string_int(const void* string);
void* ROMlib_carbon_cf_string_copy(uint32 allocator, const void* string);
void* ROMlib_carbon_cf_string_constant(const Byte* literal);

/* Raw guest-word PPC low-memory ABI. */
uint32 ROMlib_carbon_LMGetApFontID(void);
uint32 ROMlib_carbon_LMGetApplZone(void);
uint32 ROMlib_carbon_LMGetBootDrive(void);
uint32 ROMlib_carbon_LMGetBufPtr(void);
uint32 ROMlib_carbon_LMGetBufTgDate(void);
uint32 ROMlib_carbon_LMGetBufTgFBkNum(void);
uint32 ROMlib_carbon_LMGetBufTgFFlg(void);
uint32 ROMlib_carbon_LMGetBufTgFNum(void);
uint32 ROMlib_carbon_LMGetCPUFlag(void);
uint32 ROMlib_carbon_LMGetCurApName(void);
uint32 ROMlib_carbon_LMGetCurApRefNum(void);
uint32 ROMlib_carbon_LMGetCurPageOption(void);
uint32 ROMlib_carbon_LMGetCurPitch(void);
uint32 ROMlib_carbon_LMGetCurStackBase(void);
uint32 ROMlib_carbon_LMGetCursorNew(void);
uint32 ROMlib_carbon_LMGetDefltStack(void);
uint32 ROMlib_carbon_LMGetDeviceList(void);
uint32 ROMlib_carbon_LMGetDiskFormatingHFSDefaults(void);
uint32 ROMlib_carbon_LMGetFinderName(void);
uint32 ROMlib_carbon_LMGetFractEnable(void);
uint32 ROMlib_carbon_LMGetGZMoveHnd(void);
uint32 ROMlib_carbon_LMGetGZRootHnd(void);
uint32 ROMlib_carbon_LMGetHeapEnd(void);
uint32 ROMlib_carbon_LMGetHighHeapMark(void);
uint32 ROMlib_carbon_LMGetHiliteMode(void);
uint32 ROMlib_carbon_LMGetHiliteRGB(uint32 destination);
uint32 ROMlib_carbon_LMGetIntlSpec(void);
uint32 ROMlib_carbon_LMGetJStash(void);
uint32 ROMlib_carbon_LMGetKbdLast(void);
uint32 ROMlib_carbon_LMGetKbdType(void);
uint32 ROMlib_carbon_LMGetKeyRepThresh(void);
uint32 ROMlib_carbon_LMGetKeyThresh(void);
uint32 ROMlib_carbon_LMGetLastFOND(void);
uint32 ROMlib_carbon_LMGetLastSPExtra(void);
uint32 ROMlib_carbon_LMGetLvl2DT(uint32 index);
uint32 ROMlib_carbon_LMGetMainDevice(void);
uint32 ROMlib_carbon_LMGetMemErr(void);
uint32 ROMlib_carbon_LMGetMemTop(void);
uint32 ROMlib_carbon_LMGetMinStack(void);
uint32 ROMlib_carbon_LMGetMinusOne(void);
uint32 ROMlib_carbon_LMGetOneOne(void);
uint32 ROMlib_carbon_LMGetPrintErr(void);
uint32 ROMlib_carbon_LMGetQDColors(void);
uint32 ROMlib_carbon_LMGetResErr(void);
uint32 ROMlib_carbon_LMGetResLoad(void);
uint32 ROMlib_carbon_LMGetRndSeed(void);
uint32 ROMlib_carbon_LMGetSEvtEnb(void);
uint32 ROMlib_carbon_LMGetScrDmpEnb(void);
uint32 ROMlib_carbon_LMGetScrHRes(void);
uint32 ROMlib_carbon_LMGetScrVRes(void);
uint32 ROMlib_carbon_LMGetSdVolume(void);
uint32 ROMlib_carbon_LMGetSoundBase(void);
uint32 ROMlib_carbon_LMGetSoundLevel(void);
uint32 ROMlib_carbon_LMGetSoundPtr(void);
uint32 ROMlib_carbon_LMGetStackLowPoint(void);
uint32 ROMlib_carbon_LMGetSysFontFam(void);
uint32 ROMlib_carbon_LMGetSysFontSize(void);
uint32 ROMlib_carbon_LMGetSysMap(void);
uint32 ROMlib_carbon_LMGetSysResName(void);
uint32 ROMlib_carbon_LMGetSysZone(void);
uint32 ROMlib_carbon_LMGetTheGDevice(void);
uint32 ROMlib_carbon_LMGetTheMenu(void);
uint32 ROMlib_carbon_LMGetTmpResLoad(void);
uint32 ROMlib_carbon_LMGetToExtFS(void);
uint32 ROMlib_carbon_LMGetToolScratch(void);
uint32 ROMlib_carbon_LMGetWidthListHand(void);
uint32 ROMlib_carbon_LMGetWidthPtr(void);
uint32 ROMlib_carbon_LMGetWidthTabHandle(void);
uint32 ROMlib_carbon_LMGetWordRedraw(void);
void ROMlib_carbon_LMSetApFontID(uint32 value);
void ROMlib_carbon_LMSetApplZone(uint32 value);
void ROMlib_carbon_LMSetBootDrive(uint32 value);
void ROMlib_carbon_LMSetBufPtr(uint32 value);
void ROMlib_carbon_LMSetBufTgDate(uint32 value);
void ROMlib_carbon_LMSetBufTgFBkNum(uint32 value);
void ROMlib_carbon_LMSetBufTgFFlg(uint32 value);
void ROMlib_carbon_LMSetBufTgFNum(uint32 value);
void ROMlib_carbon_LMSetCPUFlag(uint32 value);
void ROMlib_carbon_LMSetCurApName(uint32 source);
void ROMlib_carbon_LMSetCurApRefNum(uint32 value);
void ROMlib_carbon_LMSetCurPageOption(uint32 value);
void ROMlib_carbon_LMSetCurPitch(uint32 value);
void ROMlib_carbon_LMSetCurStackBase(uint32 value);
void ROMlib_carbon_LMSetCursorNew(uint32 value);
void ROMlib_carbon_LMSetDefltStack(uint32 value);
void ROMlib_carbon_LMSetDeviceList(uint32 value);
void ROMlib_carbon_LMSetDiskFormatingHFSDefaults(uint32 value);
void ROMlib_carbon_LMSetFinderName(uint32 source);
void ROMlib_carbon_LMSetFractEnable(uint32 value);
void ROMlib_carbon_LMSetGZMoveHnd(uint32 value);
void ROMlib_carbon_LMSetGZRootHnd(uint32 value);
void ROMlib_carbon_LMSetHeapEnd(uint32 value);
void ROMlib_carbon_LMSetHighHeapMark(uint32 value);
void ROMlib_carbon_LMSetHiliteMode(uint32 value);
void ROMlib_carbon_LMSetHiliteRGB(uint32 source);
void ROMlib_carbon_LMSetIntlSpec(uint32 value);
void ROMlib_carbon_LMSetJStash(uint32 value);
void ROMlib_carbon_LMSetKbdLast(uint32 value);
void ROMlib_carbon_LMSetKbdType(uint32 value);
void ROMlib_carbon_LMSetKeyRepThresh(uint32 value);
void ROMlib_carbon_LMSetKeyThresh(uint32 value);
void ROMlib_carbon_LMSetLastFOND(uint32 value);
void ROMlib_carbon_LMSetLastSPExtra(uint32 value);
void ROMlib_carbon_LMSetLvl2DT(uint32 value, uint32 index);
void ROMlib_carbon_LMSetMainDevice(uint32 value);
void ROMlib_carbon_LMSetMemErr(uint32 value);
void ROMlib_carbon_LMSetMemTop(uint32 value);
void ROMlib_carbon_LMSetMinStack(uint32 value);
void ROMlib_carbon_LMSetMinusOne(uint32 value);
void ROMlib_carbon_LMSetOneOne(uint32 value);
void ROMlib_carbon_LMSetPrintErr(uint32 value);
void ROMlib_carbon_LMSetQDColors(uint32 value);
void ROMlib_carbon_LMSetResErr(uint32 value);
void ROMlib_carbon_LMSetResLoad(uint32 value);
void ROMlib_carbon_LMSetRndSeed(uint32 value);
void ROMlib_carbon_LMSetSEvtEnb(uint32 value);
void ROMlib_carbon_LMSetScrDmpEnb(uint32 value);
void ROMlib_carbon_LMSetScrHRes(uint32 value);
void ROMlib_carbon_LMSetScrVRes(uint32 value);
void ROMlib_carbon_LMSetSdVolume(uint32 value);
void ROMlib_carbon_LMSetSoundBase(uint32 value);
void ROMlib_carbon_LMSetSoundLevel(uint32 value);
void ROMlib_carbon_LMSetSoundPtr(uint32 value);
void ROMlib_carbon_LMSetStackLowPoint(uint32 value);
void ROMlib_carbon_LMSetSysFontFam(uint32 value);
void ROMlib_carbon_LMSetSysFontSize(uint32 value);
void ROMlib_carbon_LMSetSysMap(uint32 value);
void ROMlib_carbon_LMSetSysResName(uint32 source);
void ROMlib_carbon_LMSetSysZone(uint32 value);
void ROMlib_carbon_LMSetTheGDevice(uint32 value);
void ROMlib_carbon_LMSetTmpResLoad(uint32 value);
void ROMlib_carbon_LMSetToExtFS(uint32 value);
void ROMlib_carbon_LMSetToolScratch(uint32 source);
void ROMlib_carbon_LMSetWidthListHand(uint32 value);
void ROMlib_carbon_LMSetWidthPtr(uint32 value);
void ROMlib_carbon_LMSetWidthTabHandle(uint32 value);
void ROMlib_carbon_LMSetWordRedraw(uint32 value);

/* Raw guest-word PPC classic record ABI. */
uint32 ROMlib_carbon_GetListPort(uint32 handle);
uint32 ROMlib_carbon_GetListVerticalScrollBar(uint32 handle);
uint32 ROMlib_carbon_GetListHorizontalScrollBar(uint32 handle);
uint32 ROMlib_carbon_GetListFlags(uint32 handle);
uint32 ROMlib_carbon_GetListSelectionFlags(uint32 handle);
uint32 ROMlib_carbon_GetListActive(uint32 handle);
uint32 ROMlib_carbon_GetListClickTime(uint32 handle);
uint32 ROMlib_carbon_GetListClickLoop(uint32 handle);
uint32 ROMlib_carbon_GetListRefCon(uint32 handle);
uint32 ROMlib_carbon_GetListDefinition(uint32 handle);
uint32 ROMlib_carbon_GetListUserHandle(uint32 handle);
uint32 ROMlib_carbon_GetListDataHandle(uint32 handle);
uint32 ROMlib_carbon_GetListViewBounds(uint32 handle, uint32 output);
uint32 ROMlib_carbon_GetListCellIndent(uint32 handle, uint32 output);
uint32 ROMlib_carbon_GetListCellSize(uint32 handle, uint32 output);
uint32 ROMlib_carbon_GetListVisibleCells(uint32 handle, uint32 output);
uint32 ROMlib_carbon_GetListClickLocation(uint32 handle, uint32 output);
uint32 ROMlib_carbon_GetListMouseLocation(uint32 handle, uint32 output);
uint32 ROMlib_carbon_GetListDataBounds(uint32 handle, uint32 output);
void ROMlib_carbon_SetListViewBounds(uint32 handle, uint32 value);
void ROMlib_carbon_SetListCellIndent(uint32 handle, uint32 value);
void ROMlib_carbon_SetListLastClick(uint32 handle, uint32 value);
void ROMlib_carbon_SetListPort(uint32 handle, uint32 value);
void ROMlib_carbon_SetListClickTime(uint32 handle, uint32 value);
void ROMlib_carbon_SetListClickLoop(uint32 handle, uint32 value);
void ROMlib_carbon_SetListRefCon(uint32 handle, uint32 value);
void ROMlib_carbon_SetListFlags(uint32 handle, uint32 value);
void ROMlib_carbon_SetListSelectionFlags(uint32 handle, uint32 value);
void ROMlib_carbon_SetListUserHandle(uint32 handle, uint32 value);
uint32 ROMlib_carbon_GetControlHilite(uint32 handle);
uint32 ROMlib_carbon_IsControlHilited(uint32 handle);
uint32 ROMlib_carbon_GetControlDataHandle(uint32 handle);
uint32 ROMlib_carbon_GetControlOwner(uint32 handle);
void ROMlib_carbon_SetControlBounds(uint32 handle, uint32 value);
void ROMlib_carbon_SetControlDataHandle(uint32 handle, uint32 value);
uint32 ROMlib_carbon_GetControlPopupMenuHandle(uint32 handle);
uint32 ROMlib_carbon_GetControlPopupMenuID(uint32 handle);
void ROMlib_carbon_SetControlPopupMenuHandle(uint32 handle, uint32 value);
void ROMlib_carbon_SetControlPopupMenuID(uint32 handle, uint32 value);
uint32 ROMlib_carbon_GetMenuWidth(uint32 handle);
uint32 ROMlib_carbon_GetMenuHeight(uint32 handle);
void ROMlib_carbon_SetMenuID(uint32 handle, uint32 value);
void ROMlib_carbon_SetMenuWidth(uint32 handle, uint32 value);
void ROMlib_carbon_SetMenuHeight(uint32 handle, uint32 value);
uint32 ROMlib_carbon_GetMenuTitle(uint32 handle, uint32 output);
uint32 ROMlib_carbon_GetDialogCancelItem(uint32 handle);
uint32 ROMlib_carbon_GetDialogKeyboardFocusItem(uint32 handle);
uint32 ROMlib_carbon_GetDialogPort(uint32 handle);

#endif
