# Samsung Galaxy Z Flip 5 (`SM-F7310` / 直板改装版) 硬件兼容性评估报告

> **采集基线说明**：本报告中的所有硬件参数、总线地址、芯片型号及固件路径，均通过 Root ADB 直接采集自目标真机（序列号 `R5CW80VY4KB`，固件版本 `F7310ZSS7DYB3`），原始文件保存在仓库 [`extracted_hw_info/`](../extracted_hw_info/) 目录下。

---

## 一、 开发宿主机与目标设备基线

### 1.1 开发宿主机 (Host Machine) 可行性评估
- **处理器与架构**：Apple A18 Pro（6 核：2P + 4E），原生 `arm64` 架构，支持硬件虚拟化 (`kern.hv_support: 1`)。
  - **编译优势**：与目标手机同为 `aarch64` 架构，在 Linux 容器/虚拟机（OrbStack 或 Lima）内可直接执行**原生编译 (Native Build)**，无需跨架构转译，完整构建主线 Linux 内核仅需 3~6 分钟。
- **内存 (RAM)**：8 GB（分配 4 GB 给 Linux 虚拟机即可满足主线内核 `make -j6` 及 `pmbootstrap` 镜像打包需求）。
- **存储与文件系统约束**：
  - 宿主机本地磁盘剩余空间约 17~18 GB，且默认 APFS 分区为**大小写不敏感 (Case-insensitive)**。
  - **应对方案**：内核源码必须在 Linux 虚拟机内部文件系统（ext4/btrfs）或大小写敏感的 APFS 子卷中检出；大体积分区备份与构建缓存可协同利用**目标手机端剩余的 146 GB 高速 UFS 4.0 存储空间**或外置 SSD。

### 1.2 目标设备 (Target Device) 基础状态
| 属性 | 实测值 | 评估结论 |
| :--- | :--- | :--- |
| **机型与代号** | `SM-F7310` (`b5qzhx` / `samsung-b5q` / `TGY` 港版) | 无运营商锁，支持完整刷写与自定义内核引导。 |
| **主板标识** | `Samsung B5Q PROJECT (board-id,11)` | `qcom,msm-id = <0x218 0x20000 ...>`, `qcom,board-id = <0x10008 0xb>` |
| **SoC 平台** | Qualcomm Snapdragon 8 Gen 2 for Galaxy (`KALAMA`, `soc_id: 519`, Rev `2.0`) | 对应主线 `arch/arm64/boot/dts/qcom/sm8550.dtsi`，上游支持高度成熟。 |
| **内存与闪存** | 8 GB LPDDR5X + 512 GB UFS 4.0（`/data` 剩余 **146 GB**） | GKI 2.0 动态分区架构，充足的剩余空间可用于双系统或存放镜像。 |
| **Bootloader 与安全状态** | `ro.boot.flash.locked: 0`<br>`ro.boot.verifiedbootstate: orange`<br>`ro.boot.vbmeta.device_state: unlocked` | **Bootloader 已解锁，AVB 校验已关闭**，已具备 `/system/bin/su` Root 权限。 |
| **物理改装状态** | **已改装为直板机** | **彻底忽略副屏与折叠铰链状态**，显著降低主线 Linux 移植复杂度。 |

---

## 二、 直板化改装对主线 Linux 移植的架构收益

原厂折叠屏在主线 Linux / Wayland 桌面环境下的三大难点分别是：**双 DSI 面板动态电源/热插拔切换**、**主副触控屏随开合状态启停**、以及**霍尔传感器 (`SW_LID`) 误触发休眠锁屏**。

由于本机已物理改装为直板机且无需兼容副屏与折叠态，在编写主线设备树 `sm8550-samsung-b5q.dts` 时可直接采取以下简化策略：
1. **永久移除/禁用副屏显示节点**：不启用 `DSI-2` (`B5_S6E3FC5_AMB338EH01`, `720×748`)，仅保留主屏 `DSI-1` 作为唯一 DRM 显示输出。
2. **永久移除/禁用副屏触控节点**：不启用 `spi@890000` (`qupv3_se12_spi`) 上的 `Synaptics S3908` 触控节点，仅保留主屏 I2C 触控。
3. **永久移除/禁用折叠霍尔与双姿态融合**：不启用 `i2c@a8c000` 上的 `digital_hall_akm@10` (`akm-digital-hall`) 及虚拟 `flip` / `certify_hall` 输入设备，不启用副半区 `LSM6DSVW` 传感器。

> **效果**：系统在内核层将其视为一台标准的 **6.7 英寸 `1080×2640 @ 120Hz` 骁龙 8 Gen 2 直板手机**（软件架构与 Galaxy S23 `dm1q` 高度一致），彻底杜绝改装直板后因磁铁移位导致的黑屏或切屏问题。

---

## 三、 实机核心硬件拓扑与关键参数清单（提取自 `fdt.dts`）

