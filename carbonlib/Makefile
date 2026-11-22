# +===================================================================
#   File:      Makefile
#
#   Summary:   Defines build targets and dependencies for carbonlib package support.
#
#   Classes:   None.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in packages/carbonlib; original attribution and provenance comments are
#              preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

SHELL := /bin/bash
.SHELLFLAGS := -eu -o pipefail -c
.DEFAULT_GOAL := all
ROOT := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
WORKSPACE := $(abspath $(ROOT)/..)
OBJ ?= $(WORKSPACE)/build/carbonlib
ROMAW_OBJ ?= $(WORKSPACE)/system/obj
PREFIX ?= $(WORKSPACE)/win64-prefix
STAGE ?= $(WORKSPACE)/windows-binaries/bin
PPC_LIBRARY ?= $(WORKSPACE)/synppc/obj/libsynppc.a
CC = x86_64-w64-mingw32-gcc
CXX = x86_64-w64-mingw32-g++
AR = x86_64-w64-mingw32-ar
CFLAGS ?= -std=gnu17 -g -O0 -Werror=implicit-int -Werror=implicit-function-declaration
CXXFLAGS ?= -std=gnu++17 -g -O0
CPPFLAGS += -DHAVE_CONFIG_H -I$(ROOT)/include -I$(ROMAW_OBJ) -I$(OBJ) \
 -I$(WORKSPACE)/. -I$(WORKSPACE)/include \
 -I$(WORKSPACE)/frontend-sdl \
 -I$(WORKSPACE)/platform/os/mingw32 \
 -I$(WORKSPACE)/platform/arch/x86_64 \
 -I$(WORKSPACE)/platform/os/cygwin32 -I$(PREFIX)/include
library := $(OBJ)/libromaw-carbon.a
objects := $(addprefix $(OBJ)/,windows.o quickdraw.o gworld.o dialogs.o invalidation.o strings.o pascal.o atomic.o appleevents.o corefoundation.o encodings.o bundles.o urls.o cfstrings.o lowmem.o records.o cf_arrays.o cf_batch.o cf_charsets.o cf_data.o cf_numbers.o cf_set_queries.o cf_string_create.o cf_string_edit.o cf_string_encodings.o cf_string_queries.o cf_values.o cfc_runtime.o cfc_trees.o cfc_hash.o cfc_storage.o cfc_uuid.o cfc_preferences.o cfc_propertylists.o cfw_application_preferences.o cfw_preference_domains.o cfw_xml.o cfw_plugins.o cfw_bundles.o ppc_calls.o exports.o)
objects += $(addprefix $(OBJ)/,cfx_algorithms.o cfx_bundles.o cfx_charsets.o cfx_dates.o cfx_files.o cfx_float.o cfx_logs.o cfx_paths.o cfx_plists.o cfx_process.o cfx_records.o cfx_runtime.o cfx_strings.o cfx_url_services.o cfx_values.o math_native.o math_extended.o math_wide.o file_manager.o)
objects += $(OBJ)/collection_manager.o $(OBJ)/atsu.o $(OBJ)/printing.o $(OBJ)/fs_refs.o
objects += $(OBJ)/carbon_events.o $(OBJ)/carbon_scrap.o $(OBJ)/text_conversion.o $(OBJ)/encoding_converter.o
objects += $(OBJ)/threads.o
objects += $(OBJ)/sound_input.o
objects += $(OBJ)/math_decimal.o
objects += $(OBJ)/cfx_timezones.o
objects += $(OBJ)/cf_keys.o
objects += $(OBJ)/cf_singletons.o
objects += $(OBJ)/textension.o

all: $(library)
$(OBJ):
	mkdir -p $@
