# +===================================================================
#   File:      autogen.sh
#
#   Summary:   Implements autogen support for ROMAW build and development support.
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

# /bin/bash

# $Id: autogen.sh 63 2004-12-24 18:19:43Z ctm $

set -o errexit -o nounset -o noclobber

main() {
  cd "$(dirname "$0")/../.."
  aclocal
  autoheader
  automake --add-missing -Wall
  autoconf
  echo "Run make from the repository root; configuration and objects belong in system/obj."
}

main "$@"
