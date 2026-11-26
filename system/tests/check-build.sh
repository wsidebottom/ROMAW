#!/usr/bin/env bash
# +===================================================================
#   File:      check-build.sh
#
#   Summary:   Run build regression checks independently.
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
    printf '%s\n' 'build-config' 'build-object-isolation' 'build-profiles' 'debug-info' 'generated-file' 'platform-target'
    ;;
  build-config)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      fixture=$(mktemp -d "$root/build/config-freshness.XXXXXX")
      case "$fixture" in
        "$root/build/config-freshness."*) ;;
        *) exit 1 ;;
      esac
      trap 'rm -rf -- "$fixture"' EXIT
      mkdir -p "$fixture/." "$fixture/system/obj" \
        "$fixture/win64-prefix/lib" "$fixture/bin"
      cp "$root/system/build-windows.sh" "$fixture/system/"
      mkdir -p "$fixture/tools"
      cp "$root/tools/build_version.py" "$fixture/tools/"
      touch "$fixture/win64-prefix/lib/libsyn68k.a" "$fixture/system/obj/Makefile" \
        "$fixture/./Makefile.in" "$fixture/system/obj/config.h" \
        "$fixture/config.h.in"
      cat >"$fixture/configure" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' configured > configure-called
exit 73
EOF
      cat >"$fixture/bin/gcc" <<'EOF'
#!/usr/bin/env bash
exit 74
EOF
      chmod +x "$fixture/configure" "$fixture/bin/gcc"
      target=x86_64-w64-mingw32
      signature="$fixture/system/obj/.windows-build-config"
      printf '%s\n' "$target|$target-gcc|$target-g++|gcc|$fixture/win64-prefix" >"$signature"
      run() {
        local expected=$1 status=0
        env -u CC -u PPC_CXX -u BUILD_CC -u WINDOWS_PREFIX -u ROMAW_OBJ_DIR \
          -u PPC_OBJ_DIR -u WINDOWS_TARGET -u ROMAW_STAGE_DIR ROMAW_BUILD_MODE=debug \
          PATH="$fixture/bin:$PATH" bash "$fixture/system/build-windows.sh" \
          >"$fixture/log" 2>&1 || status=$?
        if [[ $status != "$expected" ]]; then
          cat "$fixture/log" >&2
          echo "Expected wrapper stop $expected, got $status" >&2
          exit 1
        fi
      }
      # Reproduce a matching compiler signature left behind by an earlier configure.
      touch -d '3 days ago' "$signature"
      run 73
      test -f "$fixture/system/obj/configure-called"
      rm "$fixture/system/obj/configure-called"
      # A changed feature-header template also invalidates the configuration.
      touch -d '4 days ago' "$fixture/configure"
      run 73
      test -f "$fixture/system/obj/configure-called"
      rm "$fixture/system/obj/configure-called"
      # Current configuration skips configure and reaches the build helper instead.
      touch "$signature"
      run 74
      test ! -f "$fixture/system/obj/configure-called"
      echo 'PASS: stale configure/header inputs refresh configuration before build helpers; current configuration is reused'
    )
    ;;
  build-profiles)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      fixture=$(mktemp -d "$root/build/build-profiles.XXXXXX")
      trap 'rm -rf -- "$fixture"' EXIT
      mkdir -p "$fixture/system" "$fixture/win64-prefix/lib"
      cp "$root/system/build-windows.sh" "$fixture/system/"
      mkdir -p "$fixture/tools"
      cp "$root/tools/build_version.py" "$fixture/tools/"
      touch "$fixture/win64-prefix/lib/libsyn68k.a" "$fixture/Makefile.in" "$fixture/config.h.in"
      cat >"$fixture/configure" <<'EOF'
#!/usr/bin/env bash
for option in "$@"; do
  case "$option" in
    CFLAGS=* | CXXFLAGS=* | CPPFLAGS=*) printf '%s\n' "$option" ;;
  esac
