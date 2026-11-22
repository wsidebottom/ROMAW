#!/usr/bin/perl
# +===================================================================
#   File:      maketestbattery.pl
#
#   Summary:   Implements maketestbattery support for syn68k 68K translation and runtime support.
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

print("#include \"syn68k_public.h\"\n", "#include \"testbattery.h\"\n", "\n");

while (<>) {
    if (/^TEST\s*\(([^,]*),\s*([^,]*),\s*([^,]*),\s*([^,]*),\s*([^\)]*)\s*\)/) {
        $decls .= "extern void $1 (uint16 *);\n";
        $array .= "  { \"$1\", $1, $2, $3, $4, $5 },\n";
    }
}

print($decls, "\n", "const TestInfo test_info[] = {\n", $array, "  { 0, 0, 0, 0, 0, 0 }\n", "};\n");
