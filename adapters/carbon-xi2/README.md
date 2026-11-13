# Carbon SDL3 controller adapter

Experimental adapter for using multiple controllers with the native Xbox 360
port of Need for Speed Carbon on Termux. It translates the separate XInput2
controllers exposed by Termux:X11-Extra into SDL3 gamepads for the port.

The adapter is separate from `libtermux-gamepad`: it does not add multi-device
support to the existing evdev bridge or change installed SDL libraries. It does
not include the game, game data, or the game runtime.

## Requirements

- Android AArch64 with Termux, a compatible Carbon native port and its game data.
- Termux:X11-Extra 2.0 Alpha 2 or newer, with **Controller → Forward gamepad to
  X11** and **Separate controllers in X11** enabled.
- A running X11 display, two connected Android controllers, and the native port's
  SDL3 shared library (`libSDL3.so`).
- Termux build dependencies: `clang`, `pkg-config`, `sdl3`, `libx11`, and `libxi`
  from the Termux repositories; enable `x11-repo` if needed.

## Build

Use the **same SDL3 library as the game**. The bridge and the game must load the
same SDL3 instance, or the game will not see the virtual controllers. Set this
to an absolute path to the port's `libSDL3.so`:

```sh
cd adapters/carbon-xi2
SDL3_LIBRARY=/path/to/port/lib/libSDL3.so ./build.sh --test
```

The outputs are `out/runner-multi` and `out/xi2_sdl_bridge.so`. The optional
`--test` exercises button and axis mapping using two SDL virtual controllers;
it does not require controllers or an X11 display. Matching SDL3 headers must
also be installed.

## Integrate with a port launcher

The port launcher must supply its usual runtime libraries, game data path,
rendering and audio configuration. In that environment, set
`CARBON_XI2_BRIDGE` to the absolute path of `out/xi2_sdl_bridge.so` and use
`out/runner-multi` in place of the port's normal SDL entry-point launcher.
Pass through the arguments expected by the existing launcher. For example:

```sh
export DISPLAY=:1
export CARBON_XI2_BRIDGE=/path/to/termux-gamepad/adapters/carbon-xi2/out/xi2_sdl_bridge.so
/path/to/termux-gamepad/adapters/carbon-xi2/out/runner-multi [port arguments]
```

The port must provide `libmain.so`, `libSDL3.so`, `SDL_main`, and
`rex_sdl_set_touch_gamepad_state`; the launcher must make its runtime libraries
available via `LD_LIBRARY_PATH`. Keep the port's normal runtime setup and use
separate test saves while adapting a launcher. The adapter cannot configure a
port or obtain game files on its own.

## Limitations

- The native port's touch controller shares its first player slot with the first
  physical controller. The second physical controller is assigned separately.
  Device order may change when controllers reconnect.
- SDL rumble is not forwarded by this adapter.
- This is a preview integration for the compatible SDL3-based native port, not
  a universal split-screen patch for other versions of the game.