### 3.1 关键物理内存保留区 (`reserved-memory`)
| 区域名称 | 起始物理地址 | 大小 | 主线移植用途 |
| :--- | :--- | :--- | :--- |
| **`splash_region` (`cont_splash_region`)** | **`0xb8000000`** | **`0x2b00000` (43 MB)** | Bootloader (ABL) 已初始化的帧缓冲内存区。主线 DTS 配置 `simple-framebuffer` 指向此地址即可在**启动第一秒直接点亮主屏** (`1080×2640`, `a8r8g8b8`)。 |
| `smem_region` | `0x81d00000` | `0x200000` (2 MB) | 高通共享内存 (SMEM)。 |
| `aop_cmd_db_region` | `0x81c60000` | `0x20000` (128 KB) | `qcom,cmd-db` RPMh 命令数据库。 |
| `mpss_region` | `0x8a800000` | `0x10800000` (264 MB) | 5G 基带调制解调器 (MPSS) 固件运行区。 |
| `cdsp_region` | `0x9c900000` | `0x2000000` (32 MB) | CDSP 计算协处理器运行区。 |
| `adspslpi_region` | `0x9ea00000` | `0x59b4000` (~89.7 MB) | ADSP 音频与传感器协处理器运行区。 |

### 3.2 显示与触控子系统（仅主屏）
- **主显示屏 (`card0-DSI-1`)**：
  - **面板标识**：`B5_S6E3FAC_AMF670BS03` (`msm_drm.lcd_id=0x810012`)
  - **控制器与 PHY**：`qcom,dsi-ctrl-hw-v2.7` + `qcom,dsi-phy-v5.2`
  - **分辨率与刷新率**：`1080 × 2640`，MIPI DSI Command Mode，支持 `10 / 24 / 30 / 48 / 60 / 96 / 120 Hz`
  - **面板专用 PMIC**：**Samsung `S2DOS05` (`s2dos05_pmic@60`)**，中断引脚 `TLMM GPIO 187`
- **主屏触控 IC (`sec_touchscreen`)**：
  - **重要实测发现**：下游设备树在 `i2c@a90000` 上同时定义了 `goodix-berlin@5d` (`status = "disabled"`) 和 `touchscreen@20` (`status = "ok"`)。本机实测激活的是 **Synaptics `S3916A` (`synaptics_tcm_i2c`)**。
  - **挂载总线**：`qupv3_se4_i2c` (`i2c@a90000`)，I2C 从机地址 **`0x20`**
  - **中断引脚**：`TLMM GPIO 19` (`0x13`)
  - **配套固件**：`/vendor/firmware/tsp_synaptics/s3916a_b5.bin`

### 3.3 无线通信、音频、电源与外设拓扑
- **Wi-Fi 6E & 蓝牙 5.3**：
  - **芯片型号**：Qualcomm **FastConnect 6900 (`QCA6490` / `WCN6855`)**
  - **实机固件**：`/vendor/firmware/qca6490/{amss20.bin, m3.bin, bdwlan.elf*}`（Wi-Fi）、`/vendor/firmware/{hpbtfw21.tlv, hpnv21.bin}`（蓝牙）
- **音频子系统**：
  - **主编解码器**：Qualcomm **`WCD938x`** (`wcd938x-codec` + SoundWire `wcd938x-slave`) + **`WSA884x`**
  - **双扬声器智能功放**：2 × **NXP `TFA9878`** (`tfa98xx@34` 与 `tfa98xx@35`，挂载于 I2C Bus 18，配置文件 `/vendor/firmware/tfa98xx.cnt`)
  - **DSP 固件**：`/vendor/firmware_mnt/image/adsp.mdt` 及 `adsp_dtb.mdt`
- **电源管理、充放电与马达**：
  - **高通主 PMIC 组**：`PMK8550`、`PM8550`、`PM8550VS`、`PM8550VE`、`PMR735D`
  - **三星伴侣 PMIC**：**Maxim `MAX77705` (`max77705@66`，IRQ `GPIO 133 / 0x85`)**，集成：
    - `max77705-charger`（主充电管理）
    - `max77705-fuelgauge`（电池电量计）
    - `max77705_pdic`（USB Type-C CC/PD 控制与 OTG）
    - `max77705_vibrator`（线性震动马达控制，配合 `/vendor/firmware/cs40l26*`）
  - **辅助电源 IC**：`NXP PCA9482`（电荷泵快充）、`Samsung S2ASL01`（主副双电芯限流均衡器）
- **实体按键与传感器**：
  - **电源键**：`pmk8550` `pon_hlos@1300:pwrkey` (`event4`)
  - **音量下键**：`pmk8550` `pon_hlos@1300:resin` (`event5`)
  - **音量上键**：`gpio_keys_flex` (`event0`)
  - **主传感器组**：加速度计/陀螺仪 **STM `LSM6DSV`**、磁力计 **AKM `AK09918`**、气压计 **STM `LPS22DF`**、环境光/距离传感器 **SensorTek `STK33911` / `STK6D2X`**

---

## 四、 postmarketOS / 主线 Linux 硬件兼容性四级矩阵

