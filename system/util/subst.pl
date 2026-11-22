#!/usr/bin/perl
# +===================================================================
#   File:      subst.pl
#
#   Summary:   useage: subst.pl pattern:file ...
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

# useage:
# subst.pl pattern:file ...

while (<STDIN>) {
    @args = @ARGV;

    foreach $arg (@args) {
        ($current_pattern, $current_file) = split(':', $arg);
        if (/$current_pattern/) {
            open(F, $current_file) || die $!;
            while (<F>) {
                print $_;
            }
            close(F);
            goto end_line;
        }
    }
    print $_;
    end_line:
}

