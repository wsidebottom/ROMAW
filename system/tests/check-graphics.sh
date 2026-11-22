#!/usr/bin/env bash
# +===================================================================
#   File:      check-graphics.sh
#
#   Summary:   Run graphics regression checks independently.
#
#   Classes:   None.
#
#   Functions: Script entry point and named regression checks; use --list.
#
#   Origin:    Consolidated repository regression checks; original file-specific notices remain below.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

check=${1:---list}
if [ "$#" -gt 0 ]; then shift; fi
case "$check" in
  --list | list)
    printf '%s\n' 'app-icon' 'bitmap-region' 'desk-shape' 'dib' 'gworld' 'ico' 'icon-lifetime' 'palette' 'picture-clip' 'picture-header' 'pixmap-copy' 'quickdraw-host' 'sdl-gpu' 'sdl-pixel' 'text-scale' 'toolbox-benchmark' 'win-font'
    ;;
  toolbox-benchmark)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      x86_64-w64-mingw32-gcc -std=gnu17 -g -O2 -Wall -Wextra -Werror -shared -static-libgcc \
        -I"$root/include" "$root/system/tests/toolbox_benchmark.c" -o "$root/build/toolbox-benchmark.dll"
      if [[ ${1:-} == --build-only ]]; then exit 0; fi
      python3 "$root/system/tests/compare-builds.py" "$@"
    )
    ;;
  sdl-gpu)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      prefix=${WINDOWS_PREFIX:-$root/win64-prefix}
      mkdir -p "$root/build"
      "${CXX:-x86_64-w64-mingw32-g++}" -std=gnu++17 -g -O0 -Wall -Wextra -Werror \
        -I"$root/include" -I"$prefix/include" -I"$root/frontend-sdl" \
        "$root/system/tests/sdl_gpu_test.cpp" -L"$prefix/lib" -lSDL -luser32 \
        -static-libgcc -static-libstdc++ -o "$root/build/sdl-gpu-test.exe"
      cd "$root/windows-binaries/bin"
      "$root/build/sdl-gpu-test.exe" "$@"
    )
    ;;
  app-icon)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      "${CC:-cc}" -std=gnu17 -g -O0 -Wall -Wextra -Werror \
        -I"$root/include" "$root/system/tests/app_icon_test.c" \
        -o "$root/build/app-icon-test"
      "$root/build/app-icon-test"
    )
    ;;
  bitmap-region)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      cd "$root"
      x86_64-w64-mingw32-gcc -std=gnu17 -g -O0 \
        -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
        -Wl,--gc-sections -DHAVE_CONFIG_H -I"$root/system/obj" -I. -Iinclude \
        -I"$root/system/obj/generated" \
        -Ifrontend-sdl -Iplatform/os/cygwin32 -Iplatform/arch/x86_64 \
        -I"$root/win64-prefix/include" system/tests/bitmap_region_test.c \
        -o "$root/build/bitmap-region-test.exe"
      "$root/build/bitmap-region-test.exe"
    )
    ;;
  desk-shape)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      x86_64-w64-mingw32-gcc -std=gnu17 -g -O0 -ffunction-sections -fdata-sections \
        -fno-asynchronous-unwind-tables -Wl,--gc-sections -DHAVE_CONFIG_H \
        -I"$root/system/obj" -I. -Iinclude -Ifrontend-sdl -Iplatform/os/mingw32 \
        -Iplatform/arch/x86_64 -Iplatform/os/cygwin32 -I"$root/win64-prefix/include" \
        system/tests/desk_shape_test.c -L"$root/win64-prefix/lib" -lSDL -lgdi32 -luser32 \
        -o "$root/build/desk-shape-test.exe"
      cd "$root/windows-binaries/bin"
      "$root/build/desk-shape-test.exe"
    )
    ;;
  dib)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      target=${WINDOWS_TARGET:-x86_64-w64-mingw32}
      cc=${CC:-$target-gcc}
      prefix=${WINDOWS_PREFIX:-$root/win64-prefix}
      obj=${ROMAW_OBJ_DIR:-$root/system/obj}
      stage=${ROMAW_STAGE_DIR:-$root/windows-binaries/bin}
      case "$target" in
        x86_64-w64-mingw32) arch=x86_64 ;;
        *)
          echo "Tests require the Windows x64 target." >&2
          exit 1
          ;;
      esac
      cd "$root"
      "$cc" -std=gnu17 -g -O0 -ffunction-sections -fdata-sections \
        -fno-asynchronous-unwind-tables -Wl,--gc-sections -DHAVE_CONFIG_H \
        -I"$obj" -I. -Iinclude -Ifrontend-sdl -Iplatform/os/mingw32 \
        -I"platform/arch/$arch" -Iplatform/os/cygwin32 -I"$prefix/include" \
        system/tests/dib_decode_test.c -L"$prefix/lib" -lSDL -o "$root/build/dib-decode-test.exe"
      cd "$stage"
      "$root/build/dib-decode-test.exe"
    )
    ;;
  gworld)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      target=${WINDOWS_TARGET:-x86_64-w64-mingw32}
      prefix=${WINDOWS_PREFIX:-$root/win64-prefix}
      cc=${CC:-$target-gcc}
      case "$target" in
        x86_64-w64-mingw32) arch=x86_64 ;;
        *) exit 1 ;;
      esac
      "$cc" -std=gnu17 -g -O0 -ffunction-sections -fdata-sections \
        -fno-asynchronous-unwind-tables -Wl,--gc-sections -DHAVE_CONFIG_H \
        -Isystem/obj -I. -Iinclude -Ifrontend-sdl -Iplatform/os/mingw32 \
        -I"platform/arch/$arch" -Iplatform/os/cygwin32 -I"$prefix/include" \
        system/tests/gworld_output_test.c -o "$root/build/gworld-output-test.exe"
      "$root/build/gworld-output-test.exe"
    )
    ;;
  ico)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      python3 "$root/system/tests/icns_assets_test.py"
      mkdir -p "$root/build"
      cd "$root"
      x86_64-w64-mingw32-gcc -std=gnu17 -g -O0 \
        -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
        -Wl,--gc-sections -DHAVE_CONFIG_H \
        -Werror=implicit-int -Werror=implicit-function-declaration \
        -I"$root/system/obj" -I. -Iinclude -Ifrontend-sdl \
        -Iplatform/os/mingw32 -Iplatform/arch/x86_64 -Iplatform/os/cygwin32 \
        -I"$root/win64-prefix/include" \
        system/tests/image_modes_test.c -o "$root/build/image-modes-test.exe"
      "$root/build/image-modes-test.exe"
      x86_64-w64-mingw32-gcc -std=gnu17 -g -O0 -Wall -Wextra -Werror \
        system/tests/ico_windows_test.c -o "$root/build/ico-windows-test.exe" -luser32 -lgdi32
      generated=$(mktemp -d "$root/build/windows-icons.XXXXXX")
      assets=()
      while IFS= read -r -d '' asset; do
        target="$generated/$(basename "${asset%.icns}").ico"
        python3 "$root/tools/mac_icons.py" "$asset" "$target"
        assets+=("$(wslpath -w "$target")")
      done < <(find "$root/bitmaps" -type f -name '*.icns' ! -name '._*' -print0)
      "$root/build/ico-windows-test.exe" "${assets[@]}"
    )
    ;;
  icon-lifetime)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      cd "$root"
      for test in icon_lifetime icon_dispose icon_action; do
        x86_64-w64-mingw32-gcc -std=gnu17 -g -O0 \
          -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
          -Wl,--gc-sections -DHAVE_CONFIG_H -I"$root/system/obj" -I. -Iinclude \
          -Ifrontend-sdl -Iplatform/os/cygwin32 -Iplatform/arch/x86_64 \
          -I"$root/win64-prefix/include" "system/tests/${test}_test.c" \
          -o "$root/build/${test//_/-}-test.exe"
        "$root/build/${test//_/-}-test.exe"
      done
    )
    ;;
  palette)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      cd "$root"
      target=${WINDOWS_TARGET:-x86_64-w64-mingw32}
      prefix="$root/win64-prefix"
      arch=x86_64
      if [[ $target != x86_64-w64-mingw32 ]]; then
        echo "Tests require the Windows x64 target." >&2
        exit 1
      fi
      "$target-gcc" -std=gnu17 -g -O0 \
        -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
        -fno-jump-tables -Wl,--gc-sections -DHAVE_CONFIG_H \
        -I"$root/system/obj" -I. -Iinclude -Ifrontend-sdl \
        -Iplatform/os/mingw32 -I"platform/arch/$arch" -Iplatform/os/cygwin32 \
        -I"$prefix/include" system/tests/palette_bounds_test.c \
        -o "$root/build/palette-bounds-test.exe"
      "$root/build/palette-bounds-test.exe"
    )
    ;;
  picture-clip)
    (
      # Run original 68K picture clipping calls through the staged Windows ROMAW.
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      retro68=${RETRO68_PREFIX:-/home/william/retro68-toolchain}
      romaw=${ROMAW:-$root/windows-binaries/bin/ROMAW.exe}
      mkdir -p "$root/build"
      "$retro68/bin/m68k-apple-macos-gcc" -std=gnu11 \
        "$root/system/tests/picture_clip_probe.c" \
        -o "$root/build/PictureClipProbe.code.bin"
      "$retro68/bin/Rez" "$retro68/m68k-apple-macos/RIncludes/Retro68APPL.r" \
        -I"$retro68/m68k-apple-macos/RIncludes" \
        --copy "$root/build/PictureClipProbe.code.bin" \
        -o "$root/build/PictureClipProbe.bin" -t APPL -c PcPr \
        --cc "$root/build/%PictureClipProbe"
      rm -f "$root/build/._PictureClipProbe"
      python3 "$root/tools/normalize-appledouble.py" \
        "$root/build/%PictureClipProbe" "$root/build/._PictureClipProbe"
      touch "$root/build/PictureClipProbe"
      rm -f "$root/build/picture-clip-probe.txt"
      "$romaw" \
        "$(wslpath -w "$root/build/PictureClipProbe")"
      cat "$root/build/picture-clip-probe.txt"
      grep -qx 'translated picture clip failures=0' "$root/build/picture-clip-probe.txt"
    )
    ;;
  picture-header)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cc -std=gnu17 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -I"$root/include" "$root/system/tests/picture_header_test.c" \
        -o "$root/build/picture-header-test"
      "$root/build/picture-header-test"
    )
    ;;
  pixmap-copy)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      python3 - "$root" <<'PY'
