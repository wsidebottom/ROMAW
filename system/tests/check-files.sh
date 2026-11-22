#!/usr/bin/env bash
# +===================================================================
#   File:      check-files.sh
#
#   Summary:   Run files regression checks independently.
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
    printf '%s\n' 'alias-handle' 'desktop-database' 'diskimage' 'drive-prefs' 'filetype' 'host-name' 'host-trash' 'image-icon-unit' 'image-import' 'mac-storage' 'open-deny' 'root-metadata' 'sidecar' 'temporary-folder' 'volume-geometry'
    ;;
  alias-handle)
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
        -I"$prefix/include" system/tests/alias_handle_output_test.c \
        -o "$root/build/alias-handle-output-test.exe"
      "$root/build/alias-handle-output-test.exe"
    )
    ;;
  desktop-database)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      fixture=$(mktemp -d "$root/build/desktop-database-test.XXXXXX")
      trap 'rm -rf "$fixture"' EXIT
      g++ -std=c++17 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -I"$root/include" "$root/system/tests/desktop_database_test.cpp" \
        -o "$root/build/desktop-database-test"
      "$root/build/desktop-database-test" "$fixture"
    )
    ;;
  diskimage)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      fixture=$(mktemp -d "$root/build/diskimage.XXXXXX")
      sources=("$root/system/tests/diskimage_test.c" "$root/hfs/diskimage.c")
      gcc -std=gnu17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
        -I"$root/include" "${sources[@]}" -o "$fixture/diskimage-test"
      "$fixture/diskimage-test"
      x86_64-w64-mingw32-gcc -std=gnu17 -Wall -Wextra -Werror -g \
        -I"$root/include" "${sources[@]}" -o "$fixture/diskimage-test.exe"
      "$fixture/diskimage-test.exe"
    )
    ;;
  drive-prefs)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      fixture=$(mktemp -d "$root/build/drive-prefs.XXXXXX")
      sources=("$root/system/tests/drive_prefs_test.c" "$root/runtime/configprefs.c" "$root/runtime/prefspacing.c")
      gcc -std=gnu17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
        -I"$root/include" "${sources[@]}" -o "$fixture/drive-prefs-test"
      cd "$fixture"
      ./drive-prefs-test
      x86_64-w64-mingw32-gcc -std=gnu17 -Wall -Wextra -Werror -g \
        -I"$root/include" "${sources[@]}" -o drive-prefs-test.exe
      ./drive-prefs-test.exe
    )
    ;;
  filetype)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      gcc -std=gnu17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
        -I"$root/include" "$root/system/tests/filetypemap_test.c" \
        -o "$root/build/filetypemap-test"
      "$root/build/filetypemap-test" "$root/system/ROMAW.ftm"
    )
    ;;
  host-name)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      obj=${ROMAW_OBJ_DIR:-$root/system/obj}
      mkdir -p "$root/build"
      cd "$root"
      x86_64-w64-mingw32-gcc -std=gnu17 -g -O0 \
        -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
        -fno-jump-tables -Wl,--gc-sections -DHAVE_CONFIG_H -DTEST_HOST_NAMES \
        -I"$obj" -I. -Iinclude -Ifrontend-sdl -Iplatform/os/mingw32 \
        -Iplatform/arch/x86_64 -Iplatform/os/cygwin32 -I"$root/win64-prefix/include" \
        system/tests/drive_paths_test.c platform/os/cygwin32/win_names.c \
        -o "$root/build/host-names-test.exe"
      "$root/build/host-names-test.exe"
    )
    ;;
  host-trash)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      fixture=$(mktemp -d "$root/build/host-trash.XXXXXX")
      x86_64-w64-mingw32-gcc -std=gnu17 -Wall -Wextra -Werror -g \
        -I"$root/include" "$root/system/tests/host_trash_test.c" \
        -lole32 -luuid -o "$fixture/host-trash-test.exe"
      "$fixture/host-trash-test.exe"
    )
    ;;
  image-icon-unit)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      fixture=$(mktemp -d)
      trap 'rm -rf "$fixture"' EXIT
      python3 - "$root" "$fixture" <<'PY'
import pathlib
import sys

