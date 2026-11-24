# postmarketOS on Samsung Galaxy Z Flip 5 (`SM-F7310` / `samsung-b5q`) — 直板改装版

> **项目状态：已固化封版（[`v1.0.0` Final Release](https://github.com/Jerry-zirui/postmarketOS_on_zflip5/releases/tag/v1.0.0)），本项目到此完结归档。**

本项目将主线 Linux（**Linux `7.3.5-b5q-p4`** + **Alpine Linux 3.21 / KDE Plasma 6.2.4**）完整移植至已改装为**直板形态**的国行三星 Galaxy Z Flip 5（`SM-F7310` / `samsung-b5q`，高通骁龙 8 Gen 2 / `SM8550` / `kalama`），实现 120Hz 原生主屏触控、GPU 加速、Wi-Fi 6E、蓝牙 5.3、重力感应自动旋转与双扬声器/双麦克风的日常可用随身 ARM64 Linux 终端。

---

## 📌 设备基线与系统架构

- **目标设备**：Samsung Galaxy Z Flip 5 (`SM-F7310` / `b5qzhx` / `board-id,11`)
- **物理形态**：**直板改装机**（在设备树与启动链层面禁用副屏及折叠机制，通过 TLMM GPIO 42 将外屏保持硬件复位关闭，屏蔽 `spi@890000` 与折叠霍尔逻辑，作为 `1080×2640@120Hz` 单主屏直板设备使用）
- **主线内核**：Linux `7.3.5-b5q-p4`（`aarch64`，Android Boot Image v2 独立存放于 `recovery` 分区 `/dev/sda23`，原厂 Android 14 启动链零侵入）
- **桌面与用户态**：Alpine Linux 3.21 + **KDE Plasma 6.2.4（`Plasma Mobile` / `Plasma Desktop` 双模式，`b5q-plasma-mode` 切换）** + SDDM Wayland + PipeWire / WirePlumber + BlueZ + NetworkManager + Flatpak + Podman + Distrobox

---

## ✅ 最终硬件支持矩阵（`v1.0.0` 封版实测）

| 模块 | 状态 | 实现方案与实测详情 |
|---|---|---|
| **主线内核 & 启动链** | ✅ 已完成 | Linux `7.3.5-b5q-p4` 独立部署于 `recovery` 分区（`/dev/sda23`），原厂 Android 14 `boot`/`vendor_boot` 零改动；支持 `b5q-reboot [linux\|android\|download]` 免按键重启 |
| **UFS 4.0 独立分区** | ✅ 已完成 | 无损重分区：**`334.3 GiB` 独立 `ext4` 根分区（`/dev/sda37`）** + `4 GiB` Swap，完整保留 Android 14（`/dev/sda36` `128.0 GiB`）与 `/dev/sda29` 救援镜像 |
| **原生 120Hz 主屏显示** | ✅ 已完成 | 自研 DRM 面板驱动 [`drivers/panel-samsung-s6e3fac-b5q.c`](drivers/panel-samsung-s6e3fac-b5q.c)（`B5_S6E3FAC_AMF670BS03`）接入 `msm_dpu` + `mdss_dsi0` + 4nm PHY + **DSC 1.1**（配合 [`patches/0001-drm-msm-sm8550-s6e3fac-dsc-initial-lines-cmd-cfg0.patch`](patches/0001-drm-msm-sm8550-s6e3fac-dsc-initial-lines-cmd-cfg0.patch)）：`DSI-1 1080×2640@120Hz`、12-bit 无级亮度调节（`0..2047`）、真息屏休眠唤醒 |
| **外屏硬件关闭** | ✅ 已完成 | [`drivers/b5q-cover-off.c`](drivers/b5q-cover-off.c) 在启动早期拉低 TLMM GPIO 42（`cover-panel-reset-hold`），确保直板状态下副屏 OLED 保持断电休眠 |
| **Adreno 740 GPU** | ✅ 已完成 | 与 `msm_dpu` 融合于统一 `/dev/dri/card1` + `renderD128`，原生 `freedreno FD740`（OpenGL 4.6 / GLES 3.2）+ Turnip Vulkan + `GMU v4.1.9` + UBWC 4.0 硬件加速就绪 |
| **主屏 10 点触控** | ✅ 已完成 | 用户态硬件中断守护进程 [`drivers/b5q_touchd.c`](drivers/b5q_touchd.c) + 内核模块 [`drivers/synaptics_s3916_sec.c`](drivers/synaptics_s3916_sec.c)，通过 `i2c-gpio`（GPIO 44/45）绕过 SM8550 `gpi_dma1` 丢包缺陷，支持 GPIO 19 硬件中断与亮屏自动归零 |
| **Wi-Fi 6E** | ✅ 已完成 | `WCN6855 / QCA6490`（`ath11k_pci` `01:00.0`）+ `pmk8550_sleep_clk` + 4-cell `iommu-map` + `NFA725` (`iPA`) BDF，支持 `NetworkManager` 开机自动联网与无线 SSH |
| **蓝牙 5.3** | ✅ 已完成 | `WCN6855`（`uart14`）+ `hci_uart`/`btqca` + 固定 `local-bd-address`（`02:B5:73:10:00:02`）+ `BlueZ` / `Bluedevil` |
| **重力感应自动旋转** | ✅ 已完成 | SM8550 ADSP/SSC（FastRPC Service 400）+ [`patches/0002-hexagonrpcd-sm8550-nonfatal-unsupported.patch`](patches/0002-hexagonrpcd-sm8550-nonfatal-unsupported.patch) 修正版 `hexagonrpcd` + [`drivers/b5q_sensor_proxy.c`](drivers/b5q_sensor_proxy.c)（`net.hadess.SensorProxy` 桥接至 KWin 自动旋转） |
| **双立体声扬声器** | ✅ 已完成 | 上下双 `TFA9878` SmartPA（`i2c_hub_6` `@0x34` / `@0x35`）+ [`patches/0003-asoc-tfa9878-sc8280xp-linux735.patch`](patches/0003-asoc-tfa9878-sc8280xp-linux735.patch) + AudioReach [`audio/audioreach/SM8550-Samsung-B5Q.m4`](audio/audioreach/SM8550-Samsung-B5Q.m4)（`PRIMARY_MI2S_RX` `SD1`）+ ALSA UCM2 + PipeWire 立体声实测通过 |
| **双数字麦克风** | ✅ 已完成 | 底部 `DMIC1` + 顶部 `DMIC3` 接入 `TX_CODEC_DMA_TX_3`，设备树 `audio-routing` 级联 `VA_MCLK` 与 `vreg_l10b_1p8`（`qcom,dmic-sample-rate = 2400000`），PipeWire `HiFi__Mic__source` 实测低底噪立体声采集 |
| **电源管理 / PMIC / USB** | ✅ 已完成 | `MAX77705`（Rev.0x2）电量计/充电器 + `Samsung S2DOS05`（`i2c-gpio` 20/21 @ `0x60`）+ 动态功率域同步（`b5q-power-sync`）+ USB CDC-NCM 免驱网卡（`172.16.42.1`）+ 物理按键 + `elogind` 关机/重启包装 |
| **桌面应用生态** | ✅ 已完成 | **KDE Plasma 6.2.4**（Mobile / Desktop 切换）+ **Flatpak**（Flathub + `xdg-desktop-portal-kde`）+ **Podman** + **Distrobox** |

### 不在封版范围内的项目（未实现）

- 5G / LTE 蜂窝基带数据、语音通话与短信
- 前置 / 后置摄像头传感器与 ISP 管线
- NFC 控制器
- 外屏 OLED 显示驱动与折叠霍尔传感器逻辑

---

## 📦 Release `v1.0.0` 发布包清单与刷写说明

固化发布包位于 GitHub Releases：[`v1.0.0`](https://github.com/Jerry-zirui/postmarketOS_on_zflip5/releases/tag/v1.0.0)，包含以下 6 个产物：

| 文件名 | 内容与部署路径 |
|---|---|
| `b5q-linux-7.3.5-p4-recovery.img` | 可直接刷入 `/dev/sda23`（`recovery` 分区）的 Android Boot Image v2 镜像（含内核 `Image_p4`、最新设备树 DTB 与 `initramfs_p5.cpio.gz`） |
| `b5q-linux-7.3.5-p4-boot-components.tar.gz` | 散装启动组件包：`Image_p4`、`sm8550-samsung-b5q_p4.dtb`、`initramfs_p5.cpio.gz`、[`configs/kernel_7.3.5-b5q-p4.config`](configs/kernel_7.3.5-b5q-p4.config) 及打包脚本 `mkbootimg_v2.py` |
| `b5q-linux-7.3.5-p4-modules.tar.gz` | 对应 `7.3.5-b5q-p4` 内核的全部已剥离符号 `.ko` 内核模块，解压至根文件系统 `/lib/modules/7.3.5-b5q-p4` 并执行 `depmod -a 7.3.5-b5q-p4` |
| `b5q-audio-ucm2-tplg-v1.0.0.tar.gz` | 自研开源音频拓扑与 UCM2 配置包：解压安装 `/lib/firmware/qcom/sm8550/SM8550-Samsung-B5Q-tplg.bin` 与 `/usr/share/alsa/ucm2/` 配置文件 |
| `b5q-userspace-v1.0.0.tar.gz` | 用户态自研二进制与初始化脚本集合：解压覆盖 `/sbin/b5q-desktop-init` 与 `/usr/local/bin/b5q-*` |
| `SHA256SUMS.txt` | 上述 5 个发布包的 SHA256 校验清单 |

### 安全刷写步骤（仅针对 `recovery` 分区 `/dev/sda23`）

> ⚠️ **警告**：切勿向 `/dev/sda` 的其他分区写入镜像；操作前请确保已保留原厂备份。

```sh
# 1. 在宿主机或设备端核对下载包的 SHA256
shasum -a 256 -c SHA256SUMS.txt

# 2. 在设备端备份当前 recovery 分区（可选）并刷入 /dev/sda23
dd if=b5q-linux-7.3.5-p4-recovery.img of=/dev/sda23 bs=4M conv=fsync

# 3. 逐字节回读校验 SHA256 一致后再重启
img_bytes=$(wc -c < b5q-linux-7.3.5-p4-recovery.img)
sha256sum b5q-linux-7.3.5-p4-recovery.img
head -c "$img_bytes" /dev/sda23 | sha256sum
sync && /usr/local/bin/b5q-reboot linux
```

若部署根文件系统配套文件，在目标根分区 `/` 中解压模块、音频配置与用户态工具：

```sh
tar -xzf b5q-linux-7.3.5-p4-modules.tar.gz -C /
depmod -a 7.3.5-b5q-p4
tar -xzf b5q-audio-ucm2-tplg-v1.0.0.tar.gz -C /
tar -xzf b5q-userspace-v1.0.0.tar.gz -C /
```

### ⚠️ 闭源固件声明

为遵守许可协议，**GitHub Release 发布包与本仓库均不包含任何高通/三星/Goodix/Synaptics 闭源专有固件二进制**（如 `adsp.mbn`、`a740_zap.mbn`、`tfa98xx.cnt`、`WCN6855` / `QCA6490` WLAN/BT 固件及 `NFA725` BDF 等）。使用本系统前需自行从合法持有的 `SM-F7310` 原厂分区提取对应固件并放置到 `/lib/firmware/` 相应路径。

---

## 📂 仓库目录结构

```text
.
├── README.md                                  # 项目总览与 v1.0.0 封版说明
├── LICENSE                                    # GNU General Public License v3.0 协议全文
├── NOTICE                                     # 版权声明、双 GPL 适用范围与强制署名条款
├── audio/
│   ├── audioreach/
│   │   └── SM8550-Samsung-B5Q.m4              # SM8550 AudioReach 音频拓扑源码
│   └── ucm2/Qualcomm/sm8550/
│       ├── SM8550-Samsung-B5Q.conf            # ALSA UCM2 主配置与 BootSequence
│       └── HiFi.conf                          # Speaker (PRIMARY_MI2S_RX) & Mic (TX_CODEC_DMA_TX_3) 定义
├── configs/
│   └── kernel_7.3.5-b5q-p4.config             # Linux 7.3.5-b5q-p4 最终内核 .config 快照
├── docs/
│   ├── HARDWARE_COMPATIBILITY_REPORT.md       # 实机提取硬件参数与兼容性评估报告
│   └── IMPLEMENTATION_PLAN.md                 # 主线 Linux 分阶段移植实施记录
├── drivers/
│   ├── panel-samsung-s6e3fac-b5q.c            # S6E3FAC 1080x2640@120Hz DSC 1.1 主屏 DRM 驱动
│   ├── synaptics_s3916_sec.c                  # Synaptics S3916A 触控内核输入驱动
│   ├── b5q_touchd.c                           # S3916A 低延迟触控守护进程
│   ├── b5q_sensor_proxy.c                     # ADSP Sensor Core -> net.hadess.SensorProxy 桥接服务
│   └── b5q-cover-off.c                        # 外屏 TLMM GPIO 42 硬件复位拉低工具
├── dts/
│   └── sm8550-samsung-b5q.dts                 # SM-F7310 直板改装版完整主线设备树
├── patches/
│   ├── 0001-drm-msm-sm8550-s6e3fac-dsc-initial-lines-cmd-cfg0.patch
│   ├── 0002-hexagonrpcd-sm8550-nonfatal-unsupported.patch
│   └── 0003-asoc-tfa9878-sc8280xp-linux735.patch
├── scripts/
│   ├── build_phase4_kernel_on_phone.sh        # 设备端原生编译 Linux 7.3.5-b5q-p4 自动化脚本
│   ├── configure_phase5_kde_on_phone.sh       # KDE Plasma 6 + PipeWire + BlueZ + UCM 配置脚本
│   ├── b5q-desktop-init                       # 固化版 PID 1 初始化脚本 (/sbin/b5q-desktop-init)
│   ├── ssh_b5q.sh                             # USB CDC-NCM (172.16.42.1) 快捷连接脚本
│   └── ...                                    # 分阶段历史构建与恢复脚本
├── tools/
│   └── mkbootimg_v2.py                        # Android Boot Image v2 打包脚本
└── extracted_hw_info/                         # 真机 SM-F7310 提取的硬件基线参考数据
```

---

## 📖 核心技术文档

1. **[硬件架构解析与兼容性评估报告](docs/HARDWARE_COMPATIBILITY_REPORT.md)** — 外设芯片型号、总线拓扑、中断引脚、内存保留区及兼容性基线。
2. **[主线 Linux 移植分阶段实施计划](docs/IMPLEMENTATION_PLAN.md)** — 从引导链路、设备树、显示/触控、Wi-Fi/蓝牙/传感器到音频和桌面栈的完整演进记录。

---

## ⚖️ 开源协议与署名要求

本项目采用**强传染性（Strict Copyleft）开源协议**，严禁任何形式的闭源商用或未公开源码的衍生分发：

- **仓库主体、用户态守护进程、脚本、音频拓扑与 UCM2 配置、文档**：采用 **[GNU GPL v3.0 (`GPL-3.0-only`)](LICENSE)**。
- **Linux 内核驱动源码、设备树与内核补丁**（`drivers/panel-samsung-s6e3fac-b5q.c`、`drivers/synaptics_s3916_sec.c`、`dts/sm8550-samsung-b5q.dts`、`patches/0001-*.patch`、`patches/0003-*.patch`）：采用 **GNU GPL v2.0 (`GPL-2.0-only`)**，与主线 Linux 内核许可保持严格一致。

**引用与衍生分发强制条款**（依据 GPLv3 第 7(b) 条与 [NOTICE](NOTICE)）：
1. **保持开源**：任何修改、衍生工程、ROM/镜像打包或二次分发，必须以相同适用 GPL 协议提供完整可编译源码，不得闭源或附加限制条款。
2. **必须注明来源**：任何引用、借鉴、转载或复用本项目源码、设备树、驱动补丁、硬件参数或音频配置的作品，必须在仓库显著位置、`NOTICE` 与发布说明中保留原始版权并明确标注来源：**[`Jerry-zirui/postmarketOS_on_zflip5`](https://github.com/Jerry-zirui/postmarketOS_on_zflip5)**。
