# MT6572 Z1 WiFi/BT Combo Firmware 整理报告 (MT6627 + mainline 7.0-rc7)

> 日期: 2026-07-10
> 目标设备: 佳伯听力机 Z1 (JCB Z1, SoC: MT6572, Combo: MT6627)
> 不碰硬件、不操作 /dev/ttyACM0、纯离线分析

---

## A. out/firmware_backup 完整文件清单

### A.1 firmware/ 目录

| 文件 | 大小 | 类型/magic | 用途判断 |
|---|---|---|---|
| `WIFI_RAM_CODE` | 158,784 B | 无标准 MT6627 magic，含 `LE#M` offset 0x28 | WiFi RAM code，非 MT6627 专属，可能是其他 66xx 变体或 3.4 编译产物 |
| `WIFI_RAM_CODE_SOC` | 160,464 B | **前4字节 `MTKW`**, platform=0x0030 (SOC integrated mode) | WiFi RAM code，**SOC 集成模式版本**，可用于 MT6572+MT6627 整合方案 |
| `WIFI_RAM_CODE_E6` | 159,264 B | 无 MTKW magic，含 `LE#M` offset 0x28 | WiFi RAM code，**E6 variant** (可能是早期 ROM)，非标准 |
| `WIFI_RAM_CODE_MT6628` | 157,344 B | **前4字节 `MTKW`**, platform=0x37b0 (MT6628) | MT6628 WiFi firmware — **不属于 Z1**, MT6628 是不同芯片 |
| `WIFI_RAM_CODE_MT6630` | 235,760 B | **前4字节 `MTKW`**, platform=0x0813 (MT6630) | MT6630 WiFi firmware — **不属于 Z1**, MT6630 是更高端芯片 |
| `WMT_SOC.cfg` | 66 B | ASCII text | **WMT 配置文件**，指定 coex_wmt_ant_mode=1, co_clock_flag=1，用于 BT/WiFi 共天/共时 |
| `S_ANDRO_SFL.ini` | 417 B | 以 `SSSS` 开头 | Android SFL (Serial Flash Loader) 配置文件，用于 eMMC 刷机，**不需要运行时加载** |
| `mt6572_82_patch_e1_0_hdr.bin` | 37,360 B | 前16字节 `"20140507093455a..."` ASCII 时间戳 + 平台头 | **MT6572 基带/Modem patch**, 属于 2G modem firmware，**不是 WiFi/BT 相关** |
| `mt6572_82_patch_e1_1_hdr.bin` | 20,488 B | 前16字节 `"20140507093455a..."` ASCII 时间戳 | **MT6572 基带/Modem patch v2**, 同上 **不是 WiFi/BT 相关** |
| `catcher_filter_1_2g_n.bin` | 1,367 B | 以 `CDAB 5424` 开头 | **Catcher 滤波器参数**，RF 测试用，**不是运行时固件** |
| `modem_1_2g_n.img` | 2,399,148 B | ARM 裸机镜像 (`18F09FE5` = `E59FF018` = `STR R0,[R0]` 循环) | **2G Modem firmware** (MT6572 内置 2G 基带), **不是 WiFi/BT 相关** |

### A.2 firmware/mt6627/ 目录 (FM patch — 6627 专属)

| 文件 | 大小 | 用途 |
|---|---|---|
| `mt6627_fm_v1_patch.bin` | 1,028 B | **FM patch v1** (有效固件，魔数 `0020FF21`) |
| `mt6627_fm_v1_coeff.bin` | 1,092 B | **FM coeff v1** (系数文件，有效数据) |
| `mt6627_fm_v2_patch.bin` | 0 B | FM patch v2 — **空文件** (无用) |
| `mt6627_fm_v2_coeff.bin` | 2 B | FM coeff v2 — **几乎空** (无用) |
| `mt6627_fm_v3_patch.bin` | 0 B | FM patch v3 — **空文件** |
| `mt6627_fm_v3_coeff.bin` | 0 B | FM coeff v3 — **空文件** |
| `mt6627_fm_v4_patch.bin` | 0 B | FM patch v4 — **空文件** |
| `mt6627_fm_v4_coeff.bin` | 0 B | FM coeff v4 — **空文件** |
| `mt6627_fm_v5_patch.bin` | 0 B | FM patch v5 — **空文件** |
| `mt6627_fm_v5_coeff.bin` | 0 B | FM coeff v5 — **空文件** |

### A.3 bin/ 目录 (3.4.67 编译的 Android 原生二进制)

