#!/bin/bash
# +===================================================================
#   File:      cleanup.sh
#
#   Summary:   Implements cleanup support for ROMAW build and development support.
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

find /ardi/. -type f -name '*.[chm]' -print | xargs co -l -f -q
find /ardi/. -type f -name '*.[chm]' -print | xargs /ardi/romaw/util/uncruft.pl
find /ardi/. -type f -name '*.[chm]' -exec /ardi/executor/util/temp-hack.sh /ardi/executor/util/uncopyright.pl '{}' ';'
find /ardi/. -type f -name '*.[cm]' -exec /ardi/romaw/util/temp-hack.sh /ardi/romaw/util/commonify.pl '{}' ';'
