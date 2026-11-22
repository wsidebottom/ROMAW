#!/usr/bin/perl -ni
# +===================================================================
#   File:      unmacify.pl
#
#   Summary:   Implements unmacify support for ROMAW build and development support.
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

s/\r/\n/g;
print;