root, fixture = map(pathlib.Path, sys.argv[1:])
source = (root / "host/diskimages.c").read_text()
begin = source.index("static int image_icon_family(")
end = source.index("static int image_volume_data_icon(", begin)
(fixture / "image_icon_impl.inc").write_text(source[begin:end])
PY
      ${CC:-cc} -std=c17 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
        -Wall -Wextra -Werror -I"$root/include" -I"$root/finder-metadata/include" -I"$fixture" \
        "$root/system/tests/image_icon_test.c" "$root/finder-metadata/src/icons.c" \
        -o "$fixture/image-icon-test"
      "$fixture/image-icon-test"
    )
    ;;
  image-import)
    (
      set -eu
      cd "$(dirname "$0")/../.."
      suite="build/finder-image-tests"
      work="$suite/run.$(date +%s).$$"
      mkdir -p "$work"
      cxx=${CXX:-x86_64-w64-mingw32-g++}
      "$cxx" -std=c++17 -O1 -g -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN \
        -D_WIN32_WINNT=0x0601 -ffunction-sections -fdata-sections \
        -Iinclude -Ifinder-metadata/include -Idisk-images/include \
        system/tests/native-finder/image_import_test.cpp finder/image_copy.cpp finder/files.cpp finder/actions.cpp finder/views.cpp \
        system/obj/images/libromaw-images.a system/obj/finder-metadata/libromaw-finder-metadata.a \
        -static -Wl,--gc-sections -lcomctl32 -lshlwapi -lshell32 -lole32 -loleaut32 -luuid -o "$suite/finder-image-tests.exe"
      "$suite/finder-image-tests.exe" "$(wslpath -w "$PWD/$work/source")" \
        "$(wslpath -w "$PWD/build/image-desktop-fixture/DesktopMetadata.hfv")"
    )
    ;;
  mac-storage)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build/mac-storage-tests"
      cc=${CC:-gcc}
      "$cc" -std=c17 -g -O1 -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -I"$root/include" "$root/system/tests/mac_storage_test.c" \
        -o "$root/build/mac-storage-tests/layout-test"
      "$root/build/mac-storage-tests/layout-test"
    )
    ;;
  open-deny)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/system/obj/native-tests"
      python3 - "$root" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
source = (root / "files/fileAccess.c").read_text()
start = source.index("static OSErr\nopen_deny_fork (")
end = source.index("\nA2 (PUBLIC trap, OSErrRET, OpenDeny,", start)
(root / "system/obj/native-tests/open-deny-implementation.h").write_text(source[start:end])
PY
      gcc -std=c17 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined \
        -I"$root/system/obj/native-tests" "$root/system/tests/open_deny_test.c" \
        -o "$root/system/obj/native-tests/open-deny-test"
      "$root/system/obj/native-tests/open-deny-test"
    )
    ;;
  root-metadata)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      cd "$root"
      obj=${ROMAW_OBJ_DIR:-$root/system/obj}
      g++ -std=c++17 -g -O0 -ffunction-sections -fdata-sections \
        -fsanitize=address,undefined -Wl,--gc-sections -Iinclude \
        system/tests/root_metadata_test.cpp -o build/root-metadata-test
      build/root-metadata-test
      x86_64-w64-mingw32-gcc -std=gnu17 -Iinclude \
        -c runtime/configprefs.c -o build/root-metadata-configprefs.o
      x86_64-w64-mingw32-gcc -std=gnu17 -Iinclude \
        -c runtime/prefspacing.c -o build/root-metadata-prefspacing.o
      x86_64-w64-mingw32-g++ -std=c++17 -g -O0 \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -static-libgcc -static-libstdc++ -Iinclude \
        system/tests/root_metadata_test.cpp build/root-metadata-configprefs.o \
        build/root-metadata-prefspacing.o \
        -o build/root-metadata-test.exe
      build/root-metadata-test.exe
      cd "$root"
      x86_64-w64-mingw32-gcc -std=gnu17 -g -O0 \
        -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
        -fno-jump-tables -Wl,--gc-sections -DHAVE_CONFIG_H \
        -I"$obj" -I. -Iinclude -Ifrontend-sdl -Iplatform/os/mingw32 \
        -Iplatform/arch/x86_64 -Iplatform/os/cygwin32 -I"$root/win64-prefix/include" \
        system/tests/drive_paths_test.c -o "$root/build/root-metadata-catalog-test.exe"
      "$root/build/root-metadata-catalog-test.exe"
    )
    ;;
  sidecar)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      mkdir -p "$root/build"
      target=${WINDOWS_TARGET:-x86_64-w64-mingw32}
      arch=x86_64
      prefix="$root/win64-prefix"
      if [[ $target != x86_64-w64-mingw32 ]]; then
        echo "Tests require the Windows x64 target." >&2
        exit 1
      fi
      "$target-gcc" -std=gnu17 -g -O0 \
        -ffunction-sections -fdata-sections -Wl,--gc-sections -DHAVE_CONFIG_H \
        -I"$root/system/obj" -I. -Iinclude -Ifrontend-sdl -Iplatform/os/mingw32 \
        -I"platform/arch/$arch" -Iplatform/os/cygwin32 -I"$prefix/include" \
        system/tests/sidecar_test.c -o "$root/build/sidecar-test.exe"
      run_dir=$(mktemp -d "$root/build/sidecar-test.XXXXXX")
      cd "$run_dir"
      "$root/build/sidecar-test.exe" >stdout.txt 2>stderr.txt
      cat stdout.txt
      printf 'Fixtures and logs: %s\n' "$run_dir"
    )
    ;;
  temporary-folder)
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
        -I"$prefix/include" system/tests/temporary_folder_test.c \
        -o "$root/build/temporary-folder-test.exe"
      "$root/build/temporary-folder-test.exe"
    )
    ;;
  volume-geometry)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      gcc -std=c17 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -I"$root/include" "$root/system/tests/volume_geometry_test.c" \
        -o "$root/build/volume-geometry-test"
      "$root/build/volume-geometry-test"
    )
    ;;
  *)
    printf 'Unknown check: %s. Use --list.\n' "$check" >&2
    exit 2
    ;;
esac
