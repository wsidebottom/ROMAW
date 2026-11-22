#!/usr/bin/perl -ni
# +===================================================================
#   File:      nukeincl.pl
#
#   Summary:   Implements nukeincl support for ROMAW build and development support.
#
#   Classes:   None.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in executor/util; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

s/blockinterrupts_t/virtual_int_state_t/g;
s/unblockinterrupts\s*\(\&([\w\d]+)\)/restore_virtual_ints ($1)/g;
s/blockinterrupts\s*\(\&([\w\d]+)\)/$1 = block_virtual_ints ()/g;

print;
