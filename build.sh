#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/src"
INCLUDE="$ROOT/include"
if [ "$(uname -m)" != aarch64 ]; then
  echo "Native Termux AArch64 is required." >&2
  exit 1
fi
mkdir -p "$ROOT/out/android-aarch64"
CC=${CC:-clang}
COMMON="-O2 -fPIC -fvisibility=hidden -Wall -Wextra -Wno-unused-parameter"

if [ -n "${TERMUX_VERSION:-}" ] || [ -d /data/data/com.termux/files/usr ]; then
  OUT="$ROOT/out/android-aarch64"
  $CC $COMMON -Werror -I"$INCLUDE" -I"$SRC" -shared \
    "$SRC/termux_gamepad.c" -Wl,-soname,libtermux-gamepad.so.1 \
    -o "$OUT/libtermux-gamepad.so.1"
  ln -sf libtermux-gamepad.so.1 "$OUT/libtermux-gamepad.so"
  $CC $COMMON -Werror -I"$INCLUDE" -shared "$SRC/libudev_fake.c" -ldl \
    -L"$OUT" -ltermux-gamepad -Wl,-soname,libudev.so.1 -o "$OUT/libudev.so.1"
  $CC $COMMON -Werror -I"$INCLUDE" -shared "$SRC/evdev_preload.c" -ldl \
    -L"$OUT" -ltermux-gamepad -Wl,-soname,libtermux-evdev.so -o "$OUT/libtermux-evdev.so"
  $CC -O2 -Wall -Wextra -Werror "$SRC/test_udev.c" -ldl -o "$OUT/test_udev"
  $CC -O2 -Wall -Wextra -Werror "$SRC/test_udev_monitor.c" -ldl \
    -o "$OUT/test_udev_monitor"
  $CC -O2 -Wall -Wextra -Werror "$SRC/test_evdev.c" -o "$OUT/test_evdev"
  $CC -O2 -Wall -Wextra -Werror "$SRC/test_joydev.c" -o "$OUT/test_joydev"
  $CC -O2 -Wall -Wextra -Werror "$SRC/test_sdl3_gamepad.c" \
    $(pkg-config --cflags --libs sdl3) -o "$OUT/test_sdl3_gamepad"
  $CC -O2 -Wall -Wextra -Werror "$SRC/test_sdl2_haptic.c" \
    $(pkg-config --cflags --libs sdl2) -o "$OUT/test_sdl2_haptic"
  $CC -O2 -Wall -Wextra -Werror "$SRC/test_sdl2_effects.c" \
    $(pkg-config --cflags --libs sdl2) -o "$OUT/test_sdl2_effects"
  $CC -O2 -Wall -Wextra -Werror "$SRC/test_ff.c" -o "$OUT/test_ff"
  $CC -O2 -Wall -Wextra -Werror -I"$INCLUDE" \
    "$SRC/test_termux_gamepad.c" -L"$OUT" -ltermux-gamepad \
    -Wl,-rpath,'$ORIGIN' -o "$OUT/test_termux_gamepad"
  $CC -O2 -Wall -Wextra -Werror "$SRC/event_server.c" -o "$OUT/event_server"
  $CC -O2 -Wall -Wextra -Werror "$SRC/lorie_evdev_bridge.c" \
    $(pkg-config --cflags --libs x11 xi) -o "$OUT/lorie_evdev_bridge"
  ln -sf libudev.so.1 "$OUT/libudev.so.0"
  ln -sf libtermux-gamepad.so.1 "$OUT/libtermux-gamepad.so"
  echo "Built native Android AArch64 outputs on Termux."
  exit 0
fi

echo "This build supports native Termux AArch64 only." >&2
exit 1
