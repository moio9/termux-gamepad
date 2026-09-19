# Autostart the Termux:X11 controller bridge for interactive login shells.
# Set TERMUX_GAMEPAD_AUTOSTART=0 before this file is sourced to disable it.
if [ "${TERMUX_GAMEPAD_AUTOSTART:-1}" != 0 ] &&
   [ -x /data/data/com.termux/files/usr/bin/gamepad-start ]; then
    termux_gamepad_display="${DISPLAY:-:0}"
    termux_gamepad_display_number="${termux_gamepad_display##*:}"
    termux_gamepad_display_number="${termux_gamepad_display_number%%.*}"
    case "$termux_gamepad_display_number" in
        ''|*[!0-9]*)
            ;;
        *)
            if [ -S "/data/data/com.termux/files/usr/tmp/.X11-unix/X$termux_gamepad_display_number" ]; then
                if [ -z "${DISPLAY:-}" ]; then
                    DISPLAY="$termux_gamepad_display"
                    export DISPLAY
                fi
                /data/data/com.termux/files/usr/bin/gamepad-start \
                    >/dev/null 2>&1 || :
            fi
            ;;
    esac
    unset termux_gamepad_display termux_gamepad_display_number
fi

# Prefer the broad fake udev/evdev route. The custom SDL checks the exported
# marker in libtermux-evdev and disables its direct backend, avoiding a second
# copy of the same controller. If a secure process rejects LD_PRELOAD, the
# marker disappears and custom SDL becomes the automatic fallback.
termux_gamepad_prefix=/data/data/com.termux/files/usr
termux_gamepad_shims="$termux_gamepad_prefix/lib/termux-input"
if [ -f "$termux_gamepad_shims/libtermux-evdev.so" ] &&
   [ -f "$termux_gamepad_shims/libudev.so.1" ]; then
    case ":${LD_LIBRARY_PATH:-}:" in
        *:"$termux_gamepad_shims":*) ;;
        *) LD_LIBRARY_PATH="$termux_gamepad_shims${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ;;
    esac
    case ":${LD_PRELOAD:-}:" in
        *:"$termux_gamepad_shims/libtermux-evdev.so":*) ;;
        *) LD_PRELOAD="$termux_gamepad_shims/libtermux-evdev.so:$termux_gamepad_shims/libudev.so.1${LD_PRELOAD:+:$LD_PRELOAD}" ;;
    esac
    # Termux:X11 normally injects this library when XSTARTUP_LD_PRELOAD is
    # unset. Since the gamepad profile sets that variable indirectly, retain
    # Termux's /usr/bin and shebang path translation explicitly.
    case "$LD_PRELOAD" in
        *libtermux-exec.so*|*libtermux-exec-ld-preload.so*) ;;
        *) LD_PRELOAD="$LD_PRELOAD:$termux_gamepad_prefix/lib/libtermux-exec.so" ;;
    esac
    SDL_JOYSTICK_DEVICE=/dev/input/event99
    SDL_JOYSTICK_HIDAPI=0
    TERMUX_GAMEPAD_EVDEV_SOCKET="${TERMUX_GAMEPAD_EVDEV_SOCKET:-$termux_gamepad_prefix/tmp/termux-gamepad-evdev.sock}"
    export LD_LIBRARY_PATH LD_PRELOAD SDL_JOYSTICK_DEVICE
    export SDL_JOYSTICK_HIDAPI TERMUX_GAMEPAD_EVDEV_SOCKET
fi
unset termux_gamepad_prefix termux_gamepad_shims
