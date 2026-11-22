#!/bin/sh
# +===================================================================
#   File:      move-if-changed.sh
#
#   Summary:   Like mv $1 $2, but if the files are the same, just delete $1. Status is 0 if $2 is changed, 1 otherwise.
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

# Like mv $1 $2, but if the files are the same, just delete $1.
# Status is 0 if $2 is changed, 1 otherwise.
if
  test -r $2
then
  if
    cmp -s $1 $2
  then
    echo $2 is unchanged
    rm -f $1
  else
    mv -f $1 $2
  fi
else
  mv -f $1 $2
fi
