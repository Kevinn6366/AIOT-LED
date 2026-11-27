#!/bin/sh
# Run after programming panel_io_wrapper.bit on the current Linux image.
set -eu
dir=${1:?Pass the board runtime directory}
cd "$dir"
dev=/sys/bus/platform/devices/41200000.gpio
name=${dev##*/}
test -e "$dev/driver_override"
driver=$(basename "$(readlink "$dev/driver" || true)")
if [ "$driver" != uio_pdrv_genirq ]; then
    modprobe uio_pdrv_genirq
    echo uio_pdrv_genirq > "$dev/driver_override"
    if [ -n "$driver" ]; then echo "$name" > "$dev/driver/unbind"; fi
    echo "$name" > /sys/bus/platform/drivers/uio_pdrv_genirq/bind
fi
set -- "$dev"/uio/uio*
test "$#" -eq 1 && test -d "$1"
uio=${1##*/}
test "$(cat "/sys/class/uio/$uio/maps/map0/addr")" = 0x41200000
overlay=/sys/kernel/config/device-tree/overlays/aiot-rgb
rgb=/sys/bus/platform/devices/41210000.rgb
if [ ! -d "$rgb" ]; then
    mkdir -p "$overlay"
    cat build/artifacts/rgb-overlay.dtbo > "$overlay/dtbo"
fi
if [ ! -d "$rgb/uio" ]; then
    echo uio_pdrv_genirq > "$rgb/driver_override"
    echo 41210000.rgb > /sys/bus/platform/drivers/uio_pdrv_genirq/bind
fi
set -- "$rgb"/uio/uio*
test "$#" -eq 1 && test -d "$1"
rgb_uio=${1##*/}
test "$(cat "/sys/class/uio/$rgb_uio/maps/map0/addr")" = 0x41210000
exec build/artifacts/panel-io.elf "/dev/$uio" "$dir/pl-commands" "$dir/pl-status" "/dev/$rgb_uio"
