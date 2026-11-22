# +===================================================================
#   File:      Makefile
#
#   Summary:   Windows debug and optimized cross-build entry points with separate compiler output.
#
#   Classes:   None.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in executor; original attribution and provenance comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

# Optimized by default; the debug target keeps separate compiler output.
.PHONY: all windows optimized debug configure clean native-finder test-native-finder
PYTHON ?= python3

all: windows

windows:
	+bash ./build-windows.sh

optimized:
	+ROMAW_BUILD_MODE=optimized bash ./build-windows.sh

debug:
	+ROMAW_BUILD_MODE=debug bash ./build-windows.sh

configure:
	+bash ./build-windows.sh --configure

native-finder:
	$(PYTHON) ../tools/romaw/native_finder/build.py

test-native-finder:
	$(PYTHON) ../tools/romaw/native_finder/build.py --test

clean:
	+bash ./build-windows.sh --clean
