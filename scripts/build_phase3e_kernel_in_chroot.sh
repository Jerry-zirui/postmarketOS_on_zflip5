#!/bin/sh
# Phase 3E fast incremental build (~15s):
# 1. Installs IRQ-driven Synaptics S3916A SEC 0xC1 kernel touchscreen module (synaptics_s3916_sec.ko)
# 2. Compiles updated SM-F7310 DTB (touchscreen@20 + pcie0 max-link-speed = <2> + WCN6855 AOP QMP)
set -eu

L=/root/work/linux
O=/root/work/linux/.output
MODS=/root/work/phase3_mods

echo "=== [1/3] Installing synaptics_s3916_sec.c and Phase 3E DTS ==="
cp /root/work/synaptics_s3916_sec.c "$L/drivers/input/touchscreen/synaptics_s3916_sec.c"
if ! grep -q "synaptics_s3916_sec.o" "$L/drivers/input/touchscreen/Makefile"; then
    echo "obj-m += synaptics_s3916_sec.o" >> "$L/drivers/input/touchscreen/Makefile"
fi
cp /root/work/sm8550-samsung-b5q.dts "$L/arch/arm64/boot/dts/qcom/sm8550-samsung-b5q.dts"

echo "=== [2/3] Building synaptics_s3916_sec.ko & DTB (8 cores) ==="
cd "$L"
make -j8 O="$O" M=drivers/input/touchscreen modules
make -j8 O="$O" DTC_FLAGS="-@" qcom/sm8550-samsung-b5q.dtb

echo "=== [3/3] Installing synaptics_s3916_sec.ko into $MODS ==="
mkdir -p "$MODS/lib/modules/7.3.5/extra"
cp -f "$O/drivers/input/touchscreen/synaptics_s3916_sec.ko" "$MODS/lib/modules/7.3.5/extra/synaptics_s3916_sec.ko"

ls -lh "$O/drivers/input/touchscreen/synaptics_s3916_sec.ko" "$O/arch/arm64/boot/dts/qcom/sm8550-samsung-b5q.dtb"
echo "=== PHASE 3E BUILD COMPLETE ==="
