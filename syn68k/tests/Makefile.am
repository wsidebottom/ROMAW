# +===================================================================
#   File:      Makefile.am
#
#   Summary:   Defines build targets and dependencies for syn68k 68K translation and runtime support.
#
#   Classes:   None.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              syn68k/COPYING.
# ===================================================================+

noinst_PROGRAMS = syngentest

syngentest_CPPFLAGS = -DMEMORY_OFFSET=8192

  syngentest_SOURCES = main.c driver.c tests.c setup.c \
	testtrap.c crc.c testrt.c \
\
        include/crc.h include/driver.h \
        include/setup.h include/testbattery.h \
	include/testrt.h include/testtrap.h \
\
        maketestbattery.pl testall.sh

nodist_syngentest_SOURCES = testbattery.c

testbattery.c: tests.c maketestbattery.pl
	rm -f testbattery.c
	$(srcdir)/maketestbattery.pl < $(srcdir)/tests.c > testbattery.c
	chmod a-w testbattery.c

syngentest_LDADD = ../src/runtime/libsyn68k.a

syngentest_LDFLAGS = @EXECSTACK@

INCLUDES = -I$(srcdir)/include -I../src/include -I$(srcdir)/../src/include

CLEANFILES = testbattery.c

