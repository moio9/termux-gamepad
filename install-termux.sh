#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
DEST="$PREFIX/lib/termux-input"
mkdir -p "$DEST" "$PREFIX/tmp"
cp "$HERE/out/android-aarch64/libudev.so.1" "$DEST/libudev.so.1"
cp "$HERE/out/android-aarch64/libtermux-evdev.so" "$DEST/libtermux-evdev.so"
ln -sf libudev.so.1 "$DEST/libudev.so.0"
install -m 755 "$HERE/out/android-aarch64/libtermux-gamepad.so.1" \
  "$PREFIX/lib/libtermux-gamepad.so.1"
ln -sf libtermux-gamepad.so.1 "$PREFIX/lib/libtermux-gamepad.so"
install -m 644 "$HERE/include/termux_gamepad.h" \
  "$PREFIX/include/termux_gamepad.h"
install -m 755 "$HERE/out/android-aarch64/lorie_evdev_bridge" \
  "$PREFIX/bin/termux-gamepad-evdev-bridge"
printf 'Installed to %s\n' "$DEST"
printf 'Installed bridge to %s\n' "$PREFIX/bin/termux-gamepad-evdev-bridge"
printf 'Installed public gamepad API to %s\n' "$PREFIX/lib/libtermux-gamepad.so.1"
printf '%s\n' 'First test (do not add to .bashrc yet):'
printf '%s\n' 'export DISPLAY=:0'
printf '%s\n' 'export TGDIR="$PREFIX/lib/termux-input"'
printf '%s\n' 'export TERMUX_GAMEPAD_EVDEV_SOCKET="$PREFIX/tmp/termux-gamepad-evdev.sock"'
printf '%s\n' 'termux-gamepad-evdev-bridge "$TERMUX_GAMEPAD_EVDEV_SOCKET"'
printf '%s\n' 'Then, in a second shell:'
printf '%s\n' 'LD_LIBRARY_PATH="$TGDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" LD_PRELOAD="$TGDIR/libtermux-evdev.so:$TGDIR/libudev.so.1${LD_PRELOAD:+:$LD_PRELOAD}" firefox'
