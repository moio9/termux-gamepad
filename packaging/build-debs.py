#!/usr/bin/env python3
"""Package native Termux gamepad artifacts, optionally with companion SDL2."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PREFIX = Path("data/data/com.termux/files/usr")

def run(*args):
    subprocess.run([str(a) for a in args], check=True)

def install(src, dst, mode=0o755):
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dst)
    dst.chmod(mode)

def link(target, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.is_symlink() or dst.exists():
        dst.unlink()
    dst.symlink_to(target)

def finish(stage, control, output):
    install(control, stage / "DEBIAN/control", 0o644)
    (stage / "DEBIAN").chmod(0o755)
    # Packaging directories must be traversable regardless of the build umask.
    for path in stage.rglob("*"):
        if path.is_dir() and not path.is_symlink():
            path.chmod(0o755)
    run("dpkg-deb", "--build", "--root-owner-group", stage, output)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdl-source", type=Path)
    parser.add_argument("--sdl-build", type=Path)
    parser.add_argument("--sdl-base-deb", type=Path)
    args = parser.parse_args()
    sdl_args = (args.sdl_source, args.sdl_build, args.sdl_base_deb)
    if any(sdl_args) and not all(sdl_args):
        parser.error("all three --sdl-* arguments are required to package SDL2")
    output = ROOT / "output"
    output.mkdir(exist_ok=True)
    artifacts = ROOT / "out/android-aarch64"
    packages = []
    with tempfile.TemporaryDirectory(prefix="termux-gamepad-debs-") as work:
        stage = Path(work) / "runtime"
        usr = stage / PREFIX
        install(artifacts / "libtermux-gamepad.so.1", usr / "lib/libtermux-gamepad.so.1")
        link("libtermux-gamepad.so.1", usr / "lib/libtermux-gamepad.so")
        install(ROOT / "include/termux_gamepad.h", usr / "include/termux_gamepad.h", 0o644)
        for name in ("libudev.so.1", "libtermux-evdev.so"):
            install(artifacts / name, usr / "lib/termux-input" / name)
        link("libudev.so.1", usr / "lib/termux-input/libudev.so.0")
        install(artifacts / "lorie_evdev_bridge", usr / "bin/termux-gamepad-evdev-bridge")
        install(artifacts / "test_termux_gamepad", usr / "bin/gamepad-api-test")
        for name in ("gamepad-start", "gamepad-run"):
            install(ROOT / "tools" / name, usr / "bin" / name)
        install(ROOT / "tools/termux-gamepad-profile.sh", usr / "etc/profile.d/termux-gamepad.sh", 0o644)
        (stage / "DEBIAN").mkdir(parents=True)
        (stage / "DEBIAN/conffiles").write_text("/" + str(PREFIX / "etc/profile.d/termux-gamepad.sh") + "\n")
        package = output / "termux-gamepad_0.1.0_aarch64.deb"
        finish(stage, ROOT / "packaging/termux-gamepad/control", package)
        packages.append(package)
        if all(sdl_args):
            stage = Path(work) / "sdl"
            run("dpkg-deb", "-x", args.sdl_base_deb, stage)
            usr = stage / PREFIX
            real = (args.sdl_build / "libSDL2-2.0.so.0").resolve(strict=True)
            install(real, usr / "lib" / real.name)
            link(real.name, usr / "lib/libSDL2-2.0.so.0")
            for name in ("libSDL2-2.0.so", "libSDL2.so"):
                link("libSDL2-2.0.so.0", usr / "lib" / name)
            isolated = usr / "lib/termux-gamepad-sdl2"
            link("../" + real.name, isolated / "libSDL2-2.0.so.0")
            for name in ("libSDL2-2.0.so", "libSDL2.so"):
                link("libSDL2-2.0.so.0", isolated / name)
            for name in ("testtermuxx11gamepad", "testtermuxx11haptic"):
                install(args.sdl_build / name, isolated / name)
            for name in ("gamepad-test", "gamepad-haptic-test"):
                install(ROOT / "packaging/sdl2" / name, usr / "bin" / name)
            install(args.sdl_source / "include/SDL_hints.h", usr / "include/SDL2/SDL_hints.h", 0o644)
            install(args.sdl_build / "include-config-release/SDL2/SDL_config.h", usr / "include/SDL2/SDL_config.h", 0o644)
            package = output / "sdl2_2.32.10+termuxgamepad4_aarch64.deb"
            finish(stage, ROOT / "packaging/sdl2/control", package)
            packages.append(package)
    (output / "SHA256SUMS").write_text("".join(
        hashlib.sha256(p.read_bytes()).hexdigest() + "  " + p.name + "\n" for p in packages))

if __name__ == "__main__":
    main()
