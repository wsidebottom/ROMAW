/*+===================================================================
  File:      cf_batch.h

  Summary:   Shared support for the collection/value API cohort. ifndef ROMAW_CARBON_CF_BATCH_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Shared support for the collection/value API cohort. */
#ifndef ROMAW_CARBON_CF_BATCH_H
#define ROMAW_CARBON_CF_BATCH_H

#include "internal.h"

/* Native buffers consumed through guest pointers must fit a single 128-KiB
   host-alias slot. Align fixed small buffers to a power of two covering their
   byte extent; use checked guest allocations for larger/dynamic scratch data. */

int cfp_reference(const char* name, uint32 offset, const uint32* arguments, unsigned count,
                  ROMlib_carbon_reference* reference, uint32* result);
int cfp_call(const char* name, const uint32* arguments, unsigned count, uint32* result);
int cfp_private(const ROMlib_carbon_reference* reference, uint32 offset, const uint32* arguments, unsigned count,
                uint32* result);
uint32 cfp_read(uint32 address);
void* cfp_handle_data(uint32 handle, uint32* extent, int write_access);
int cfp_get(uint32 address, uint32* value);
int cfp_write(uint32 address, uint32 value);
uint32 cfp_toc(const ROMlib_carbon_reference* reference, unsigned offset);
int cfp_move(uint32 destination, uint32 source, uint32 length);
int cfp_clear(uint32 destination, uint32 length);
int cfp_failure(const char* message);
int cfp_callback(uint32 procedure, const uint32* arguments, unsigned count, uint32* result);

uint32 ROMlib_cfdata_type_id(void);
uint32 ROMlib_cfdata_create(uint32 allocator, uint32 bytes, uint32 length);
uint32 ROMlib_cfdata_create_no_copy(uint32 allocator, uint32 bytes, uint32 length, uint32 deallocator);
uint32 ROMlib_cfdata_create_copy(uint32 allocator, uint32 data);
uint32 ROMlib_cfdata_create_mutable(uint32 allocator, uint32 capacity);
uint32 ROMlib_cfdata_create_mutable_copy(uint32 allocator, uint32 capacity, uint32 data);
void ROMlib_cfdata_replace(uint32 data, uint32 location, uint32 length, uint32 bytes, uint32 count);
void ROMlib_cfdata_set_length(uint32 data, uint32 length);
void ROMlib_cfdata_increase_length(uint32 data, uint32 extra);
void ROMlib_cfdata_append(uint32 data, uint32 bytes, uint32 count);
void ROMlib_cfdata_delete(uint32 data, uint32 location, uint32 length);

uint32 ROMlib_cfvalue_boolean_type(void);
uint32 ROMlib_cfvalue_boolean(uint32 object);
uint32 ROMlib_cfvalue_date_type(void);
uint32 ROMlib_cfvalue_string_type(void);
uint32 ROMlib_cfvalue_charset_type(void);
uint32 ROMlib_cfvalue_set_type(void);
uint32 ROMlib_cfvalue_number_type_id(void);
uint32 ROMlib_cfvalue_number_type(uint32 object);
uint32 ROMlib_cfvalue_number_size(uint32 object);
uint32 ROMlib_cfvalue_number_float(uint32 object);
uint32 ROMlib_cfvalue_equal(uint32 first, uint32 second);
uint32 ROMlib_cfvalue_hash(uint32 object);
uint32 ROMlib_cfvalue_description(uint32 object);
uint32 ROMlib_cfset_count(uint32 set);
uint32 ROMlib_cfset_count_value(uint32 set, uint32 value);
uint32 ROMlib_cfset_contains(uint32 set, uint32 value);
uint32 ROMlib_cfset_value(uint32 set, uint32 value);
void ROMlib_cfset_values(uint32 set, uint32 output);

uint32 ROMlib_cfarray_create_mutable(uint32 allocator, uint32 capacity, uint32 callbacks);
uint32 ROMlib_cfarray_create_copy(uint32 allocator, uint32 source);
uint32 ROMlib_cfarray_create_mutable_copy(uint32 allocator, uint32 capacity, uint32 source);
uint32 ROMlib_cfarray_count_value(uint32 array, uint32 location, uint32 length, uint32 value);
uint32 ROMlib_cfarray_contains(uint32 array, uint32 location, uint32 length, uint32 value);
uint32 ROMlib_cfarray_first(uint32 array, uint32 location, uint32 length, uint32 value);
uint32 ROMlib_cfarray_last(uint32 array, uint32 location, uint32 length, uint32 value);
void ROMlib_cfarray_replace(uint32 array, uint32 location, uint32 length, uint32 values, uint32 count);
void ROMlib_cfarray_append(uint32 array, uint32 value);
void ROMlib_cfarray_insert(uint32 array, uint32 index, uint32 value);
void ROMlib_cfarray_set(uint32 array, uint32 index, uint32 value);
void ROMlib_cfarray_exchange(uint32 array, uint32 first, uint32 second);
void ROMlib_cfarray_remove(uint32 array, uint32 index);
void ROMlib_cfarray_remove_all(uint32 array);
void ROMlib_cfarray_append_array(uint32 array, uint32 source, uint32 location, uint32 length);
void ROMlib_cfarray_sort(uint32 array, uint32 location, uint32 length, uint32 comparator, uint32 context);

