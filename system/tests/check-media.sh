#!/usr/bin/env bash
# +===================================================================
#   File:      check-media.sh
#
#   Summary:   Run media regression checks independently.
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
    printf '%s\n' 'aiff' 'host-speech' 'mace' 'movie-runtime' 'movie' 'pcm' 'sdl-sound'
    ;;
  aiff)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      gcc -std=gnu17 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -I"$root/include" "$root/system/tests/aiff_test.c" \
        -o "$root/build/aiff-test"
      "$root/build/aiff-test"
    )
    ;;
  host-speech)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      fixture=$(mktemp -d "$root/build/host-speech.XXXXXX")
      x86_64-w64-mingw32-gcc -std=gnu17 -g -O0 -Wall -Wextra -Werror \
        -I"$root/include" "$root/system/tests/host_speech_test.c" \
        "$root/platform/os/cygwin32/win_speech.c" -lole32 -lsapi \
        -o "$fixture/host-speech-test.exe"
      "$fixture/host-speech-test.exe" "$(wslpath -w "$fixture/single.wav")" --single
      "$fixture/host-speech-test.exe" "$(wslpath -w "$fixture/output.wav")"
      python3 - "$fixture/output.wav" "$fixture/single.wav" <<'PY'
import sys
import wave
with wave.open(sys.argv[1], "rb") as sound:
    assert sound.getnchannels() == 1
    assert sound.getsampwidth() == 2
    assert sound.getframerate() == 22050
    assert sound.getnframes() > 2205
    frames = sound.getnframes()
    samples = sound.readframes(frames)
    assert any(samples), "Speech backend produced silence"
with wave.open(sys.argv[2], "rb") as single:
    assert frames > single.getnframes() * 1.1, "Queued speech did not extend the first buffer"
print("Host speech WAV: mono 16-bit PCM, 22050 Hz, nonzero samples and appended buffer passed")
PY
    )
    ;;
  mace)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      x86_64-w64-mingw32-gcc -std=gnu17 -g -O0 \
        "$root/system/tests/mace_test.c" -o "$root/build/mace-test.exe"
      "$root/build/mace-test.exe"
    )
    ;;
  movie-runtime)
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
        system/tests/movie_runtime_test.c -o "$root/build/movie-runtime-test.exe"
      "$root/build/movie-runtime-test.exe"
    )
    ;;
  movie)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      gcc -std=gnu17 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -I"$root/include" "$root/system/tests/movie_test.c" \
        -o "$root/build/movie-test"
      "$root/build/movie-test"
    )
    ;;
  pcm)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      mkdir -p "$root/build"
      gcc -std=gnu17 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
        "$root/system/tests/pcm_test.c" -o "$root/build/pcm-test"
      "$root/build/pcm-test"
    )
    ;;
  sdl-sound)
    (
      set -euo pipefail
      root=$(cd "$(dirname "$0")/../.." && pwd)
      cd "$root"
      mkdir -p "$root/build"
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
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -DHAVE_CONFIG_H -DNDEBUG \
        -I"$root/system/obj" -I. -Iinclude -Ifrontend-sdl -Isound/sdl \
        -Iplatform/os/mingw32 -I"platform/arch/$arch" -Iplatform/os/cygwin32 \
        -I"$prefix/include" system/tests/sdl_sound_test.c \
        -L"$prefix/lib" -lSDL -lwinpthread \
        -o "$root/build/sdl-sound-test.exe"
      cp "$prefix/bin/SDL.dll" "$root/build/SDL.dll"
      cp "$("$cc" -print-file-name=libwinpthread-1.dll)" "$root/build/"
      cd "$root/build"
      "$root/build/sdl-sound-test.exe" "$(wslpath -w "$root/build/sdl-test-capture.raw")"
    )
    ;;
  *)
    printf 'Unknown check: %s. Use --list.\n' "$check" >&2
    exit 2
    ;;
esac
