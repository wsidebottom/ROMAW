# +===================================================================
#   File:      Makefile
#
#   Summary:   Coordinate Windows x64 static-library, application and regression builds from the repository root.
#
#   Classes:   None.
#
#   Functions: GNU Make entry points; no exported functions.
#
#   Origin:    Repository-level coordination of the existing CPU, Executor and package build entry points.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

SHELL := /bin/bash
.DEFAULT_GOAL := all
.NOTPARALLEL:
JOBS ?= 1
WINDOWS_TARGET ?= x86_64-w64-mingw32
export JOBS WINDOWS_TARGET
ROMAW_BUILD_MODE ?= optimized
profile_obj = $(if $(filter debug,$(ROMAW_BUILD_MODE)),system/obj,build/romaw-optimized/obj)
profile_ppc_obj = $(if $(filter debug,$(ROMAW_BUILD_MODE)),synppc/obj,build/romaw-optimized/ppc)
profile_archive_suffix = $(if $(filter optimized,$(ROMAW_BUILD_MODE)),/optimized,)
profile_cmake_type = $(if $(filter debug,$(ROMAW_BUILD_MODE)),Debug,RelWithDebInfo)
export ROMAW_BUILD_MODE

.PHONY: all cpu romaw libraries library-syn68k library-synppc library-classic-ui library-images \
  library-finder-metadata library-finder list-libraries check check-all check-build check-libraries \
  check-runtime check-cpu check-ppc check-ui clean help debug optimized
all: romaw

cpu:
	+$(MAKE) -C syn68k

romaw: cpu
	+$(MAKE) -C system


optimized: cpu
	+$(MAKE) -C system optimized

debug: cpu
	+$(MAKE) -C system debug

libraries: cpu
	+bash system/build-windows.sh --libraries

library-syn68k: cpu

library-synppc:
	+$(MAKE) -C synppc OBJ="$(abspath $(or $(PPC_OBJ_DIR),$(profile_ppc_obj)))"

package_classic-ui = classic-ui
package_images = disk-images
package_finder-metadata = finder-metadata
library-classic-ui library-images library-finder-metadata:
	cmake -S "$(package_$(@:library-%=%))" -B "$(or $(ROMAW_OBJ_DIR),$(profile_obj))/$(@:library-%=%)" \
	  -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER="$(WINDOWS_TARGET)-gcc" \
	  -DCMAKE_CXX_COMPILER="$(WINDOWS_TARGET)-g++" -DCMAKE_BUILD_TYPE=$(profile_cmake_type) \
	  "-DCMAKE_C_FLAGS_RELWITHDEBINFO=-O2 -g -fno-strict-aliasing" \
	  "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O2 -g -fno-strict-aliasing"
	cmake --build "$(or $(ROMAW_OBJ_DIR),$(profile_obj))/$(@:library-%=%)" --target "romaw-$(@:library-%=%)" --parallel $(JOBS)

library-finder:
	python3 tools/romaw/native_finder/build.py --output-dir "$(or $(ROMAW_OBJ_DIR),$(profile_obj))/native-finder/windows-x64"

library-%: cpu
	+bash system/build-windows.sh --library "$*"

list-libraries: cpu
	+$(MAKE) -C system configure
	+$(MAKE) --no-print-directory -s -C "$(or $(ROMAW_OBJ_DIR),$(profile_obj))" library-manifest

check-libraries: libraries
	python3 tools/check-static-libraries.py --obj "$(or $(ROMAW_OBJ_DIR),$(profile_obj))" \
	  --stage "$${ROMAW_LIBRARY_STAGE_DIR:-$${ROMAW_STAGE_DIR:-windows-binaries/bin}/libraries/x64$(profile_archive_suffix)}"

check-build check-runtime check-ppc check-ui: romaw
	python3 tools/run-checks.py "$(@:check-%=%)"

check-cpu: cpu
	python3 tools/run-checks.py cpu

check: romaw
	python3 tools/run-checks.py standard

check-all: romaw
	python3 tools/run-checks.py all

clean:
	+$(MAKE) -C system clean
	+$(MAKE) -C syn68k clean

help:
	@printf '%s\n' 'Run in WSL from the repository root; set JOBS=10 for parallel compilation.' \
	  'make                 Build and stage optimized ROMAW.exe and static libraries.' \
	  'make debug           Build and stage ROMAW-debug.exe with -O0 and debug symbols.' \
	  'make libraries       Build and stage all runtime archives without linking ROMAW.exe.' \
	  'make library-NAME    Build one ROMAW module without linking the application.' \
	  'make list-libraries  Show configured ROMAW archive ownership.' \
	  'make check-libraries Validate source ownership, archive contents and rebuild propagation.' \
	  'make check           Run build and runtime suites after one prerequisite build.' \
	  'make check-all       Run all five suites: build, runtime, CPU, PPC and UI.' \
	  'make check-SUITE     Run one of build, runtime, cpu, ppc or ui.'
