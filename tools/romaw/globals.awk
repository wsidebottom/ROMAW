# +===================================================================
#   File:      globals.awk
#
#   Summary:   Implements globals support for ROMAW build and development support.
#
#   Classes:   None.
#
#   Functions: Pattern/action entry points.
#
#   Origin:    Existing repository source in executor/src; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

BEGIN	{ FS=","; printf(".text\n.space 0x10000\n") }
$1 ~ /^DATA/ && NF==7	{ print ".set _" $2 ", " $4 ; print ".globl _" $2 }
