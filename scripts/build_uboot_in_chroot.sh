#!/bin/sh
set -e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

cd /root/work/u-boot

# 1. Customize qcom-phone.env so recovery boot enters Fastboot immediately
#    (pressing any hardware button drops straight into interactive bootmenu)
sed -i 's|^bootcmd=.*|bootcmd=echo "SM-F7310 b5q U-Boot Fastboot Mode - press any key for menu"; run fastboot; run menucmd|' board/qualcomm/qcom-phone.env

# 2. Configure qcom_defconfig + qcom-phone.config for sm8550-samsung-b5q
make O=.output qcom_defconfig qcom-phone.config
./scripts/config --file .output/.config --set-str DEFAULT_DEVICE_TREE "qcom/sm8550-samsung-b5q"
make O=.output olddefconfig

# 3. Build U-Boot natively on 8-core Snapdragon 8 Gen 2 (with DTB symbols -@)
make O=.output -j8 DTC_FLAGS="-@"

ls -lh .output/u-boot-nodtb.bin .output/dts/upstream/src/arm64/qcom/sm8550-samsung-b5q.dtb
