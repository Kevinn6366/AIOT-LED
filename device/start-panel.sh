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
exec build/artifacts/panel-io.elf "/dev/$uio" "$dir/pl-commands" "$dir/pl-status"
