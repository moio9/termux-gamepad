# Termux Gamepad — experimental preview

This project exposes the Termux:X11 Android controller to native Bionic Linux
applications without root, PRoot, `uinput`, `ptrace`, or a real file under
`/dev/input`.

## Architecture

- `libtermux-gamepad.so.1` is the regular, public client library. Native
  applications link to it normally and receive controller identity, state,
  hotplug notifications, and rumble without `LD_PRELOAD` or evdev emulation.
- `libudev.so.1` enumerates one virtual Xbox-compatible device at
  `/dev/input/event99`.
- `libtermux-evdev.so`, scoped with `LD_PRELOAD`, handles `stat`, `open`,
  `openat`, device `ioctl`, output `write`, and `close` for that one virtual
  path. Other paths and file descriptors are passed through unchanged.
- `termux-gamepad-evdev-bridge` reads the real controller through XI2 and
  sends ordinary 64-bit Linux `struct input_event` records over a local Unix
  stream socket.
- evdev clients consume events through a Unix socket; the shim also hooks
  `read` to translate events for the legacy joydev interface (`/dev/input/js99`).
- `FF_RUMBLE` uploads and `write(EV_FF)` travel back over the same full-duplex
  socket and are forwarded through the `LORIE-CONTROLLER` X11 extension.

The default socket is:

```text
/data/data/com.termux/files/usr/tmp/termux-gamepad-evdev.sock
```

## Project status and companion components

This repository contains the native client library, compatibility shims, XI2
bridge and diagnostic programs. It is an experimental source preview.
It requires a matching modified Termux:X11 server implementing the
`LORIE-CONTROLLER` extension; stock Termux:X11 is not sufficient.
The companion project is https://github.com/moio9/termux-x11-extra.
The modified APK and Termux:X11 companion are distributed separately; install
a compatible pair before testing input.

The SDL2 fork is a separate component. Packaging scripts here can create
its Debian package from an existing SDL2 build. The standalone installer here
keeps the udev/evdev shims scoped to explicitly launched applications and does
not modify shell startup files.

## Build and install on Termux AArch64

Install build dependencies in Termux (with the X11 repository enabled):

```sh
pkg install x11-repo
pkg install clang make pkg-config binutils libx11 libxi sdl2 sdl3
```

Clone this repository into `~/termux-gamepad`. SDL2 and SDL3 are needed to
build their diagnostic programs. Building and verification do not install
anything; run `install-termux.sh` only when ready to install the libraries.


The libraries must be linked natively against Bionic. The previous
`-nostdlib` cross-linked artifacts had no `libc.so` dependency and could not
be loaded with `dlopen` on Android.

```sh
cd ~/termux-gamepad
./build.sh
./verify.sh
./install-termux.sh
```

The process-scoped shim libraries are installed in `$PREFIX/lib/termux-input`,
not globally in `$PREFIX/lib`. The regular API is installed as
`$PREFIX/lib/libtermux-gamepad.so.1`, its linker symlink and
`$PREFIX/include/termux_gamepad.h`. The bridge is installed as
`$PREFIX/bin/termux-gamepad-evdev-bridge`.

## Run the bridge

Enable `Forward gamepad to X11` in Termux:X11, then start:

```sh
export DISPLAY=:0
export TERMUX_GAMEPAD_EVDEV_SOCKET="$PREFIX/tmp/termux-gamepad-evdev.sock"
termux-gamepad-evdev-bridge "$TERMUX_GAMEPAD_EVDEV_SOCKET"
```

The bridge supports multiple clients, never blocks XI2 on a stalled client,
sends an initial state snapshot, and batches changed values with
`EV_SYN/SYN_REPORT`.

## Test without a GUI

In a second shell:

```sh
export TGDIR="$PREFIX/lib/termux-input"
export TERMUX_GAMEPAD_EVDEV_SOCKET="$PREFIX/tmp/termux-gamepad-evdev.sock"

~/termux-gamepad/out/android-aarch64/test_udev "$TGDIR/libudev.so.1"

LD_PRELOAD="$TGDIR/libtermux-evdev.so" \
  ~/termux-gamepad/out/android-aarch64/test_evdev --monitor 10

LD_PRELOAD="$TGDIR/libtermux-evdev.so" \
  ~/termux-gamepad/out/android-aarch64/test_ff
```

`test_evdev` should show six absolute axes, one hat, twelve buttons, and live
changes. `test_ff` requests 500 ms of standard Linux `FF_RUMBLE`.

## Direct native API (no preload)

Applications under our control should use the public API instead of pretending
that a `/dev/input` node exists:

```sh
cc application.c -ltermux-gamepad -o application
application
```

