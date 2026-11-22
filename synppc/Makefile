# +===================================================================
#   File:      Makefile
#
#   Summary:   Defines build targets and dependencies for synppc PowerPC execution and fragment loading.
#
#   Classes:   None.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in synppc; original attribution and provenance comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

SHELL := /bin/bash
.SHELLFLAGS := -eu -o pipefail -c
.DEFAULT_GOAL := all

# Portable PowerPC/PEF runtime, built independently of ROMAW's sources.
ROOT := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
WORKSPACE := $(abspath $(ROOT)/..)
OBJ ?= $(ROOT)/obj
CORE := $(ROOT)/third_party/sheepshaver
SOFTFLOAT := $(ROOT)/third_party/softfloat3
ROMAW_ROOT ?= $(WORKSPACE)/system
ROMAW_OBJ ?= $(ROMAW_ROOT)/obj
PREFIX ?= $(WORKSPACE)/win64-prefix
STAGE ?= $(WORKSPACE)/windows-binaries/bin
WINDOWS_TARGET ?= x86_64-w64-mingw32
ifneq ($(WINDOWS_TARGET),x86_64-w64-mingw32)
$(error synppc supports only x86_64-w64-mingw32 (64-bit Windows))
endif
CXX = $(or $(PPC_CXX),$(WINDOWS_TARGET)-g++)
CC = $(WINDOWS_TARGET)-gcc
AR = $(WINDOWS_TARGET)-ar
PYTHON ?= python3
CPPFLAGS += -I$(ROOT)/platform -I$(ROOT)/include -I$(CORE)/include -I$(CORE)/src -I$(OBJ)
CXXFLAGS = -std=gnu++11 -g -O2 -fno-strict-aliasing -Wno-deprecated-declarations
BRIDGE_CFLAGS ?= -std=gnu17 -g -O0 -Werror=implicit-int -Werror=implicit-function-declaration
BRIDGE_CPPFLAGS = -DHAVE_CONFIG_H -I$(ROOT)/include -I$(OBJ) \
 -I$(WORKSPACE)/carbonlib/include \
 -I$(ROMAW_OBJ) -I$(WORKSPACE) -I$(WORKSPACE)/include \
 -I$(WORKSPACE)/frontend-sdl -I$(WORKSPACE)/platform/os/mingw32 \
 -I$(WORKSPACE)/platform/arch/x86_64 -I$(WORKSPACE)/platform/os/cygwin32 \
 -I$(PREFIX)/include
