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
| **主线内核 & 启动链** | ✅ 已完成 | Linux 7.3.5 独立运行于 `recovery` 分区 (`sda23`)，日常 Android 14 启动链 0 改动 |
| **持久化 Rootfs** | ✅ 已完成 | Alpine 3.21 (`aarch64`) 落盘 `/dev/sda29` (`/cache/pmos_root`)，不破坏 `/cache/recovery` |
| **主屏显示 & 图形桌面** | ✅ 已完成 | `1080×2640@60Hz` OLED (`SimpleDRM`) + **Weston 14 Wayland 图形桌面** + `weston-terminal` |
| **主屏 10 点触控** | ✅ 已完成 | 自研中断式内核驱动 [`drivers/synaptics_s3916_sec.c`](drivers/synaptics_s3916_sec.c)（`S3916A` SEC `0xC1` 16 字节协议，IRQ GPIO 19），Weston 实机触控流畅 |
| **Adreno 740 3D GPU** | ✅ 已完成 | `msm.separate_gpu_kms=1` -> `/dev/dri/renderD128`，实测 `FD740` **OpenGL 4.6 / GLES 3.2 / Vulkan Turnip** + `GMU v4.1.9` |
| **电池 / 充电 / 电量计** | ✅ 已完成 | `MAX77705` (`Rev.0x2` 补丁) 实时上报电量%、电压、电流、温度、循环次数与 USB 快充 |
| **屏幕/触控 PMIC** | ✅ 已完成 | `Samsung S2DOS05` (`i2c-gpio` 20/21 @ `0x60`) 5 路电源轨全部在线 |
| **蓝牙 5.3** | ✅ 已完成 | `WCN6855 / QCA6490` (`uart14`) 固件加载完成，`hci0` 在线 |
| **USB 网卡 & SSH & UFS 4.0** | ✅ 已完成 | macOS 原生免驱 USB CDC-NCM (`scripts/ssh_b5q.sh`) + 512 GB UFS 4.0 + 全部物理按键 |
| **Wi-Fi 6E (`ath11k_pci`)** | 🔄 调试收尾 | PCIe Gen 2 建链 + AOP QMP 投票 + SBL BHI 加载均已成功，剩余 `01:00.0` MSI 中断路由排查 |

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
