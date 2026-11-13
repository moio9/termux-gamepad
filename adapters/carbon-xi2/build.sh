#!/data/data/com.termux/files/usr/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
: "${SDL3_LIBRARY:?Set SDL3_LIBRARY to the SDL3 shared library used by the game}"
if [ ! -f "$SDL3_LIBRARY" ]; then
    printf 'SDL3 library not found: %s\n' "$SDL3_LIBRARY" >&2
    exit 1
fi
SDL3_LIBRARY=$(realpath "$SDL3_LIBRARY")
mkdir -p "$HERE/out"
CC=${CC:-clang}
# pkg-config emits compiler arguments as separate words.
# shellcheck disable=SC2046
"$CC" -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -fPIC -shared \
    $(pkg-config --cflags sdl3 x11 xi) "$HERE/xi2_sdl_bridge.c" \
    -o "$HERE/out/xi2_sdl_bridge.so" "$SDL3_LIBRARY" \
    $(pkg-config --libs x11 xi) -pthread
# shellcheck disable=SC2046
"$CC" -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror \
    $(pkg-config --cflags sdl3) "$HERE/runner-multi.c" \
    -o "$HERE/out/runner-multi" -ldl -pthread
if [ "${1:-}" = "--test" ]; then
    # shellcheck disable=SC2046
    "$CC" -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror \
        $(pkg-config --cflags sdl3 x11 xi) "$HERE/bridge_mapping_test.c" \
        -o "$HERE/out/bridge_mapping_test" "$SDL3_LIBRARY" \
        $(pkg-config --libs x11 xi) -pthread
    SDL3_DIR=$(dirname "$SDL3_LIBRARY")
    LD_LIBRARY_PATH="$SDL3_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$HERE/out/bridge_mapping_test"
    printf '%s\n' 'Two-controller SDL mapping test passed.'
fi
printf 'Built in %s/out\n' "$HERE"
