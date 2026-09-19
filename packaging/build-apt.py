#!/usr/bin/env python3
"""Build a signed, flat APT repository from the preview Debian packages."""
import argparse
from datetime import datetime, timezone
from email.utils import format_datetime
import gzip
import hashlib
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--gnupghome", type=Path, required=True)
    parser.add_argument("--key", required=True, help="Signing key fingerprint")
    args = parser.parse_args()
    dest = args.output.resolve()
    dest.mkdir(parents=True, exist_ok=True)
    pool = dest / "pool"
    pool.mkdir(exist_ok=True)
    packages = sorted((ROOT / "output").glob("*.deb"))
    if not packages:
        parser.error("build the Debian packages in output/ first")
    paragraphs = []
    for package in packages:
        copied = pool / package.name
        shutil.copyfile(package, copied)
        control = subprocess.check_output(["dpkg-deb", "-f", str(package)], text=True).strip()
        data = copied.read_bytes()
        paragraphs.append(control + "\nFilename: pool/" + copied.name +
                          "\nSize: " + str(len(data)) + "\nSHA256: " +
                          hashlib.sha256(data).hexdigest() + "\n")
    index = ("\n".join(paragraphs) + "\n").encode()
    (dest / "Packages").write_bytes(index)
    (dest / "Packages.gz").write_bytes(gzip.compress(index, mtime=0))
    release = ("Origin: TermuxGamepad\nLabel: Termux Gamepad\nSuite: preview\n"
               "Codename: preview\nArchitectures: aarch64\n"
               "Date: " + format_datetime(datetime.now(timezone.utc), usegmt=True) +
               "\nDescription: Experimental Termux controller packages\nSHA256:\n")
    for name in ("Packages", "Packages.gz"):
        data = (dest / name).read_bytes()
        release += " " + hashlib.sha256(data).hexdigest() + " " + str(len(data)) + " " + name + "\n"
    (dest / "Release").write_text(release)
    gpg = ["gpg", "--homedir", str(args.gnupghome), "--batch", "--yes", "--local-user", args.key]
    subprocess.run(gpg + ["--armor", "--detach-sign", "--output", str(dest / "Release.gpg"), str(dest / "Release")], check=True)
    subprocess.run(gpg + ["--clearsign", "--output", str(dest / "InRelease"), str(dest / "Release")], check=True)
    public = subprocess.check_output(gpg + ["--armor", "--export", args.key])
    (dest / "termux-gamepad.asc").write_bytes(public)
    (dest / ".nojekyll").touch()
    (dest / "index.html").write_text('<!doctype html><meta charset="utf-8"><title>Termux Gamepad APT</title>'
        '<h1>Termux Gamepad preview repository</h1><p>Signed packages for Termux AArch64.</p>'
        '<p><a href="https://github.com/moio9/termux-gamepad#apt-repository">Installation instructions</a></p>')

if __name__ == "__main__":
    main()
