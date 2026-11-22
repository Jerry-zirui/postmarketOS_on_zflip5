#!/bin/sh
# Phase 4 native Linux 7.3.5 build on Z Flip 5 (SM-F7310 / 8-core Snapdragon 8 Gen 2 / 334 GiB sda37):
# - Preserves all Phase 2/3 hardware fixes (PCIe0 PIPE clk, MAX77705 Rev.0x2, WCN6855 AOP QMP, PCI_PWRCTRL)
# - Enables native Qualcomm SM8550 MDSS DPU + DSI0 + 4nm/7nm DSI PHY + DSC 1.1
# - Builds custom Samsung S6E3FAC (AMF670BS03) 1080x2640@120Hz Command-Mode DSC OLED panel driver
# - Unified DRM card0 (msm_dpu + Adreno 740 FD740 zero-copy compositing, separate_gpu_kms=false)
# - Uses LOCALVERSION="-b5q-p4" so existing /lib/modules/7.3.5/ fallback stays 100% untouched!
set -eu

L=/root/work/linux
O=/root/work/linux/.output_p4
OUT=/root/work/phase4_out
mkdir -p "$O" "$OUT"

echo "=== [1/5] Installing Phase 4 DTS & panel-samsung-s6e3fac-b5q.c ==="
cp /root/work/sm8550-samsung-b5q.dts "$L/arch/arm64/boot/dts/qcom/sm8550-samsung-b5q.dts"
if ! grep -q "sm8550-samsung-b5q.dtb" "$L/arch/arm64/boot/dts/qcom/Makefile"; then
    sed -i '/sm8550-samsung-q5q.dtb/a dtb-$(CONFIG_ARCH_QCOM) += sm8550-samsung-b5q.dtb' "$L/arch/arm64/boot/dts/qcom/Makefile"
fi

cp /root/work/panel-samsung-s6e3fac-b5q.c "$L/drivers/gpu/drm/panel/panel-samsung-s6e3fac-b5q.c"
if ! grep -q "DRM_PANEL_SAMSUNG_S6E3FAC_B5Q" "$L/drivers/gpu/drm/panel/Kconfig"; then
    cat << 'KEOF' >> "$L/drivers/gpu/drm/panel/Kconfig"

config DRM_PANEL_SAMSUNG_S6E3FAC_B5Q
	tristate "Samsung S6E3FAC (AMF670BS03) 120Hz DSC AMOLED panel (Galaxy Z Flip 5)"
	depends on OF
	depends on DRM_MIPI_DSI
	depends on BACKLIGHT_CLASS_DEVICE
	select DRM_DISPLAY_DSC_HELPER
	help
	  Say Y or M here for the Samsung S6E3FAC (AMF670BS03) 6.7-inch
	  1080x2640 120Hz command-mode DSC 1.1 AMOLED display panel used in
	  the Samsung Galaxy Z Flip 5 (SM-F7310).
KEOF
fi
if ! grep -q "panel-samsung-s6e3fac-b5q.o" "$L/drivers/gpu/drm/panel/Makefile"; then
    echo 'obj-$(CONFIG_DRM_PANEL_SAMSUNG_S6E3FAC_B5Q) += panel-samsung-s6e3fac-b5q.o' >> "$L/drivers/gpu/drm/panel/Makefile"
fi

echo "=== [2/5] Applying Phase 3/4 Kernel Patches (idempotent) ==="
python3 - "$L" << 'PYEOF'
import sys, os

L = sys.argv[1]

# Patch 1: Samsung SM8550 PCIe0 PIPE clock fix
p = os.path.join(L, "drivers/phy/qualcomm/phy-qcom-qmp-pcie.c")
s = open(p).read()
if "clk_set_rate(qmp->pipe_clks[0].clk, ULONG_MAX)" not in s:
    needle = "skip_tbls_init:\n\tret = clk_bulk_prepare_enable(qmp->num_pipe_clks, qmp->pipe_clks);\n\tif (ret)\n\t\treturn ret;"
    repl = needle + "\n\n\t/* Samsung SM8550 ABL parks PCIe0 PIPE mux on XO ref; switch to PHY pipe clk */\n\tif (qmp->num_pipe_clks > 0)\n\t\tclk_set_rate(qmp->pipe_clks[0].clk, ULONG_MAX);"
    assert needle in s, "phy-qcom-qmp-pcie.c pattern not found"
    open(p, "w").write(s.replace(needle, repl, 1))
    print("[+] Patched phy-qcom-qmp-pcie.c")
else:
    print("[=] phy-qcom-qmp-pcie.c already patched")

# Patch 2: MAX77705 Rev.0x2 support
p = os.path.join(L, "drivers/mfd/max77705.c")
s = open(p).read()
if "pmic_rev != 0x2" not in s:
    old = "if (pmic_rev != MAX77705_PASS3)"
    new = "if (pmic_rev != MAX77705_PASS3 && pmic_rev != 0x2)"
    assert old in s, "max77705.c pattern not found"
    open(p, "w").write(s.replace(old, new, 1))
    print("[+] Patched max77705.c")
else:
    print("[=] max77705.c already patched")

