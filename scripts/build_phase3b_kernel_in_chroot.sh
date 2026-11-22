#!/bin/sh
# Phase 3B incremental Linux 7.3.5 build:
# 1. MAX77705 Rev.0x2 support (Z Flip 5 SM-F7310 companion PMIC / charger / fuelgauge)
# 2. Adreno 740 GPU headless render node (/dev/dri/renderD128) via separate_gpu_kms=true
# 3. WCN6855 AOP QMP wlan_pdc init (fixes MHI BHI -5 timeout on Samsung SM8550)
# 4. Built-in PCI_PWRCTRL_PWRSEQ (default y if ARCH_QCOM)
set -eu

L=/root/work/linux
O=/root/work/linux/.output
MODS=/root/work/phase3_mods

echo "=== [1/4] Updating Phase 3B DTS ==="
cp /root/work/sm8550-samsung-b5q.dts "$L/arch/arm64/boot/dts/qcom/sm8550-samsung-b5q.dts"

echo "=== [2/4] Applying Phase 3B Kernel Patches (idempotent) ==="
python3 - "$L" << 'PYEOF'
import sys, os

L = sys.argv[1]

# Patch 1: drivers/mfd/max77705.c -> allow silicon Rev.0x2 on Samsung Z Flip 5
p = os.path.join(L, "drivers/mfd/max77705.c")
s = open(p).read()
if "pmic_rev != 0x2" not in s:
    old = "if (pmic_rev != MAX77705_PASS3)"
    new = "if (pmic_rev != MAX77705_PASS3 && pmic_rev != 0x2)"
    assert old in s, "max77705.c pattern not found"
    open(p, "w").write(s.replace(old, new, 1))
    print("[+] Patched drivers/mfd/max77705.c (allow Rev.0x2)")
else:
    print("[=] drivers/mfd/max77705.c already patched")

# Patch 2: drivers/gpu/drm/msm/msm_drv.c -> default separate_gpu_kms = true for headless Adreno 740
p = os.path.join(L, "drivers/gpu/drm/msm/msm_drv.c")
s = open(p).read()
if "static bool separate_gpu_kms = true;" not in s:
    old = "static bool separate_gpu_kms;"
    new = "static bool separate_gpu_kms = true;"
    assert old in s, "msm_drv.c separate_gpu_kms pattern not found"
    open(p, "w").write(s.replace(old, new, 1))
    print("[+] Patched drivers/gpu/drm/msm/msm_drv.c (separate_gpu_kms = true)")
else:
    print("[=] drivers/gpu/drm/msm/msm_drv.c already patched")

# Patch 3: drivers/power/sequencing/pwrseq-qcom-wcn.c -> program Samsung AOP QMP wlan_pdc votes
p = os.path.join(L, "drivers/power/sequencing/pwrseq-qcom-wcn.c")
s = open(p).read()
if "pwrseq_qcom_wcn_program_wlan_pdc" not in s:
    inc_old = "#include <linux/pwrseq/provider.h>"
    inc_new = "#include <linux/pwrseq/provider.h>\n#include <linux/soc/qcom/qcom_aoss.h>"
    assert inc_old in s, "pwrseq include not found"
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
    assert probe_old in s, "pwrseq probe not found"
    s = s.replace(probe_old, helper, 1)

    call_old = "\tmemset(&config, 0, sizeof(config));"
    call_new = """\tret = pwrseq_qcom_wcn_program_wlan_pdc(dev);
\tif (ret)
\t\treturn ret;

\tmemset(&config, 0, sizeof(config));"""
    assert call_old in s, "pwrseq memset(&config) not found"
    s = s.replace(call_old, call_new, 1)

    open(p, "w").write(s)
    print("[+] Patched drivers/power/sequencing/pwrseq-qcom-wcn.c (AOP QMP wlan_pdc)")
else:
    print("[=] drivers/power/sequencing/pwrseq-qcom-wcn.c already patched")

# Patch 4: drivers/pci/pwrctrl/Kconfig -> build PCI_PWRCTRL_PWRSEQ in on ARCH_QCOM
p = os.path.join(L, "drivers/pci/pwrctrl/Kconfig")
s = open(p).read()
if "default y if ARCH_QCOM" not in s:
    old = "config PCI_PWRCTRL_PWRSEQ\n\ttristate\n\tselect POWER_SEQUENCING"
    new = "config PCI_PWRCTRL_PWRSEQ\n\ttristate\n\tdefault y if ARCH_QCOM\n\tselect POWER_SEQUENCING"
    assert old in s, "drivers/pci/pwrctrl/Kconfig pattern not found"
    open(p, "w").write(s.replace(old, new, 1))
    print("[+] Patched drivers/pci/pwrctrl/Kconfig (default y if ARCH_QCOM)")
else:
    print("[=] drivers/pci/pwrctrl/Kconfig already patched")
PYEOF

echo "=== [3/4] Updating .config & Building Phase 3B incrementally (8 cores) ==="
cd "$L"
$L/scripts/config --file "$O/.config" --enable PCI_PWRCTRL_PWRSEQ
make O="$O" olddefconfig
grep -E "^CONFIG_(PCI_PWRCTRL_PWRSEQ|MFD_MAX77705|DRM_MSM|ATH11K_PCI|FONT_TER16x32)=" "$O/.config" || true

make -j8 O="$O" Image modules
make -j8 O="$O" DTC_FLAGS="-@" qcom/sm8550-samsung-b5q.dtb

echo "=== [4/4] Re-installing modules to $MODS ==="
rm -rf "$MODS"
make O="$O" INSTALL_MOD_PATH="$MODS" INSTALL_MOD_STRIP=1 modules_install

ls -lh "$O/arch/arm64/boot/Image" "$O/arch/arm64/boot/dts/qcom/sm8550-samsung-b5q.dtb"
echo "=== PHASE 3B KERNEL BUILD COMPLETE ==="
