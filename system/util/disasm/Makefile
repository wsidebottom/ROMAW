# +===================================================================
#   File:      Makefile
#
#   Summary:   Defines build targets and dependencies for ROMAW build and development support.
#
#   Classes:   None.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in executor/util/disasm; original attribution and provenance comments are
#              preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

OBJS = main.o disasm.o codeseg.o label.o hash.o
CC = gcc
CFLAGS = -g -O2 -Wall

disasm:	$(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o disasm

clean:
	rm -f $(OBJS) *~ disasm
