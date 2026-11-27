#!/bin/sh
# Run at the repository root with the matching PetaLinux build and native tools.
set -eu
images=${1:?PetaLinux images/linux directory}
native=${2:?recipe-sysroot-native/usr/bin directory}
export PATH="$native:$PATH"
out=$(pwd)/build/sd
tree=$out/overlay
mkdir -p "$tree/etc/rc5.d" "$tree/lib/firmware" "$tree/opt/aiot"
cp device/S98aiot "$tree/etc/rc5.d/S98aiot"
chmod 755 "$tree/etc/rc5.d/S98aiot"
cp build/board.tgz "$tree/opt/aiot/"
cp build/artifacts/panel_io_wrapper.bit.bin "$tree/lib/firmware/aiot-panel.bin"
cp "$out/public.hex" "$tree/etc/aiot-public.hex"
sha256sum build/board.tgz | cut -c 1-12 > "$tree/etc/aiot-version"
deploy="$images/../../build/tmp/deploy/images/plnx-zynq7"
cp "$deploy/fitImage-linux.bin-plnx-zynq7" "$out/kernel"
cp "$images/system.dtb" "$out/system.dtb"
gzip -dc "$images/rootfs.cpio.gz" > "$out/rootfs.cpio"
gen="$native/../../../linux-plnx_zynq7-standard-build/usr/gen_init_cpio"
cat > "$out/initramfs.list" <<EOF
dir /opt 0755 0 0
dir /opt/aiot 0755 0 0
dir /lib/firmware 0755 0 0
file /opt/aiot/board.tgz $tree/opt/aiot/board.tgz 0644 0 0
file /lib/firmware/aiot-panel.bin $tree/lib/firmware/aiot-panel.bin 0644 0 0
file /etc/rc5.d/S98aiot $tree/etc/rc5.d/S98aiot 0755 0 0
file /etc/aiot-public.hex $tree/etc/aiot-public.hex 0644 0 0
file /etc/aiot-version $tree/etc/aiot-version 0644 0 0
EOF
"$gen" "$out/initramfs.list" >> "$out/rootfs.cpio"
gzip -n -c "$out/rootfs.cpio" > "$out/rootfs.gz"
sed -e 's|/incbin/("linux.bin")|/incbin/("kernel")|' \
    -e '/data = .*\.dtb"/s|/incbin/(".*")|/incbin/("system.dtb")|' \
    -e '/data = .*\.cpio.gz"/s|/incbin/(".*")|/incbin/("rootfs.gz")|' \
    "$deploy/fitImage-its-petalinux-user-image-plnx-zynq7-plnx-zynq7" > "$out/system.its"
(cd "$out" && mkimage -f system.its aiot.ub)
