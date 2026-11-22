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
