#! /bin/awk -f
# +===================================================================
#   File:      mkglobs.awk
#
#   Summary:   Implements mkglobs support for ROMAW build and development support.
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

BEGIN {
  FS=",";
  print "extern uint32 ROMlib_offset;";
}

$1 ~/^DATA/ {
  gsub("DATA[(]", "", $1);
  gsub("[ 	]", "", $2);
  gsub("[ 	]", "", $4);
  if ($3 == "")
    star = "*";
  else
    star = "(*)" $3;
  print "#define " $2 " (*(" $1 " " star ")(" $4 " + ROMlib_offset))";
}
