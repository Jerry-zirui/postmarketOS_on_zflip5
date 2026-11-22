#!/bin/sh
set -eu

L=/root/work/linux
O=/root/work/linux/.output
MODS=/root/work/phase3_mods

echo "=== [1/5] Installing Phase 3A DTS ==="
cp /root/work/sm8550-samsung-b5q.dts "$L/arch/arm64/boot/dts/qcom/sm8550-samsung-b5q.dts"
if ! grep -q "sm8550-samsung-b5q.dtb" "$L/arch/arm64/boot/dts/qcom/Makefile"; then
    sed -i '/sm8550-samsung-q5q.dtb/a dtb-$(CONFIG_ARCH_QCOM) += sm8550-samsung-b5q.dtb' "$L/arch/arm64/boot/dts/qcom/Makefile"
fi

echo "=== [2/5] Applying Samsung SM8550 PCIe0 PIPE clock fix ==="
PHY_FILE="$L/drivers/phy/qualcomm/phy-qcom-qmp-pcie.c"
if ! grep -q "clk_set_rate(qmp->pipe_clks\[0\].clk, ULONG_MAX)" "$PHY_FILE"; then
    python3 - "$PHY_FILE" << 'PYEOF'
import sys
p = sys.argv[1]
s = open(p).read()
needle = """skip_tbls_init:
\tret = clk_bulk_prepare_enable(qmp->num_pipe_clks, qmp->pipe_clks);
\tif (ret)
\t\treturn ret;"""
repl = """skip_tbls_init:
\tret = clk_bulk_prepare_enable(qmp->num_pipe_clks, qmp->pipe_clks);
\tif (ret)
\t\treturn ret;

\t/* Samsung SM8550 ABL parks PCIe0 PIPE mux on XO ref; switch to PHY pipe clk */
\tif (qmp->num_pipe_clks > 0)
\t\tclk_set_rate(qmp->pipe_clks[0].clk, ULONG_MAX);"""
assert needle in s, "Could not find skip_tbls_init block in phy-qcom-qmp-pcie.c!"
open(p, "w").write(s.replace(needle, repl, 1))
print("Patched phy-qcom-qmp-pcie.c successfully.")
PYEOF
else
    echo "phy-qcom-qmp-pcie.c already patched."
fi

echo "=== [3/5] Configuring Phase 3A Kernel (.config) ==="
cd "$L"
SC="$L/scripts/config --file $O/.config"

# Enable loadable modules so GPU/WiFi/BT firmware loads after switch_root
$SC --enable MODULES
$SC --enable MODULE_UNLOAD

# Core Qualcomm SoC / GPU clocks / LLCC / SCM / Reboot Mode
$SC --enable SM_GPUCC_8550
$SC --enable QCOM_LLCC
$SC --enable QCOM_AOSS_QMP
$SC --enable QCOM_SCM
$SC --enable QCOM_MDT_LOADER
$SC --enable REMOTEPROC
$SC --enable POWER_RESET
$SC --enable POWER_RESET_QCOM_PON
$SC --enable REBOOT_MODE
$SC --enable NVMEM_REBOOT_MODE
$SC --enable NVMEM_QCOM_SPMI_SDAM

# PCIe0 + WCN6855 Power Sequencing
$SC --enable PCI
$SC --enable PCIEPORTBUS
$SC --enable PCIE_QCOM
$SC --enable PHY_QCOM_QMP
$SC --enable PHY_QCOM_QMP_PCIE
$SC --enable POWER_SEQUENCING
$SC --enable POWER_SEQUENCING_QCOM_WCN
$SC --enable PCI_PWRCTRL
$SC --enable PCI_PWRCTRL_PWRSEQ

# I2C, GPI DMA, S2DOS05 Display/Touch PMIC, MAX77705 Battery/Charger PMIC
$SC --enable I2C_GPIO
$SC --enable I2C_CHARDEV
$SC --enable I2C_QCOM_GENI
$SC --enable QCOM_GPI_DMA
$SC --enable MFD_SEC_CORE
$SC --enable MFD_SEC_I2C
$SC --enable REGULATOR_S2DOS05
$SC --enable MFD_MAX77705
$SC --enable CHARGER_MAX77705
$SC --enable BATTERY_MAX17042
$SC --enable SENSORS_MAX77705
$SC --enable LEDS_MAX77705
$SC --enable SERIAL_QCOM_GENI
$SC --enable SERIAL_DEV_BUS
$SC --enable SERIAL_DEV_CTRL_TTYPORT

