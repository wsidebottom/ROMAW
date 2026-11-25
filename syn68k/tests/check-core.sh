#!/usr/bin/env bash
# +===================================================================
#   File:      check-core.sh
#
#   Summary:   Run core regression checks independently.
#
#   Classes:   None.
#
#   Functions: Script entry point and named regression checks; use --list.
#
#   Origin:    Consolidated repository regression checks; original file-specific notices remain below.
#
#   Copyright and Legal notices: Applicable repository license: syn68k/COPYING.
# ===================================================================+

check=${1:---list}
if [ "$#" -gt 0 ]; then shift; fi
case "$check" in
  --list | list)
    printf '%s\n' 'aot-x64' 'cache040' 'configured-header' 'control-register' 'fpsp' 'fpu' 'fpu-native' 'generator-dependency' 'jit-x64-benchmark' 'jit-x64' 'mmu040' 'mmu040-cache' 'native040' 'rte' 'threaded-dispatch'
    ;;
  cache040)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      output="$root/syn68k/obj/tests/cache040"
      mkdir -p "$output"
      sources=("$root/syn68k/src/runtime/cache040.c" "$root/syn68k/tests/cache040-test.c")
      flags=(-std=gnu17 -g -Wall -Wextra -Werror -I"$root/syn68k/src/runtime/include")
      "${CC:-x86_64-w64-mingw32-gcc}" "${flags[@]}" -O2 "${sources[@]}" -o "$output/cache040-test.exe"
      "$output/cache040-test.exe"
      "${CC:-x86_64-w64-mingw32-gcc}" "${flags[@]}" -O2 "$root/syn68k/src/runtime/cache040.c" \
        "$root/syn68k/tests/cache040-traffic-test.c" -o "$output/traffic.exe"
      "$output/traffic.exe"
      gcc "${flags[@]}" -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
        "${sources[@]}" -o "$output/cache040-test"
      ASAN_OPTIONS=detect_leaks=1 "$output/cache040-test"
      gcc "${flags[@]}" -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
        "$root/syn68k/src/runtime/cache040.c" "$root/syn68k/tests/cache040-traffic-test.c" -o "$output/traffic"
      ASAN_OPTIONS=detect_leaks=1 "$output/traffic"
    )
    ;;
  configured-header)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      fixture=$(mktemp -d "$root/build/configured-header.XXXXXX")
      case "$fixture" in
        "$root/build/configured-header."*) ;;
        *) exit 1 ;;
      esac
      trap 'rm -rf -- "$fixture"' EXIT
      repo="$fixture/syn68k"
      mkdir -p "$repo/src/include" "$repo/tests" "$repo/obj/src/include"
      cp "$root/syn68k/build-windows.sh" "$repo/"
      touch "$repo/Makefile.am" "$repo/Makefile.in" "$repo/configure.ac"
      cat >"$repo/configure" <<'EOF'
#!/usr/bin/env bash
echo 'Unexpected reconfigure' >&2
exit 71
EOF
      chmod +x "$repo/configure"
      touch "$repo/obj/config.status" "$repo/obj/src/include/config.h"
      cat >"$repo/obj/Makefile" <<'EOF'
src/include/config.h: ;
src/include/syn68k_private.h: ../src/include/syn68k_private.h.in
	cp $< $@
	echo generated >> generation-log
