#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail
termux_prefix="${PREFIX:-/data/data/com.termux/files/usr}"
repo_url=https://moio9.github.io/termux-gamepad
fingerprint=1EB75BAC05FC0DD7DF1462F7281527CDA5BCC1A0
for tool in curl gpg; do
    command -v "$tool" >/dev/null || {
        echo "Install prerequisites first: pkg install curl gnupg" >&2
        exit 1
    }
done
key_tmp=$(mktemp)
trap 'rm -f "$key_tmp"' EXIT
curl --fail --show-error --silent --location "$repo_url/termux-gamepad.asc" -o "$key_tmp"
actual=$(gpg --batch --show-keys --with-colons "$key_tmp" | awk -F: '$1 == "fpr" { print $10; exit }')
if [ "$actual" != "$fingerprint" ]; then
    echo "Repository key fingerprint mismatch; configuration was not changed." >&2
    exit 1
fi
mkdir -p "$termux_prefix/etc/apt/keyrings" "$termux_prefix/etc/apt/sources.list.d" "$termux_prefix/etc/apt/preferences.d"
install -m 644 "$key_tmp" "$termux_prefix/etc/apt/keyrings/termux-gamepad.asc"
printf 'deb [arch=aarch64 signed-by=%s/etc/apt/keyrings/termux-gamepad.asc] %s ./\n' \
    "$termux_prefix" "$repo_url" > "$termux_prefix/etc/apt/sources.list.d/termux-gamepad.list"
cat > "$termux_prefix/etc/apt/preferences.d/termux-gamepad" <<'EOF'
Package: sdl2 termux-gamepad
Pin: release o=TermuxGamepad
Pin-Priority: 990
EOF
printf '%s\n' 'Repository configured. Run:' '  pkg update' \
    '  apt-mark unhold sdl2' '  pkg install termux-gamepad sdl2' \
    'Future published updates are available through pkg upgrade.'
