#!/usr/bin/env bash
# +===================================================================
#   File:      check-core.sh
#
#   Summary:   Build and run grouped and focused PowerPC regression checks.
#
#   Classes:   None.
#
#   Functions: Script entry point and named regression checks; use --list.
#
#   Origin:    Consolidated repository regression checks; original file-specific notices remain below.
#
#   Copyright and Legal notices: Applicable repository license: synppc/COPYING.
# ===================================================================+

# Keep each fixture in its own translation unit and link it into a shared runner.
build_native_memory_runner() (
  set -euo pipefail
  local project=$1 obj=$2 compiler_string=$3
  shift 3
  local compiler=() objects=() source name
  read -r -a compiler <<<"$compiler_string"
  mkdir -p "$obj/generated"
  python3 "$project/tools/ppc-native-memory.py" "$obj/generated/ppc-native-memory.h" "$compiler_string"
  for source in "$project"/src/{cache750,l2cache750,system750,mmu750,bus750,ppc-native-memory-code}.cpp \
    "$project/tests/ppc_native_memory_checks.cpp"; do
    name=$(basename "$source" .cpp)
    "${compiler[@]}" -std=gnu++11 "$@" -I"$project/include" -I"$obj" -c "$source" -o "$obj/$name.o"
    objects+=("$obj/$name.o")
  done
  for source in "$project"/tests/ppc_{system750,cache_collision,snoop750,bus750,cache750,clock750,mmu750}_test.cpp \
    "$project/tests/ppc_l2cache750_checks.cpp"; do
    name=$(basename "$source" .cpp)
    "${compiler[@]}" -std=gnu++11 "$@" -I"$project/include" \
      -include "$project/tests/ppc_native_memory_checks.h" -c "$source" -o "$obj/$name.o"
    objects+=("$obj/$name.o")
  done
  "${compiler[@]}" "$@" "${objects[@]}" -o "$obj/runner"
)

build_core_runner() (
  set -e
  local project=$1 obj=$2 cxx=$3 suite=$4
  shift 4
  local runner_dir="$obj/tests/core" name objcopy target
  local link_flags=() executable=$suite
  local default_checks=(stfiwx memory_encoding multiple string dcbz ni_operand fpr_jit branch_jit
    conditional_branch_jit indirect_branch_jit cr_jit cr_transfer_jit spr_jit shift_jit
    carry_jit extended_carry_jit multiply_jit divide_jit multiply_overflow_jit reservation
    fpscr estimate)
  local checks=("${default_checks[@]}") plain_checks=() argument_checks=() objects=()
  local inc=(-I"$project/include")
  if [[ $suite == ppc-core-checks ]]; then
    local source="$project/third_party/sheepshaver"
    inc+=(-I"$project/platform" -I"$source/include" -I"$source/src" -I"$obj")
    plain_checks+=(jit_cache conversion_hardware)
    argument_checks+=(runtime memory_jit hardware_fpu alu pef_loader)
    checks+=("${plain_checks[@]}" "${argument_checks[@]}")
    default_checks+=(jit_cache runtime alu)
  fi
  mkdir -p "$runner_dir"
  objcopy=$("$cxx" -print-prog-name=objcopy)
  target=$("$cxx" -dumpmachine)
  if [[ $target == *mingw* ]]; then
    link_flags=(-static-libgcc -static-libstdc++)
    executable+=.exe
  fi
  {
    printf '#include <cstdio>\n#include <cstring>\n\n'
    for name in "${checks[@]}"; do
      if [[ " ${argument_checks[*]} " == *" $name "* ]]; then
        printf 'extern "C" int ppc_%s_check(int, char**);\n' "$name"
      else
        printf 'extern "C" int ppc_%s_check();\n' "$name"
      fi
    done
    printf '\nint main(int argc, char** argv)\n{\n    if (argc > 1) {\n'
    for name in "${checks[@]}"; do
      printf '        if (std::strcmp(argv[1], "%s") == 0) {\n' "${name//_/-}"
      printf '            std::puts("Running %s");\n            std::fflush(stdout);\n' "$name"
      if [[ " ${argument_checks[*]} " == *" $name "* ]]; then
        printf '            return ppc_%s_check(argc - 1, argv + 1);\n' "$name"
      else
        printf '            if (argc == 2)\n                return ppc_%s_check();\n' "$name"
      fi
      printf '        }\n'
    done
    printf '        std::fprintf(stderr, "Invalid check or arguments: %%s\\n", argv[1]);\n        return 2;\n    }\n'
    for name in "${default_checks[@]}"; do
      printf '    std::puts("Running %s");\n    std::fflush(stdout);\n' "$name"
      if [[ " ${argument_checks[*]} " == *" $name "* ]]; then
        printf '    if (int status = ppc_%s_check(argc, argv))\n        return status;\n' "$name"
      else
        printf '    if (int status = ppc_%s_check())\n        return status;\n' "$name"
      fi
    done
    printf '    std::puts("All %s PPC checks passed");\n}\n' "${#default_checks[@]}"
  } >"$runner_dir/$suite.cpp"
  for name in "${checks[@]}"; do
    # Compile each main with its standard implicit return, then rename it for the shared link.
    local fixture="$project/tests/ppc_${name}_test.cpp" warn=()
    if [[ $name == pef_loader ]]; then fixture="$project/tests/pef_loader_test.cpp"; fi
    if [[ $name == memory_jit ]]; then warn=(-O2 -Wall -Wextra -Werror); fi
    "$cxx" -std=gnu++11 "$@" "${warn[@]}" "${inc[@]}" -c "$fixture" -o "$runner_dir/$name.o"
    "$objcopy" --redefine-sym "main=ppc_${name}_check" "$runner_dir/$name.o"
    objects+=("$runner_dir/$name.o")
  done
  "$cxx" -std=gnu++11 "$@" "${link_flags[@]}" "$runner_dir/$suite.cpp" \
    "${objects[@]}" "$obj/libsynppc.a" -o "$runner_dir/$executable"
  printf '%s\n' "$runner_dir/$executable"
)

