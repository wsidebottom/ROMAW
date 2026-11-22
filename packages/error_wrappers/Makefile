# +===================================================================
#   File:      Makefile
#
#   Summary:   Defines build targets and dependencies for error_wrappers package support.
#
#   Classes:   None.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in packages/error_wrappers; original attribution and provenance comments are
#              preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

CFLAGS=-g -Wall -ansi -pedantic -O

test:	test.o error.o
	$(CC) -o test test.o error.o

test.o error.o:	error.h MacTypes.h Makefile
