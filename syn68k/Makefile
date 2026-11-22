# +===================================================================
#   File:      Makefile
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

.PHONY: all windows configure check clean

all windows:
	bash ./build-windows.sh

configure:
	bash ./build-windows.sh --configure-only

check: all
	bash ./tests/check-core.sh mmu040
	bash ./tests/check-core.sh fpu --oracle
	bash ./tests/check-core.sh control-register
	bash ./tests/testall.sh

clean:
	if test -f obj/Makefile; then $(MAKE) -C obj clean; fi
