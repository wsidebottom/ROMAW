#!/usr/bin/env bash
# +===================================================================
#   File:      check-toolbox.sh
#
#   Summary:   Run toolbox regression checks independently.
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
    printf '%s\n' 'dcomp0' 'help-manager' 'hle' 'resource-map' 'system-resource' 'toolbox-additions'
    ;;
  dcomp0)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      gcc -std=gnu17 -g -O0 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -I"$root/include" "$root/system/tests/dcomp0_test.c" \
        -o "$root/build/dcomp0-test"
      "$root/build/dcomp0-test"
    )
    ;;
  help-manager)
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
        system/tests/help_menu_handle_test.c -o "$root/build/help-menu-handle-test.exe"
      "$root/build/help-menu-handle-test.exe"
    )
    ;;
  hle)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      gcc -m32 -std=gnu17 -g -O0 -DCOMPILE_FOR_BUILD \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        -I"$root/system/obj" -I"$root/." \
        -I"$root/include" -I"$root/win64-prefix/include" \
        "$root/system/tests/hle_ownership_test.c" \
        -o "$root/build/hle-ownership-test-linux"
      "$root/build/hle-ownership-test-linux"
    )
    ;;
  resource-map)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      # Compile the real Resource Manager with host headers and 32-bit guest pointers.
      # The same fixture can also be cross-compiled and run on Windows.
      gcc -m32 -std=gnu17 -g -O0 -DCOMPILE_FOR_BUILD \
        -DSIZEOF_CHAR_P=4 -DSIZEOF_INT=4 -DSIZEOF_LONG=4 \
        -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
        -Wl,--gc-sections -I"$root/system/obj" -I"$root/include" \
        -I"$root/tools/romaw" -I"$root/platform/arch/x86_64" \
        -I"$root/win64-prefix/include" "$root/system/tests/resource_map_eof_test.c" \
        -o "$root/build/resource-map-eof-test-linux"
      "$root/build/resource-map-eof-test-linux"
      gcc -m32 -std=gnu17 -g -O0 -DCOMPILE_FOR_BUILD \
        -DSIZEOF_CHAR_P=4 -DSIZEOF_INT=4 -DSIZEOF_LONG=4 \
        -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
        -Wl,--gc-sections -I"$root/system/obj" -I"$root/include" \
        -I"$root/tools/romaw" -I"$root/platform/arch/x86_64" \
        -I"$root/win64-prefix/include" "$root/system/tests/resource_map_header_test.c" \
        -o "$root/build/resource-map-header-test-linux"
      "$root/build/resource-map-header-test-linux"

      gcc -m32 -std=gnu17 -g -O0 -DCOMPILE_FOR_BUILD \
        -DSIZEOF_CHAR_P=4 -DSIZEOF_INT=4 -DSIZEOF_LONG=4 \
        -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
        -Wl,--gc-sections -I"$root/system/obj" -I"$root/include" \
        -I"$root/tools/romaw" -I"$root/platform/arch/x86_64" \
        -I"$root/win64-prefix/include" "$root/system/tests/get_map_test.c" \
        -o "$root/build/get-map-test-linux"
      "$root/build/get-map-test-linux"
    )
    ;;
  system-resource)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      python3 "$root/system/tests/test-resource-catalog.py"
      python3 "$root/system/tests/test-system-resources.py"
      python3 "$root/system/tests/test-edited-icons.py"
      bash "$root/system/tests/check-toolbox.sh" toolbox-additions parameter_preferences
      gcc -std=gnu17 -g -fsanitize=address,undefined \
        -I"$root/include" \
        "$root/system/tests/system_prefs_test.c" -o "$root/build/system-prefs-test"
      "$root/build/system-prefs-test" "$root/build/system-prefs-test.ini"

      target=${WINDOWS_TARGET:-x86_64-w64-mingw32}
      "$target-gcc" -std=gnu17 -g -Wall -Wextra -Werror \
        -I"$root/include" "$root/system/tests/system_prefs_test.c" \
        -o "$root/build/system-rsrc-storage-test.exe"
      "$root/build/system-rsrc-storage-test.exe" \
        "$(wslpath -w "$root/build/system-rsrc-storage-test.ini")"

      gcc -m32 -std=gnu17 -g -O0 -DCOMPILE_FOR_BUILD \
        -DSIZEOF_CHAR_P=4 -DSIZEOF_INT=4 -DSIZEOF_LONG=4 \
        -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
        -Wl,--gc-sections -I"$root/system/obj" -I"$root/include" \
        -I"$root/tools/romaw" -I"$root/platform/arch/x86_64" \
        -I"$root/win64-prefix/include" "$root/system/tests/system_resource_delta_test.c" \
        -o "$root/build/system-resource-delta-test"
      "$root/build/system-resource-delta-test" "$root/build/system-resource-overrides.rsrc"
      python3 "$root/system/tests/test-resource-overrides.py" "$root/build/system-resource-overrides.rsrc"
    )
    ;;
  toolbox-additions)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      if [[ $# == 0 ]]; then
        for test in alias_record date_parse intl_map itl2_layout; do
          gcc -std=gnu17 -g -O0 -Wall -Wextra -Werror \
            -fsanitize=address,undefined -I"$root/include" \
            "$root/system/tests/${test}_test.c" -o "$root/build/${test}-test"
          "$root/build/${test}-test"
        done
        set -- script_run sys_environs font_cache powerpc_cache intl_compare itl2_sort alias_update alias_relative alias_file partial_resource date_api dialog_filter textedit_style_handle
      fi
      cd "$root"
      for test in "$@"; do
        x86_64-w64-mingw32-gcc -std=gnu17 -g -O0 \
          -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
          -fno-jump-tables -Wl,--gc-sections -DHAVE_CONFIG_H \
          -I"$root/system/obj" -I. -Iinclude -Ifrontend-sdl \
          -Iplatform/os/mingw32 -Iplatform/arch/x86_64 -Iplatform/os/cygwin32 \
          -I"$root/win64-prefix/include" "system/tests/${test}_test.c" \
          -o "$root/build/${test}-test.exe"
        "$root/build/${test}-test.exe"
      done
    )
    ;;
  *)
    printf 'Unknown check: %s. Use --list.\n' "$check" >&2
    exit 2
    ;;
esac