objects = $(addprefix $(OBJ)/,ppc-cpu.o ppc-decode.o ppc-execute.o ppc-runtime.o ppc-system.o mmu750.o system750.o bus750.o cache750.o l2cache750.o ppc-jit.o ppc-fused.o pef-loader.o ppc-printf.o mathlib.o)
softfloat_sources = $(wildcard $(SOFTFLOAT)/source/*.c $(SOFTFLOAT)/source/ARM-VFPv2/*.c)
objects += $(patsubst $(SOFTFLOAT)/source/%.c,$(OBJ)/softfloat/%.o,$(softfloat_sources))
objects += $(OBJ)/ppc-arithmetic.o
objects += $(OBJ)/ppc-native-memory-code.o
SOFTFLOAT_CFLAGS ?= -std=c11 -g -O2 -fno-strict-aliasing -Werror=implicit-function-declaration
library = $(OBJ)/libsynppc.a
bridge = $(OBJ)/libsynppc-romaw.a

all: $(library)
$(OBJ):
	mkdir -p $@
$(library): $(objects)
	$(AR) rcs $@ $^
$(objects): $(ROOT)/Makefile
$(OBJ)/ppc-execute-impl.cpp: $(CORE)/src/cpu/ppc/ppc-decode.cpp $(CORE)/src/cpu/ppc/genexec.pl $(ROOT)/platform/sysdeps.h $(ROOT)/Makefile | $(OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DGENEXEC -E $< | perl $(CORE)/src/cpu/ppc/genexec.pl > $@.tmp
	mv $@.tmp $@
$(OBJ)/ppc-execute.o: $(OBJ)/ppc-execute-impl.cpp
$(OBJ)/ppc-%.o: $(CORE)/src/cpu/ppc/ppc-%.cpp | $(OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/ppc-runtime.o: $(ROOT)/src/ppc-runtime.cpp | $(OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/ppc-system.o: $(ROOT)/src/ppc-system.cpp | $(OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
# Cache compatibility follows all native helpers/layouts and effective flags.
.PHONY: ppc-build-id-force
ppc-build-id-force:
$(OBJ)/generated/ppc-jit-build-id.h: ppc-build-id-force | $(OBJ)
	$(PYTHON) $(ROOT)/tools/ppc-build-id.py $@ "$(CXX)" "$(CPPFLAGS) $(CXXFLAGS) $(SOFTFLOAT_CFLAGS)"
$(OBJ)/system750.o: $(ROOT)/src/system750.cpp | $(OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/bus750.o: $(ROOT)/src/bus750.cpp | $(OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/cache750.o: $(ROOT)/src/cache750.cpp | $(OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/l2cache750.o: $(ROOT)/src/l2cache750.cpp | $(OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(OBJ)/mmu750.o: $(ROOT)/src/mmu750.cpp | $(OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/generated/ppc-native-fused.h: $(ROOT)/tools/ppc-native-fused.py $(ROOT)/src/ppc-native-fused.cpp $(ROOT)/src/ppc-native-fused-state.h $(ROOT)/src/ppc-fused-integer.h $(ROOT)/src/ppc-fused.h $(ROOT)/Makefile | $(OBJ)
	$(PYTHON) $(ROOT)/tools/ppc-native-fused.py $@ "$(CXX)"
$(OBJ)/ppc-jit.o: $(ROOT)/src/ppc-jit.cpp $(OBJ)/generated/ppc-jit-build-id.h $(OBJ)/generated/ppc-native-fused.h | $(OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
native_memory_sources = $(addprefix $(ROOT)/src/,mmu750.cpp cache750.cpp l2cache750.cpp bus750.cpp system750.cpp ppc-native-memory.cpp)
$(OBJ)/generated/ppc-native-memory.h: $(ROOT)/tools/ppc-native-memory.py $(WORKSPACE)/tools/native_code_image.py $(native_memory_sources) $(wildcard $(ROOT)/include/synppc/*.h) $(ROOT)/src/ppc-native-memory.h $(ROOT)/src/ppc-physical-bus.h $(ROOT)/Makefile | $(OBJ)
	$(PYTHON) $(ROOT)/tools/ppc-native-memory.py $@ "$(CXX)"
$(OBJ)/ppc-native-memory-code.o: $(ROOT)/src/ppc-native-memory-code.cpp $(OBJ)/generated/ppc-native-memory.h | $(OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/ppc-fused.o: $(ROOT)/src/ppc-fused.cpp | $(OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/ppc-arithmetic.o: $(ROOT)/src/ppc-arithmetic.cpp | $(OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/softfloat/%.o: $(SOFTFLOAT)/source/%.c $(ROOT)/platform/softfloat/platform.h | $(OBJ)
	mkdir -p $(dir $@)
	$(CXX) -x c $(SOFTFLOAT_CFLAGS) -DSOFTFLOAT_FAST_INT64 -DSOFTFLOAT_ROUND_ODD -DINLINE_LEVEL=5 \
	 -DSOFTFLOAT_FAST_DIV32TO16 -DSOFTFLOAT_FAST_DIV64TO32 \
	 -I$(ROOT)/platform/softfloat -I$(SOFTFLOAT)/source/ARM-VFPv2 -I$(SOFTFLOAT)/source/include \
	 -MMD -MP -c $< -o $@
$(OBJ)/ppc-printf.o: $(ROOT)/src/ppc-printf.cpp | $(OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/pef-loader.o: $(ROOT)/src/pef-loader.cpp | $(OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/mathlib.o: $(CORE)/src/mathlib/mathlib.cpp | $(OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

# Optional host adapter. Only this target requires a configured ROMAW tree.
romaw-bridge: $(bridge)
$(OBJ)/generated/ppc-toolbox.inc: $(ROOT)/tools/gen-toolbox.py $(WORKSPACE)/runtime/emutraptables.c | $(OBJ)
	mkdir -p $(OBJ)/generated
	$(PYTHON) $^ $@
$(OBJ)/romaw-bridge.o: $(ROOT)/src/romaw/bridge.c $(ROMAW_OBJ)/config.h $(OBJ)/generated/ppc-toolbox.inc $(ROOT)/Makefile | $(OBJ)
	$(CC) $(BRIDGE_CPPFLAGS) $(BRIDGE_CFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/romaw-memory-manager.o: $(ROOT)/src/romaw/memory-manager.c $(ROOT)/src/romaw/memory-manager.h $(ROOT)/Makefile | $(OBJ)
	$(CC) $(BRIDGE_CPPFLAGS) $(BRIDGE_CFLAGS) -MMD -MP -c $< -o $@
$(bridge): $(OBJ)/romaw-bridge.o $(OBJ)/romaw-memory-manager.o
	$(AR) rcs $@ $^
	$(AR) d $@ romaw-threads.o romaw-decimal.o

check: all
	SYN_PPC_SKIP_BUILD=1 CXX="$(CXX)" OBJ="$(OBJ)" bash $(ROOT)/tests/check-core.sh core
stage: all
	mkdir -p $(PREFIX)/lib $(PREFIX)/include/synppc $(STAGE)/licenses
	cp $(library) $(PREFIX)/lib/
	cp $(library) $(STAGE)/
	cp $(ROOT)/include/synppc/*.h $(PREFIX)/include/synppc/
	cp $(SOFTFLOAT)/COPYING.txt $(STAGE)/licenses/softfloat3.txt
stage-romaw: stage romaw-bridge
	cp $(bridge) $(PREFIX)/lib/
	cp $(bridge) $(STAGE)/
clean:
	rm -f $(OBJ)/romaw-memory-manager.o $(OBJ)/romaw-memory-manager.d
	rm -f $(objects) $(objects:.o=.d) $(library) $(bridge) $(OBJ)/romaw-bridge.o $(OBJ)/romaw-bridge.d $(OBJ)/romaw-threads.o $(OBJ)/romaw-threads.d $(OBJ)/romaw-decimal.o $(OBJ)/romaw-decimal.d
	rm -f $(OBJ)/ppc-execute-impl.cpp $(OBJ)/ppc-execute-impl.cpp.tmp $(OBJ)/generated/ppc-toolbox.inc
	rm -f $(OBJ)/ppc-runtime-test.exe $(OBJ)/pef-loader-test.exe

-include $(objects:.o=.d) $(OBJ)/romaw-bridge.d $(OBJ)/romaw-memory-manager.d
.PHONY: all romaw-bridge check stage stage-romaw clean