$(OBJ)/%.o: $(ROOT)/src/%.c $(ROOT)/include/carbonlib.h $(ROOT)/src/internal.h $(ROMAW_OBJ)/config.h $(ROOT)/Makefile | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/%.o: $(ROOT)/src/%.cpp $(ROOT)/Makefile | $(OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/math_decimal.o: $(WORKSPACE)/synppc/src/romaw/decimal.cpp $(ROOT)/Makefile | $(OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(library): $(objects)
	$(AR) rcs $@ $^
stage: all
	mkdir -p $(STAGE) $(PREFIX)/lib
	cp $(library) $(STAGE)/
	cp $(library) $(PREFIX)/lib/
$(OBJ)/classic-access-cases.inc: $(ROOT)/tests/classic_access_cases.json $(ROOT)/tests/make_classic_access_cases.py | $(OBJ)
	python3 $(ROOT)/tests/make_classic_access_cases.py $< $@
$(OBJ)/cf-values-cases.inc: $(ROOT)/plans/cf-values-batch.json $(ROOT)/tests/make_cf_values_cases.py | $(OBJ)
	python3 $(ROOT)/tests/make_cf_values_cases.py $< $@
$(OBJ)/cf-collections-cases.inc: $(ROOT)/plans/cf-collections-batch.json $(ROOT)/tests/make_cf_values_cases.py | $(OBJ)
	python3 $(ROOT)/tests/make_cf_values_cases.py $< $@ collections_cases
$(OBJ)/cf-frameworks-cases.inc: $(ROOT)/plans/cf-frameworks-batch.json $(ROOT)/tests/make_cf_values_cases.py | $(OBJ)
	python3 $(ROOT)/tests/make_cf_values_cases.py $< $@ frameworks_cases
$(OBJ)/cf-services-cases.inc: $(ROOT)/plans/cf-services-batch.json $(ROOT)/tests/make_cf_services_cases.py | $(OBJ)
	python3 $(ROOT)/tests/make_cf_services_cases.py $< $@
$(OBJ)/libromaw-carbon-test.a: $(library)
	cp $< $@
test-archive: $(OBJ)/libromaw-carbon-test.a
$(OBJ)/windows-test.exe: $(OBJ)/cf-services-cases.inc $(ROOT)/tests/cf_services_file_stubs.c $(PPC_LIBRARY)
$(OBJ)/windows-test.exe: $(ROOT)/tests/toolbox_link_stubs.c
$(OBJ)/windows-test.exe: $(OBJ)/cf-collections-cases.inc $(OBJ)/cf-frameworks-cases.inc
$(OBJ)/windows-test.exe: $(ROOT)/tests/cf_values_test.c $(ROOT)/tests/cf_values_test.h $(OBJ)/cf-values-cases.inc $(ROOT)/tests/windows_test.c $(ROOT)/tests/host_tests.c $(ROOT)/tests/pascal_test.c $(ROOT)/tests/corefoundation_test.c $(ROOT)/tests/encodings_test.c $(ROOT)/tests/encodings_test.h $(ROOT)/tests/bundle_state_test.c $(ROOT)/tests/bundle_state_test.h $(ROOT)/tests/bundle_lifecycle_test.c $(ROOT)/tests/bundle_lifecycle_test.h $(ROOT)/tests/urls_test.c $(ROOT)/tests/urls_test.h $(ROOT)/tests/cfstrings_test.c $(ROOT)/tests/cfstrings_test.h $(ROOT)/tests/classic_access_test.c $(OBJ)/classic-access-cases.inc $(ROOT)/tests/gworld_test.c $(library)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter-out $(PPC_LIBRARY),$(filter %.c %.a,$^)) $(PPC_LIBRARY) -static -lstdc++ -lquadmath -lwinmm -o $@
check: $(OBJ)/windows-test.exe
	$(OBJ)/windows-test.exe
clean:
	rm -f $(objects) $(objects:.o=.d) $(library) $(OBJ)/windows-test.exe $(OBJ)/exports-test.o $(OBJ)/exports-test.d $(OBJ)/libromaw-carbon-test.a

-include $(objects:.o=.d)
.PHONY: all stage check clean test-archive