import sys
from pathlib import Path
root = Path(sys.argv[1])
source = (root / "quickdraw/qPixMapConv.c").read_text()
start = source.index("void pixmap_copy(")
end = source.index("boolean_t pixmap_copy_if_screen", start)
(root / "build/pixmap-copy-function.inc").write_text(source[start:end])
PY
      cd "$root"
      x86_64-w64-mingw32-gcc -std=gnu17 -g -O0 -ffunction-sections -fdata-sections \
        -Wl,--gc-sections -DHAVE_CONFIG_H -I"$root/build" -I"$root/system/obj" \
        -I. -Iinclude -Ifrontend-sdl -Iplatform/os/mingw32 \
        -Iplatform/arch/x86_64 -Iplatform/os/cygwin32 -I"$root/win64-prefix/include" \
        system/tests/pixmap_copy_test.c -o "$root/build/pixmap-copy-test.exe"
      "$root/build/pixmap-copy-test.exe"
    )
    ;;
  quickdraw-host)
    (
      # Real Toolbox rendering plus the native presentation adapter, in private profiles.
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      obj=${ROMAW_TEST_OBJ:-$root/system/obj}
      frontend="$obj/native-finder/windows-x64"
      out="$root/build/quickdraw-host-probe"
      executable=${ROMAW_TEST_EXE:-$root/windows-binaries/bin/ROMAW.exe}
      mkdir -p "$out"
      x86_64-w64-mingw32-gcc -std=gnu17 -g -O2 -Wall -Wextra -Werror \
        -shared -static-libgcc \
        -I"$root/include" -I"$frontend" \
        "$root/system/tests/quickdraw_host_probe.c" \
        -o "$out/quickdraw-host-probe.dll"
      x86_64-w64-mingw32-g++ -std=c++17 -g -O2 -DUNICODE -D_UNICODE -DFINDER_TESTING \
        -shared -static-libgcc -static-libstdc++ \
        -I"$root/include" -I"$frontend" \
        "$root/system/tests/quickdraw_finder_probe.cpp" "$root/finder/classic.cpp" "$root/finder/text.cpp" \
        "$root/finder/views.cpp" \
        "$root/classic-ui/src/classic_ui.c" \
        -lcomctl32 -luxtheme -lgdi32 -o "$out/quickdraw-finder-probe.dll"
      # Compile the two aggregate modules in WSL; callers may run them through
      # the Windows runner directly when WSL process interop is unavailable.
      if [ "${1:-}" = --build-only ]; then exit 0; fi
      for component in host finder; do
        marker="QUICKDRAW_${component^^}_PROBE_OK"
        powershell.exe -NoProfile -ExecutionPolicy Bypass \
          -File "$(wslpath -w "$root/system/tests/check-graphics.ps1")" quickdraw-host \
          -Executable "$(wslpath -w "$executable")" \
          -Module "$(wslpath -w "$out/quickdraw-$component-probe.dll")" -Marker "$marker"
      done
    )
    ;;
  sdl-pixel)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      "${CC:-cc}" -std=gnu17 -g -O0 -Wall -Wextra -Werror \
        -I"$root/include" "$root/system/tests/sdl_pixels_test.c" \
        -o "$root/build/sdl-pixels-test"
      "$root/build/sdl-pixels-test"
    )
    ;;
  text-scale)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      gcc -std=gnu17 -g -O0 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -I"$root/include" "$root/system/tests/text_scale_test.c" \
        -o "$root/build/text-scale-test"
      "$root/build/text-scale-test"
    )
    ;;
  win-font)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      x86_64-w64-mingw32-gcc -std=gnu17 -g -O0 -Wall -Wextra -Werror \
        -I"$root/include" "$root/system/tests/win_font_test.c" \
        "$root/platform/os/cygwin32/win_font.c" \
        -lgdi32 -luser32 -o "$root/build/win-font-test.exe"
      "$root/build/win-font-test.exe" "$(wslpath -w "$root")"
    )
    ;;
  *)
    printf 'Unknown check: %s. Use --list.\n' "$check" >&2
    exit 2
    ;;
esac
