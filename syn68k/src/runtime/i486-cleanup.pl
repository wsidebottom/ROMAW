#!/usr/bin/perl -ni
# +===================================================================
#   File:      i486-cleanup.pl
#
#   Summary:   Filters redundant assembly around generated syn68k handlers, retaining required code and debug sections.
#
#   Classes:   None.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in syn68k/src/runtime; original attribution and provenance comments are
#              preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              syn68k/COPYING.
# ===================================================================+

# This perl script strips out the extra cruft created as a side effect of
# splitting syn68k.c into many functions.  This will make the resulting
# syn68k.o file smaller and also force the real branch targets to be
# aligned optimally.

# Opcode names also occur in DWARF strings and references.  Those bytes must
# remain intact: deleting them leaves compilation-unit lengths and offsets
# pointing into unrelated data.  Only executable assembly needs cleanup.
if (/^\s*\.section\s+([.\w]+)/) {
    $debug_section = ($1 =~ /^\.(?:z?debug|stab)/);
} elsif (/^\s*\.(?:text|data|bss)\b/) {
    $debug_section = 0;
}
if ($debug_section) {
    print;
    next;
}

# Delete the header for each function
if (/^\.globl _?s68k_handle_opcode_0x/ .. /^[#\/]APP$|^_?S68K_HANDLE_0x....:$/) {
    print if (/^[#\/]APP/ || /^L/ || /^_?S68K_HANDLE_0x....:$/);
}

# Delete the trailer for each function
elsif (/_S68K_DONE_WITH/ .. /^\s*ret$|^\s*jmp\s+_s68k_handle_opcode_dummy$/) {
    print
      if ( !/\s*movl %ebp,%esp$/
        && !/\s*leal\s*(-?\d+)?\(%ebp\),%esp$/
        && !/\s*movl\s*(-?\d+)?\(%ebp\),%edi$/
        && !/\s*popl/
        && !/\s*ret$/
        && !/^_S68K_DONE_WITH/
        && !/s68k_handle_opcode/
        && !/^\s*jmp\s+_s68k_handle_opcode_dummy$/);
} elsif (/^\s*lods/) {
    die "lods not allowed; we're punting cld's these days.";
}

# elsif (/^\s*call/ && !/s68k_handle_opcode/)
# {  # no longer necessary since we punted lodsl
#     print "$_\tcld\n";
# }
elsif (!/s68k_handle_opcode/) {
    print;
}
