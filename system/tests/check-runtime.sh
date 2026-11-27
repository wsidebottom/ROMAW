#!/usr/bin/env bash
# +===================================================================
#   File:      check-runtime.sh
#
#   Summary:   Run runtime regression checks independently.
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
    printf '%s\n' '24bit-memory' 'application-instance' 'browser-compat' 'callback-width' 'config-prefs' 'cooperative-context' 'ecf-unit' 'guest-abi' 'guest-pointer' 'mactcp' 'mactcp-guest' 'mactcp-ppc' 'quit' 'robustness' 'native-preferences' 'sdl-preferences' 'syserr'
    ;;
  mactcp-guest)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      romaw=$(realpath "${1:-${ROMAW_TEST_EXE:-$root/windows-binaries/bin/ROMAW.exe}}")
      retro68=${RETRO68_PREFIX:-/home/william/retro68-toolchain}
      out="$root/build/mactcp"
      mkdir -p "$out"
      cd "$root"
      python3 system/tests/mactcp_resolver_fixtures.py
      for entry in mactcp_guest_probe:MacTCPProbe:guest-results.txt mactcp_stream_guest_probe:MacTCPStreamProbe:stream-guest-results.txt mactcp_resolver_guest_probe:MacTCPResolverProbe:resolver-guest-results.txt mactcp_driver_guest_probe:MacTCPDriverProbe:driver-results.txt; do
        IFS=: read -r source name report <<<"$entry"
        "$retro68/bin/m68k-apple-macos-gcc" -std=gnu11 -O1 "system/tests/$source.c" -o "$out/$name.code.bin"
        "$retro68/bin/Rez" "$retro68/m68k-apple-macos/RIncludes/Retro68APPL.r" \
          -I"$retro68/m68k-apple-macos/RIncludes" --copy "$out/$name.code.bin" \
          -o "$out/$name.bin" -t APPL -c MTPr --cc "$out/%$name"
        if [ "$source" = mactcp_driver_guest_probe ]; then
          "$retro68/bin/Rez" "system/tests/$source.r" --append -o "$out/%$name" -t APPL -c MTPr
        fi
        rm -f -- "$out/._$name"
        python3 tools/normalize-appledouble.py "$out/%$name" "$out/._$name"
        touch "$out/$name"
        rm -f -- "$out/$report"
        (
          cd "$out"
          "$romaw" -debug trace "$(wslpath -w "$out/$name")" >"$name-launch.log" 2>&1
        )
        # A partition relaunch can outlive the original GUI launcher.
        for ((attempt = 0; attempt < 450; ++attempt)); do
          if grep -q '^failures=' "$out/$report" 2>/dev/null; then break; fi
          sleep 0.1
        done
        cat "$out/$report"
        grep -q '^failures=0 checks=' "$out/$report"
        if grep -s 'win_exception' "$out/stdout.txt" "$out/stderr.txt" "$out/$name-launch.log"; then exit 1; fi
      done
    )
    ;;
  mactcp-ppc)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      romaw=$(realpath "${1:-${ROMAW_TEST_EXE:-$root/windows-binaries/bin/ROMAW.exe}}")
      out="$root/build/mactcp/ppc-probe"
      cd "$root"
      python3 system/tests/make_mactcp_ppc_probe.py "$out"
      powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w "$root/system/tests/run-mactcp-ppc.ps1")" \
        -ROMAW "$(wslpath -w "$romaw")" -Probe "$(wslpath -w "$out/PPC MacTCP Probe")"
    )
    ;;
  mactcp)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build/mactcp"
      x86_64-w64-mingw32-gcc -std=gnu17 -g -O0 -Wall -Wextra -Werror -I"$root/include" \
        "$root/system/tests/mactcp_host_test.c" "$root/platform/os/cygwin32/win_mactcp.c" \
        -o "$root/build/mactcp/host-test.exe" -liphlpapi -lws2_32
      "$root/build/mactcp/host-test.exe"
      x86_64-w64-mingw32-gcc -std=gnu17 -g -O0 -Wall -Wextra -Werror -I"$root/include" \
        "$root/system/tests/mactcp_stream_host_test.c" "$root/platform/os/cygwin32/win_mactcp_stream.c" \
        "$root/platform/os/cygwin32/win_mactcp.c" -o "$root/build/mactcp/stream-host-test.exe" -liphlpapi -lws2_32
      "$root/build/mactcp/stream-host-test.exe"
      x86_64-w64-mingw32-gcc -std=gnu17 -g -O0 -Wall -Wextra -Werror -I"$root/include" \
        "$root/system/tests/mactcp_dns_host_test.c" "$root/platform/os/cygwin32/win_mactcp_dns.c" \
        "$root/platform/os/cygwin32/win_mactcp.c" -o "$root/build/mactcp/dns-host-test.exe" -ldnsapi -liphlpapi -lws2_32
      "$root/build/mactcp/dns-host-test.exe"
      x86_64-w64-mingw32-gcc -std=gnu17 -g -O0 -Wall -Wextra -Werror -DMACTCP_DNS_FIXTURE -I"$root/include" \
        "$root/system/tests/mactcp_dns_host_test.c" "$root/platform/os/cygwin32/win_mactcp.c" \
        -o "$root/build/mactcp/dns-record-test.exe" -ldnsapi -liphlpapi -lws2_32
      "$root/build/mactcp/dns-record-test.exe"
    )
    ;;
  syserr)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      fixture=$(mktemp -d "$root/build/syserr-test.XXXXXX")
      trap 'rm -rf -- "$fixture"' EXIT
      mkdir -p "$fixture/rsys"
      # Isolate the drawing/host actions; use the production procedure body and interface.
      for header in rsys/common.h QuickDraw.h DialogMgr.h SegmentLdr.h ShutDown.h rsys/mman.h; do
        : >"$fixture/$header"
      done
      cc=${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}
      "$cc" -std=gnu17 -g -O0 -Wall -Wextra -Werror -I"$fixture" -I"$root/include" \
        "$root/system/tests/syserr_procedures_test.c" -o "$fixture/syserr-test.exe"
      "$fixture/syserr-test.exe"
    )
    ;;
  application-instance)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      cc=${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}
      "$cc" -std=gnu17 -g -O0 -Wall -Wextra -Werror -I"$root/include" \
        "$root/system/tests/application_instance_test.c" \
        -o "$root/build/application-instance-test.exe" -luser32
      "$root/build/application-instance-test.exe"
    )
    ;;
  24bit-memory)
    (
      set -euo pipefail
      cc=${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}
      root=$(cd "$(dirname "$0")/../.." && pwd)
      prefix=${WINDOWS_PREFIX:-$root/win64-prefix}
      obj=${ROMAW_OBJ_DIR:-$root/system/obj}
      case ${WINDOWS_TARGET:-x86_64-w64-mingw32} in
        x86_64-w64-mingw32) arch=x86_64 ;;
        *)
          echo "Tests require the Windows x64 target." >&2
          exit 1
          ;;
      esac
      cd "$root"
      mkdir -p "$root/build"
      "$cc" -std=gnu17 -g -O0 \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -DHAVE_CONFIG_H -DNDEBUG \
        -I"$obj" -I. -Iinclude -Ifrontend-sdl -Iplatform/os/mingw32 \
        -Iplatform/os/cygwin32 -I"platform/arch/$arch" -I"$prefix/include" \
        system/tests/mman24_test.c memory/mman.c memory/mmansubr.c \
        "$prefix/lib/libsyn68k.a" -o "$root/build/mman24-test.exe"
      "$root/build/mman24-test.exe"
    )
    ;;
  browser-compat)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      x86_64-w64-mingw32-gcc -g -O2 \
        -I"$root/syn68k/obj/src/include" -I"$root/syn68k/src/include" -I"$root/include" \
        "$root/system/tests/browser_compat_test.c" \
        "$root/win64-prefix/lib/libsyn68k.a" -lm \
        -o "$root/build/browser-compat-test.exe"
      "$root/build/browser-compat-test.exe" \
        >"$root/build/browser-compat-test.stdout.txt" \
        2>"$root/build/browser-compat-test.stderr.txt"
      cat "$root/build/browser-compat-test.stdout.txt"
    )
    ;;
  callback-width)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      cc=${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}
      prefix=${WINDOWS_PREFIX:-$root/win64-prefix}
      mkdir -p build/callback-width
      "$cc" -std=gnu17 -g -O0 -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -DHAVE_CONFIG_H -Isystem/obj -I. -Iinclude \
        -Ifrontend-sdl -Iplatform/os/mingw32 \
        -Iplatform/os/cygwin32 -I"$prefix/include" \
        system/tests/callback_width_test.c runtime/emutrap.c \
        "$prefix/lib/libsyn68k.a" -o build/callback-width/test.exe
      build/callback-width/test.exe
    )
    ;;
  config-prefs)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      fixture=$(mktemp -d "$root/build/config-prefs.XXXXXX")
      sources=("$root/system/tests/config_prefs_test.c" "$root/runtime/configprefs.c" "$root/runtime/prefspacing.c")
      gcc -std=gnu17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
        -I"$root/include" "${sources[@]}" -o "$fixture/config-prefs-test"
      cd "$fixture"
      ./config-prefs-test "$root/system/ROMAW.prefs.master"
      mkdir windows
      cd windows
      x86_64-w64-mingw32-gcc -std=gnu17 -Wall -Wextra -Werror -g \
        -I"$root/include" "${sources[@]}" -o config-prefs-test.exe
      ./config-prefs-test.exe "$(wslpath -w "$root/system/ROMAW.prefs.master")"
    )
    ;;
  cooperative-context)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      gcc -std=gnu17 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -I"$root/include" "$root/system/tests/cooperative_context_test.c" \
        -o "$root/build/cooperative-context-test"
      "$root/build/cooperative-context-test"
    )
    ;;
  native-preferences)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      obj=${ROMAW_OBJ_DIR:-$root/build/romaw-optimized/obj}
      prefix=${WINDOWS_PREFIX:-$root/win64-prefix}
      out="$root/build/native-preferences"
      mkdir -p "$out"
      x86_64-w64-mingw32-windres -i platform/os/cygwin32/preferences.rc -o "$out/preferences.res" -O res
      x86_64-w64-mingw32-windres -DROMAW_RESOURCE_EDITOR -i platform/os/cygwin32/preferences.rc \
        -o "$out/preferences-editor.res" -O res
      cmp "$out/preferences.res" "$out/preferences-editor.res"
      # DESIGNINFO is Visual Studio syntax; compare runtime resources with its style definitions.
      x86_64-w64-mingw32-windres -i "$out/preferences.res" -J res -o "$out/preferences.o" -O coff
      x86_64-w64-mingw32-gcc -std=gnu17 -g -O0 -DHAVE_CONFIG_H \
        -I"$obj" -I. -Iinclude -Ifrontend-sdl -Iplatform/os/mingw32 \
        -Iplatform/arch/x86_64 -Iplatform/os/cygwin32 -I"$prefix/include" \
        system/tests/native_preferences_test.c runtime/configprefs.c runtime/prefspacing.c \
        "$out/preferences.o" -lcomctl32 -lshell32 -lole32 -luuid -o "$out/native-preferences-test.exe"
      "$out/native-preferences-test.exe"
    )
    ;;
  ecf-unit)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      obj=${ROMAW_OBJ_DIR:-$root/system/obj}
      configuration="$obj/.windows-build-config"
      if [[ ! -r "$configuration" ]]; then
        echo "Configure the Windows ROMAW build before running ECF tests." >&2
        exit 1
      fi
      IFS='|' read -r target cc cxx build_cc prefix profile_options <"$configuration"
      case $target in
        x86_64-w64-mingw32) arch=x86_64 ;;
        *)
          echo "Unsupported ECF test target: $target" >&2
          exit 1
          ;;
      esac
      mkdir -p "$root/build"
      echo "Building ECF unit tests for $target with $cc"
      cd "$root"
      for test in ecf_preferences ecf_memory image_session image_warning native_launch; do
        extra_sources=()
        if [[ $test == image_session || $test == ecf_preferences ]]; then
          extra_sources+=(runtime/configprefs.c runtime/prefspacing.c)
        fi
        "$cc" -std=gnu17 -g -O0 \
          -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-jump-tables -Wl,--gc-sections -DHAVE_CONFIG_H \
          -I"$obj" -I. -Iinclude -Ifrontend-sdl -Iplatform/os/mingw32 \
          -I"platform/arch/$arch" -Iplatform/os/cygwin32 -I"$prefix/include" \
          "system/tests/${test}_test.c" "${extra_sources[@]}" -lcomdlg32 -lcomctl32 -lole32 -luuid -o "$root/build/${test}-test.exe"
        "$root/build/${test}-test.exe"
      done
    )
    ;;
  guest-abi)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      cc=${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}
      prefix=${WINDOWS_PREFIX:-$root/win64-prefix}
      mkdir -p build
      "$cc" -std=gnu17 -g -Isystem/obj -I. -Iinclude \
        -Ifrontend-sdl -Iplatform/os/mingw32 \
        -Iplatform/os/cygwin32 -I"$prefix/include" \
        system/tests/guest_abi_test.c -o build/guest-abi-test.exe
      build/guest-abi-test.exe
    )
    ;;
  guest-pointer)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      mkdir -p build/guest-pointers
      for target in x86_64-w64-mingw32; do
        "$target-gcc" -std=gnu17 -g -O1 -Wall -Wextra -Werror \
          -Isyn68k/src/include -Iinclude \
          system/tests/guest_pointers_test.c -o "build/guest-pointers/$target.exe"
        "build/guest-pointers/$target.exe"
      done
    )
    ;;
  quit)
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
      for test in sdl_quit application_quit; do
        "$target-gcc" -std=gnu17 -g -O0 \
          -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
          -fno-jump-tables -Wl,--gc-sections -DHAVE_CONFIG_H \
          -I"$root/system/obj" -I. -Iinclude -Ifrontend-sdl \
          -Iplatform/os/mingw32 -I"platform/arch/$arch" -Iplatform/os/cygwin32 \
          -I"$prefix/include" "system/tests/${test}_test.c" \
          -o "$root/build/${test}-test.exe"
        "$root/build/${test}-test.exe"
      done
    )
    ;;
  robustness)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      obj=${ROMAW_OBJ_DIR:-$root/system/obj}
      mkdir -p "$root/build"
      gcc -std=gnu17 -g -O0 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -I"$root/include" "$root/system/tests/paramline_test.c" \
        -o "$root/build/paramline-test"
      "$root/build/paramline-test"
      bash "$root/system/tests/check-toolbox.sh" hle
      bash "$root/system/tests/check-media.sh" pcm
      bash "$root/system/tests/check-media.sh" aiff
      bash "$root/system/tests/check-media.sh" movie
      bash "$root/system/tests/check-graphics.sh" win-font
      bash "$root/system/tests/check-ui.sh" native-dialog
      bash "$root/system/tests/check-runtime.sh" quit
      bash "$root/system/tests/check-graphics.sh" picture-header
      bash "$root/system/tests/check-toolbox.sh" resource-map
      bash "$root/system/tests/check-files.sh" temporary-folder
      bash "$root/system/tests/check-files.sh" alias-handle
      x86_64-w64-mingw32-gcc -std=gnu17 -g -O0 -Wall -Wextra -Werror \
        -I"$root/include" "$root/system/tests/paramline_test.c" \
        -o "$root/build/paramline-test.exe"
      "$root/build/paramline-test.exe"
      cd "$root"
      for test in bounded_names serial_allocation version_resources version_parse printer_resolution number_parse option_help host_path win_temp native_launch clipboard_write clipboard_read clipboard_render fill_pattern pattern_origin font_info_callback drive_paths long_dates gestalt_alias icon_action scrap_offset tempmem systemnotify component_lifetime process_client application_registry keyboard_resource sdl_keyboard sdl_focus sdl_presentation srcblt getcpixel ecf_preferences ecf_memory print_records print_settings native_file_reply legacy_sound sound_status fileplay speech movie_runtime desk_files munger_growth type_select palette_bounds cancel_event layer_find window_state trash_folder; do
        x86_64-w64-mingw32-gcc -std=gnu17 -g -O0 \
          -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-jump-tables -Wl,--gc-sections -DHAVE_CONFIG_H \
          -I"$obj" -I. -Iinclude -Ifrontend-sdl -Iplatform/os/mingw32 \
          -Iplatform/arch/x86_64 -Iplatform/os/cygwin32 -I"$root/win64-prefix/include" \
          "system/tests/${test}_test.c" -o "$root/build/${test}-test.exe"
        "$root/build/${test}-test.exe"
      done
      bash "$root/system/tests/check-files.sh" host-name
    )
    ;;
  sdl-preferences)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      prefix=${WINDOWS_PREFIX:-$root/win64-prefix}
      obj=${ROMAW_OBJ_DIR:-$root/system/obj}
      x86_64-w64-mingw32-gcc -std=gnu17 -g -O0 -ffunction-sections -fdata-sections \
        -fno-asynchronous-unwind-tables \
        -Wl,--gc-sections -I"$obj" -I"$root" -I"$root/include" -I"$root/frontend-sdl" \
        -I"$root/platform/os/cygwin32" -I"$root/platform/arch/x86_64" -I"$prefix/include" \
        "$root/system/tests/sdl_preferences_test.c" -L"$prefix/lib" -lSDL \
        -o "$root/build/sdl-preferences-test.exe"
      cd "$root/windows-binaries/bin"
      "$root/build/sdl-preferences-test.exe" "$(wslpath -w "$root/build/sdl-preferences.bmp")"
    )
    ;;
  *)
    printf 'Unknown check: %s. Use --list.\n' "$check" >&2
    exit 2
    ;;
esac