run_model_checks() (
  local project=$1 obj=$2 cxx=$3
  shift 3
  local runner_dir="$obj/tests/models" cases_dir="$obj/tests/models/cases"
  local link_flags=() executable=ppc-model-checks
  mkdir -p "$cases_dir"
  python3 "$project/tests/arithmetic-reference.py" "$cases_dir/arithmetic.txt"
  python3 "$project/tests/rounding-status-reference.py" "$cases_dir/arithmetic.txt" "$cases_dir/rounding-status.txt"
  python3 "$project/tests/integer-reference.py" "$cases_dir/integer.txt"
  python3 "$project/tests/compare-reference.py" "$cases_dir/compare.txt"
  python3 "$project/tests/fused-reference.py" "$cases_dir/fused.txt"
  python3 "$project/tests/fused-status-reference.py" "$cases_dir/fused.txt" "$cases_dir/fused-status.txt"
  if [[ $("$cxx" -dumpmachine) == *mingw* ]]; then
    link_flags=(-static-libgcc -static-libstdc++)
    executable+=.exe
    cases_dir=$(wslpath -m "$cases_dir")
  fi
  "$cxx" -std=gnu++11 "$@" "${link_flags[@]}" -I"$project/include" \
    "$project/tests/ppc_model_checks.cpp" "$project"/tests/ppc_{arithmetic,rounding_status,integer,compare,fused_status}_test.cpp \
    "$obj/libsynppc.a" -o "$runner_dir/$executable"
  if [[ ${SYN_PPC_SKIP_RUN:-0} == 1 ]]; then exit; fi
  "$runner_dir/$executable" "$cases_dir"
)

