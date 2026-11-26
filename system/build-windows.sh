#!/usr/bin/env bash
# +===================================================================
#   File:      build-windows.sh
#
#   Summary:   Build and stage isolated debug or optimized Windows x64 ROMAW binaries and libraries.
#
#   Classes:   None.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in executor; original attribution and provenance comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

set -euo pipefail

repo=$(cd "$(dirname "$0")" && pwd)
workspace=$(cd "$repo/.." && pwd)
mode=${ROMAW_BUILD_MODE:-optimized}
case "$mode" in
  debug)
    default_obj="$repo/obj"
    default_ppc_obj="$workspace/synppc/obj"
    optimization=-O0
    cmake_type=Debug
    executable=ROMAW-debug.exe
    ;;
  optimized)
    default_obj="$workspace/build/romaw-optimized/obj"
    default_ppc_obj="$workspace/build/romaw-optimized/ppc"
    # Historical Toolbox code intentionally aliases packed guest representations.
    # Keep assertions, IEEE math and debug information while optimizing execution.
    optimization='-O2 -fno-strict-aliasing'
    cmake_type=RelWithDebInfo
    executable=ROMAW.exe
    ;;
  *)
    echo "ROMAW_BUILD_MODE must be debug or optimized." >&2
    exit 2
    ;;
esac
obj=${ROMAW_OBJ_DIR:-$default_obj}
ppc_obj=${PPC_OBJ_DIR:-$default_ppc_obj}
obj=$(realpath -m "$obj")
ppc_obj=$(realpath -m "$ppc_obj")
case "$obj" in
  "$repo/obj" | "$repo/obj/"* | "$workspace/build/"*) ;;
  *)
    echo "ROMAW objects must stay in system/obj or workspace build/." >&2
    exit 1
    ;;
esac
case "$ppc_obj" in
  "$workspace/synppc/obj" | "$workspace/synppc/obj/"* | "$workspace/build/"*) ;;
  *)
    echo "PPC objects must stay in synppc/obj or workspace build/." >&2
    exit 1
    ;;
esac
target=${WINDOWS_TARGET:-x86_64-w64-mingw32}
if [[ $target != x86_64-w64-mingw32 ]]; then
  echo "ROMAW supports only x86_64-w64-mingw32 (64-bit Windows)." >&2
  exit 1
fi
prefix=${WINDOWS_PREFIX:-$workspace/win64-prefix}
stage=${ROMAW_STAGE_DIR:-$workspace/windows-binaries/bin}
cc=${CC:-$target-gcc}
cxx=${PPC_CXX:-$target-g++}
build_cc=${BUILD_CC:-gcc}

case ${1:-} in
  '' | --configure | --libraries | --clean) ;;
  --library)
    if [[ -z ${2:-} || ${2:-} =~ [^a-z0-9-] ]]; then
      echo "A library module name is required." >&2
      exit 2
    fi
    ;;
  *)
    echo "Usage: bash build-windows.sh [--configure | --libraries | --library NAME | --clean]" >&2
    exit 2
    ;;
esac

# Both build modes update shared libraries in the toolchain prefix. Hold the
# workspace lock through linking and staging so another build cannot truncate
# an output while it is being copied.
mkdir -p "$workspace/build"
exec {build_lock}>"$workspace/build/.windows-build.lock"
flock "$build_lock"

if [[ ${1:-} == --clean ]]; then
  if [[ -f "$obj/Makefile" ]]; then
    make -C "$obj" clean
  fi
  make -C "$workspace/synppc" OBJ="$ppc_obj" clean
  make -C "$workspace/carbonlib" OBJ="$obj/carbonlib" clean
  rm -f -- "$obj/map_to_c" "$obj/mkseedtables" "$obj/mkultable" "$obj/mksspairtable"
  rm -rf -- "$obj/native-finder/windows-x64"
  exit 0
fi

mkdir -p "$obj" "$stage" "$workspace/build"
signature="$target|$cc|$cxx|$build_cc|$prefix"
if [[ $mode != debug ]]; then
  signature+="|$mode|$optimization"
