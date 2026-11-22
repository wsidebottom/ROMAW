# +===================================================================
#   File:      ostrap.awk
#
#   Summary:   Implements ostrap support for ROMAW build and development support.
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

substr($1, 4, 1) ~ /[0-7]/ && NF >= 2 	{ print "0x00" substr($1, 5, 2), $2 }