The header is `termux_gamepad.h`. Create one reconnectable context with
`termux_gamepad_create()`, call `termux_gamepad_update()` from the application's
event loop, and inspect the descriptor and state getters. The context exposes a
pollable socket fd, follows runtime XInput/DirectInput/None changes, and sends
rumble through the same local Unix socket. It is currently intended to be used
from one serialized application thread.

The companion custom SDL2 backend is a thin adapter over
this API. It links to `libtermux-gamepad.so.1` normally, so SDL applications
using that build do not need the fake udev library, `/dev/input/event99`, or
`LD_PRELOAD`. The evdev/libudev route remains useful for unmodified native
applications that only know Linux input APIs.

## Firefox

Fully close any existing Firefox process first, because an already-running
process will not inherit the shim environment. With the bridge running:

```sh
export TGDIR="$PREFIX/lib/termux-input"
export TERMUX_GAMEPAD_EVDEV_SOCKET="$PREFIX/tmp/termux-gamepad-evdev.sock"

LD_LIBRARY_PATH="$TGDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
LD_PRELOAD="$TGDIR/libtermux-evdev.so:$TGDIR/libudev.so.1${LD_PRELOAD:+:$LD_PRELOAD}" \
firefox --no-remote
```

Do not add these variables globally to `.bashrc`. The fake udev implementation
is intentionally scoped to applications that need the controller.

## SDL without the custom Termux:X11 backend

An SDL build with dynamic udev support can enumerate the fake udev device.
The current Termux SDL package was built with `SDL_LIBUDEV=OFF`, so it can use
the same evdev path through SDL's explicit device hint:

```sh
SDL_TERMUXX11_GAMEPAD=0 \
SDL_JOYSTICK_HIDAPI=0 \
SDL_JOYSTICK_DEVICE=/dev/input/event99 \
LD_PRELOAD="$TGDIR/libtermux-evdev.so" \
your-sdl-application
```

This path has been validated as an `X360 Controller` with six axes, one hat,
twelve buttons, a standard SDL game-controller mapping, and rumble.

## Wine and Hangover

Stop the matching wineserver before the first test so Wine's SDL process
inherits the shim environment. Use the Wine build that created the prefix.
For example, with Hangover:

```sh
WINEPREFIX=/cale/catre/prefix \
  "$PREFIX/opt/hangover-wine/bin/wineserver" -k

SDL_TERMUXX11_GAMEPAD=0 \
SDL_JOYSTICK_HIDAPI=0 \
SDL_JOYSTICK_DEVICE=/dev/input/event99 \
TERMUX_GAMEPAD_EVDEV_SOCKET="$PREFIX/tmp/termux-gamepad-evdev.sock" \
LD_PRELOAD="$TGDIR/libtermux-evdev.so" \
WINEPREFIX=/cale/catre/prefix \
  hangover-wine joc.exe
```

Hangover has been validated through this evdev route with XInput and WGI,
including rumble. DirectInput input is visible too, but Wine's mapped
XInput-compatible view does not expose DirectInput force feedback.

For a game that specifically needs DirectInput force feedback, select
`DirectInput` in Termux:X11. The mode is forwarded at runtime and applications
with udev hotplug support receive REMOVE/ADD notifications. You can still force
the legacy behavior for troubleshooting by starting a fresh
Wine server with the optional generic joystick mode:

```sh
WINEPREFIX=/cale/catre/prefix \
  "$PREFIX/opt/wine-staging/bin/wineserver" -k

TERMUX_GAMEPAD_MODE=dinput \
SDL_JOYSTICK_HIDAPI=0 \
SDL_JOYSTICK_DEVICE=/dev/input/event99 \
TERMUX_GAMEPAD_EVDEV_SOCKET="$PREFIX/tmp/termux-gamepad-evdev.sock" \
LD_PRELOAD="$TGDIR/libtermux-evdev.so" \
WINEPREFIX=/cale/catre/prefix \
  wine-staging joc.exe
```

In this mode the same physical controller is exposed as a generic joystick,
so Wine uses DirectInput with FF. Constant, sine, triangle, sawtooth, ramp,
spring, damper, inertia, and friction effects are accepted through the
standard SDL/Linux evdev interfaces and approximated on the controller's two
rumble motors. XInput and WGI are intentionally absent for that process.
`TERMUX_GAMEPAD_MODE=xinput` forces the normal Xbox-compatible mode instead.
With no override, the shim follows the Termux:X11 setting automatically;
XDInput uses the Xbox-compatible evdev profile so Wine can also build WGI on
top of it.

## Current limitations