fi
bridge_flags="-std=gnu17 -g $optimization -Werror=implicit-int -Werror=implicit-function-declaration"
cxx_flags="-std=gnu++17 -g $optimization"
cmake_options=("ROMAW_CMAKE_BUILD_TYPE=$cmake_type")
if [[ -f "$obj/Makefile" ]] \
  && [[ $(cat "$obj/.windows-build-config" 2>/dev/null || true) != "$signature" ]]; then
  make -C "$obj" clean
  make -C "$workspace/synppc" OBJ="$ppc_obj" clean
  # CMake archives retain their compiler and architecture independently.
  rm -rf -- "$obj/images" "$obj/finder-metadata"
  rm -f "$obj/Makefile" "$obj/config.status"
fi
if [[ ! -f "$prefix/lib/libsyn68k.a" ]]; then
  echo "Build syn68k and stage it in $prefix first." >&2
  exit 1
fi

# Reconfigure before building the PPC bridge or running Make with link-only
# LIBS overrides. Autotools input changes must not leave stale feature headers.
if [[ ! -f "$obj/Makefile" || ${1:-} == --configure ||
  "$workspace/configure" -nt "$obj/.windows-build-config" ||
  "$workspace/config.h.in" -nt "$obj/.windows-build-config" ||
  "$repo/build-windows.sh" -nt "$obj/.windows-build-config" ]]; then
  if [[ ! -f "$workspace/configure" || ! -f "$workspace/Makefile.in" ]]; then
    (cd "$workspace" && autoreconf -fi)
  fi
  cd "$obj"
  "$workspace/configure" --build=x86_64-pc-linux-gnu \
    --host="$target" CC="$cc" CXX="$cxx" \
    --with-front-end=sdl --with-sound=sdl ac_cv_header_db_h=yes \
    CPPFLAGS="-I$prefix/include" \
    CFLAGS="$bridge_flags -I. -I$workspace/include -I$workspace/platform/os/cygwin32 -I$prefix/include" \
    CXXFLAGS="$cxx_flags" \
    LDFLAGS="-L$prefix/lib"
  printf '%s\n' "$signature" >"$obj/.windows-build-config"
fi
ROMAW_BUILD_COMMIT=$(git -C "$workspace" rev-parse --short=12 HEAD)
export ROMAW_BUILD_COMMIT
python3 "$workspace/tools/build_version.py" "$obj/generated/build_version.h"

if [[ ${1:-} == --configure ]]; then
  exit 0
fi

# These table generators run on Linux, even though ROMAW targets Windows.
# Compile in obj/ so their executables and generated tables stay out of src/.
cd "$obj"
for helper in mkseedtables mkultable mksspairtable; do
  if [[ ! -x "$helper" || "$workspace/tools/romaw/$helper.c" -nt "$helper" ||
    config.h -nt "$helper" ]]; then
    $build_cc -std=gnu17 -g \
      -Werror=implicit-int -Werror=implicit-function-declaration \
      -I. -I"$workspace" -I"$workspace/include" \
      -I"$workspace/platform/arch/x86_64" -I"$prefix/include" \
      "$workspace/tools/romaw/$helper.c" -o "$helper"
  fi
done

# Discover module ownership from the configured build instead of maintaining a
# second source list in this wrapper. A selected-module build stops before the
# independent CPU/Finder/Carbon packages and never links the application.
module_manifest=$(make --no-print-directory -s library-manifest)
mapfile -t module_targets < <(printf '%s\n' "$module_manifest" | cut -f1 | sed '/^ROMAW\.exe$/d')
if [[ ${1:-} == --library ]]; then
  case "$2" in
    classic-ui | images | finder-metadata)
      BUILD_CC="$build_cc" BUILD_CFLAGS='-fno-pie -no-pie' make -j"${JOBS:-1}" "${cmake_options[@]}" "$2/libromaw-$2.a"
      exit 0
      ;;
    finder)
      python3 "$workspace/tools/romaw/native_finder/build.py" --output-dir "$obj/native-finder/windows-x64"
      exit 0
      ;;
    carbon)
      make -C "$workspace/carbonlib" CC="$cc" AR="$target-ar" \
        CFLAGS="$bridge_flags" CXXFLAGS="$cxx_flags" \
        OBJ="$obj/carbonlib" ROMAW_OBJ="$obj" "$obj/carbonlib/libromaw-carbon.a"
      exit 0
      ;;
    synppc | synppc-romaw)
      make -C "$workspace/synppc" CC="$cc" CXX="$cxx" PREFIX="$prefix" OBJ="$ppc_obj" \
        BRIDGE_CFLAGS="$bridge_flags" \
        ROMAW_OBJ="$obj" WINDOWS_TARGET="$target" AR="$target-ar" "$ppc_obj/lib$2.a" -j"${JOBS:-1}"
      exit 0
      ;;
  esac
  module="libraries/libromaw-$2.a"
  found=false
  for archive in "${module_targets[@]}"; do
    [[ $archive != "$module" ]] || found=true
  done
  if [[ $found != true ]]; then
    echo "Unknown ROMAW library module: $2" >&2
    exit 2
  fi
  BUILD_CC="$build_cc" BUILD_CFLAGS='-fno-pie -no-pie' make -j"${JOBS:-1}" "$module"
  exit 0
