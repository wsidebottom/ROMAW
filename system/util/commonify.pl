#!/usr/bin/perl
# +===================================================================
#   File:      commonify.pl
#
#   Summary:   Implements commonify support for ROMAW build and development support.
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

while (<STDIN>) {
    if (!$found && /^\#include/) {
        print "#include \"rsys/common.h\"\n";
        $found = 1;
    }
    print $_;
}
