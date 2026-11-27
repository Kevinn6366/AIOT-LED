#!/bin/sh
# Assemble the temporary board runtime from a PetaLinux 2019.1 Zynq build.
set -eu
tmp=${1:?Pass the PetaLinux project build/tmp directory}
core="$tmp/deploy/rpm/cortexa9t2hf_neon"
stdlib="$tmp/sysroots-components/cortexa9t2hf-neon/python3/usr"
rpmtool=${RPM2CPIO:-rpm2cpio}
test -s build/led-control
test -s build/python/bottle.py
"$rpmtool" "$core"/python3-core-*.rpm > build/core.cpio
cpio -i --to-stdout './usr/bin/python3.5m' < build/core.cpio > build/python-arm
test -s build/python-arm
chmod +x build/python-arm
tar czf build/board.tgz --exclude=.debug --exclude=__pycache__ \
    --exclude='*.a' --exclude='*.pyc' --exclude=test --exclude=tests \
    web server/app.py server/panel.py server/firmware.py device/start-panel.sh build/artifacts/panel-io.elf build/artifacts/rgb-overlay.dtbo \
    build/led-control build/python-arm build/python/bottle.py \
    -C "$stdlib" lib/python3.5 lib/libpython3.5m.so.1.0 \
    -C "$tmp/sysroots-components/cortexa9t2hf-neon/libgcc" lib/libgcc_s.so.1
