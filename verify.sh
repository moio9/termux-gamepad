#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
U="$ROOT/out/android-aarch64/libudev.so.1"
E="$ROOT/out/android-aarch64/libtermux-evdev.so"
G="$ROOT/out/android-aarch64/libtermux-gamepad.so.1"
required='udev_new udev_unref udev_device_unref udev_device_new_from_syspath udev_device_new_from_devnum udev_device_get_devnum udev_device_get_devnode udev_device_get_driver udev_device_get_parent udev_device_get_parent_with_subsystem_devtype udev_device_get_property_value udev_device_get_action udev_device_get_sysattr_value udev_enumerate_new udev_enumerate_unref udev_enumerate_add_match_property udev_enumerate_add_match_subsystem udev_enumerate_scan_devices udev_enumerate_get_list_entry udev_list_entry_get_next udev_list_entry_get_name udev_monitor_new_from_netlink udev_monitor_filter_add_match_subsystem_devtype udev_monitor_enable_receiving udev_monitor_get_fd udev_monitor_receive_device udev_monitor_unref'
for s in $required; do
  readelf -Ws "$U" | grep -E "[[:space:]]${s}$" >/dev/null || { echo "missing $s"; exit 1; }
done
gamepad_required='termux_gamepad_get_abi_version termux_gamepad_create termux_gamepad_destroy termux_gamepad_refresh termux_gamepad_update termux_gamepad_get_descriptor termux_gamepad_get_state termux_gamepad_get_fd termux_gamepad_rumble termux_gamepad_stop_rumble'
for s in $gamepad_required; do
  readelf -Ws "$G" | grep -E "[[:space:]]${s}$" >/dev/null || { echo "missing $s"; exit 1; }
done
for s in termux_evdev_shim_active open openat ioctl close read write stat stat64; do
  readelf -Ws "$E" | grep -E "[[:space:]]${s}$" >/dev/null || { echo "missing $s"; exit 1; }
done
readelf -d "$U" | grep -q 'Shared library: \[libc.so\]' || { echo 'libudev has no libc dependency'; exit 1; }
readelf -d "$E" | grep -q 'Shared library: \[libc.so\]' || { echo 'evdev preload has no libc dependency'; exit 1; }
readelf -d "$G" | grep -q 'Shared library: \[libc.so\]' || { echo 'gamepad API has no libc dependency'; exit 1; }
file "$U" "$E" "$G"
echo OK
