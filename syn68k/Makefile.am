# +===================================================================
#   File:      Makefile.am
#
#   Summary:   Defines build targets and dependencies for syn68k 68K translation and runtime support.
#
#   Classes:   None.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in syn68k; original attribution and provenance comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              syn68k/COPYING.
# ===================================================================+

# $Id: Makefile.am 61 2004-12-21 23:44:50Z ctm $
ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src/syngen src/runtime tests
BUILT_SOURCES = src/include/syn68k_private.h

DIST_SOURCES = src/include/safe_alloca.h src/include/syn68k_public.h

EXTRA_DIST = README.md build-windows.sh \
  docs/68040-status.md src/runtime/fpu.c src/runtime/include/fpu_exception.h \
  src/runtime/include/fpu_arithmetic.h \
  third_party/softfloat/README.md \
  third_party/softfloat/softfloat.c \
  third_party/softfloat/softfloat.h \
  third_party/softfloat/softfloat-compat.h \
  third_party/softfloat/softfloat-macros \
  third_party/softfloat/softfloat-specialize \
  third_party/mame/README.md third_party/mame/LICENSE \
  tests/fpu-test.c tests/fpu-oracle.S tests/fpu-oracle.c \
  tests/fpu-oracle-runner.c tests/compare-fpu-oracle.py tests/check-core.sh \
  tests/fpu-division-test.c tests/test-fpu-division.py \
  tests/fpu-arithmetic-test.c tests/test-fpu-arithmetic.py

EXTRA_DIST += tools/build-fpsp.py \
  tests/fpsp-test.c tests/fpsp-reference.py \
  $(patsubst $(srcdir)/%,%,$(wildcard $(srcdir)/third_party/fpsp040/*))

EXTRA_DIST += tools/jit-build-id.py docs/aot-x64.md \
  tests/aot-x64-cache-test.c tests/aot-x64-execution-test.c tests/jit-build-id-test.py
