#!/usr/bin/env bash
# +===================================================================
#   File:      build-windows-sdl.sh
#
#   Summary:   Build the SDL 1.2 dependency for the selected Windows target.
#
#   Classes:   None.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in tools; original attribution and provenance comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

# Build the SDL 1.2 dependency for the selected Windows target.
set -euo pipefail
workspace=$(cd "$(dirname "$0")/.." && pwd)
target=${WINDOWS_TARGET:-x86_64-w64-mingw32}
if [[ $target != x86_64-w64-mingw32 ]]; then
  echo "ROMAW SDL supports only x86_64-w64-mingw32 (64-bit Windows)." >&2
  exit 1
fi
prefix=${WINDOWS_PREFIX:-$workspace/win64-prefix}
obj="$workspace/build/sdl-$target"
mkdir -p "$obj"
cd "$obj"
archive=SDL-1.2.15.tar.gz
if [[ ! -f "$archive" ]]; then
  curl -fL "https://www.libsdl.org/release/$archive" -o "$archive"
fi
echo "d6d316a793e5e348155f0dd93b979798933fb98aa1edebcc108829d6474aad00  $archive" | sha256sum -c -
if [[ ! -d SDL-1.2.15 ]]; then
  tar -xzf "$archive"
fi
mkdir -p obj
cd obj
../SDL-1.2.15/configure --host="$target" --prefix="$prefix" \
  --disable-video-directx --disable-joystick --disable-cdrom
make -j"${JOBS:-1}"
make install

# Resolve SDL discovery paths from the installed helpers after a checkout move.
python3 - "$prefix" <<'PY'
from pathlib import Path
import re
import sys

prefix = Path(sys.argv[1])
helpers = [
    (prefix / "bin/sdl-config", 'prefix=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)'),
    (prefix / "lib/pkgconfig/sdl.pc", "prefix=${pcfiledir}/../.."),
]
for path, replacement in helpers:
    text, count = re.subn(r"^prefix=.*$", lambda match: replacement, path.read_text(), count=1, flags=re.MULTILINE)
    if count != 1:
        raise ValueError(f"Missing SDL prefix declaration in {path}")
    path.write_text(text, newline="\n")
PY
