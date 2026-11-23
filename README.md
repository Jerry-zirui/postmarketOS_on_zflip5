# postmarketOS on Samsung Galaxy Z Flip 5 (`SM-F7310` / `samsung-b5q`) — 直板改装版

本项目致力于将主线 Linux (**postmarketOS**) 移植到已改装为**直板形态**的三星 Galaxy Z Flip 5（公开版 `SM-F7310`，高通骁龙 8 Gen 2 / `SM8550` / `kalama`），打造一台兼具 **120Hz 触屏移动交互** 与 **完整 Linux 桌面生产力体验** 的随身 ARM64 掌上电脑。

---

## 📌 项目核心背景与定位

- **目标设备**：Samsung Galaxy Z Flip 5 (`SM-F7310` / `b5qzhx` / `board-id,11`)
- **物理形态**：**已改装为直板机**（彻底移除/忽略副屏 `DSI-2`、副屏触控 `spi@890000` 及折叠霍尔开关 `akm-digital-hall`，从设备树底层将其固化为单主屏 `1080×2640 @ 120Hz` 骁龙 8 Gen 2 直板设备）
- **目标系统栈**：
  - **底层发行版**：`postmarketOS (edge, systemd)`
  - **桌面与人机交互环境**：`GNOME Shell (Mobile 自适应版)` 或 `KDE Plasma 6 (Plasma Mobile / Desktop 双模式)`
  - **完整桌面生态扩展**：`Flatpak` + `Distrobox`（零损耗容器化运行 Arch Linux ARM / Ubuntu `glibc` 桌面应用）

---

## 🚀 当前真机实测进度（Mainline Linux 7.3.5）

| 模块 | 状态 | 实测详情 |
|---|---|---|
| **主线内核 & 启动链** | ✅ 已完成 | Linux 7.3.5 独立运行于 `recovery` 分区 (`sda23`)，支持三级自举（`sda37` 桌面 -> `sda29` 救援 -> initramfs SSH），日常 Android 14 启动链 0 改动；支持 `b5q-reboot [linux\|android\|download]` 免按键重启 |
| **独立原生 Linux 分区** | ✅ 已完成 | UFS 4.0 无损重分区：**`334.3 GiB` 独立 `ext4` 分区 (`/dev/sda37`)** + `4 GiB` Swap，为 Android 14 保留 `128.0 GiB` (`/dev/sda36`)，保留 `/dev/sda29` 救援根文件系统 |
| **主屏显示（原生 120Hz DRM）** | ✅ 已完成 | 自研主线 DRM 面板驱动 [`drivers/panel-samsung-s6e3fac-b5q.c`](drivers/panel-samsung-s6e3fac-b5q.c)（`B5_S6E3FAC_AMF670BS03`）接入 `msm_dpu` + `mdss_dsi0` + 4nm PHY + **DSC 1.1**（`951.2 Mbps` 匹配 `FFC_SET_0` + [`0001-drm-msm-sm8550-s6e3fac-dsc-initial-lines-cmd-cfg0.patch`](patches/0001-drm-msm-sm8550-s6e3fac-dsc-initial-lines-cmd-cfg0.patch) 对齐 ABL `ENC_DF_CTRL` `initial_lines=3` 与 `CMD_CFG0`）：**`DSI-1 1080×2640@120Hz`** 原生清晰点亮，支持原生 **Adreno 740 UBWC 4.0** 零拷贝合成、**12-bit 硬件亮度无级调节（`0..2047`）** 与真息屏休眠 |
| **KDE Plasma 6 桌面环境** | ✅ 已完成 | **KDE Plasma 6.2.4（Plasma Mobile + Plasma Desktop 双模式，`b5q-plasma-mode` 一键切换）** + SDDM 0.21 Wayland + `elogind` + `XWayland` + Maliit 虚拟键盘 + **KWin & Wayland 全程 Adreno 740 (`FD740`) 零拷贝 120Hz 硬件合成（待机 CPU 0%）** |
| **主屏 10 点触控** | ✅ 已完成 | 硬件中断驱动 [`drivers/b5q_touchd.c`](drivers/b5q_touchd.c) + [`drivers/synaptics_s3916_sec.c`](drivers/synaptics_s3916_sec.c)（`i2c-gpio` GPIO 44/45 绕过 SM8550 `gpi_dma1` 丢包 Bug，TLMM GPIO 19 硬件中断，7 步全手势 0 丢包验收通过） |
| **Adreno 740 3D GPU** | ✅ 已完成 | 与 `msm_dpu` 融合于统一 `/dev/dri/card1` + `renderD128`，实测 KWin 6 与所有 Wayland 应用原生 **`freedreno FD740` OpenGL 4.6 / GLES 3.2 / Vulkan Turnip** + `GMU v4.1.9` 全程硬件加速 |
| **桌面应用生态** | ✅ 已完成 | **Flatpak 1.14.10**（已配置 Flathub + `xdg-desktop-portal-kde`）+ **Podman 5.3.2 (`crun` / `cgroups v2` / `overlay`)** + **Distrobox 1.8.0** 全线验证就绪 |
| **电池 / 充电 / 电量计** | ✅ 已完成 | `MAX77705` (`Rev.0x2` 补丁) + `UPower` 实时上报电量%、电压、电流、温度、循环次数与 USB 快充 |
| **屏幕/触控 PMIC** | ✅ 已完成 | `Samsung S2DOS05` (`i2c-gpio` 20/21 @ `0x60`) 5 路电源轨全部在线 |
| **蓝牙 5.3** | ✅ 已完成 | `WCN6855 / QCA6490` (`uart14`) + `BlueZ` / `Bluedevil` 固件加载完成，`hci0` 在线 |
| **USB 网卡 & SSH & UFS 4.0** | ✅ 已完成 | macOS 原生免驱 USB CDC-NCM (`scripts/ssh_b5q.sh`) + 512 GB UFS 4.0 + 全部物理按键 |
| **Wi-Fi 6E (`ath11k_pci`)** | ✅ 已完成 | `WCN6855 / QCA6490` (`01:00.0`) + `pmk8550_sleep_clk` + 4-cell `iommu-map` + **`NFA725` (`iPA`) BDF** -> NetworkManager 开机自动联网，支持无线免线 SSH 直连 |