| 文件 | 解释器 | 依赖共享库 | 用途 |
|---|---|---|---|
| `wmt_loader` | `/system/bin/linker` | libcutils.so, libc.so, libstdc++.so, libm.so | **WMT firmware downloader** — 通过 UART 将 WiFi/BT firmware 下载到 MT6627 RAM |
| `6620_launcher` | `/system/bin/linker` | libcutils.so, libc.so, libstdc++.so, libm.so | **MT6620/MT6627 启动器** — 发送 WMT 初始化序列 |
| `mtkbt` | `/system/bin/linker` | libcutils.so, libc.so, libstdc++.so, libm.so | **Android Bluetooth 守护进程** — 管理 BT stack，非 firmware loader |
| `wlan_loader` | `/system/bin/linker` | libcutils.so, libc.so, libsysutils.so, libstdc++.so, libm.so | **WiFi driver loader** — 加载 wlan.ko + 发 firmware 到芯片 (Android property 交互) |
| `6620_wmt_lpbk` | `/system/bin/linker` | — | **WMT loopback test** binary — 蓝牙测试工具 |
| `6620_wmt_concurrency` | `/system/bin/linker` | — | **WMT 并发测试** — WiFi+BT 并发测试工具 |
| `btconfig` | `/system/bin/linker` | — | **BT socket 配置工具** — 通过 UNIX socket 配 BT |

### A.4 bluetooth/ 目录

| 文件 | 用途 |
|---|---|
| `bt_stack.conf` | BlueZ/BTstack 配置 — 定义 bt_snoop_log, trace level 等 |
| `bt_did.conf` | BT Device ID Profile 配置 (PnP vendor/product/ver) |
| `btconfig.xml` | BT 配置 XML (可能是 proprietary) |
| `auto_pair_blacklist.conf` | 自动配对黑名单 |
| `auto_pair_devlist.conf` | 自动配对白名单 |

### A.5 wifi/ 目录

| 文件 | 用途 |
|---|---|
| `wpa_supplicant.conf` | wpa_supplicant 全局配置 — 定义 ctrl_interface=wlan0, MTK WMM params |
| `wpa_supplicant_overlay.conf` | 覆盖配置 — p2p_no_group_iface=1 |
| `p2p_supplicant_overlay.conf` | P2P 叠加配置 — p2p_no_group_iface + driver_param |

### A.6 fmr/ 目录

| 文件 | 用途 |
|---|---|
| `mt6627_fm_cust.cfg` | FM 用户定制配置 (ASCII) |

### A.7 modules/ 目录

| 文件 | 状态 |
|---|---|
| `wlan.ko` | **3.4.67 编译**，mainline 不能用 |
| `wlan_.ko` | **3.4.67 编译**，mainline 不能用 |

### A.8 根目录

| 文件 | 用途 |
|---|---|
| `build.prop` | Android build prop — ro.build=KOT49H, SDK19, Android 4.4.2, device=Z1/j72_z1 |

---

## B. Z1 应该挑的固件子集 (MT6627 专属)

### B.1 WiFi firmware — 确定用 `WIFI_RAM_CODE_SOC`

```
WIFI_RAM_CODE_SOC  (160,464 B, magic "MTKW", platform=0x0030 = SOC mode)
```

**理由**: Z1 的 MT6572 + MT6627 是整合 SoC 方案 (SOC 集成模式), WMT_SOC.cfg 中 `co_clock_flag=1` 确认了共时。`WIFI_RAM_CODE_SOC` 的 `platform` 字段值为 0x0030 表明是为 SOC 整合模式编译的。MT6628/MT6630/E6 版本分别对应不同硬件平台/芯片，不应该用于 Z1。

### B2. FM patch — 只有 v1 有效

```
mt6627_fm_v1_patch.bin  (1,028 B)
mt6627_fm_v1_coeff.bin  (1,092 B)
```

v2-v5 全部为空/近空文件，排除。

### B.3 WMT 配置

```
WMT_SOC.cfg  (66 B)
```

### B.4 明确不需要的文件

| 排除文件 | 原因 |
|---|---|
| `WIFI_RAM_CODE` | 非标准 magic，非 6627 专属 |
| `WIFI_RAM_CODE_E6` | E6 变体，非 6627 |
| `WIFI_RAM_CODE_MT6628` | MT6628 专用 firmware，不是 Z1 的芯片 |
| `WIFI_RAM_CODE_MT6630` | MT6630 专用 firmware，不是 Z1 的芯片 |
| `modem_1_2g_n.img` | 2G 基带固件，非 WiFi/BT |
| `mt6572_82_patch_e1_0_hdr.bin` | 基带 patch，非 WiFi/BT |
| `mt6572_82_patch_e1_1_hdr.bin` | 基带 patch v2，非 WiFi/BT |
| `catcher_filter_1_2g_n.bin` | RF 测试参数，非运行时固件 |
| `S_ANDRO_SFL.ini` | 刷机配置文件，非运行时 |
| FM v2-v5 patch/coeff | 空文件 |
| `wlan.ko` / `wlan_.ko` | 3.4.67 ABI，mainline 不兼容 |