fi

# synppc owns the portable CPU, loader, generated imports and host bridge.
make -C "$workspace/synppc" stage-romaw \
  CC="$cc" CXX="$cxx" PREFIX="$prefix" OBJ="$ppc_obj" \
  ROMAW_OBJ="$obj" STAGE="$stage" WINDOWS_TARGET="$target" \
  BRIDGE_CFLAGS="$bridge_flags" \
  AR="$target-ar" -j"${JOBS:-1}"

finder_obj="$obj/native-finder/windows-x64"
python3 "$workspace/tools/romaw/native_finder/build.py" --output-dir "$finder_obj"
finder_dependencies="$finder_obj/libfinder.a $finder_obj/finder-integrated-res.o $finder_obj/archives/libromaw-archives.a $finder_obj/stuffit/libstuffit_ffi.a"
finder_libs="$finder_dependencies $obj/finder-metadata/libromaw-finder-metadata.a -lcomctl32 -luxtheme -lshlwapi -lmpr -lpsapi -lws2_32 -luserenv -lbcrypt -lntdll -lsynchronization"

carbon_obj="$obj/carbonlib"
make -C "$workspace/carbonlib" stage \
  CFLAGS="$bridge_flags" CXXFLAGS="$cxx_flags" \
  CC="$cc" AR="$target-ar" OBJ="$carbon_obj" ROMAW_OBJ="$obj" PREFIX="$prefix" STAGE="$stage"
carbon_library="$carbon_obj/libromaw-carbon.a"

build_targets=(ROMAW.exe generated/ROMAW.rsrc)
if [[ ${1:-} == --libraries ]]; then
  build_targets=("${module_targets[@]}" classic-ui/libromaw-classic-ui.a images/libromaw-images.a finder-metadata/libromaw-finder-metadata.a)
fi
BUILD_CC="$build_cc" BUILD_CFLAGS='-fno-pie -no-pie' \
  make -j"${JOBS:-1}" "${build_targets[@]}" \
  "${cmake_options[@]}" \
  OBJCLD="$cxx -static-libgcc -static-libstdc++" \
  CCLD="$cxx -static-libgcc -static-libstdc++" \
  EXTRA_ROMAW_DEPENDENCIES="$prefix/lib/libsyn68k.a $ppc_obj/libsynppc-romaw.a $ppc_obj/libsynppc.a $carbon_library $finder_dependencies" \
  LIBS="$ppc_obj/libsynppc-romaw.a $carbon_library $ppc_obj/libsynppc.a $finder_libs -lsyn68k -lSDL -lgdi32 -luser32 -lwinmm -ldxguid -lcomdlg32 -lshell32 -lole32 -luuid -lwinpthread -Wl,-Bstatic -lquadmath -Wl,-Bdynamic -lm"

archive_stage=${ROMAW_LIBRARY_STAGE_DIR:-$stage/libraries/x64}
if [[ $mode == optimized && -z ${ROMAW_LIBRARY_STAGE_DIR:-} ]]; then
  archive_stage+="/optimized"
