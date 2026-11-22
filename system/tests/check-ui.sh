#!/usr/bin/env bash
# +===================================================================
#   File:      check-ui.sh
#
#   Summary:   Run ui regression checks independently.
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
    printf '%s\n' 'classic-ui' 'finder-code' 'finder-guest' 'list-click' 'list-layout' 'menu-list' 'mouse-input' 'native-dialog' 'window-prefs'
    ;;
  finder-code)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      out="$root/system/obj/finder-code"
      mkdir -p "$out"
      x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Wconversion -Werror \
        "$root"/finder/guest/{CDEF-128,WDEF-128,gest-128,hcsl-0}.c \
        "$root/system/tests/finder_code_test.c" -o "$out/finder-code-test.exe"
      "$out/finder-code-test.exe"
    )
    ;;
  finder-guest)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      out="$root/build/finder-guest"
      romaw=$(realpath "${1:-${ROMAW_TEST_EXE:-$root/windows-binaries/bin/ROMAW.exe}}")
      retro68=${RETRO68_PREFIX:-/home/william/retro68-toolchain}
      mkdir -p "$out"
      cd "$root"
      "$retro68/bin/m68k-apple-macos-gcc" -std=gnu11 -O1 system/tests/finder_guest_probe.c \
        -o "$out/FinderGuestProbe.code.bin"
      "$retro68/bin/Rez" "$retro68/m68k-apple-macos/RIncludes/Retro68APPL.r" \
        -I"$retro68/m68k-apple-macos/RIncludes" \
        --copy "$out/FinderGuestProbe.code.bin" -o "$out/FinderGuestProbe.bin" -t APPL -c FGPr \
        --cc "$out/%FinderGuestProbe"
      rm -f "$out/._FinderGuestProbe"
      python3 "$root/tools/normalize-appledouble.py" "$out/%FinderGuestProbe" "$out/._FinderGuestProbe"
      touch "$out/FinderGuestProbe"
      rm -f "$out/guest-results.txt"
      (
        cd "$out"
        "$romaw" -debug trace -syszone 2m -applzone 7m "$(wslpath -w "$out/FinderGuestProbe")" \
          >guest-launch.log 2>&1
      )
      cat "$out/guest-results.txt"
      grep -q '^failures=0 checks=' "$out/guest-results.txt"
      if grep -s 'win_exception' "$out/stdout.txt" "$out/stderr.txt" "$out/guest-launch.log"; then
        exit 1
      fi
    )
    ;;
  classic-ui)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      out="$root/build/classic-ui-tests"
      mkdir -p "$out"
      cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
        -fno-omit-frame-pointer -I"$root/include" \
        "$root/classic-ui/src/classic_ui.c" \
        "$root/system/tests/classic_ui_test.c" -o "$out/classic-ui-test"
      "$out/classic-ui-test"
    )
    ;;
  list-click)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      mkdir -p "$root/build"
      x86_64-w64-mingw32-gcc -std=gnu17 -g -O0 \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -DHAVE_CONFIG_H -DNDEBUG \
        -I"$root/system/obj" -I. -Iinclude -Ifrontend-sdl -Iplatform/os/mingw32 \
        -Iplatform/arch/x86_64 -Iplatform/os/cygwin32 -I"$root/win64-prefix/include" \
        system/tests/list_click_test.c lists/listMouse.c quickdraw/qRect.c \
        "$root/win64-prefix/lib/libsyn68k.a" -lm \
        -o "$root/build/list-click-test.exe"
      "$root/build/list-click-test.exe"
    )
    ;;
  list-layout)
    (
      # Run original 68K List Manager calls through the staged Windows ROMAW.
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      retro68=${RETRO68_PREFIX:-/home/william/retro68-toolchain}
      mkdir -p "$root/build"
      "$retro68/bin/m68k-apple-macos-gcc" -std=gnu11 \
        "$root/system/tests/list_layout_probe.c" \
        -o "$root/build/ListLayoutProbe.code.bin"
      "$retro68/bin/Rez" "$retro68/m68k-apple-macos/RIncludes/Retro68APPL.r" \
        -I"$retro68/m68k-apple-macos/RIncludes" \
        --copy "$root/build/ListLayoutProbe.code.bin" \
        -o "$root/build/ListLayoutProbe.bin" -t APPL -c LsPr \
        --cc "$root/build/%ListLayoutProbe"
      rm -f "$root/build/._ListLayoutProbe"
      python3 "$root/tools/normalize-appledouble.py" \
        "$root/build/%ListLayoutProbe" "$root/build/._ListLayoutProbe"
      touch "$root/build/ListLayoutProbe"
      rm -f "$root/build/list-layout-probe.txt"
      "$root/windows-binaries/bin/ROMAW.exe" \
        -syszone 2m -applzone 7m \
        "$(wslpath -w "$root/build/ListLayoutProbe")"
      cat "$root/build/list-layout-probe.txt"
      grep -qx 'failures=0' "$root/build/list-layout-probe.txt"
    )
    ;;
  menu-list)
    (
      # Run original 68K List Manager calls through the staged Windows ROMAW.
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      retro68=${RETRO68_PREFIX:-/home/william/retro68-toolchain}
      mkdir -p "$root/build"
      "$retro68/bin/m68k-apple-macos-gcc" -std=gnu11 \
        "$root/system/tests/menu_list_probe.c" \
        -o "$root/build/MenuListProbe.code.bin"
      "$retro68/bin/Rez" "$retro68/m68k-apple-macos/RIncludes/Retro68APPL.r" \
        -I"$retro68/m68k-apple-macos/RIncludes" \
        --copy "$root/build/MenuListProbe.code.bin" \
        -o "$root/build/MenuListProbe.bin" -t APPL -c MnPr \
        --cc "$root/build/%MenuListProbe"
      rm -f "$root/build/._MenuListProbe"
      python3 "$root/tools/normalize-appledouble.py" \
        "$root/build/%MenuListProbe" "$root/build/._MenuListProbe"
      touch "$root/build/MenuListProbe"
      rm -f "$root/build/menu-list-probe.txt"
      "$root/windows-binaries/bin/ROMAW.exe" \
        "$(wslpath -w "$root/build/MenuListProbe")"
      cat "$root/build/menu-list-probe.txt"
      grep -qx 'cleared private menu list failures=0' "$root/build/menu-list-probe.txt"
    )
    ;;
  mouse-input)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      "${CC:-cc}" -std=gnu17 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
        -I"$root/include" "$root/system/tests/mousewheel_test.c" \
        -o "$root/build/mousewheel-test"
      "$root/build/mousewheel-test"
    )
    ;;
  native-dialog)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      target=${WINDOWS_TARGET:-x86_64-w64-mingw32}
      prefix=${WINDOWS_PREFIX:-$root/win64-prefix}
      cc=${CC:-$target-gcc}
      case "$target" in
        x86_64-w64-mingw32) arch=x86_64 ;;
        *)
          echo "Unsupported Windows target: $target" >&2
          exit 1
          ;;
      esac
      "$cc" -std=gnu17 -g -O0 \
        -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
        -fno-jump-tables -Wl,--gc-sections -DHAVE_CONFIG_H \
        -Isystem/obj -I. -Iinclude -Ifrontend-sdl -Iplatform/os/mingw32 \
        -I"platform/arch/$arch" -Iplatform/os/cygwin32 -I"$prefix/include" \
        system/tests/native_dialogs_test.c -lcomdlg32 -lgdi32 -luser32 -lshell32 -lole32 -luuid \
        -o "$root/build/native-dialogs-test.exe"
      "$root/build/native-dialogs-test.exe"

      "$cc" -std=gnu17 -g -O0 \
        -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
        -fno-jump-tables -Wl,--gc-sections -DHAVE_CONFIG_H \
        -Isystem/obj -I. -Iinclude -Ifrontend-sdl -Iplatform/os/mingw32 \
        -I"platform/arch/$arch" -Iplatform/os/cygwin32 -I"$prefix/include" \
        system/tests/native_file_reply_test.c -o "$root/build/native-file-reply-test.exe"
      "$root/build/native-file-reply-test.exe"

      "$cc" -std=gnu17 -g -O0 \
        -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
        -Wl,--gc-sections -DHAVE_CONFIG_H \
        -Isystem/obj -I. -Iinclude -Ifrontend-sdl -Iplatform/os/mingw32 \
        -I"platform/arch/$arch" -Iplatform/os/cygwin32 -I"$prefix/include" \
        system/tests/icon_depth_test.c -o "$root/build/icon-depth-test.exe"
      "$root/build/icon-depth-test.exe"

      "$cc" -std=gnu17 -g -O0 \
        -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
        -fno-jump-tables -Wl,--gc-sections -DHAVE_CONFIG_H \
        -Isystem/obj -I. -Iinclude -Ifrontend-sdl -Iplatform/os/mingw32 \
        -I"platform/arch/$arch" -Iplatform/os/cygwin32 -I"$prefix/include" \
        system/tests/native_raster_test.c -lcomdlg32 -lgdi32 -luser32 \
        -o "$root/build/native-raster-test.exe"
      "$root/build/native-raster-test.exe"
      gcc -std=gnu17 -g -O0 -fsanitize=address,undefined -Iinclude \
        system/tests/raster_pdf_test.c printing/rasterpdf.c -o "$root/build/raster-pdf-test"
      "$root/build/raster-pdf-test"
    )
    ;;
  window-prefs)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      fixture=$(mktemp -d "$root/build/window-prefs.XXXXXX")
      x86_64-w64-mingw32-gcc -std=gnu17 -g -Wall -Wextra -Werror \
        -I"$root/frontend-sdl" \
        -I"$root/include" \
        "$root/system/tests/window_prefs_test.c" \
        "$root/frontend-sdl/windowprefs.c" \
        "$root/runtime/prefspacing.c" \
        "$root/runtime/configprefs.c" \
        -o "$fixture/window-prefs-test.exe"
      "$fixture/window-prefs-test.exe" "$(wslpath -w "$fixture/ROMAW.prefs")"
    )
    ;;
  *)
    printf 'Unknown check: %s. Use --list.\n' "$check" >&2
    exit 2
    ;;
esac