check=${1:---list}
if [ "$#" -gt 0 ]; then shift; fi
case "$check" in
  native-memory750)
    (
      set -euo pipefail
      project=$(cd "$(dirname "$0")/.." && pwd)
      obj=${OBJ:-"$project/obj"}/tests/native-memory750
      cxx=${PPC_CXX:-${CXX:-x86_64-w64-mingw32-g++}}
      build_native_memory_runner "$project" "$obj/windows" "$cxx" -g -O2 -Wall -Wextra -Werror \
        -static-libgcc -static-libstdc++
      runner="$obj/windows/runner"
      if [[ -f $runner.exe ]]; then runner+=.exe; fi
      if [[ ${SYN_PPC_SKIP_RUN:-0} != 1 ]]; then "$runner" "$@"; fi
      build_native_memory_runner "$project" "$obj/native" "${NATIVE_CXX:-g++}" -g -O1 -fno-omit-frame-pointer \
        -fsanitize=address,undefined -fno-sanitize-recover=all
      if [[ ${SYN_PPC_SKIP_RUN:-0} != 1 ]]; then "$obj/native/runner" "$@"; fi
    )
    ;;
  translation-all)
    (
      set -euo pipefail
      project=$(cd "$(dirname "$0")/.." && pwd)
      base=${OBJ:-"$project/obj"}
      WINDOWS_TARGET=x86_64-w64-mingw32 OBJ="$base" PPC_CXX=x86_64-w64-mingw32-g++ \
        CXX=x86_64-w64-mingw32-g++ bash "$project/tests/check-core.sh" translation
      native="$base/native-translation"
      OBJ="$native" PPC_CXX="${NATIVE_CXX:-g++}" CXX="${NATIVE_CXX:-g++}" \
        bash "$project/tests/check-core.sh" translation
      "$base/tests/translation/ppc-translation-checks.exe" --reject \
        "$(wslpath -w "$native/tests/translation/replay.cache")"
      "$native/tests/translation/ppc-translation-checks.exe" --reject \
        "$base/tests/translation/replay.cache"
    )
    ;;
  --list | list)
    printf '%s\n' 'host750' 'hardware750' 'native-memory750' 'collision750' 'snoop750' 'cache750' 'timebase' 'clock750' 'system750' 'state750' 'bus750' 'aot' 'mmu750' 'jit' 'translation' 'translation-all' 'fused' 'hardware-fpu' 'hardware-integer' 'model' 'sanitized' 'core' 'thread'
    ;;
  timebase)
    (
      set -euo pipefail
      project=$(cd "$(dirname "$0")/.." && pwd)
      obj=${OBJ:-"$project/obj"}
      cxx=${PPC_CXX:-${CXX:-x86_64-w64-mingw32-g++}}
      if [[ ${SYN_PPC_SKIP_BUILD:-0} != 1 ]]; then
        make -C "$project" OBJ="$obj" CXX="$cxx" -j"${JOBS:-1}"
      fi
      "$cxx" -std=gnu++11 -g -O2 -static-libgcc -static-libstdc++ -I"$project/include" \
        -Wl,--wrap=_Z13GetTicks_usecv "$project/tests/ppc_timebase_alias_test.cpp" \
        "$obj/libsynppc.a" -o "$obj/ppc-timebase-alias-test.exe"
      "$obj/ppc-timebase-alias-test.exe"
    )
    ;;
  host750 | system750)
    (
      set -euo pipefail
      project=$(cd "$(dirname "$0")/.." && pwd)
      obj=${OBJ:-"$project/obj"}
      cxx=${PPC_CXX:-${CXX:-x86_64-w64-mingw32-g++}}
      if [[ ${SYN_PPC_SKIP_BUILD:-0} != 1 ]]; then
        make -C "$project" OBJ="$obj" CXX="$cxx" -j"${JOBS:-1}"
      fi
      if [[ $check == host750 ]]; then
        "$cxx" -std=gnu++11 -g -O2 -static-libgcc -static-libstdc++ -I"$project/include" \
          "$project/tests/ppc_host_cache_test.cpp" "$obj/libsynppc.a" -o "$obj/ppc-host-cache-test.exe"
        if [[ ${SYN_PPC_SKIP_RUN:-0} != 1 ]]; then "$obj/ppc-host-cache-test.exe"; fi
        exit
      fi
      "$cxx" -std=gnu++11 -g -O2 -static-libgcc -static-libstdc++ -I"$project/include" \
        -DSYN_PPC_CHECK_NATIVE_FETCH -Wl,--wrap=ROMlib_ppc_750_read \
        -Wl,--wrap=ROMlib_ppc_750_write -Wl,--wrap=ROMlib_ppc_750_cache_control -Wl,--wrap=ROMlib_ppc_750_external \
        -Wl,--wrap=ROMlib_ppc_750_load_reserve -Wl,--wrap=ROMlib_ppc_750_store_conditional -Wl,--wrap=ROMlib_ppc_750_write_spr -Wl,--wrap=ROMlib_ppc_750_set_msr \
        "$project/tests/ppc_system_execution_test.cpp" "$obj/libsynppc.a" -o "$obj/ppc-system-execution-test.exe"
      fixture="$obj/tests/system750/pef"
      python3 "$project/tests/make_ppc_system_probe.py" "$fixture"
      if [[ $("$cxx" -dumpmachine) == *mingw* ]]; then
        fixture=$(wslpath -m "$fixture")
      fi
      if [[ ${SYN_PPC_SKIP_RUN:-0} != 1 ]]; then
        "$obj/ppc-system-execution-test.exe" --suite "$fixture/PPC 750 "{System,FP,Break,Watch,Program,Cache,Reserve}" Probe"
      fi
    )
    ;;
  hardware750 | collision750 | snoop750 | cache750 | clock750 | state750 | bus750 | mmu750)
    (
      set -euo pipefail
      project=$(cd "$(dirname "$0")/.." && pwd)
      obj=${OBJ:-"$project/obj"}/tests/hardware750
      mkdir -p "$obj"
      cxx=${PPC_CXX:-${CXX:-x86_64-w64-mingw32-g++}}
      sources=("$project"/src/{cache750,l2cache750,system750,mmu750,bus750}.cpp
        "$project"/tests/ppc_{system750,cache_collision,snoop750,bus750,cache750,clock750,mmu750}_test.cpp
        "$project/tests/ppc_l2cache750_checks.cpp")
      args=()
      if [[ $check != hardware750 ]]; then args+=("$check"); fi
      "$cxx" -std=gnu++11 -g -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ \
        -I"$project/include" "${sources[@]}" -o "$obj/windows.exe"
      if [[ ${SYN_PPC_SKIP_RUN:-0} != 1 ]]; then "$obj/windows.exe" "${args[@]}"; fi
      ${NATIVE_CXX:-g++} -std=gnu++11 -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined \
        -fno-sanitize-recover=all -I"$project/include" "${sources[@]}" -o "$obj/sanitized"
      if [[ ${SYN_PPC_SKIP_RUN:-0} != 1 ]]; then "$obj/sanitized" "${args[@]}"; fi
    )
    ;;
  aot | jit | translation)
    (
      set -euo pipefail
      project=$(cd "$(dirname "$0")/.." && pwd)
      obj=${OBJ:-"$project/obj"}
      source="$project/third_party/sheepshaver"
      cxx=${PPC_CXX:-${CXX:-x86_64-w64-mingw32-g++}}
      if [[ ${SYN_PPC_SKIP_BUILD:-0} != 1 ]]; then
        make -C "$project" OBJ="$obj" CXX="$cxx" -j"${JOBS:-1}"
      fi
      runner_dir="$obj/tests/translation"
      mkdir -p "$runner_dir"
      executable="$runner_dir/ppc-translation-checks.exe"
      inc=(-I"$project/platform" -I"$project/include" -I"$source/include" -I"$source/src" -I"$obj")
      "$cxx" -std=gnu++11 -g -O2 -static-libgcc -static-libstdc++ -DSYNPPC_TRANSLATION_CHECKS "${inc[@]}" \
        "$project"/tests/ppc_{translation_checks,translation_cache_test,kernel_jit_test}.cpp \
        "$obj/libsynppc.a" -o "$executable"
      if [[ ${SYN_PPC_SKIP_RUN:-0} == 1 ]]; then exit; fi
      if [[ $check == jit ]]; then
        "$executable" --jit
        exit
      fi
      python3 "$project/tests/ppc_build_id_test.py"
      if [[ $check == aot ]]; then
        "$executable" --aot
      else
        "$executable"
      fi
      cache_path="$runner_dir/replay.cache"
      if [[ $("$cxx" -dumpmachine) == *mingw* ]]; then
        cache_path=$(wslpath -w "$cache_path")
      fi
      "$executable" --save "$cache_path"
      "$executable" --load "$cache_path"
    )
    ;;
  fused)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      output=synppc/obj/tests/fused
      mkdir -p "$output"
      python3 synppc/tests/fused-reference.py "$output/cases.txt"
      ${NATIVE_CXX:-g++} -std=gnu++11 -g -O1 -fno-omit-frame-pointer \
        -fsanitize=address,undefined synppc/tests/ppc_fused_test.cpp \
        synppc/src/ppc-fused.cpp -o "$output/native"
      "$output/native" <"$output/cases.txt"
      ${PPC_CXX:-x86_64-w64-mingw32-g++} -std=gnu++11 -g -O2 \
        -static-libgcc -static-libstdc++ synppc/tests/ppc_fused_test.cpp \
        synppc/src/ppc-fused.cpp -o "$output/windows.exe"
      "$output/windows.exe" <"$output/cases.txt"
    )
    ;;
  hardware-fpu)
    (
      set -euo pipefail
      if [[ $# != 1 ]]; then
        echo "Usage: $0 decoded-hardware.txt" >&2
        exit 2
      fi
      project=$(cd "$(dirname "$0")/.." && pwd)
      obj=${OBJ:-"$project/obj"}
      cxx=${PPC_CXX:-${CXX:-x86_64-w64-mingw32-g++}}
      if [[ ${SYN_PPC_SKIP_BUILD:-0} != 1 ]]; then
        make -C "$project" OBJ="$obj" CXX="$cxx"
      fi
      "$cxx" -std=gnu++11 -g -O1 -static-libgcc -static-libstdc++ \
        -I"$project/include" "$project/tests/ppc_hardware_fpu_test.cpp" \
        "$obj/libsynppc.a" -o "$obj/ppc-hardware-fpu-test.exe"
      "$obj/ppc-hardware-fpu-test.exe" <"$1"
    )
    ;;
  hardware-integer)
    (
      set -euo pipefail
      if [[ $# != 1 ]]; then
        echo "Usage: $0 decoded-hardware.txt" >&2
        exit 2
      fi
      project=$(cd "$(dirname "$0")/.." && pwd)
      obj=${OBJ:-"$project/obj"}
      cxx=${PPC_CXX:-${CXX:-x86_64-w64-mingw32-g++}}
      if [[ ${SYN_PPC_SKIP_BUILD:-0} != 1 ]]; then
        make -C "$project" OBJ="$obj" CXX="$cxx"
      fi
      "$cxx" -std=gnu++11 -g -O1 -static-libgcc -static-libstdc++ \
        -I"$project/include" "$project/tests/ppc_hardware_integer_test.cpp" \
        "$obj/libsynppc.a" -o "$obj/ppc-hardware-integer-test.exe"
      capture=$(realpath "$1")
      if [[ $("$cxx" -dumpmachine) == *mingw* ]]; then
        capture=$(wslpath -m "$capture")
      fi
      # A file argument avoids Windows interop stdin/stderr pipe deadlocks.
      "$obj/ppc-hardware-integer-test.exe" "$capture"
    )
    ;;
  model)
    (
      set -euo pipefail
      project=$(cd "$(dirname "$0")/.." && pwd)
      obj=${OBJ:-"$project/obj"}
      cxx=${PPC_CXX:-${CXX:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-g++}}
      if [[ ${SYN_PPC_SKIP_BUILD:-0} != 1 ]]; then
        make -C "$project" OBJ="$obj" CXX="$cxx" -j"${JOBS:-1}"
      fi
      run_model_checks "$project" "$obj" "$cxx" -g -O1
    )
    ;;
  sanitized)
    (
      set -euo pipefail
      project=$(cd "$(dirname "$0")/.." && pwd)
      obj=${OBJ:-"$project/obj/sanitized"}
      cxx=${CXX:-g++}
      flags=(-g -O1 -fno-strict-aliasing -fno-omit-frame-pointer
        -fsanitize=address,undefined -fno-sanitize-recover=all)
      export ASAN_OPTIONS=${ASAN_OPTIONS:-quarantine_size_mb=16}
      export UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1}

      # Instrument the CPU, adapters, loader and SoftFloat as well as the fixtures.
      # Keep these native objects separate from Windows and ordinary Linux builds.
      make -C "$project" OBJ="$obj" CXX="$cxx" AR="${AR:-ar}" \
        CXXFLAGS="-std=gnu++11 ${flags[*]}" \
        SOFTFLOAT_CFLAGS="-std=c11 ${flags[*]}" -j"${JOBS:-1}"

      if [[ $# == 0 ]]; then
        runner=$(build_core_runner "$project" "$obj" "$cxx" ppc-instruction-checks "${flags[@]}")
        "$runner"
        set -- runtime ni reciprocal conversion memory-jit alu pef
      fi
      for test in "$@"; do
        if [[ $test == model ]]; then
          run_model_checks "$project" "$obj" "$cxx" "${flags[@]}"
          continue
        fi
        link_flags=()
        case "$test" in
          timebase)
            source="$project/tests/ppc_timebase_alias_test.cpp"
            link_flags=(-Wl,--wrap=_Z13GetTicks_usecv)
            ;;
          host750)
            source="$project/tests/ppc_host_cache_test.cpp"
            ;;
          system750)
            source="$project/tests/ppc_system_execution_test.cpp"
            ;;
          runtime | reservation | alu | fpscr | estimate | stfiwx | dcbz | multiple | string)
            source="$project/tests/ppc_${test}_test.cpp"
            ;;
          pef)
            source="$project/tests/pef_loader_test.cpp"
            ;;
          ni | reciprocal)
            source="$project/tests/ppc_hardware_fpu_test.cpp"
            ;;
          conversion)
            source="$project/tests/ppc_conversion_hardware_test.cpp"
            ;;
          memory-encoding)
            source="$project/tests/ppc_memory_encoding_test.cpp"
            ;;
          kernel-jit)
            source="$project/tests/ppc_kernel_jit_test.cpp"
            ;;
          memory-jit)
            source="$project/tests/ppc_memory_jit_test.cpp"
            ;;
          ni-operand)
            source="$project/tests/ppc_ni_operand_test.cpp"
            ;;
          fpr-jit)
            source="$project/tests/ppc_fpr_jit_test.cpp"
            ;;
          shift-jit)
            source="$project/tests/ppc_shift_jit_test.cpp"
            ;;
          branch-jit)
            source="$project/tests/ppc_branch_jit_test.cpp"
            ;;
          conditional-branch-jit)
            source="$project/tests/ppc_conditional_branch_jit_test.cpp"
            ;;
          indirect-branch-jit)
            source="$project/tests/ppc_indirect_branch_jit_test.cpp"
            ;;
          cr-jit)
            source="$project/tests/ppc_cr_jit_test.cpp"
            ;;
          cr-transfer-jit)
            source="$project/tests/ppc_cr_transfer_jit_test.cpp"
            ;;
          spr-jit)
            source="$project/tests/ppc_spr_jit_test.cpp"
            ;;
          carry-jit)
            source="$project/tests/ppc_carry_jit_test.cpp"
            ;;
          extended-carry-jit)
            source="$project/tests/ppc_extended_carry_jit_test.cpp"
            ;;
          divide-jit)
            source="$project/tests/ppc_divide_jit_test.cpp"
            ;;
          multiply-overflow-jit)
            source="$project/tests/ppc_multiply_overflow_jit_test.cpp"
            ;;
          multiply-jit)
            source="$project/tests/ppc_multiply_jit_test.cpp"
            ;;
          *)
            echo "Unknown sanitizer fixture: $test" >&2
            exit 2
            ;;
        esac
        "$cxx" -std=gnu++11 "${flags[@]}" -I"$project/include" \
          "${link_flags[@]}" "$source" "$obj/libsynppc.a" -o "$obj/$test-sanitized-test"
        if [[ $test == ni ]]; then
          "$obj/$test-sanitized-test" --include-ni --dispatch \
            <"$project/tests/data/ppc-ni-603-normal.txt"
          "$obj/$test-sanitized-test" --include-ni --dispatch \
            <"$project/tests/data/ppc-ni-603-underflow-enabled.txt"
        elif [[ $test == reciprocal ]]; then
          "$obj/$test-sanitized-test" --include-ni --reciprocal-powers --dispatch \
            <"$project/tests/data/ppc-fres-603-powers.txt"
        elif [[ $test == conversion ]]; then
          "$obj/$test-sanitized-test" \
            <"$project/tests/data/ppc-conversion-603-normal.txt"
        else
          "$obj/$test-sanitized-test"
        fi
      done
    )
    ;;
  core)
    (
      set -euo pipefail
      oracle=false
      selected=()
      if [[ $# == 1 && $1 == --oracle ]]; then
        oracle=true
      elif [[ $# != 0 ]]; then
        selected=("$@")
      fi
      project=$(cd "$(dirname "$0")/.." && pwd)
      root=$(cd "$project/.." && pwd)
      obj=${OBJ:-"$project/obj"}
      python3 "$project/tests/check-softfloat-source.py"
      mkdir -p "$obj"
      cxx=${PPC_CXX:-${CXX:-${WINDOWS_TARGET:-x86_64-w64-mingw32}-g++}}
      if [[ ${SYN_PPC_SKIP_BUILD:-0} != 1 ]]; then
        make -C "$project" OBJ="$obj" CXX="$cxx" -j"${JOBS:-1}"
      fi
      runner=$(build_core_runner "$project" "$obj" "$cxx" ppc-core-checks -g -O1)
      if [[ ${SYN_PPC_SKIP_RUN:-0} == 1 ]]; then exit; fi
      if [[ ${#selected[@]} != 0 ]]; then
        for name in "${selected[@]}"; do "$runner" "$name"; done
        exit
      fi
      "$runner"
      SYN_PPC_SKIP_BUILD=1 OBJ="$obj" CXX="$cxx" bash "$project/tests/check-core.sh" translation
      aot_cache="$obj/ppc-aot-test.cache"
      if [[ $("$cxx" -dumpmachine) == *mingw* ]]; then
        aot_cache=$(wslpath -w "$aot_cache")
      fi
      "$runner" memory-jit "$aot_cache.memory"
      "$runner" memory-jit --load "$aot_cache.memory"
      "$runner" hardware-fpu --include-ni --dispatch \
        <"$project/tests/data/ppc-ni-603-normal.txt"
      "$runner" hardware-fpu --include-ni --dispatch \
        <"$project/tests/data/ppc-ni-603-underflow-enabled.txt"
      "$runner" hardware-fpu --include-ni --reciprocal-powers --dispatch \
        <"$project/tests/data/ppc-fres-603-powers.txt"
      "$runner" conversion-hardware \
        <"$project/tests/data/ppc-conversion-603-normal.txt"
      if $oracle; then
        python3 "$project/tests/alu-qemu-reference.py" "$obj/tests/alu"
        "$runner" alu --oracle <"$obj/tests/alu/alu-750.txt"
      fi
      run_model_checks "$project" "$obj" "$cxx" -g -O1
      if [[ -f "$root/windows-binaries/bin/Apps/MyABCD/MyABCD" ]]; then
        fixture="$root/windows-binaries/bin/Apps/MyABCD/MyABCD"
        if [[ $("$cxx" -dumpmachine) == *mingw* ]]; then fixture=$(wslpath -w "$fixture"); fi
        "$runner" pef-loader "$fixture"
      else
        "$runner" pef-loader
      fi
      bash "$project/tests/check-core.sh" fused
      bash "$project/tests/check-core.sh" thread
      "$cxx" -std=gnu++17 -g -O1 -static-libgcc -static-libstdc++ -I"$project/src/romaw" \
        "$project/src/romaw/decimal.cpp" "$project/tests/decimal_format_test.cpp" -o "$obj/decimal-format-test.exe"
      "$obj/decimal-format-test.exe"
    )
    ;;
  thread)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/.." && pwd)
      obj=${OBJ:-$root/obj}
      mkdir -p "$obj"
      "${CXX:-x86_64-w64-mingw32-g++}" -std=c++17 -g -O1 -Wall -Wextra -Werror \
        -I"$root/include" -I"$root/../carbonlib/include" \
        "$root/../carbonlib/src/threads.cpp" "$root/tests/thread_scheduler_test.cpp" \
        -static -static-libgcc -static-libstdc++ -o "$obj/thread-scheduler-test.exe"
      "$obj/thread-scheduler-test.exe"
      if [[ ${SYN_PPC_SKIP_BUILD:-0} != 1 ]]; then
        make -C "$root" OBJ="$obj"
      fi
      "${CXX:-x86_64-w64-mingw32-g++}" -std=c++17 -g -O1 -Wall -Wextra -Werror \
        -I"$root/include" "$root/tests/ppc_fiber_context_test.cpp" "$obj/libsynppc.a" \
        -static -static-libgcc -static-libstdc++ -o "$obj/ppc-fiber-context-test.exe"
      "$obj/ppc-fiber-context-test.exe"
    )
    ;;
  *)
    printf 'Unknown check: %s. Use --list.\n' "$check" >&2
    exit 2
    ;;
esac
