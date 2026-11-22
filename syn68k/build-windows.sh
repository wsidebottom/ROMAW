#!/usr/bin/env bash
# +===================================================================
#   File:      build-windows.sh
#
#   Summary:   Implements build windows support for syn68k 68K translation and runtime support.
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
root=$(cd "$(dirname "$0")" && pwd)
workspace=$(dirname "$root")
obj=${SYN68K_OBJ_DIR:-$root/obj}
obj=$(realpath -m "$obj")
case "$obj" in
  "$root/obj" | "$root/obj/"* | "$workspace/build/"*) ;;
  *)
    echo "syn68k objects must stay in syn68k/obj or workspace build/." >&2
    exit 1
    ;;
esac
target=${WINDOWS_TARGET:-x86_64-w64-mingw32}
if [[ $target != x86_64-w64-mingw32 ]]; then
  echo "syn68k supports only x86_64-w64-mingw32 (64-bit Windows)." >&2
  exit 1
fi
prefix=${WINDOWS_PREFIX:-$workspace/win64-prefix}
stage=${SYN68K_STAGE_DIR:-$workspace/windows-binaries/bin}
cc=${CC:-$target-gcc}
case ${SYN68K_DISPATCH:-switch} in
  threaded) runtime_cppflags= ;;
  switch) runtime_cppflags=-DNONDIRECT ;;
  *)
    echo "SYN68K_DISPATCH must be threaded or switch" >&2
    exit 1
    ;;
esac
build_cc=${CC_FOR_BUILD:-gcc}
mkdir -p "$obj/native-config" "$prefix/include" "$prefix/lib" "$stage"
cd "$root"
signature="$target|$cc|$build_cc|$prefix|$runtime_cppflags"
if [[ -f "$obj/Makefile" ]] \
  && [[ $(cat "$obj/.windows-build-config" 2>/dev/null || true) != "$signature" ]]; then
  make -C "$obj" clean
  rm -f "$obj/config.status"
fi
if [[ ! -f configure || configure.ac -nt configure || Makefile.am -nt Makefile.in ]] \
  || find src tests -name Makefile.am -newer configure -print -quit | grep -q .; then
  autoreconf -fi
fi
if [[ ! -f "$obj/config.status" || configure -nt "$obj/config.status" ]]; then
  (cd "$obj" && "$root/configure" --build=x86_64-pc-linux-gnu \
    --host="$target" CC="$cc" CC_FOR_BUILD="$build_cc" \
    CPPFLAGS="$runtime_cppflags" CFLAGS='-std=gnu17 -g -O0' CFLAGS_FOR_BUILD='-g -O0' \
    SYN68K_CFLAGS='-g -O2 -fomit-frame-pointer -fno-dce')
  printf '%s\n' "$signature" >"$obj/.windows-build-config"
fi
# Refresh configured headers before any generator consumes them. Recursive
# subdirectory builds do not visit the top-level configuration output rules.
make -C "$obj" src/include/config.h src/include/syn68k_private.h
if [[ ${1:-} == --configure-only ]]; then
  exit 0
fi
sed 's/^#define HAVE_SYMBOL_UNDERSCORE 1/#undef HAVE_SYMBOL_UNDERSCORE/' \
  "$obj/src/include/config.h" >"$obj/native-config/config.h"
make -C "$obj/src/syngen" -j1
# Libtool can create a target-platform wrapper for a build-platform helper.
if [[ -f "$obj/src/syngen/.libs/syngen" ]]; then
  cp "$obj/src/syngen/.libs/syngen" "$obj/src/syngen/syngen"
  chmod +x "$obj/src/syngen/syngen"
fi
make -C "$obj" -j1 \
  CC_FOR_BUILD="$build_cc -I$obj/native-config" CFLAGS_FOR_BUILD='-g -O0' \
  SYN68K_CFLAGS='-g -O2 -fomit-frame-pointer -fno-dce'
# Build the distributable generator with the target compiler separately.
"$cc" -std=gnu17 -g -O0 $runtime_cppflags -DSYNGEN -DHAVE_CONFIG_H \
  -I"$obj/src/include" -I"$root/src/include" -I"$root/src/syngen/include" \
  "$root"/src/syngen/{main,token,hash,parse,list,reduce,macro,boolean,error,defopcode,bitstring,generatecode,uniquestring,byteorder}.c \
  -o "$obj/syngen.exe"
cp "$obj/src/runtime/libsyn68k.a" "$prefix/lib/"
if ! cmp -s "$root/src/include/syn68k_public.h" "$prefix/include/syn68k_public.h"; then
  cp "$root/src/include/syn68k_public.h" "$prefix/include/"
fi
cp "$obj/syngen.exe" "$obj/src/runtime/libsyn68k.a" "$stage/"