---

## C. mainline /lib/firmware 目标布局

### C.1 mainline 驱动分析

从 torvalds 7.0-rc7 的蓝牙驱动 (`drivers/bluetooth/btmtkuart.c`) 可知:

- mainline **已有** `btmtkuart` 驱动 (serdev 框架, 处理 UART MTK BT)
- 但 **仅支持 MT7622/MT7663u/MT7668u** 三种 standalone 蓝牙芯片
- **不支持 MT6627** (MT6627 是更老的 WCN 2.5 combo, 走 proprietary WMT 协议)
- firmware 加载用 `request_firmware()`，路径格式为 `"mediatek/BT_RAM_CODE_MT7663_1_1_hdr.bin"` 或 `FIRMWARE_MT7663 = "mediatek/mt7663pr2h.bin"`
- WMT patch 协议通过 HCI_VENDOR_PKT (0xfc6f) 下发，与 mt76xx 类似

**核心结论**: MT6627 的 WiFi + BT combo 在 mainline 7.0-rc7 **没有原生驱动**。需要从 vendor 3.4 内核移植 `drivers/misc/mediatek/connectivity/` 整个 connectivity driver 栈 (包括 wlan.ko 对应的 mtk_wmt + mtk_aux supp + stp_btx 等)。

### C.2 mainline 固件文件名规范

参考 `btmtk.c` 中已有的 firmware 名称约定:

| 驱动类型 | 命名模式 | 例子 |
|---|---|---|
| BT USB (mt76xx) | `mediatek/BT_RAM_CODE_MT{chip}_{ver}_{rev}_hdr.bin` | `mediatek/BT_RAM_CODE_MT7663_1_1_hdr.bin` |
| BT USB (mt76xx) | `mediatek/mt{chip}pr{ver}.bin` | `mediatek/mt7663pr2h.bin` |
| WiFi (vendor 移植) | 通常直接用 `WIFI_RAM_CODE` 或 `wlan_mt6627` | — |

### C.3 推荐的 /lib/firmware 布局

```
/lib/firmware/
├── mediatek/
│   ├── mt6627_bt_ram_code.bin          ← WIFI_RAM_CODE_SOC 的 BT 部分 (待从 SOC 版本拆分或直接用 SOC 版)
│   └── mt6627_fm_v1_patch.bin          ← FM patch (如果需要 FM)
│   └── mt6627_fm_v1_coeff.bin          ← FM coeff
├── WIFI_RAM_CODE                       ← WiFi firmware (vendor 驱动可能直接 request "WIFI_RAM_CODE")
│                                          或改名 WIFI_RAM_CODE_MT6627 更清晰
└── WMT_SOC.cfg                         ← WMT 配置 (放在这里)
```

**但是** — 由于 mainline 7.0-rc7 **没有 MT6627 WiFi 驱动**, 实际上目前不需要拷 firmware 进 /lib/firmware。
WiFi 驱动需要从 3.4 vendor 移植，移植好之后才能确定驱动 `request_firmware()` 期待的具体文件名。

**建议做法**: 先不拷入 /lib/firmware，等 WiFi driver 移植完成后，根据驱动中的固件加载代码来定文件名。
参考 `drivers/misc/mediatek/connectivity/wmt_core.c` (3.4 vendor) 中通常会硬编码固件名如 `"WIFI_RAM_CODE"` 或 `"WMT_SOC.cfg"`。

**预计 vendor 驱动期望的 firmware 文件名** (基于 3.4 mt6572 驱动惯例):
- `WIFI_RAM_CODE` (WiFi RAM code)
- `WMT_SOC.cfg` (WMT config)

### C.4 需要额外准备的固件

MT6627 的 BT firmware 和 WiFi firmware 通常在同一个 `WIFI_RAM_CODE_SOC` 文件中 (SOC 整合模式),
或者 BT firmware 在另一个单独的文件中。从 3.4 vendor 源码来看，MTK WMT 驱动会：

1. 先通过 UART 发送 semaphore 握手
2. 加载 BT firmware (通常是 `WMT.conf` 或 `bt_config` 指定的文件名)
3. 加载 WiFi firmware (`WIFI_RAM_CODE`)
4. 发送配置 (`WMT_SOC.cfg`)
5. 激活 BT 和 WiFi

