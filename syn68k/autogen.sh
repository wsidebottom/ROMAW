#!/usr/bin/env bash
# +===================================================================
#   File:      autogen.sh
#
#   Summary:   Implements autogen support for syn68k 68K translation and runtime support.
#
#   Classes:   None.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in syn68k; original attribution and provenance comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              syn68k/COPYING.
# ===================================================================+

set -euo pipefail
cd "$(dirname "$0")"
autoreconf -fi