- One virtual controller is exposed at a time; simultaneous physical
  controllers are not implemented yet.
- Standard Linux `FF_RUMBLE` is implemented. DInput physical and periodic
  effects are approximated by their magnitude on the two rumble motors;
  directional resistance and true waveforms cannot be reproduced by ordinary
  gamepad motors.
- The fake `stat` layout currently targets Android AArch64/Bionic.
- The fake udev library does not proxy unrelated real udev devices, so it must
  remain process-scoped.
- `libtermux-gamepad` currently exposes one controller and its context is not
  designed for concurrent calls from multiple application threads.

## Source licensing

No project license has been selected for this initial source preview.
Publication alone does not grant an open-source license.

## Split Debian packages

The preview release provides two packages for native Termux AArch64:

- `termux-gamepad_0.1.0_aarch64.deb`: client API, udev/evdev shims, bridge,
  `gamepad-start`, `gamepad-run`, `gamepad-api-test` and shell profile.
- `sdl2_2.32.10+termuxgamepad4_aarch64.deb`: modified SDL2 and its
  `gamepad-test` / `gamepad-haptic-test` commands. Depends on `termux-gamepad`.

After downloading both files, install them together from their directory:

```sh
apt install ./termux-gamepad_0.1.0_aarch64.deb ./sdl2_2.32.10+termuxgamepad4_aarch64.deb
```

This also migrates files previously owned by the combined `termuxgamepad1`,
`termuxgamepad2` and `termuxgamepad3` SDL2 packages. If SDL2 is held, explicitly
unhold it before upgrading and restore the hold afterward:

```sh
apt-mark unhold sdl2
# Run the apt install command above.
apt-mark hold sdl2
```

Unlike the standalone source installer, the Debian package installs a shell
profile that enables the compatibility shims in new login shells and starts
the bridge when the X11 display is available. It preserves Termux exec's
preload. `TERMUX_GAMEPAD_AUTOSTART=0` disables bridge autostart, not the shim
exports. No bridge is started by package installation itself.

For manual GitHub Release downloads, holding SDL2 prevents replacement by
the official package. For automatic updates, use the signed APT repository
below and remove that hold.

Build the runtime package after `./build.sh` and `./verify.sh`:

```sh
python3 packaging/build-debs.py
```

To additionally package the companion SDL2, supply its source, configured
build directory and the original **unmodified** Termux SDL2 2.32.10 package:

```sh
python3 packaging/build-debs.py \
  --sdl-source ../sdl2-termux-x11-gamepad \
  --sdl-build ../sdl2-termux-x11-gamepad/build-termux-xi2-release \
  --sdl-base-deb ../sdl2-lorie-package/original/sdl2_2.32.10_aarch64.deb
```

Outputs and SHA-256 checksums are written to `output/`. This command packages
existing builds; it does not rebuild SDL2 or change installed packages.

## APT repository

Signed preview packages are available at https://moio9.github.io/termux-gamepad/.
This is a third-party repository for native Termux AArch64.

From a clone of this repository:

```sh
pkg install curl gnupg
./setup-apt-repository.sh
pkg update
apt-mark unhold sdl2
pkg install termux-gamepad sdl2
```

The setup script verifies the downloaded signing key fingerprint, then adds
an APT source with `signed-by` restricted to that key. It pins only `sdl2`
and `termux-gamepad` from the `TermuxGamepad` release origin to priority 990,
above the default priority of the official repositories. It does not install
packages itself. Keep SDL2 unheld to receive updates through `pkg upgrade`
(`pkg up`). Updates become available when new signed packages are published.
Custom user APT pins can override this policy; inspect `apt-cache policy sdl2`
if the expected version is not selected.

Signing key fingerprint:

```text
1EB75BAC05FC0DD7DF1462F7281527CDA5BCC1A0
```

To stop using the repository, remove these three configuration files:

```sh
rm "$PREFIX/etc/apt/sources.list.d/termux-gamepad.list"
rm "$PREFIX/etc/apt/preferences.d/termux-gamepad"
rm "$PREFIX/etc/apt/keyrings/termux-gamepad.asc"
pkg update
```

Removing a source does not uninstall its packages or automatically downgrade
SDL2; restore the official SDL2 package explicitly if desired.

### Publishing repository updates

Build new versioned Debian packages, then run:

```sh
python3 packaging/build-apt.py --output ../termux-gamepad-apt \
  --gnupghome /path/to/private-signing-directory --key SIGNING_FINGERPRINT
```

Commit and push that output on the `gh-pages` branch. Keep signing private keys
and revocation certificates outside the repository and back them up privately.
GitHub Pages serves the public key, signed metadata and Debian packages only.