fi
mkdir -p "$archive_stage"
archive_inputs=("${module_targets[@]/#/$obj/}" "$obj/classic-ui/libromaw-classic-ui.a" "$obj/images/libromaw-images.a"
  "$obj/finder-metadata/libromaw-finder-metadata.a" "$finder_obj/libfinder.a"
  "$prefix/lib/libsyn68k.a" "$ppc_obj/libsynppc.a" "$ppc_obj/libsynppc-romaw.a"
  "$carbon_library")
archive_manifest="$archive_stage/manifest.tsv"
: >"$archive_manifest"
for archive in "${archive_inputs[@]}"; do
  cp "$archive" "$archive_stage/"
  printf '%s\t%s\n' "$(basename "$archive")" "$archive" >>"$archive_manifest"
done
if [[ ${1:-} == --libraries ]]; then
  echo "Staged static libraries in $archive_stage"
  exit 0
fi
# Validate a private snapshot before touching the previously staged executable.
staged_exe=$(mktemp "$stage/.romaw-exe.XXXXXX")
trap 'rm -f -- "$staged_exe"' EXIT
cp -p ROMAW.exe "$staged_exe"
if ! LC_ALL=C "$target-objdump" -f "$staged_exe" | grep -F 'file format pei-x86-64' >/dev/null; then
  echo "Linked ROMAW.exe is not a valid Windows x64 executable; keeping the staged build." >&2
  exit 1
fi

publish_executable() {
  local destination=$1 pending
  if cmp -s "$staged_exe" "$destination"; then
    return
  fi
  pending=$(mktemp "$stage/.romaw-publish.XXXXXX")
  if ! cp -p "$staged_exe" "$pending" || ! mv -fT -- "$pending" "$destination"; then
    rm -f -- "$pending"
    return 1
  fi
}

python3 "$workspace/tools/build_version.py" >"$stage/build-version.txt"
cp generated/ROMAW.rsrc "$stage/ROMAW.rsrc"
mkdir -p "$stage/fonts"
cp "$workspace"/fonts/*.ttf "$stage/fonts/"
publish_executable "$stage/$executable"
if [[ $mode == optimized ]]; then
  publish_executable "$stage/ROMAW-optimized.exe"
fi
pthread_dll=$($cc -print-file-name=libwinpthread-1.dll)
if [[ -f "$pthread_dll" ]] && ! cmp -s "$pthread_dll" "$stage/libwinpthread-1.dll"; then
  cp "$pthread_dll" "$stage/libwinpthread-1.dll"
fi
if [[ -f "$prefix/bin/SDL.dll" ]] && ! cmp -s "$prefix/bin/SDL.dll" "$stage/SDL.dll"; then
  cp "$prefix/bin/SDL.dll" "$stage/SDL.dll"
fi
mkdir -p "$stage/licenses"
cp "$workspace/syn68k/third_party/mame/LICENSE" "$stage/licenses/mame-fpu.txt"
cp "$workspace/syn68k/third_party/fpsp040/README" "$stage/licenses/motorola-fpsp.txt"
cp "$workspace/disk-images/third_party/7zip/DOC/License.txt" "$stage/licenses/7zip.txt"
cp "$workspace/disk-images/third_party/hfsutils/COPYING" "$stage/licenses/hfsutils.txt"
cp "$repo/ROMAW.prefs.master" "$stage/ROMAW.prefs.master"
# Immutable first-start templates stay with the executable; mutable assets belong
# to the Windows user's profile. A cross-build without interop can supply this
# directory explicitly, or skip per-user driver installation.
cp "$repo/ROMAW.ftm" "$stage/ROMAW.ftm.master"
profile_dir=${ROMAW_PROFILE_DIR:-}
if [[ -z "$profile_dir" ]] && command -v powershell.exe >/dev/null 2>&1; then
  windows_home=$(powershell.exe -NoProfile -NonInteractive -Command '[Environment]::GetFolderPath("UserProfile")' | tr -d '\r')
  profile_dir="$(wslpath "$windows_home")/.romaw"
fi
if [[ -z "$profile_dir" ]]; then
  echo "Staged $stage/$executable ($mode; per-user asset installation skipped)"
  exit 0
fi
system_folder="$profile_dir/Apps/System Folder"
mkdir -p "$system_folder"

echo "Staged $stage/$executable ($mode)"
