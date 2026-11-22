#!/usr/bin/perl
# +===================================================================
#   File:      globals.pl
#
#   Summary:   Implements globals support for ROMAW build and development support.
#
#   Classes:   None.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in executor/src; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

@globals = <STDIN>;
$globals = join("", @globals);
$prefix  = $ARGV[0];

print "\t.space 0x10000\n";

while ($globals =~ m/\nDATA\([^,]*,([^,]*),[^,]*,([^,]*),.*\)/g) {
    print "\t.set $prefix$1, $2\n";
    print "\t.globl $prefix$1\n";
}