### 🟢 第一梯队：主线原生支持（开箱即用或仅需常规 DTS 引用 + 本机固件）
| 模块 | 驱动 / 子系统 | 兼容性评级 | 说明 |
| :--- | :--- | :---: | :--- |
| **SoC / CPU 8 核调度与变频** | `sm8550.dtsi` / `qcom-cpufreq-hw` | ⭐⭐⭐⭐⭐ | 主线已高度成熟，支持完整 SMP 与动态调频。 |
| **UFS 4.0 内部存储** | `ufs-qcom` (`1d84000`) | ⭐⭐⭐⭐⭐ | 主线完美支持，可作为高速 Rootfs 载体。 |
| **Adreno 740 GPU (3D 加速)** | DRM `msm` + Mesa `Freedreno` / `Turnip` | ⭐⭐⭐⭐⭐ | 提取 `gmu_gen70200.bin` 后，OpenGL 4.6 / Vulkan 1.3 均达桌面级成熟度。 |
| **Wi-Fi 6E (`QCA6490`)** | `ath11k_pci` | ⭐⭐⭐⭐⭐ | **重大优势**：`QCA6490/WCN6855` 在主线极为成熟稳定，打包本机固件即可满速联网。 |
| **蓝牙 5.3 (`WCN6855`)** | `hci_uart` / `btqca` | ⭐⭐⭐⭐⭐ | 提取 `hpbtfw21.tlv` / `hpnv21.bin` 后原生支持蓝牙键鼠与蓝牙音频。 |
| **实体按键 (电源 / 音量±)** | `pm8941-pwrkey` / `gpio-keys` | ⭐⭐⭐⭐⭐ | 标准主线驱动，零障碍支持。 |
| **姿态与环境传感器** | `st_lsm6dsx` / `ak8975` / `st_pressure` | ⭐⭐⭐⭐ | 主线内置 IIO 驱动，支持屏幕方向自动旋转。 |

### 🟡 第二梯队：主线已有驱动，需按本机 `fdt.dts` 适配节点参数（高可行性）
| 模块 | 驱动 / 子系统 | 兼容性评级 | 适配要点 |
| :--- | :--- | :---: | :--- |
| **主屏幕显示 (`S6E3FAC`)** | 早期：`simple-framebuffer` (`0xb8000000`)<br>正式：`drm/panel` + `regulator/s2dos05` | ⭐⭐⭐⭐ | 早期直接用 `0xb8000000` 帧缓冲亮屏；后续提取 `fdt.dts` 中的 DSI 初始化序列生成 `panel-samsung-s6e3fac.c`，配合已在主线的 `s2dos05` 供电驱动实现休眠唤醒与 120Hz。 |
| **主屏触控 (`Synaptics S3916A`)** | `synaptics_tcm_i2c` / `syna_tcm2` | ⭐⭐⭐⭐ | 在 `i2c@a90000` 节点下配置 `reg = <0x20>`、中断 `GPIO 19` 及对应的 LDO 供电轨。 |
| **电池电量 / 充电 / 震动 (`MAX77705`)** | `mfd/max77705` + `max17042_battery` + `max77705_charger` | ⭐⭐⭐⭐ | 三星 `MAX77705` 主线驱动已被社区合入，配置 I2C 节点后可读取电池百分比、支持基础 USB-C 充电（约 10~15W）及马达震动。 |
| **USB 3.1 / OTG / DP 视频输出** | `dwc3-qcom` + `qcom-qmp-usb-dp-phy` | ⭐⭐⭐⭐ | 早期固定 Device/Host 模式用于 USB 网卡调试；后续对接 `max77705` Type-C 角色切换及 DisplayPort 外接显示器。 |

### 🟠 第三梯队：需深度配平与用户态调优（中期目标）
| 模块 | 驱动 / 子系统 | 兼容性评级 | 适配要点 |
| :--- | :--- | :---: | :--- |
| **内置双扬声器与麦克风** | `qcom,sm8550-sndcard` + `wcd938x` + `tfa98xx` | ⭐⭐⭐ | 蓝牙耳机与 USB-C 声卡可优先直接使用；内置扬声器需配平 ADSP AudioReach 拓扑、`TFA9878` (`0x34`/`0x35`) I2S 路由及 ALSA UCM 配置。 |
| **5G/4G 移动数据与 GPS** | `qcom_q6v5_mss` + `rmtfs` + `pd-mapper` + `ModemManager` | ⭐⭐⭐ | 数据流量上网与短信在 `sm8550` postmarketOS 下可调通；运营商原生 VoLTE 语音通话暂不作为目标。 |

### 🔴 第四梯队：主线暂不具备实用条件（直接略过）
| 模块 | 状态与原因 |
| :--- | :--- |
| **副屏 (`DSI-2`) 与折叠霍尔传感器** | **已改直板机，主动废弃**，不在主线 DTS 中声明。 |
| **前后摄像头 (`Titan 780 ISP`)** | SM8550 `camss` 与三星传感器 `libcamera` 管线尚未成熟。 |
| **侧边指纹识别 (`Goodix gw3x`)** | 依赖高通 TrustZone 闭源 TA，主线无法使用（实体电源键按压不受影响）。 |
| **NFC (`SN220U`)** | 缺乏主线内核与开源用户态协议栈支持。 |