---

## 📂 仓库目录结构

```text
.
├── README.md                                # 项目总览与导航
├── docs/
│   ├── HARDWARE_COMPATIBILITY_REPORT.md     # 实机提取硬件参数与 postmarketOS 兼容性评估报告
│   └── IMPLEMENTATION_PLAN.md               # 主线 Linux (postmarketOS) 移植分阶段实施计划
└── extracted_hw_info/                       # 从真机 (SM-F7310) 实时提取的底层硬件基线数据
    ├── fdt.dtb                              # 运行时导出的原始设备树二进制 (/sys/firmware/fdt)
    ├── fdt.dts                              # 反编译出的 35,752 行完整下游设备树源码
    ├── kernel_config.txt                    # 原厂 GKI 5.15.148 内核完整编译配置 (.config)
    ├── cmdline.txt                          # Bootloader (ABL) 传递的启动参数与屏幕批次 ID
    ├── iomem.txt                            # 物理内存映射表 (/proc/iomem)
    ├── partitions.txt                       # UFS 4.0 分区布局与 /dev/block/by-name 映射
    ├── input_devices.txt                    # 按键、触控屏与传感器输入总线拓扑
    ├── hardware_peripherals.txt             # DRM 显示状态、传感器芯片型号、PMIC 与电源信息
    └── firmware_files.txt                   # /vendor/firmware_mnt 与 /vendor/firmware 固件清单
```

---

## 📖 核心文档入口

1. **[硬件架构解析与 postmarketOS 兼容性评估报告](docs/HARDWARE_COMPATIBILITY_REPORT.md)**
   - 开发宿主机与目标手机硬件基线盘点
   - 真机实测外设芯片型号、总线地址、中断引脚与内存保留区详细清单
   - 砍掉副屏与折叠逻辑后的各模块主线 Linux 兼容性四级矩阵
2. **[主线 Linux (postmarketOS) 移植实施计划](docs/IMPLEMENTATION_PLAN.md)**
   - **Phase 0**：宿主机与手机端协同开发环境搭建、关键分区安全备份
   - **Phase 1**：无损二级引导链路（UniLoader / Fastboot 内存加载内核）与早期调试通道 (`simple-framebuffer` + `pstore` + `USB NCM/RNDIS`)
   - **Phase 2**：直板化主线设备树 `sm8550-samsung-b5q.dts` 编写与主线内核首次启动
   - **Phase 3**：核心交互外设调通（主屏 `Synaptics S3916A` 触控、UFS 4.0、`QCA6490` Wi-Fi 6E/蓝牙 5.3、`MAX77705` 电源管理、Adreno 740 3D 加速）
   - **Phase 4**：原生 DRM 面板驱动 (`S6E3FAC` + `S2DOS05` 120Hz 与休眠唤醒)、音频 (`WCD938x` + `TFA9878`) 与 5G 数据通信
   - **Phase 5**：完整桌面融合体验 (`GNOME Mobile` / `KDE Plasma 6` + `Distrobox`) 打磨