done > profile-args
printf '%s\n' "$PWD" >> profile-args
EOF
      chmod +x "$fixture/configure"
      for mode in debug optimized; do
        env -u ROMAW_OBJ_DIR -u PPC_OBJ_DIR -u WINDOWS_PREFIX -u ROMAW_STAGE_DIR \
          -u CC -u PPC_CXX -u BUILD_CC -u WINDOWS_TARGET ROMAW_BUILD_MODE="$mode" \
          bash "$fixture/system/build-windows.sh" --configure
      done
      debug="$fixture/system/obj"
      optimized="$fixture/build/romaw-optimized/obj"
      grep -Fq -- '-O0' "$debug/profile-args"
      grep -Fq -- '-O2 -fno-strict-aliasing' "$optimized/profile-args"
      grep -Fq -- '-g' "$optimized/profile-args"
      ! grep -Eq -- '-DNDEBUG|-ffast-math|-Ofast' "$optimized/profile-args"
      grep -Fxq "$debug" "$debug/profile-args"
      grep -Fxq "$optimized" "$optimized/profile-args"
      ! cmp -s "$debug/.windows-build-config" "$optimized/.windows-build-config"
      # An ordinary invocation must choose the same profile as explicit optimized.
      cp "$optimized/profile-args" "$fixture/optimized-args"
      env -u ROMAW_OBJ_DIR -u PPC_OBJ_DIR -u WINDOWS_PREFIX -u ROMAW_STAGE_DIR \
        -u CC -u PPC_CXX -u BUILD_CC -u WINDOWS_TARGET -u ROMAW_BUILD_MODE \
        bash "$fixture/system/build-windows.sh" --configure
      cmp "$fixture/optimized-args" "$optimized/profile-args"
      if ROMAW_BUILD_MODE=invalid bash "$fixture/system/build-windows.sh" --configure \
        >"$fixture/invalid.log" 2>&1; then
        echo "Invalid build profile was accepted" >&2
        exit 1
      fi
      grep -Fq 'ROMAW_BUILD_MODE must be debug or optimized' "$fixture/invalid.log"
      echo 'PASS: debug/optimized profiles use separate objects, distinct signatures and compatible optimization flags'
    )
    ;;
  build-object-isolation)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      command mkdir -p "$root/build"
      fixture=$(mktemp -d "$root/build/object-isolation.XXXXXX")
      trap 'rm -f "$fixture/log" "$fixture/link"; rmdir "$fixture"' EXIT

      # A guard regression must fail this test before a build can write to sources.
      mkdir() {
        echo "Unexpected build write before object-directory rejection" >&2
        return 77
      }
      export -f mkdir

      reject() {
        local variable=$1 directory=$2 script=$3 expected=$4
        if env "$variable=$directory" bash "$root/$script" >"$fixture/log" 2>&1; then
          echo "Accepted invalid object directory: $directory" >&2
          exit 1
        fi
        if ! grep -Fq "$expected" "$fixture/log"; then
          cat "$fixture/log" >&2
          exit 1
        fi
      }

      reject ROMAW_OBJ_DIR "$root/." system/build-windows.sh 'ROMAW objects must stay'
      reject PPC_OBJ_DIR "$root/synppc/src" system/build-windows.sh 'PPC objects must stay'
      reject SYN68K_OBJ_DIR "$root/syn68k/src" syn68k/build-windows.sh 'syn68k objects must stay'
      ln -s "$root/." "$fixture/link"
      reject ROMAW_OBJ_DIR "$fixture/link" system/build-windows.sh 'ROMAW objects must stay'
      echo 'PASS: isolated object roots reject source directories and symlink escapes before writes'
    )
    ;;
  debug-info)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      objdump=${OBJDUMP:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-objdump}
      python3 "$root/syn68k/tests/test-assembly-cleanup.py"
      for object in "$root/syn68k/obj/src/runtime/syn68k.o" "${ROMAW_TEST_EXE:-$root/windows-binaries/bin/ROMAW.exe}"; do
        log="$root/build/debug-info-$(basename "$object").log"
        "$objdump" --dwarf=info "$object" >/dev/null 2>"$log"
        if [[ -s "$log" ]]; then
          cat "$log" >&2
          exit 1
        fi
        printf 'PASS: valid DWARF information in %s\n' "$object"
      done
    )
    ;;
  generated-file)
    (
      set -euo pipefail
      repo=$(cd "$(dirname "$0")/.." && pwd)
      workspace=$(cd "$repo/.." && pwd)
      obj=${ROMAW_OBJ_DIR:-$repo/obj}
      fixture=$(mktemp -d "$workspace/build/generated-files.XXXXXX")
      output="$obj/generated/apple.inc"

      test -f "$output" || {
        echo "Build ROMAW before running this test." >&2
        exit 1
      }
      # Reproduce migration from an in-source build without modifying real sources.
      cp -p "$output" "$fixture/apple.inc"
      trap 'cp -p "$fixture/apple.inc" "$output"' EXIT
      mv "$output" "$fixture/original.inc"
      make -C "$obj" generated/apple.inc >"$fixture/regeneration.log" 2>&1
      cmp "$fixture/apple.inc" "$output"
      grep -q 'generated/apple.inc' "$obj/menus/.deps/stdmbdf.Po"
      if grep -q '/src/apple.c' "$obj/menus/.deps/stdmbdf.Po"; then
        echo "Compiler consumed the legacy source-directory output." >&2
        exit 1
      fi

      if make -C "$obj" -q -W "$workspace/bitmaps/apple.icns" generated/apple.inc; then
        echo "Changing apple.icns did not invalidate the generated bitmap." >&2
        exit 1
      else
        status=$?
        test "$status" -eq 1
      fi
      make -C "$obj" -n -W "$workspace/bitmaps/apple.icns" menus/stdmbdf.o >"$fixture/rebuild.log"
      grep -q 'icns_to_c.py' "$fixture/rebuild.log"
      grep -q -- '-c.*menus/stdmbdf.c' "$fixture/rebuild.log"
      if make -C "$obj" -q -W "$workspace/tools/romaw/icns_to_c.py" generated/apple.inc; then
        echo "Changing icns_to_c.py did not invalidate the generated bitmap." >&2
        exit 1
      else
        status=$?
        test "$status" -eq 1
      fi
      # The error fallback is compiled independently of the System resource fork.
      fallback="$obj/generated/syserr_table.inc"
      cp -p "$fallback" "$fixture/syserr_table.inc"
      rm "$fallback"
      make -C "$obj" generated/syserr_table.inc >"$fixture/syserr-regeneration.log" 2>&1
      cmp "$fixture/syserr_table.inc" "$fallback"
      grep -q 'generated/syserr_table.inc' "$obj/diagnostics/.deps/syserr.Po"
      for input in bitmaps/syserr/*.icns bitmaps/stop.icns bitmaps/bomb.icns system/resources/system/syserr.r tools/rez_types.py tools/romaw/system_resources.py; do
        make -C "$obj" -n -W "$workspace/$input" diagnostics/syserr.o >"$fixture/syserr-rebuild.log"
        grep -q -- '--alert-table' "$fixture/syserr-rebuild.log"
        grep -q -- '-c.*diagnostics/syserr.c' "$fixture/syserr-rebuild.log"
      done
      # New typed encoders and exported pattern artwork must rebuild the fork and consumer.
      for input in tools/rez_system_types.py bitmaps/system-patterns/*.png; do
        if make -C "$obj" -q -W "$workspace/$input" generated/system_resources.inc; then
          echo "Changing $input did not invalidate the embedded System." >&2
          exit 1
        else
          status=$?
          test "$status" -eq 1
        fi
        make -C "$obj" -n -W "$workspace/$input" resources/system_resources.o >"$fixture/pattern-rebuild.log"
        grep -q 'system_resources.py' "$fixture/pattern-rebuild.log"
        grep -q -- '-c.*resources/system_resources.c' "$fixture/pattern-rebuild.log"
      done
      # Every maintained System Rez file must invalidate both the fork and its consumer.
      for source in catalog.r interface.r payloads.r artwork.r resource_types.r \
        keyboards/us-canada-english.rinc keyboards/finnish-swedish.rinc keyboards/french-flemish.rinc; do
        if make -C "$obj" -q -W "$workspace/system/resources/system/$source" generated/system_resources.inc; then
          echo "Changing $source did not invalidate the embedded System." >&2
          exit 1
        else
          status=$?
          test "$status" -eq 1
        fi
        make -C "$obj" -n -W "$workspace/system/resources/system/$source" resources/system_resources.o >"$fixture/system-rebuild.log"
        grep -q 'system_resources.py' "$fixture/system-rebuild.log"
        grep -q -- '-c.*resources/system_resources.c' "$fixture/system-rebuild.log"
      done
      if make -C "$obj" -q -W "$workspace/tools/rez_types.py" generated/system_resources.inc; then
        echo "Changing the typed Rez encoder did not invalidate the embedded System." >&2
        exit 1
      else
        status=$?
        test "$status" -eq 1
      fi
      # Palette data and its transitive Python readers feed every icon consumer.
      for input in bitmaps/mac-palettes.r tools/resource_catalog.py tools/rez_types.py; do
        make -C "$obj" -n -W "$workspace/$input" \
          resources/system_resources.o menus/stdmbdf.o generated/ROMAW.ico \
          finder-metadata/libromaw-finder-metadata.a >"$fixture/palette-rebuild.log"
        grep -q 'system_resources.py' "$fixture/palette-rebuild.log"
        grep -q 'icns_to_c.py' "$fixture/palette-rebuild.log"
        grep -q 'tools/mac_icons.py' "$fixture/palette-rebuild.log"
        grep -q 'cmake --build finder-metadata' "$fixture/palette-rebuild.log"
      done
      for input in bitmaps/mac-palettes.r tools/resource_catalog.py tools/rez_types.py; do
        grep -Fq "$input" "$obj/finder-metadata/CMakeFiles/romaw-finder-metadata.dir/build.make"
      done
      # Every Shell image family must invalidate the embedded Windows resources.
      for input in bitmaps/file_{hfs,dmg,iso,img,toast,dsk}.icns tools/generate-folder-icons.py; do
        make -C "$obj" -n -W "$workspace/$input" generated/ROMAW-res.o >"$fixture/image-icons-rebuild.log"
        grep -q 'generate-folder-icons.py --windows-output' "$fixture/image-icons-rebuild.log"
        grep -q 'windres.*image-file-icons' "$fixture/image-icons-rebuild.log"
      done
      for pair in 'menu_right_arrow icns menus/stdmdef' 'menu_up_arrow icns menus/stdmdef' \
        'menu_down_arrow icns menus/stdmdef' 'popup_down_arrow icns controls/ctlPopup' \
        'color_picker_normal png quickdraw/qColorPicker' 'color_wheel_4 png ui/color_wheel_bits'; do
        read -r asset extension consumer <<<"$pair"
        bitmap_output="$obj/generated/$asset.inc"
        cp -p "$bitmap_output" "$fixture/$asset.inc"
        rm "$bitmap_output"
        make -C "$obj" "generated/$asset.inc" >"$fixture/bitmap-regeneration.log" 2>&1
        cmp "$fixture/$asset.inc" "$bitmap_output"
        bitmap_input="$workspace/bitmaps/$asset.$extension"
        if [[ $asset == popup_down_arrow ]]; then bitmap_input="$workspace/bitmaps/3061.icns"; fi
        if [[ $asset == menu_right_arrow || $asset == menu_up_arrow || $asset == menu_down_arrow ]]; then
          bitmap_input="$workspace/bitmaps/arrow_closed.icns"
        fi
        make -C "$obj" -n -W "$bitmap_input" "$consumer.o" >"$fixture/bitmap-rebuild.log"
        grep -q 'tools/bitmaps.py' "$fixture/bitmap-rebuild.log"
        grep -q -- "-c.*$consumer.c" "$fixture/bitmap-rebuild.log"
      done
      make -C "$obj" -n -W "$workspace/bitmaps/PICT-11.png" resources/system_resources.o >"$fixture/pict-rebuild.log"
      grep -q 'system_resources.py' "$fixture/pict-rebuild.log"
      grep -q -- '-c.*resources/system_resources.c' "$fixture/pict-rebuild.log"
      make -C "$obj" -n -W "$workspace/tools/pictures.py" resources/system_resources.o >"$fixture/pict-tool-rebuild.log"
      grep -q 'system_resources.py' "$fixture/pict-tool-rebuild.log"
      grep -q -- '-c.*resources/system_resources.c' "$fixture/pict-tool-rebuild.log"
      echo "Generated-file regeneration, source dependencies, and consumer rebuild checks passed"
    )
    ;;
  platform-target)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      fixture=$(mktemp -d "$root/build/platform-targets.XXXXXX")
      trap 'rm -rf -- "$fixture"' EXIT
      for target in i686-w64-mingw32 aarch64-w64-mingw32 x86_64-linux-gnu x86_64-apple-darwin; do
        for script in system/build-windows.sh syn68k/build-windows.sh tools/build-windows-sdl.sh; do
          if WINDOWS_TARGET="$target" bash "$root/$script" >"$fixture/log" 2>&1; then
            echo "FAIL: $script accepted $target" >&2
            exit 1
          fi
          grep -q 'supports only x86_64-w64-mingw32' "$fixture/log"
        done
        if make -C "$root/synppc" -n WINDOWS_TARGET="$target" >"$fixture/log" 2>&1; then
          echo "FAIL: synppc accepted $target" >&2
          exit 1
        fi
        grep -q 'supports only x86_64-w64-mingw32' "$fixture/log"
        for source in . syn68k; do
          mkdir -p "$fixture/$source"
          if (cd "$fixture/$source" && "$root/$source/configure" --host="$target") >"$fixture/log" 2>&1; then
            echo "FAIL: $source/configure accepted $target" >&2
            exit 1
          fi
          grep -q 'supports only 64-bit Windows' "$fixture/log"
        done
      done
      echo 'PASS: application and runtime builds reject Windows x86, ARM64, Linux and macOS targets'
    )
    ;;
  *)
    printf 'Unknown check: %s. Use --list.\n' "$check" >&2
    exit 2
    ;;
esac