uint32 ROMlib_cfcharset_predefined(uint32 kind);
uint32 ROMlib_cfcharset_range(uint32 allocator, uint32 location, uint32 length);
uint32 ROMlib_cfcharset_create_mutable(uint32 allocator);
uint32 ROMlib_cfcharset_bitmap(uint32 allocator, uint32 data);
uint32 ROMlib_cfcharset_mutable_copy(uint32 allocator, uint32 source);
uint32 ROMlib_cfcharset_member(uint32 object, uint32 character);
uint32 ROMlib_cfcharset_create_bitmap(uint32 allocator, uint32 object);
void ROMlib_cfcharset_add_range(uint32 object, uint32 location, uint32 length);
void ROMlib_cfcharset_remove_range(uint32 object, uint32 location, uint32 length);
void ROMlib_cfcharset_add_string(uint32 object, uint32 string);
void ROMlib_cfcharset_remove_string(uint32 object, uint32 string);
uint32 ROMlib_cfcharset_string(uint32 allocator, uint32 string);
void ROMlib_cfcharset_union(uint32 object, uint32 other);
void ROMlib_cfcharset_intersect(uint32 object, uint32 other);
void ROMlib_cfcharset_invert(uint32 object);
uint32 ROMlib_cfnumber_create(uint32 allocator, uint32 type, uint32 value);
uint32 ROMlib_cfnumber_value(uint32 object, uint32 type, uint32 destination);
uint32 ROMlib_cfnumber_compare(uint32 first, uint32 second, uint32 context);
uint32 ROMlib_cfstring_bytes(uint32 allocator, uint32 bytes, uint32 length, uint32 encoding, uint32 external);
uint32 ROMlib_cfstring_cstring(uint32 allocator, uint32 bytes, uint32 encoding);
uint32 ROMlib_cfstring_cstring_no_copy(uint32 allocator, uint32 bytes, uint32 encoding, uint32 deallocator);
uint32 ROMlib_cfstring_pascal(uint32 allocator, uint32 bytes, uint32 encoding);
uint32 ROMlib_cfstring_pascal_no_copy(uint32 allocator, uint32 bytes, uint32 encoding, uint32 deallocator);
uint32 ROMlib_cfstring_characters(uint32 allocator, uint32 characters, uint32 length);
uint32 ROMlib_cfstring_characters_no_copy(uint32 allocator, uint32 characters, uint32 length, uint32 deallocator);
uint32 ROMlib_cfstring_create_mutable(uint32 allocator, uint32 capacity);
uint32 ROMlib_cfstring_mutable_copy(uint32 allocator, uint32 capacity, uint32 source);
void ROMlib_cfstring_set_external(uint32 object, uint32 characters, uint32 length, uint32 capacity);
uint32 ROMlib_cfstring_mutable_external(uint32 allocator, uint32 characters, uint32 length, uint32 capacity,
                                        uint32 deallocator);
uint32 ROMlib_cfstring_from_external(uint32 allocator, uint32 data, uint32 encoding);
uint32 ROMlib_cfstring_substring(uint32 allocator, uint32 source, uint32 location, uint32 length);
void ROMlib_cfstring_insert(uint32 object, uint32 index, uint32 source);
void ROMlib_cfstring_delete(uint32 object, uint32 location, uint32 length);
void ROMlib_cfstring_replace(uint32 object, uint32 location, uint32 length, uint32 source);
void ROMlib_cfstring_replace_all(uint32 object, uint32 source);
void ROMlib_cfstring_append(uint32 object, uint32 source);
void ROMlib_cfstring_append_characters(uint32 object, uint32 characters, uint32 count);
void ROMlib_cfstring_append_pascal(uint32 object, uint32 bytes, uint32 encoding);
void ROMlib_cfstring_append_cstring(uint32 object, uint32 bytes, uint32 encoding);

void ROMlib_cfstring_pad(uint32 object, uint32 padding, uint32 target, uint32 pad_index);

void ROMlib_cfstring_trim(uint32 object, uint32 pattern);

void ROMlib_cfstring_trim_whitespace(uint32 object);

void ROMlib_cfstring_lowercase(uint32 object, uint32 locale);

void ROMlib_cfstring_uppercase(uint32 object, uint32 locale);

void ROMlib_cfstring_capitalize(uint32 object, uint32 locale);

uint32 ROMlib_cfstring_system_encoding(void);

uint32 ROMlib_cfstring_encoding_list(void);

uint32 ROMlib_cfstring_encoding_name(uint32 encoding);

uint32 ROMlib_cfstring_encoding_iana(uint32 encoding);

uint32 ROMlib_cfstring_iana_encoding(uint32 name);

uint32 ROMlib_cfstring_smallest_encoding(uint32 object);

uint32 ROMlib_cfstring_external_representation(uint32 allocator, uint32 object, uint32 encoding, uint32 loss);

uint32 ROMlib_cfstring_find_options(uint32 string, uint32 pattern, uint32 location, uint32 length, uint32 options,
                                    uint32 output);

void ROMlib_cfstring_line_bounds(uint32 object, uint32 location, uint32 length, uint32 start_output, uint32 end_output,
                                 uint32 contents_output);

uint32 ROMlib_cfstring_find_results(uint32 allocator, uint32 string, uint32 pattern, uint32 location, uint32 length,
                                    uint32 options);

uint32 ROMlib_cfstring_separate(uint32 allocator, uint32 string, uint32 separator);

uint32 ROMlib_cfstring_combine(uint32 allocator, uint32 strings, uint32 separator);

#endif