EOF
      target=x86_64-w64-mingw32
      printf '%s\n' "$target|$target-gcc|gcc|$fixture/prefix|-DNONDIRECT" >"$repo/obj/.windows-build-config"
      run() {
        env -u CC -u CC_FOR_BUILD -u WINDOWS_TARGET -u SYN68K_DISPATCH \
          SYN68K_OBJ_DIR="$repo/obj" WINDOWS_PREFIX="$fixture/prefix" \
          SYN68K_STAGE_DIR="$fixture/stage" bash "$repo/build-windows.sh" --configure-only >"$fixture/log" 2>&1
      }
      printf '#define OLD_PRIVATE_SWITCH 1\n' >"$repo/obj/src/include/syn68k_private.h"
      printf '/* The obsolete switch has been removed. */\n' >"$repo/src/include/syn68k_private.h.in"
      touch -t 202601010000 "$repo/obj/src/include/syn68k_private.h"
      run
      cmp "$repo/src/include/syn68k_private.h.in" "$repo/obj/src/include/syn68k_private.h"
      test "$(wc -l <"$repo/obj/generation-log")" -eq 1
      run
      test "$(wc -l <"$repo/obj/generation-log")" -eq 1
      printf '/* A subsequent template edit must also propagate. */\n' >"$repo/src/include/syn68k_private.h.in"
      touch -t 202601010000 "$repo/obj/src/include/syn68k_private.h"
      run
      cmp "$repo/src/include/syn68k_private.h.in" "$repo/obj/src/include/syn68k_private.h"
      test "$(wc -l <"$repo/obj/generation-log")" -eq 2
      echo 'PASS: stale configured headers refresh before generators, current headers are reused and edits propagate'
    )
    ;;
  control-register)
    (
      set -euo pipefail
      cc=${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      mkdir -p syn68k/obj/tests
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/movem-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/movem-test.exe
      syn68k/obj/tests/movem-test.exe 0
      syn68k/obj/tests/movem-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/movep-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/movep-test.exe
      syn68k/obj/tests/movep-test.exe 0
      syn68k/obj/tests/movep-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/dbcc-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/dbcc-test.exe
      syn68k/obj/tests/dbcc-test.exe 0
      syn68k/obj/tests/dbcc-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/branch-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/branch-test.exe
      syn68k/obj/tests/branch-test.exe 0
      syn68k/obj/tests/branch-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/bsr-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/bsr-test.exe
      syn68k/obj/tests/bsr-test.exe 0
      syn68k/obj/tests/bsr-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/control-transfer-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/control-transfer-test.exe
      syn68k/obj/tests/control-transfer-test.exe 0
      syn68k/obj/tests/control-transfer-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/lea-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/lea-test.exe
      syn68k/obj/tests/lea-test.exe 0
      syn68k/obj/tests/lea-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/pea-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/pea-test.exe
      syn68k/obj/tests/pea-test.exe 0
      syn68k/obj/tests/pea-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/multiply-word-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/multiply-word-test.exe
      syn68k/obj/tests/multiply-word-test.exe 0
      syn68k/obj/tests/multiply-word-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/divide-word-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/divide-word-test.exe
      syn68k/obj/tests/divide-word-test.exe 0
      syn68k/obj/tests/divide-word-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/multiply-long-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/multiply-long-test.exe
      syn68k/obj/tests/multiply-long-test.exe 0
      syn68k/obj/tests/multiply-long-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/divide-long-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/divide-long-test.exe
      syn68k/obj/tests/divide-long-test.exe 0
      syn68k/obj/tests/divide-long-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/link-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/link-test.exe
      syn68k/obj/tests/link-test.exe 0
      syn68k/obj/tests/link-test.exe 1
      if [[ ${1:-} == --oracle ]]; then
        python3 syn68k/tests/link-a7-reference.py
      fi
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/unlk-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/unlk-test.exe
      syn68k/obj/tests/unlk-test.exe 0
      syn68k/obj/tests/unlk-test.exe 1
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/control-register-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/control-register-test.exe
      syn68k/obj/tests/control-register-test.exe 0
      syn68k/obj/tests/control-register-test.exe 1
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/cache-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/cache-test.exe
      syn68k/obj/tests/cache-test.exe 0
      syn68k/obj/tests/cache-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/indexed-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/indexed-test.exe
      syn68k/obj/tests/indexed-test.exe 0
      syn68k/obj/tests/indexed-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/rte-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/rte-test.exe
      syn68k/obj/tests/rte-test.exe 0
      syn68k/obj/tests/rte-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/sr-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/sr-test.exe
      syn68k/obj/tests/sr-test.exe 0
      syn68k/obj/tests/sr-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/ea-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/ea-test.exe
      syn68k/obj/tests/ea-test.exe

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/bus-read-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/bus-read-test.exe
      syn68k/obj/tests/bus-read-test.exe 0
      syn68k/obj/tests/bus-read-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/sr-move-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/sr-move-test.exe
      syn68k/obj/tests/sr-move-test.exe 0
      syn68k/obj/tests/sr-move-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/usp-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/usp-test.exe
      syn68k/obj/tests/usp-test.exe 0
      syn68k/obj/tests/usp-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/moves-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/moves-test.exe
      syn68k/obj/tests/moves-test.exe 0
      syn68k/obj/tests/moves-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/trap-state-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/trap-state-test.exe
      syn68k/obj/tests/trap-state-test.exe 0
      syn68k/obj/tests/trap-state-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/interrupt-frame-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/interrupt-frame-test.exe
      syn68k/obj/tests/interrupt-frame-test.exe 0
      syn68k/obj/tests/interrupt-frame-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/reset-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/reset-test.exe
      syn68k/obj/tests/reset-test.exe 0
      syn68k/obj/tests/reset-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/move16-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/move16-test.exe
      syn68k/obj/tests/move16-test.exe 0
      syn68k/obj/tests/move16-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/bounds-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/bounds-test.exe
      syn68k/obj/tests/bounds-test.exe 0
      syn68k/obj/tests/bounds-test.exe 1
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/chk-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/chk-test.exe
      syn68k/obj/tests/chk-test.exe 0
      syn68k/obj/tests/chk-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/rtd-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/rtd-test.exe
      syn68k/obj/tests/rtd-test.exe 0
      syn68k/obj/tests/rtd-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/rtr-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/rtr-test.exe
      syn68k/obj/tests/rtr-test.exe 0
      syn68k/obj/tests/rtr-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/rts-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/rts-test.exe
      syn68k/obj/tests/rts-test.exe 0
      syn68k/obj/tests/rts-test.exe 1

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/checked-store-coherence-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -o syn68k/obj/tests/checked-store-coherence-test.exe
      syn68k/obj/tests/checked-store-coherence-test.exe 0
      syn68k/obj/tests/checked-store-coherence-test.exe 1
    )
    ;;
  rte)
    (
      set -euo pipefail
      cc=${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      output=syn68k/obj/tests/rte
      mkdir -p "$output"
      for fixture in rte-test mmu040-flow-test mmu040-stream-test; do
        "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
          "syn68k/tests/$fixture.c" syn68k/obj/src/runtime/libsyn68k.a -o "$output/$fixture.exe"
      done
      "$output/rte-test.exe" 0
      "$output/rte-test.exe" 1
      "$output/mmu040-flow-test.exe"
      "$output/mmu040-stream-test.exe"
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" -std=gnu17 -g -O0 \
        -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/mapped-cache040-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$output/cached-cpu-test.exe"
      "$output/cached-cpu-test.exe"
    )
    ;;
  fpsp)
    (
      set -euo pipefail
      cc=${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      output=syn68k/obj/tests/fpsp
      mkdir -p "$output"
      python3 syn68k/tests/fpsp-reference.py "$output/fpsp-reference.h"
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include \
        -Isyn68k/src/include -I"$output" syn68k/tests/fpsp-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -o "$output/fpsp-test.exe"
      for native in 0 1; do
        if ! "$output/fpsp-test.exe" "$native" >"$output/dispatch-$native.log" \
          2>"$output/dispatch-$native.err"; then
          tail -n 20 "$output/dispatch-$native.err"
          exit 1
        fi
        grep -q 'Motorola FPSP cases passed' "$output/dispatch-$native.log"
        tail -n 1 "$output/dispatch-$native.log"
      done
    )
    ;;
  fpu)
    (
      set -euo pipefail
      cc=${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      output=syn68k/obj/tests/fpu
      mkdir -p "$output"
      python3 syn68k/tests/test-fpu-division.py
      python3 syn68k/tests/test-fpu-arithmetic.py
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/fpu-test.c syn68k/obj/src/runtime/libsyn68k.a -o "$output/fpu-test.exe"
      for native in 0 1; do
        if ! "$output/fpu-test.exe" "$native" >"$output/dispatch-$native.log" 2>&1; then
          cat "$output/dispatch-$native.log"
          exit 1
        fi
        tail -n 1 "$output/dispatch-$native.log"
      done
      if [[ ${1:-} == --oracle ]]; then
        m68k-linux-gnu-as -m68040 syn68k/tests/fpu-oracle.S -o "$output/oracle.o"
        m68k-linux-gnu-objcopy -O binary --only-section=.text \
          "$output/oracle.o" "$output/oracle.bin"
        m68k-linux-gnu-gcc -m68040 -static syn68k/tests/fpu-oracle.c \
          "$output/oracle.o" -o "$output/oracle"
        qemu-m68k -cpu m68040 "$output/oracle" >"$output/reference.bin"
        "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
          syn68k/tests/fpu-oracle-runner.c syn68k/obj/src/runtime/libsyn68k.a \
          -o "$output/oracle-runner.exe"
        for native in 0 1; do
          if ! "$output/oracle-runner.exe" "$output/oracle.bin" \
            "$output/actual-$native.bin" "$native" >"$output/oracle-$native.log" 2>&1; then
            cat "$output/oracle-$native.log"
            exit 1
          fi
          python3 syn68k/tests/compare-fpu-oracle.py "$output/reference.bin" \
            "$output/actual-$native.bin"
        done
      fi
    )
    ;;
  generator-dependency)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      obj=${SYN68K_OBJ_DIR:-$root/syn68k/obj}
      runtime="$obj/src/runtime"
      test -f "$runtime/syn68k.c" || {
        echo 'Build syn68k before checking generator dependencies.' >&2
        exit 1
      }
      fixture=$(mktemp -d "$root/build/generator-deps.XXXXXX")
      case "$fixture" in
        "$root/build/generator-deps."*) ;;
        *) exit 1 ;;
      esac
      trap 'rm -rf -- "$fixture"' EXIT
      for input in syn68k_header.c 68k.scm 68k.defines.scm; do
        source="$root/syn68k/src/runtime/$input"
        if make -C "$runtime" -q -W "$source" syn68k.c >"$fixture/query.log" 2>&1; then
          echo "FAIL: changing $input does not invalidate generated code" >&2
          exit 1
        else
          status=$?
          test "$status" -eq 1 || {
            cat "$fixture/query.log" >&2
            exit "$status"
          }
        fi
        make -C "$runtime" -n -W "$source" syn68k.o mapindex.o mapinfo.o >"$fixture/rebuild.log" 2>&1
        grep -Fq '../syngen/syngen -v 68k.scm' "$fixture/rebuild.log"
        for output in syn68k.o mapindex.o mapinfo.o; do
          grep -Fq -- "-o $output" "$fixture/rebuild.log"
        done
      done
      echo 'PASS: source opcode/template edits invalidate generated code and rebuild all three consumers'
    )
    ;;
  aot-x64)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      cc=${CC:-x86_64-w64-mingw32-gcc}
      mkdir -p "$root/build"
      fixture=$(mktemp -d "$root/build/aot-x64.XXXXXX")
      trap 'rm -rf -- "$fixture"' EXIT
      mkdir "$fixture/unit" "$fixture/execution" "$fixture/mapped" "$fixture/mmu" "$fixture/source" "$fixture/control" "$fixture/destination" "$fixture/list" "$fixture/predicate" "$fixture/frame" "$fixture/flat" "$fixture/flat-compiled" "$fixture/flat-trace" "$fixture/control-replay" "$fixture/cached-cpu" "$fixture/cache-op" "$fixture/cache-fault" "$fixture/cache-store" "$fixture/halt" "$fixture/allocate" "$fixture/move16" "$fixture/cas" "$fixture/unaligned-lock" "$fixture/rte-user" "$fixture/restore-capture"
      python3 syn68k/tests/jit-build-id-test.py
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/aot-x64-cache-test.c \
        -o "$fixture/cache-test.exe"
      "$fixture/cache-test.exe" "$(wslpath -m "$fixture/unit")"
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-managed-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -o "$fixture/managed-test.exe"
      "$fixture/managed-test.exe"
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/aot-x64-execution-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -Wl,--wrap=trap_direct -o "$fixture/execution-test.exe"
      export SYN68K_AOT_CACHE
      export WSLENV="${WSLENV:+$WSLENV:}SYN68K_AOT_CACHE"
      SYN68K_AOT_CACHE=$(wslpath -m "$fixture/execution")
      "$fixture/execution-test.exe" 0
      "$fixture/execution-test.exe" 1
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/mapped-fpu-native-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -Wl,--wrap=syn68k_fpu_step_decoded,--wrap=syn68k_fpu_finish_command,--wrap=syn68k_fpu_finish_command_at,--wrap=syn68k_mmu040_translate \
        -o "$fixture/mapped-test.exe"
      "$fixture/mapped-test.exe" "$(wslpath -m "$fixture/mapped")" 0
      "$fixture/mapped-test.exe" "$(wslpath -m "$fixture/mapped")" 1
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/mapped-mmu-native-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_fpu_step_decoded,--wrap=syn68k_fpu_finish_command,--wrap=syn68k_fpu_finish_command_at,--wrap=syn68k_mmu040_translate \
        -Wl,--wrap=syn68k_integer_step_decoded,--wrap=syn68k_mmu040_control,--wrap=syn68k_mmu040_ptest,--wrap=syn68k_mmu040_flush \
        -o "$fixture/mmu-test.exe"
      "$fixture/mmu-test.exe" "$(wslpath -m "$fixture/mmu")" 0
      "$fixture/mmu-test.exe" "$(wslpath -m "$fixture/mmu")" 1
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/mapped-fpu-source-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_fpu_step_decoded,--wrap=syn68k_fpu_finish_command,--wrap=syn68k_fpu_finish_command_at,--wrap=syn68k_fpu_finish_captured \
        -Wl,--wrap=syn68k_mmu040_translate,--wrap=syn68k_effective_address -o "$fixture/source-test.exe"
      "$fixture/source-test.exe" "$(wslpath -m "$fixture/source")" 0
      "$fixture/source-test.exe" "$(wslpath -m "$fixture/source")" 1
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/mapped-fpu-control-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_fpu_step_decoded,--wrap=syn68k_fpu_finish_command,--wrap=syn68k_fpu_finish_command_at,--wrap=syn68k_fpu_finish_captured \
        -Wl,--wrap=syn68k_mmu040_translate,--wrap=syn68k_effective_address -o "$fixture/control-test.exe"
      "$fixture/control-test.exe" "$(wslpath -m "$fixture/control")" 0
      "$fixture/control-test.exe" "$(wslpath -m "$fixture/control")" 1
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/mapped-fpu-destination-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_fpu_step_decoded,--wrap=syn68k_fpu_finish_command,--wrap=syn68k_fpu_finish_command_at,--wrap=syn68k_fpu_finish_captured,--wrap=syn68k_fpu_finish_destination \
        -Wl,--wrap=syn68k_mmu040_translate,--wrap=syn68k_effective_address -o "$fixture/destination-test.exe"
      "$fixture/destination-test.exe" "$(wslpath -m "$fixture/destination")" 0
      "$fixture/destination-test.exe" "$(wslpath -m "$fixture/destination")" 1
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/mapped-fpu-list-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_fpu_step_decoded,--wrap=syn68k_fpu_finish_command,--wrap=syn68k_fpu_finish_command_at,--wrap=syn68k_fpu_finish_captured \
        -Wl,--wrap=syn68k_mmu040_translate,--wrap=syn68k_effective_address -o "$fixture/list-test.exe"
      "$fixture/list-test.exe" "$(wslpath -m "$fixture/list")" 0
      "$fixture/list-test.exe" "$(wslpath -m "$fixture/list")" 1
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/mapped-fpu-predicate-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_fpu_step_decoded,--wrap=syn68k_fpu_finish_command,--wrap=syn68k_fpu_finish_command_at,--wrap=syn68k_fpu_finish_captured \
        -Wl,--wrap=syn68k_mmu040_translate,--wrap=syn68k_effective_address -o "$fixture/predicate-test.exe"
      "$fixture/predicate-test.exe" "$(wslpath -m "$fixture/predicate")" 0
      "$fixture/predicate-test.exe" "$(wslpath -m "$fixture/predicate")" 1
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/mapped-fpu-frame-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_fpu_step_decoded,--wrap=syn68k_fpu_finish_command,--wrap=syn68k_fpu_finish_command_at,--wrap=syn68k_fpu_finish_captured,--wrap=syn68k_fpu_finish_frame \
        -Wl,--wrap=syn68k_mmu040_translate,--wrap=syn68k_effective_address -o "$fixture/frame-test.exe"
      "$fixture/frame-test.exe" "$(wslpath -m "$fixture/frame")" 0
      "$fixture/frame-test.exe" "$(wslpath -m "$fixture/frame")" 1
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/mapped-fpu-restore-capture-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$fixture/restore-capture-test.exe"
      "$fixture/restore-capture-test.exe" "$(wslpath -m "$fixture/restore-capture")" 0
      "$fixture/restore-capture-test.exe" "$(wslpath -m "$fixture/restore-capture")" 1
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/flat-fpu-native-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_fpu_step_decoded,--wrap=syn68k_fpu_finish_command,--wrap=syn68k_fpu_finish_command_at,--wrap=syn68k_fpu_finish_captured,--wrap=syn68k_fpu_finish_destination,--wrap=syn68k_fpu_finish_frame \
        -Wl,--wrap=syn68k_mmu040_translate,--wrap=syn68k_effective_address,--wrap=trap_direct,--wrap=trap_exception -o "$fixture/flat-test.exe"
      for compiled in 0 1; do
        directory=flat
        if [[ $compiled == 1 ]]; then directory=flat-compiled; fi
        "$fixture/flat-test.exe" "$(wslpath -m "$fixture/$directory")" 0 "$compiled"
        "$fixture/flat-test.exe" "$(wslpath -m "$fixture/$directory")" 1 "$compiled"
      done
      "$fixture/flat-test.exe" "$(wslpath -m "$fixture/flat-trace")" 0 2
      "$fixture/flat-test.exe" "$(wslpath -m "$fixture/flat-trace")" 1 2
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/mmu040-stream-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$fixture/control-replay-test.exe"
      "$fixture/control-replay-test.exe" "$(wslpath -m "$fixture/control-replay")" 0
      "$fixture/control-replay-test.exe" "$(wslpath -m "$fixture/control-replay")" 1
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/mapped-cache040-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$fixture/cached-cpu-test.exe"
      "$fixture/cached-cpu-test.exe" "$(wslpath -m "$fixture/cached-cpu")"
      "$fixture/cached-cpu-test.exe" "$(wslpath -m "$fixture/cached-cpu")"
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/mapped-cache-op-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$fixture/cache-op-test.exe"
      "$fixture/cache-op-test.exe" "$(wslpath -m "$fixture/cache-op")"
      "$fixture/cache-op-test.exe" "$(wslpath -m "$fixture/cache-op")"
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/mapped-cache-fault-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$fixture/cache-fault-test.exe"
      "$fixture/cache-fault-test.exe" "$(wslpath -m "$fixture/cache-fault")"
      "$fixture/cache-fault-test.exe" "$(wslpath -m "$fixture/cache-fault")"
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/mapped-cache-store-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$fixture/cache-store-test.exe"
      "$fixture/cache-store-test.exe" "$(wslpath -m "$fixture/cache-store")"
      "$fixture/cache-store-test.exe" "$(wslpath -m "$fixture/cache-store")"
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/mapped-halt-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$fixture/halt-test.exe"
      "$fixture/halt-test.exe" "$(wslpath -m "$fixture/halt")"
      "$fixture/halt-test.exe" "$(wslpath -m "$fixture/halt")"
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/mapped-cache-allocate-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_mmu040_translate,--wrap=syn68k_cache040_transfer -o "$fixture/allocate-test.exe"
      "$fixture/allocate-test.exe" "$(wslpath -m "$fixture/allocate")"
      "$fixture/allocate-test.exe" "$(wslpath -m "$fixture/allocate")"
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/mapped-cache-move16-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_mmu040_translate,--wrap=syn68k_cache040_move16 -o "$fixture/move16-test.exe"
      "$fixture/move16-test.exe" "$(wslpath -m "$fixture/move16")"
      "$fixture/move16-test.exe" "$(wslpath -m "$fixture/move16")"
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/mapped-cas040-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_mmu040_translate,--wrap=syn68k_cache040_transfer -o "$fixture/cas-test.exe"
      "$fixture/cas-test.exe" "$(wslpath -m "$fixture/cas")"
      "$fixture/cas-test.exe" "$(wslpath -m "$fixture/cas")"
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/mapped-locked-unaligned-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_mmu040_translate,--wrap=syn68k_cache040_transfer -o "$fixture/unaligned-lock-test.exe"
      "$fixture/unaligned-lock-test.exe" "$(wslpath -m "$fixture/unaligned-lock")"
      "$fixture/unaligned-lock-test.exe" "$(wslpath -m "$fixture/unaligned-lock")"
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/mapped-rte-user-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$fixture/rte-user-test.exe"
      "$fixture/rte-user-test.exe" "$(wslpath -m "$fixture/rte-user")" 0
      "$fixture/rte-user-test.exe" "$(wslpath -m "$fixture/rte-user")" 1
      cp syn68k/obj/src/runtime/generated/jit-build-id.h "$fixture/identity.h"
      # Rebuild identity stays stable when sources/configuration do not change.
      make -C syn68k/obj/src/runtime generated/jit-build-id.h
      cmp "$fixture/identity.h" syn68k/obj/src/runtime/generated/jit-build-id.h
      echo 'PASS: cold/warm native cache execution and malformed-file fallback'
    )
    ;;
  jit-x64-benchmark)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      cc=${CC:-x86_64-w64-mingw32-gcc}
      mkdir -p syn68k/obj/tests
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/jit-x64-benchmark.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/jit-x64-benchmark.exe
      # Timing is informational; correctness and actual backend use are asserted.
      for native in 0 1; do
        for memory in 0 1; do
          syn68k/obj/tests/jit-x64-benchmark.exe "$native" "$memory" \
            2>"syn68k/obj/tests/jit-x64-benchmark-$native-$memory.log"
        done
      done
    )
    ;;
  jit-x64)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      cc=${CC:-x86_64-w64-mingw32-gcc}
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-allocation-test.c \
        -o build/jit-x64-allocation-test.exe
      build/jit-x64-allocation-test.exe
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/jit-x64-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o build/jit-x64-test.exe
      build/jit-x64-test.exe 0
      build/jit-x64-test.exe 1
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/jit-x64-register-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o build/jit-x64-register-test.exe
      build/jit-x64-register-test.exe 0
      build/jit-x64-register-test.exe 1
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/jit-x64-integer-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o build/jit-x64-integer-test.exe
      build/jit-x64-integer-test.exe 0
      build/jit-x64-integer-test.exe 1
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-memory-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -o build/jit-x64-memory-test.exe
      build/jit-x64-memory-test.exe
      "$cc" -std=gnu17 -g -O0 -DTEST_NO_NATIVE -Isyn68k/obj/src/include \
        -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/jit-x64-memory-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o build/jit-x64-memory-fallback-test.exe
      build/jit-x64-memory-fallback-test.exe
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-helper-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -o build/jit-x64-helper-test.exe
      build/jit-x64-helper-test.exe
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-divide-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -Wl,--wrap=trap_direct \
        -o build/jit-x64-divide-test.exe
      build/jit-x64-divide-test.exe
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-multiply-long-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -Wl,--wrap=trap_direct \
        -o build/jit-x64-multiply-long-test.exe
      build/jit-x64-multiply-long-test.exe
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-divide-long-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -Wl,--wrap=trap_direct \
        -o build/jit-x64-divide-long-test.exe
      build/jit-x64-divide-long-test.exe
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-frame-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -Wl,--wrap=trap_direct \
        -o build/jit-x64-frame-test.exe
      build/jit-x64-frame-test.exe
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-movep-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -Wl,--wrap=trap_direct \
        -o build/jit-x64-movep-test.exe
      build/jit-x64-movep-test.exe
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-movem-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -Wl,--wrap=trap_direct \
        -o build/jit-x64-movem-test.exe
      build/jit-x64-movem-test.exe
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-source-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -Wl,--wrap=trap_direct \
        -o build/jit-x64-source-test.exe
      build/jit-x64-source-test.exe
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-indexed-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -Wl,--wrap=trap_direct \
        -o build/jit-x64-indexed-test.exe
      build/jit-x64-indexed-test.exe
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-memory-ops-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -Wl,--wrap=trap_direct \
        -o build/jit-x64-memory-ops-test.exe
      build/jit-x64-memory-ops-test.exe
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-pair-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -Wl,--wrap=trap_direct \
        -o build/jit-x64-pair-test.exe
      build/jit-x64-pair-test.exe
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-ccr-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -o build/jit-x64-ccr-test.exe
      build/jit-x64-ccr-test.exe
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-fast-memory-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -o build/jit-x64-fast-memory-test.exe
      build/jit-x64-fast-memory-test.exe
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-displacement-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -o build/jit-x64-displacement-test.exe
      build/jit-x64-displacement-test.exe
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-liveness-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -o build/jit-x64-liveness-test.exe
      build/jit-x64-liveness-test.exe
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-branch-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -o build/jit-x64-branch-test.exe
      build/jit-x64-branch-test.exe 0
      build/jit-x64-branch-test.exe 1
      "$cc" -std=gnu17 -g -O0 -Werror=implicit-function-declaration \
        -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-call-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -Wl,--wrap=trap_direct \
        -o build/jit-x64-call-test.exe
      build/jit-x64-call-test.exe

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-terminal-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -Wl,--wrap=trap_direct \
        -o build/jit-x64-terminal-test.exe
      build/jit-x64-terminal-test.exe

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-exception-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -Wl,--wrap=trap_direct \
        -o build/jit-x64-exception-test.exe
      build/jit-x64-exception-test.exe

      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-complex-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -Wl,--wrap=syn68k_x64_complex_run \
        -o build/jit-x64-complex-test.exe
      build/jit-x64-complex-test.exe
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        -Isyn68k/src/runtime/include syn68k/tests/jit-x64-coverage-test.c \
        syn68k/obj/src/runtime/libsyn68k.a -o build/jit-x64-coverage-test.exe
      build/jit-x64-coverage-test.exe
    )
    ;;
  fpu-native)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      cc=${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}
      mkdir -p syn68k/obj/tests
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/fpu-native-test.c syn68k/obj/src/runtime/libsyn68k.a -Wl,--wrap=trap_direct \
        -o syn68k/obj/tests/fpu-native-test.exe
      syn68k/obj/tests/fpu-native-test.exe
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/fpu-captured-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/fpu-captured-test.exe
      syn68k/obj/tests/fpu-captured-test.exe
      "$cc" -std=gnu17 -g -O2 -Wall -Wextra -Werror -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/fpu-command-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/fpu-command-test.exe
      syn68k/obj/tests/fpu-command-test.exe
      "$cc" -std=gnu17 -g -O2 -Wall -Wextra -Werror -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/fpu-reentry-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/fpu-reentry-test.exe
      syn68k/obj/tests/fpu-reentry-test.exe
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/mapped-fpu-native-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_fpu_step_decoded,--wrap=syn68k_fpu_finish_command,--wrap=syn68k_fpu_finish_command_at,--wrap=syn68k_mmu040_translate \
        -o syn68k/obj/tests/mapped-fpu-native-test.exe
      syn68k/obj/tests/mapped-fpu-native-test.exe
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/mapped-mmu-native-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_fpu_step_decoded,--wrap=syn68k_fpu_finish_command,--wrap=syn68k_fpu_finish_command_at,--wrap=syn68k_mmu040_translate \
        -Wl,--wrap=syn68k_integer_step_decoded,--wrap=syn68k_mmu040_control,--wrap=syn68k_mmu040_ptest,--wrap=syn68k_mmu040_flush \
        -o syn68k/obj/tests/mapped-mmu-native-test.exe
      syn68k/obj/tests/mapped-mmu-native-test.exe
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/mapped-fpu-source-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_fpu_step_decoded,--wrap=syn68k_fpu_finish_command,--wrap=syn68k_fpu_finish_command_at,--wrap=syn68k_fpu_finish_captured \
        -Wl,--wrap=syn68k_mmu040_translate,--wrap=syn68k_effective_address -o syn68k/obj/tests/mapped-fpu-source-test.exe
      syn68k/obj/tests/mapped-fpu-source-test.exe
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/mapped-fpu-control-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_fpu_step_decoded,--wrap=syn68k_fpu_finish_command,--wrap=syn68k_fpu_finish_command_at,--wrap=syn68k_fpu_finish_captured \
        -Wl,--wrap=syn68k_mmu040_translate,--wrap=syn68k_effective_address -o syn68k/obj/tests/mapped-fpu-control-test.exe
      syn68k/obj/tests/mapped-fpu-control-test.exe
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/mapped-fpu-destination-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_fpu_step_decoded,--wrap=syn68k_fpu_finish_command,--wrap=syn68k_fpu_finish_command_at,--wrap=syn68k_fpu_finish_captured,--wrap=syn68k_fpu_finish_destination \
        -Wl,--wrap=syn68k_mmu040_translate,--wrap=syn68k_effective_address -o syn68k/obj/tests/mapped-fpu-destination-test.exe
      syn68k/obj/tests/mapped-fpu-destination-test.exe
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/mapped-fpu-list-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_fpu_step_decoded,--wrap=syn68k_fpu_finish_command,--wrap=syn68k_fpu_finish_command_at,--wrap=syn68k_fpu_finish_captured \
        -Wl,--wrap=syn68k_mmu040_translate,--wrap=syn68k_effective_address -o syn68k/obj/tests/mapped-fpu-list-test.exe
      syn68k/obj/tests/mapped-fpu-list-test.exe
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/mapped-fpu-predicate-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_fpu_step_decoded,--wrap=syn68k_fpu_finish_command,--wrap=syn68k_fpu_finish_command_at,--wrap=syn68k_fpu_finish_captured \
        -Wl,--wrap=syn68k_mmu040_translate,--wrap=syn68k_effective_address -o syn68k/obj/tests/mapped-fpu-predicate-test.exe
      syn68k/obj/tests/mapped-fpu-predicate-test.exe
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/mapped-fpu-frame-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_fpu_step_decoded,--wrap=syn68k_fpu_finish_command,--wrap=syn68k_fpu_finish_command_at,--wrap=syn68k_fpu_finish_captured,--wrap=syn68k_fpu_finish_frame \
        -Wl,--wrap=syn68k_mmu040_translate,--wrap=syn68k_effective_address -o syn68k/obj/tests/mapped-fpu-frame-test.exe
      syn68k/obj/tests/mapped-fpu-frame-test.exe
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/mapped-fpu-restore-capture-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/mapped-fpu-restore-capture-test.exe
      syn68k/obj/tests/mapped-fpu-restore-capture-test.exe
      "$cc" -std=gnu17 -g -O2 -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/flat-fpu-native-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_fpu_step_decoded,--wrap=syn68k_fpu_finish_command,--wrap=syn68k_fpu_finish_command_at,--wrap=syn68k_fpu_finish_captured,--wrap=syn68k_fpu_finish_destination,--wrap=syn68k_fpu_finish_frame \
        -Wl,--wrap=syn68k_mmu040_translate,--wrap=syn68k_effective_address,--wrap=trap_direct,--wrap=trap_exception -o syn68k/obj/tests/flat-fpu-native-test.exe
      syn68k/obj/tests/flat-fpu-native-test.exe
      "$cc" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/checked-store-coherence-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o syn68k/obj/tests/checked-store-coherence-test.exe
      syn68k/obj/tests/checked-store-coherence-test.exe 0
      syn68k/obj/tests/checked-store-coherence-test.exe 1
    )
    ;;
  native040)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      base=${NATIVE040_OBJ:-"$root/build/68040-direct"}
      for abi in windows native; do
        obj=$base
        cc=${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}
        flags=(-g -O2 -fwrapv)
        executable=runner.exe
        if [[ $abi == native ]]; then
          obj+=/native
          cc=${NATIVE_CC:-gcc}
          flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
          executable=runner
        fi
        mkdir -p "$obj/tests"
        python3 syn68k/tools/build-native040.py "$obj/generated/native040.h" "$cc"
        includes=(-Isyn68k/src/runtime/include -Isyn68k/obj/src/include -Isyn68k/src/include -I"$obj")
        if [[ $abi == windows ]]; then
          "$cc" -std=gnu11 "${flags[@]}" "${includes[@]}" syn68k/tests/native040-publication-test.c \
            -o "$obj/tests/publication.exe"
          "$obj/tests/publication.exe"
        fi
        objects=()
        for name in mmu040 mmu040-memory cache040 native040-code ea; do
          "$cc" -std=gnu11 "${flags[@]}" "${includes[@]}" -ffunction-sections -fdata-sections \
            '-DSYN68K_EA_MAPPED(context)=1' -DRUNTIME -DNONDIRECT \
            -c "syn68k/src/runtime/$name.c" -o "$obj/tests/runtime-$name.o"
          objects+=("$obj/tests/runtime-$name.o")
        done
        "$cc" -std=gnu11 "${flags[@]}" -c syn68k/third_party/softfloat/softfloat.c -o "$obj/tests/softfloat.o"
        "$cc" -std=gnu11 "${flags[@]}" "${includes[@]}" -Wall -Wextra -Werror \
          -c syn68k/tests/native040-checks.c -o "$obj/tests/adapter.o"
        objects+=("$obj/tests/softfloat.o" "$obj/tests/adapter.o")
        "$cc" -std=gnu11 "${flags[@]}" "${includes[@]}" -ffunction-sections -fdata-sections \
          -DRUNTIME -DNONDIRECT \
          -c syn68k/src/runtime/flat-memory.c -o "$obj/tests/runtime-flat-init.o"
        objects+=("$obj/tests/runtime-flat-init.o")
        objcopy=$("$cc" -print-prog-name=objcopy)
        for group in math mmu memory fc ram ea convert bus control registers flat cache cache_ram cache_traffic mmu_cache mmu_cache_op cache_move16 mmu_move16 pagecross read_prefix; do
          fixture=native040-test
          extra=()
          case $group in
            mmu) fixture=mmu040-test ;;
            memory) fixture=mmu040-memory-test ;;
            fc) fixture=mmu040-fc-bus-test ;;
            ram) fixture=native040-ram-test ;;
            ea) fixture=native040-ea-test ;;
            convert) fixture=native040-convert-test ;;
            bus) fixture=native040-bus-test ;;
            control) fixture=native040-control-test ;;
            registers) fixture=native040-registers-test ;;
            flat) fixture=native040-flat-test ;;
            cache) fixture=cache040-test; extra=(-DCACHE040_NATIVE_TEST) ;;
            cache_ram) fixture=cache040-native-ram-test ;;
            cache_traffic) fixture=cache040-traffic-test; extra=(-DCACHE040_NATIVE_TEST) ;;
            mmu_cache) fixture=mmu040-cache-test; extra=(-DMMU040_CACHE_NATIVE) ;;
            mmu_cache_op) fixture=mmu040-cache-op-test; extra=(-DMMU040_CACHE_NATIVE) ;;
            cache_move16) fixture=cache040-move16-test; extra=(-DCACHE040_NATIVE_TEST) ;;
            mmu_move16) fixture=mmu040-move16-test; extra=(-DMMU040_MOVE16_NATIVE -DMMU040_MOVE16_GROUPED) ;;
            pagecross) fixture=mmu040-pagecross-test; extra=(-DMMU040_PAGECROSS_NATIVE -DMMU040_PAGECROSS_GROUPED) ;;
            read_prefix) fixture=mmu040-read-prefix-test; extra=(-DPARTIAL_NATIVE -DPARTIAL_NATIVE_GROUPED) ;;
          esac
          if [[ $group == mmu || $group == memory || $group == fc ]]; then extra=(-include syn68k/tests/native040-checks.h); fi
          "$cc" -std=gnu11 "${flags[@]}" "${includes[@]}" "${extra[@]}" \
            -c "syn68k/tests/$fixture.c" -o "$obj/tests/$group.o"
          "$objcopy" --redefine-sym "main=native040_${group}_check" "$obj/tests/$group.o"
          objects+=("$obj/tests/$group.o")
        done
        "$cc" -std=gnu11 "${flags[@]}" "${includes[@]}" syn68k/tests/native040-runner.c \
          "${objects[@]}" -Wl,--gc-sections,--wrap=syn68k_mmu040_translate,--wrap=syn68k_mmu040_memory_translate,--wrap=syn68k_mmu040_cache_maintain,--wrap=syn68k_cache040_maintain_interruptible,--wrap=syn68k_cache040_access,--wrap=syn68k_cache040_maintain,--wrap=syn68k_cache040_transfer,--wrap=syn68k_cache040_maintain_traffic,--wrap=syn68k_cache040_move16 -o "$obj/tests/$executable"
        "$obj/tests/$executable"
      done
    )
    ;;
  mmu040-cache)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      output=${MMU040_CACHE_OBJ:-syn68k/obj/tests/mmu040-cache}
      mkdir -p "$output"
      for fixture in mmu040-cache-test mmu040-cache-op-test cache040-move16-test mmu040-move16-test mmu040-read-prefix-test; do
        sources=(syn68k/src/runtime/mmu040.c syn68k/src/runtime/mmu040-memory.c
                 syn68k/src/runtime/cache040.c "syn68k/tests/$fixture.c")
        "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" -std=gnu17 -O2 -g -Wall -Wextra -Werror \
          -Isyn68k/src/runtime/include "${sources[@]}" -o "$output/cache-test.exe"
        "$output/cache-test.exe"
        "${NATIVE_CC:-gcc}" -std=gnu17 -O1 -g -Wall -Wextra -Werror \
          -fsanitize=address,undefined -fno-omit-frame-pointer \
          -Isyn68k/src/runtime/include "${sources[@]}" -o "$output/cache-test"
        "$output/cache-test"
      done
    )
    ;;
  mmu040)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      output=syn68k/obj/tests/mmu040
      mkdir -p "$output"
      includes=(-Isyn68k/src/runtime/include)
      warnings=(-std=c11 -Wall -Wextra -Werror)
      "${NATIVE_CC:-gcc}" "${warnings[@]}" -g -O1 \
        -fsanitize=address,undefined -fno-omit-frame-pointer "${includes[@]}" \
        syn68k/src/runtime/mmu040.c syn68k/tests/mmu040-test.c \
        -o "$output/sanitized-test"
      "$output/sanitized-test"
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" "${warnings[@]}" -g -O2 "${includes[@]}" \
        syn68k/tests/mmu040-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$output/mmu040-test.exe"
      "$output/mmu040-test.exe"
      for pagecross_cc in "${NATIVE_CC:-gcc}" "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}"; do
        pagecross_flags=(-g -O1)
        pagecross_exe="$output/pagecross.exe"
        if [[ $pagecross_cc == "${NATIVE_CC:-gcc}" ]]; then
          pagecross_flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
          pagecross_exe="$output/pagecross"
        fi
        "$pagecross_cc" "${warnings[@]}" "${pagecross_flags[@]}" "${includes[@]}" \
          syn68k/src/runtime/mmu040.c syn68k/src/runtime/mmu040-memory.c syn68k/src/runtime/cache040.c \
          syn68k/tests/mmu040-pagecross-test.c -o "$pagecross_exe"
        "$pagecross_exe"
      done
      "${NATIVE_CC:-gcc}" "${warnings[@]}" -g -O1 \
        -fsanitize=address,undefined -fno-omit-frame-pointer "${includes[@]}" \
        syn68k/src/runtime/mmu040.c syn68k/src/runtime/mmu040-memory.c syn68k/src/runtime/cache040.c \
        syn68k/tests/mmu040-memory-test.c -o "$output/sanitized-memory-test"
      "$output/sanitized-memory-test"
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" "${warnings[@]}" -g -O2 "${includes[@]}" \
        syn68k/tests/mmu040-memory-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$output/mmu040-memory-test.exe"
      "$output/mmu040-memory-test.exe"
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" "${warnings[@]}" -g -O2 "${includes[@]}" \
        -std=gnu11 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/mmu040-ea-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$output/mmu040-ea-test.exe"
      "$output/mmu040-ea-test.exe"
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" "${warnings[@]}" -g -O2 "${includes[@]}" \
        -std=gnu11 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/mmu040-fpu-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$output/mmu040-fpu-test.exe"
      "$output/mmu040-fpu-test.exe"
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" "${warnings[@]}" -g -O2 "${includes[@]}" \
        -std=gnu11 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/mmu040-integer-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$output/mmu040-integer-test.exe"
      "$output/mmu040-integer-test.exe"
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" "${warnings[@]}" -g -O2 "${includes[@]}" \
        -std=gnu11 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/mmu040-movem-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$output/mmu040-movem-test.exe"
      "$output/mmu040-movem-test.exe"
      "${NATIVE_CC:-gcc}" "${warnings[@]}" -g -O1 \
        -fsanitize=address,undefined -fno-omit-frame-pointer "${includes[@]}" \
        syn68k/src/runtime/mmu040.c syn68k/src/runtime/mmu040-memory.c syn68k/src/runtime/cache040.c \
        syn68k/tests/mmu040-fc-bus-test.c -o "$output/sanitized-fc-bus-test"
      "$output/sanitized-fc-bus-test"
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" "${warnings[@]}" -g -O2 "${includes[@]}" \
        syn68k/tests/mmu040-fc-bus-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$output/mmu040-fc-bus-test.exe"
      "$output/mmu040-fc-bus-test.exe"
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" "${warnings[@]}" -g -O2 "${includes[@]}" \
        -std=gnu11 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/mmu040-moves-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$output/mmu040-moves-test.exe"
      "$output/mmu040-moves-test.exe"
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" "${warnings[@]}" -g -O2 "${includes[@]}" \
        -std=gnu11 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/mmu040-control-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$output/mmu040-control-test.exe"
      "$output/mmu040-control-test.exe"
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" "${warnings[@]}" -g -O2 "${includes[@]}" \
        -std=gnu11 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/mmu040-movep-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$output/mmu040-movep-test.exe"
      "$output/mmu040-movep-test.exe"
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" "${warnings[@]}" -g -O2 "${includes[@]}" \
        -std=gnu11 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/mmu040-operands-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$output/mmu040-operands-test.exe"
      "$output/mmu040-operands-test.exe"
      "$output/mmu040-operands-test.exe" --cas-native
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" "${warnings[@]}" -g -O2 "${includes[@]}" \
        -std=gnu11 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/mmu040-flow-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$output/mmu040-flow-test.exe"
      "$output/mmu040-flow-test.exe"
      # The legacy private header applies packed to byte fields; GCC ignores it.
      # Keep that diagnostic visible without disabling other fixture errors.
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" "${warnings[@]}" -Wno-error=attributes -g -O2 "${includes[@]}" \
        -std=gnu11 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/mmu040-stream-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$output/mmu040-stream-test.exe"
      "$output/mmu040-stream-test.exe"
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" -std=gnu17 -g -O0 \
        -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/mapped-cache-op-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$output/cache-op-test.exe"
      "$output/cache-op-test.exe"
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" -std=gnu17 -g -O0 \
        -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/mapped-cache-fault-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$output/cache-fault-test.exe"
      "$output/cache-fault-test.exe"
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" -std=gnu17 -g -O0 \
        -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/mapped-cache-store-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$output/cache-store-test.exe"
      "$output/cache-store-test.exe"
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" -std=gnu17 -g -O0 \
        -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/mapped-halt-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$output/halt-test.exe"
      "$output/halt-test.exe"
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" -std=gnu17 -g -O0 \
        -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/mapped-cache-allocate-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_mmu040_translate,--wrap=syn68k_cache040_transfer -o "$output/allocate-test.exe"
      "$output/allocate-test.exe"
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" -std=gnu17 -g -O0 \
        -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/mapped-cache-move16-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_mmu040_translate,--wrap=syn68k_cache040_move16 -o "$output/move16-test.exe"
      "$output/move16-test.exe"
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" -std=gnu17 -g -O0 \
        -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/mapped-cas040-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_mmu040_translate,--wrap=syn68k_cache040_transfer -o "$output/cas-test.exe"
      "$output/cas-test.exe"
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" -std=gnu17 -g -O0 \
        -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/mapped-locked-unaligned-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -Wl,--wrap=syn68k_mmu040_translate,--wrap=syn68k_cache040_transfer -o "$output/unaligned-lock-test.exe"
      "$output/unaligned-lock-test.exe"
      "${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}" -std=gnu17 -g -O0 \
        -Isyn68k/obj/src/include -Isyn68k/src/include -Isyn68k/src/runtime/include \
        syn68k/tests/mapped-rte-user-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o "$output/rte-user-test.exe"
      "$output/rte-user-test.exe"
    )
    ;;
  threaded-dispatch)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      mkdir -p build
      cc=${CC:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-gcc}
      flags=(-DEXPECT_THREADED=0)
      if [[ ${SYN68K_DISPATCH:-switch} == threaded || ${WINDOWS_TARGET:-x86_64-w64-mingw32} == i686-* ]]; then
        flags=(-DEXPECT_THREADED=1)
      fi
      "$cc" "${flags[@]}" -std=gnu17 -g -O0 -Isyn68k/obj/src/include -Isyn68k/src/include \
        syn68k/tests/threaded-dispatch-test.c syn68k/obj/src/runtime/libsyn68k.a \
        -o build/threaded-dispatch-test.exe
      build/threaded-dispatch-test.exe
    )
    ;;
  *)
    printf 'Unknown check: %s. Use --list.\n' "$check" >&2
    exit 2
    ;;
esac