# Wayland / Input / Firmware / Containers
$SC --enable INPUT_UINPUT
$SC --enable UHID
$SC --enable OVERLAY_FS
$SC --enable BLK_DEV_LOOP
$SC --enable TMPFS_POSIX_ACL
$SC --enable TMPFS_XATTR
$SC --enable FW_LOADER
$SC --enable FW_LOADER_COMPRESS
$SC --enable FW_LOADER_COMPRESS_ZSTD
$SC --enable FW_LOADER_COMPRESS_XZ
$SC --enable RFKILL
$SC --enable CRYPTO_MICHAEL_MIC
$SC --enable CRYPTO_CMAC
$SC --enable CRYPTO_CCM
$SC --enable CRYPTO_GCM
$SC --enable CRYPTO_CTR
$SC --enable CRYPTO_ARC4
$SC --enable CRYPTO_ECB

# Modules: Headless Adreno 740 GPU (no KMS conflict with SimpleDRM)
$SC --module DRM_MSM
$SC --disable DRM_MSM_MDP4
$SC --disable DRM_MSM_MDP5
$SC --disable DRM_MSM_DPU
$SC --disable DRM_MSM_DP
$SC --disable DRM_MSM_DSI
$SC --disable DRM_MSM_HDMI

# Modules: Wi-Fi 6E (WCN6855 / QCA6490 ath11k_pci)
$SC --enable WIRELESS
$SC --enable WLAN
$SC --enable WLAN_VENDOR_ATH
$SC --module CFG80211
$SC --enable CFG80211_WEXT
$SC --module MAC80211
$SC --module MHI_BUS
$SC --module QRTR
$SC --module QRTR_MHI
$SC --module ATH_COMMON
$SC --module ATH11K
$SC --module ATH11K_PCI

# Modules: Bluetooth 5.3 (WCN6855 / btqca)
$SC --module BT
$SC --enable BT_BREDR
$SC --enable BT_LE
$SC --module BT_RFCOMM
$SC --enable BT_RFCOMM_TTY
$SC --module BT_BNEP
$SC --module BT_HIDP
$SC --module BT_HCIUART
$SC --enable BT_HCIUART_SERDEV
$SC --enable BT_HCIUART_H4
$SC --enable BT_HCIUART_QCA
$SC --module BT_QCA

# Keep large crisp TER16x32 framebuffer console font ONLY
$SC --enable FONTS
$SC --disable FONT_8x8
$SC --disable FONT_8x16
$SC --enable FONT_TER16x32

make O="$O" olddefconfig

# Re-assert fonts after olddefconfig
sed -i 's/^CONFIG_FONT_8x8=y/# CONFIG_FONT_8x8 is not set/' "$O/.config"
sed -i 's/^CONFIG_FONT_8x16=y/# CONFIG_FONT_8x16 is not set/' "$O/.config"
$SC --enable FONT_TER16x32
make O="$O" olddefconfig

echo "=== Verifying key config options ==="
grep -E "^CONFIG_(MODULES|DRM_MSM|DRM_MSM_KMS|SM_GPUCC_8550|PCIE_QCOM|POWER_SEQUENCING_QCOM_WCN|PCI_PWRCTRL_PWRSEQ|ATH11K_PCI|BT_HCIUART_QCA|MFD_MAX77705|CHARGER_MAX77705|BATTERY_MAX17042|REGULATOR_S2DOS05|QCOM_GPI_DMA|NVMEM_REBOOT_MODE|FONT_TER16x32|FONT_8x)=" "$O/.config" || true

echo "=== [4/5] Building Image, modules, and DTB (8 cores) ==="
make -j8 O="$O" Image modules
make -j8 O="$O" DTC_FLAGS="-@" qcom/sm8550-samsung-b5q.dtb

echo "=== [5/5] Installing modules to $MODS ==="
rm -rf "$MODS"
make O="$O" INSTALL_MOD_PATH="$MODS" INSTALL_MOD_STRIP=1 modules_install

ls -lh "$O/arch/arm64/boot/Image" "$O/arch/arm64/boot/dts/qcom/sm8550-samsung-b5q.dtb"
find "$MODS/lib/modules" -name "*.ko" | sort
echo "=== PHASE 3A KERNEL BUILD COMPLETE ==="