# Patch 3: WCN6855 AOP QMP wlan_pdc votes
p = os.path.join(L, "drivers/power/sequencing/pwrseq-qcom-wcn.c")
s = open(p).read()
if "pwrseq_qcom_wcn_program_wlan_pdc" not in s:
    inc_old = "#include <linux/pwrseq/provider.h>"
    inc_new = "#include <linux/pwrseq/provider.h>\n#include <linux/soc/qcom/qcom_aoss.h>"
    assert inc_old in s
    s = s.replace(inc_old, inc_new, 1)

    helper = """
static int pwrseq_qcom_wcn_program_wlan_pdc(struct device *dev)
{
\tstruct property *prop;
\tstruct qmp *qmp;
\tconst char *msg;
\tint ret, count = 0;

\tif (!of_property_present(dev->of_node, "qcom,qmp"))
\t\treturn 0;

\tqmp = qmp_get(dev);
\tif (IS_ERR(qmp))
\t\treturn dev_err_probe(dev, PTR_ERR(qmp),
\t\t\t\t     "Failed to get the AOP QMP mailbox\\n");

\tof_property_for_each_string(dev->of_node, "qcom,wlan-pdc-init",
\t\t\t\t    prop, msg) {
\t\tret = qmp_send(qmp, "%s", msg);
\t\tif (ret)
\t\t\tdev_warn(dev, "AOP QMP wlan_pdc msg failed (%d): %s\\n", ret, msg);
\t\telse
\t\t\tcount++;
\t}

\tqmp_put(qmp);
\tdev_info(dev, "Programmed %d AOP QMP wlan_pdc messages\\n", count);
\treturn 0;
}

static int pwrseq_qcom_wcn_probe(struct platform_device *pdev)"""
    probe_old = "static int pwrseq_qcom_wcn_probe(struct platform_device *pdev)"
    assert probe_old in s
    s = s.replace(probe_old, helper, 1)

    call_old = "\tmemset(&config, 0, sizeof(config));"
    call_new = "\tret = pwrseq_qcom_wcn_program_wlan_pdc(dev);\n\tif (ret)\n\t\treturn ret;\n\n\tmemset(&config, 0, sizeof(config));"
    assert call_old in s
    s = s.replace(call_old, call_new, 1)
    open(p, "w").write(s)
    print("[+] Patched pwrseq-qcom-wcn.c")
else:
    print("[=] pwrseq-qcom-wcn.c already patched")

# Patch 4: PCI_PWRCTRL_PWRSEQ default y on ARCH_QCOM
p = os.path.join(L, "drivers/pci/pwrctrl/Kconfig")
s = open(p).read()
if "default y if ARCH_QCOM" not in s:
    old = "config PCI_PWRCTRL_PWRSEQ\n\ttristate\n\tselect POWER_SEQUENCING"
    new = "config PCI_PWRCTRL_PWRSEQ\n\ttristate\n\tdefault y if ARCH_QCOM\n\tselect POWER_SEQUENCING"
    assert old in s
    open(p, "w").write(s.replace(old, new, 1))
    print("[+] Patched drivers/pci/pwrctrl/Kconfig")
else:
    print("[=] drivers/pci/pwrctrl/Kconfig already patched")
PYEOF

echo "=== [3/5] Configuring Phase 4 Kernel (DRM_MSM_DPU + DSI + S6E3FAC) ==="
cp /root/work/kernel_7.3.5_phase3b.config "$O/.config"
cd "$L"
SC="$L/scripts/config --file $O/.config"
$SC --set-str LOCALVERSION "-b5q-p4"
$SC --enable DRM_MSM_DPU
$SC --enable DRM_MSM_DSI
$SC --enable DRM_MSM_DSI_7NM_PHY
$SC --enable DRM_DISPLAY_DSC_HELPER
$SC --enable BACKLIGHT_CLASS_DEVICE
$SC --module DRM_PANEL_SAMSUNG_S6E3FAC_B5Q
make O="$O" olddefconfig

grep -E "^CONFIG_(LOCALVERSION|DRM_MSM|DRM_MSM_DPU|DRM_MSM_DSI|DRM_MSM_DSI_7NM_PHY|DRM_PANEL_SAMSUNG_S6E3FAC_B5Q|BACKLIGHT_CLASS_DEVICE|SM_DISPCC_8550)=" "$O/.config"

echo "=== [4/5] Building Image, Modules, & DTB natively on 8 cores ==="
make -j8 O="$O" Image modules
make -j8 O="$O" DTC_FLAGS="-@" qcom/sm8550-samsung-b5q.dtb

echo "=== [5/5] Installing modules to /lib/modules/7.3.5-b5q-p4 & exporting artifacts ==="
make O="$O" INSTALL_MOD_STRIP=1 modules_install
cp -f "$O/arch/arm64/boot/Image" "$OUT/Image_p4"
cp -f "$O/arch/arm64/boot/dts/qcom/sm8550-samsung-b5q.dtb" "$OUT/sm8550-samsung-b5q_p4.dtb"
ls -lh "$OUT/Image_p4" "$OUT/sm8550-samsung-b5q_p4.dtb" /lib/modules/7.3.5-b5q-p4/kernel/drivers/gpu/drm/panel/panel-samsung-s6e3fac-b5q.ko /lib/modules/7.3.5-b5q-p4/kernel/drivers/gpu/drm/msm/msm.ko
echo "=== PHASE 4 KERNEL BUILD COMPLETE ==="