由于 `WIFI_RAM_CODE_SOC` 可能同时包含 BT + WiFi 固件镜像 (SOC 整合模式),
可能 **不需要单独的 BT firmware 文件**。

---

## D. wmt_loader 二进制依赖分析

### D.1 wmt_loader 分析结果

```
ELF 32-bit LSB shared object, ARM, EABI5 version 1 (SYSV)
dynamically linked, interpreter /system/bin/linker, stripped

NEEDED: libcutils.so, libc.so, libstdc++.so, libm.so

关键函数: property_get, property_set (Android property service 交互)
         load_file, chown (load firmware file to chip)
         ioctl (UART 配置)
         init_module (加载内核模块 wlan.ko — Android 专属)
```

### D.2 结论: **wmt_loader 不能直接在 mainline 下使用**

原因:
1. **Android linker**: 解释器 `/system/bin/linker` 是 bionic C library，glibc 系统上没有
2. **Android property API**: 调用 `property_get`/`property_set` 依赖 `libcutils.so` (Android 专有)
3. **内核模块加载**: 调用 `init_module` 直接加载 `wlan.ko` — 这是 Android 的做法，mainline 用 `modprobe`/`insmod`
4. **MT6572 专用**: 硬编码了 MT6572 + MT6627 的 WMT 协议

### D.3 替代方案

**方案 A: 移植 mainline 的 btmtkuart 驱动** (推荐)

mainline 7.0-rc7 已经有 `btmtkuart` 驱动，但它目前只支持 MT7622/MT7663u/MT7668u。
需要添加 MT6627 的兼容条目:
```c
{ .compatible = "mediatek,mt6627-bluetooth", .data = &mt6627_data },
```
并让 `btmtkuart_setup()` 调用已有的 `btmtk_setup_firmware()` 来加载 BT firmware。

**方案 B: 移植 vendor WMT 驱动** (更完整的方案)

MT6627 是 WiFi + BT combo chip, 需要 `mtk_wmt` 内核模块来协调 BT/WiFi 共天。
vendor 驱动 (`drivers/misc/mediatek/connectivity/`) 包含了:
- `wmt_core.ko` — WMT 核心 (UART/SDIO 通信层)
- `wmt_drv.ko` — WMT 驱动
- `wmt_platform.ko` — 平台适配
- `auxprop.ko` — 辅助属性
- `gps_25.14_epos_6_5_1.ko` — GPS

**方案 C: 用 userspace wmt_launcher 替代**

参考 frank-w/BPI-Router-Linux 的 `utils/wmt/` 目录 (WiFi 驱动适配了标准 Linux):
https://github.com/frank-w/BPI-Router-Linux/tree/master/utils/wmt/

这些工具是编译为 glibc/musl 的，可以直接在 mainline Linux 运行:
- `wmt_loader` (标准 Linux 版本) — 加载 firmware blob
- `stp_uart_launcher` — UART 协议栈 launcher
- `wmt.conf` — WMT 配置文件

但 clone 失败，以下是 URL 供后续使用:
> https://github.com/frank-w/BPI-Router-Linux/tree/master/utils/wmt/

### D.4 BPI-Router-Linux clone结果

**失败**。网络原因 (TLS recv error)。
URL 保留供后续手动 clone: `https://github.com/frank-w/BPI-Router-Linux.git`
关注目录: `utils/wmt/`

---

## E. mainline 下 MT6627 bring-up 步骤草案

### 阶段一: 先跑通 BT (纯 UART, 用 serdev 框架)

```bash
# 1. 确保内核支持 serdev 和 MTK BT
CONFIG_BT_MTK=y
CONFIG_BT_BTMTCUART=y
CONFIG_SERDEV=y
CONFIG_SERDEV_MTK_UART=y

# 2. DTS 中添加 BT 节点
&uart2 {  /* 假设 MT6627 BT 走 uart2 */
    status = "okay";
    mediatek,wmt-gpio;
    mediatek,lpo-enable;

    bluetooth {
        compatible = "mediatek,mt6627-bluetooth";
        /* 注意: mainline 目前不支持 mt6627, 需要添加 of_match 条目 */
        host-wakeup-gpios = <&pio 123 GPIO_ACTIVE_HIGH>;
        device-wakeup-gpios = <&pio 124 GPIO_ACTIVE_HIGH>;
    };
};

# 3. 加载 firmware (由 btmtkuart 驱动自动 request_firmware)
#    需要将 BT firmware 放到 /lib/firmware/mediatek/

# 4. 启动蓝牙
modprobe btmtkuart   # serdev 自动 probe
echo "connect" > /sys/class/bluetooth/hci0/device/power/control
hciconfig hci0 up
```

