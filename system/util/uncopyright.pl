#!/usr/bin/perl
# +===================================================================
#   File:      uncopyright.pl
#
#   Summary:   Match multi-line regexps
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

# Match multi-line regexps
$* = 1;

$str = "";
while (<STDIN>) {
    $str .= $_;
}

if (($copyright_string) = ($str =~ /char ROMlib_copyright[^=]*=\s*\"([^\"]*)\"\;/)) {
    $copyright_string =~ s/\\\n//g;
    $copyright_string =~ s/Research and Development/Research and\n * Development/g;
    $str              =~ s/char ROMlib_copyright[^=]*=\s*\"[^\"]*\"\;/\/\* $copyright_string\n \*\//g;
}

print $str;
