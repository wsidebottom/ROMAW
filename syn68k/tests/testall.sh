#!/usr/bin/env bash
# +===================================================================
#   File:      testall.sh
#
#   Summary:   Implements testall support for syn68k 68K translation and runtime support.
#
#   Classes:   None.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              syn68k/COPYING.
# ===================================================================+

set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
output="$root/obj/tests/reference"
mkdir -p "$output"
binary="$root/obj/tests/syngentest.exe"
# Run the actual cross-built executable; Libtool's launcher cannot resolve
# its Unix build path when Windows starts it through WSL.
if [[ -f "$root/obj/tests/.libs/syngentest.exe" ]]; then
  binary="$root/obj/tests/.libs/syngentest.exe"
fi
# Windows locks a running executable against the build's relink. Run a
# private snapshot so a long reference check does not lock Make's output.
snapshot="$output/syngentest-$$.exe"
cp "$binary" "$snapshot"
trap 'rm -f "$snapshot"' EXIT
binary="$snapshot"
native_enabled=$("$binary" --native-code-enabled 2>"$output/backend.log" | tr -d '\r')
native_flags=$("$binary" --reference-native-flags 2>>"$output/backend.log" | tr -d '\r')
case "$native_enabled" in
  0 | 1) ;;
  *)
    echo "CPU fixture did not report its native backend" >&2
    exit 1
    ;;
esac
case "$native_flags" in
  0 | 1) ;;
  *) exit 1 ;;
esac
modes=("$@")
if [[ ${#modes[@]} -eq 0 ]]; then
  if [[ $native_enabled == 1 ]]; then
    modes=(native interpreter noncc noncc-interpreter)
  else
    modes=(interpreter noncc-interpreter)
  fi
fi
for mode in "${modes[@]}"; do
  args=()
  reference=10000
  case "$mode" in
    native)
      if [[ $native_flags == 0 ]]; then reference=10000-notnative; fi
      ;;
    interpreter)
      args=(-notnative)
      reference=10000-notnative
      ;;
    noncc)
      args=(-noncc)
      reference=10000-noncc
      if [[ $native_flags == 0 ]]; then reference=10000-noncc-notnative; fi
      ;;
    noncc-interpreter)
      args=(-noncc -notnative)
      reference=10000-noncc-notnative
      ;;
    *)
      echo "Unknown integer test mode: $mode" >&2
      exit 2
      ;;
  esac
  # Windows can place a startup mapping over the legacy fixed reference RAM.
  # Retry only its exact pre-execution diagnostic, never a CPU/CRC failure.
  : >"$output/$mode.startup.log"
  for attempt in 1 2 3 4 5; do
    if "$binary" 10000 "${args[@]}" >"$output/$mode.out" 2>"$output/$mode.log"; then
      break
    else
      result=$?
    fi
    if [[ $result != 1 || -s "$output/$mode.out" ||
      $(tr -d '\r' <"$output/$mode.log") != 'Unable to reserve CPU test memory; error 487.' ]]; then
      exit "$result"
    fi
    cat "$output/$mode.log" >>"$output/$mode.startup.log"
    if [[ $attempt == 5 ]]; then
      echo "Fixed-address CPU test memory remained unavailable after 5 launches ($mode)." >&2
      exit "$result"
    fi
    echo "Retrying CPU fixture startup after address reservation failed ($mode, attempt $attempt)." >&2
  done
  diff --strip-trailing-cr "$root/tests/output/$reference" "$output/$mode.out"
  echo "PASS: integer CPU reference ($mode)"
done