### 阶段二: 移植 WiFi driver (需要从 3.4 vendor 移植)

```bash
# 1. 移植 connectivity driver 到 kernel tree
cp drivers/misc/mediatek/connectivity/* torvalds/drivers/misc/mediatek/connectivity/

# 2. 确保 WMT 走 UART (MT6572+MT6627 combo)
#    修改 wmt_platform.c:
#    - wmt_hif = WMT_HIF_UART
#    - lpo_cut = off (MT6627 no external LP0 clock)

# 3. 准备 firmware
#    WIFI_RAM_CODE → /lib/firmware/WIFI_RAM_CODE
#    WMT_SOC.cfg → /lib/firmware/WMT_SOC.cfg

# 4. 加载驱动
modprobe mtk_wmt_sdio   # 或 mtk_wmt_uart
insmod wlan.ko           # 3.4 的 wlan.ko 不能用, 需要移植
                          # 或改用 bcmdhd (如果 WiFi 芯片可兼容)

# 5. 验证
dmesg | grep -i wifi
dmesg | grep -i wmt
```

### 阶段三: WiFi + BT 共天协调

```bash
# vendor wmt_core 负责协调 BT/WiFi 共享天线
# 确保 WMT_SOC.cfg 中 coex_wmt_ant_mode=1
# 启动 btstack 之前先确认 WiFi 已就绪
rfkill list    # 查看 WiFi/BT 硬件开关状态
```

---

## F. 关键发现总结

### (1) 固件齐不齐？

**WiFi firmware: 齐。** `WIFI_RAM_CODE_SOC` 包含 MT6627 SOC 整合模式所需的 WiFi firmware。
**BT firmware: 可能不完整。** SOC 整合模式下 BT firmware 可能包含在 `WIFI_RAM_CODE_SOC` 中，
也可能需要单独的 BT patch 文件。需要检查 3.4 vendor wmt_core.c 中 BT firmware 路径配置。
**FM patch: 只有 v1 有效** (v2-v5 全空), FM 功能可能受限。
**Modem/基带固件: 独立文件**, 不属于 WiFi/BT 范围。

### (2) Z1 应该用哪几个固件？

| 组件 | 文件 | 大小 |
|---|---|---|
| WiFi firmware | `WIFI_RAM_CODE_SOC` | 160,464 B |
| FM patch | `mt6627_fm_v1_patch.bin` | 1,028 B |
| FM coeff | `mt6627_fm_v1_coeff.bin` | 1,092 B |
| WMT config | `WMT_SOC.cfg` | 66 B |

排除: WIFI_RAM_CODE, WIFI_RAM_CODE_E6, WIFI_RAM_CODE_MT6628, WIFI_RAM_CODE_MT6630,
所有空 FM v2-v5, modem patches, catcher_filter, modem_1_2g_n.img, S_ANDRO_SFL.ini.

### (3) wmt_loader 能不能直接用？

**不能。** wmt_loader (和所有 bin/ 下的二进制) 是:
- 用 Android bionic linker (`/system/bin/linker`) 链接的
- 依赖 Android 专有库 (libcutils.so, libsysutils.so)
- 调用 Android property API (property_get/property_set)
- 调用 init_module 直接加载 3.4.67 编译的 wlan.ko

在 mainline Alpine/Debian/Arch 上**无法运行**。需要:
- 用 musl/glibc 交叉编译标准 Linux 版本的 wmt_loader (参考 BPI-Router-Linux)
- 或者直接用 mainline 的 `btmtkuart` 驱动 (自动 request_firmware, 不需要 userspace loader)

### (4) 报告路径

`/home/<user>/claude/6572/mainline_recon/WIFI_FW_BLOB.md`

---

## G. 后续 TODO 清单

1. [ ] 检查 `WIFI_RAM_CODE_SOC` 是否包含 BT firmware — 需要 3.4 vendor 源码确认
2. [ ] 从 3.4 vendor kernel 提取 `drivers/misc/mediatek/connectivity/` 源码
3. [ ] 移植 mtk_wmt (UART 模式) 到 mainline tree
4. [ ] 移植 wlan driver (或改用 bcmdhd/mtk_wifi) 到 mainline
5. [ ] 编写 Z1 的 DTS WiFi/BT 节点
6. [ ] 编译测试 WiFi/BT 固件加载
7. [ ] clone BPI-Router-Linux 参考其 utils/wmt/ 的 standard Linux 适配版本
