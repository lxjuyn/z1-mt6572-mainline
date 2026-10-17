# 驱动必要/非必要拆分方案 + boot.img 瘦身估算 — Z1 (MT6572) mainline 移植

> 只读分析，2026-08-15。未修改任何源/配置/镜像，仅产出本报告。
> 数据源: `fork_linux/.config`(8/14 20:41)、`out/boot_mainline_connsys.img`(8/14 16:29, 上板验证版)、`out/ramdisk_mainline_connsys.gz`(8/14 22:43, 最新含 BT 栈版)、`bridge_backups/products/products_connsys_modules_z1vermagic_20260814_015009/`、`bridge_backups/products/products_bt_stack_20260814_204345/`、`scripts/z1_modprobe_connsys.sh`。
> 所有尺寸均为实测字节数(解包 boot.img / lzma 解压 ramdisk body / 逐文件 du)。ramdisk 已重新打包实测瘦身压缩后尺寸。

---

## 0. 结论摘要 (TL;DR)

1. **模块化已经基本就位**：`.config` 里 WiFi/BT 相关全是 `=m`（`CFG80211=m`、`MAC80211=m`、`BT=m`、`BT_HCIUART=m`…），Z1 实际用的 CONNSYS 驱动（wlan_gen2/mtk_stp_wmt_soc 等）是**树外 .ko**（不在 .config），音频 `CONFIG_SOUND is not set`（完全缺失）。**真正的改动只在"ramdisk 里塞了什么"，不在内核 .config。**
2. **boot 必要驱动（须 =y）**：eMMC(`MMC_MTK`)、rootfs(`EXT4_FS`)、PMIC(`MTK_PMIC_WRAP`/`MFD_MT6397`/`REGULATOR_MT6323`)、时钟(`COMMON_CLK_MT6572`)、console(`SERIAL_8250_MT6577`)、WDT、按键(`KEYBOARD_MT6779`/`MTK_PMIC`/`TOUCHSCREEN_GOODIX`)、显示(`FB_SIMPLE`+fbcon+backlight)、电池(`GENERIC_ADC_BATTERY`+`MT6323_AUXADC`)、USB gadget 恢复 console（建议暂留）。
3. **非必要（移 Alpine 后加载）**：WiFi CONNSYS 6 .ko + launcher + iw，BT 栈 6 .ko + 5 个工具，全部固件 blob。**vermagic 全部 `7.0.0-rc7-z1+`，与运行内核精确匹配，`CONFIG_MODULE_SIG` 未开无签名门禁。**
4. **boot.img 瘦身实测**：ramdisk（含 BT 栈版 2,225,360 B）移出 6.47 MB 解压内容（WiFi 2.50MB + BT 2.99MB + iw 0.76MB + 固件 0.22MB）后，XZ 压缩 body 从 **2,224,848 B → 925,584 B**（-1.24 MiB）。boot.img 由 **5.73 MiB → 4.50 MiB（省 1.24 MiB）**；相对当前上板验证版（5.39 MiB，无 BT 栈）省 **0.89 MiB**，6 MiB 分区富余从 0.61 MiB → **1.50 MiB**。
5. **下一步只需**：把 12 个 .ko + launcher + 5 个 BT 工具 + iw + 4 处固件放到 Alpine rootfs `/lib/modules/7.0.0-rc7-z1+/`，`/init` 换成"挂 p4 + switch_root"版，boot ramdisk 只留 busybox + init（1,535,773 B 解压 / 925,584 B 压缩）。既有 `scripts/z1_modprobe_connsys.sh` 直接可用。

---

## 1. 当前 .config 驱动 =y 盘点（必要 vs 非必要）

文件: `fork_linux/.config`（`CONFIG_LOCALVERSION="-z1"` + `LOCALVERSION_AUTO=n` → vermagic 固定 `7.0.0-rc7-z1+`，不随重编漂移）。

### 1.1 必要（boot 阶段就需要，保持 =y）

| 子系统 | CONFIG_* | 现状 | 为什么必须进内核 |
|---|---|---|---|
| eMMC/存储 | `MMC=y` `MMC_BLOCK=y` `MMC_MTK=y` `MMC_CQHCI=y` | =y | initramfs 要挂 mmcblk0p4 rootfs 必须 MMC_MTK 可用；实测 mmc0 probe ~8s。做成模块需两段 initramfs，纯增复杂度无收益 |
| rootfs | `EXT4_FS=y` `EXT4_USE_FOR_EXT2=y` | =y | p4 = ext4 Alpine |
| 块/分区 | `BLOCK=y` `DEVTMPFS=y` `DEVTMPFS_MOUNT=y` | =y | devtmpfs 生成 /dev/mmcblk0pN 节点 |
| PMIC | `MTK_PMIC_WRAP=y` `MFD_MT6397=y` `REGULATOR_MT6323=y` `MTK_REGULATOR_COUPLER=y` | =y | LK→内核 PMIC 交接；eMMC vmmc/vqmmc 稳压、VCN18/28/33(WiFi)、VGP1/VIO18(触摸)、VUSB 全出自 mt6323。PMIC 若为模块 → rootfs 挂载失败(bootloop) |
| 时钟 | `COMMON_CLK_MEDIATEK=y` `COMMON_CLK_MT6572=y` | =y | 所有外设时钟门控 |
| console | `SERIAL_8250=y` `SERIAL_8250_CONSOLE=y` `SERIAL_8250_MT6577=y` `SERIAL_CORE=y` `SERIAL_EARLYCON=y` | =y | UART0(ttyS0) 日志/登录；earlycon 抓早期 panic |
| WDT | `WATCHDOG=y` `WATCHDOG_CORE=y` `MEDIATEK_WATCHDOG=y` | =y | LK 喂的狗在内核接管前必须早停 + probe 接管，模块化会留空窗 |
| 按键(用户策略: boot 期) | `INPUT=y` `INPUT_EVDEV=y` `KEYBOARD_MT6779=y`(keypad 矩阵 8 键) `KEYBOARD_MTK_PMIC=y`(电源键) `KEYBOARD_GPIO=y` `INPUT_MATRIXKMAP=y` `TOUCHSCREEN_GOODIX=y`(GT911 + 3 电容键) | =y | 用户明确"按键放 boot"；13 键 |
| 显示 | `FB=y` `FB_SIMPLE=y` `FB_CORE=y` `FRAMEBUFFER_CONSOLE=y` `BACKLIGHT_CLASS_DEVICE=y` `BACKLIGHT_GPIO=y` `BACKLIGHT_PWM=y` | =y | dts `simple-framebuffer` + fbcon 早期 UI/启动画面 |
| 电池 | `POWER_SUPPLY=y` `GENERIC_ADC_BATTERY=y` `POWER_SUPPLY_HWMON=y` `IIO=y` `MEDIATEK_MT6323_AUXADC=y` `IIO_HRTIMER_TRIGGER=y` | =y | dts io-channels = mt6323_adc BATSNS/BAT_TEMP → 电池电压/温度 |
| GPIO/pinctrl | `GPIOLIB=y` `PINCTRL_MTK=y` `PINCTRL_MTK_V2=y` `PINCTRL_MT6572=y` `PINCTRL_MT6397=y` | =y | keypad/goodix/backlight/musb 的 GPIO 依赖 |
| USB gadget(恢复 console) | `USB_GADGET=y` `USB_G_SERIAL=<SERIAL> `USB_F_SERIAL=<SERIAL> `USB_F_ACM=y` `USB_MUSB_HDRC=y` `USB_MUSB_GADGET=y` `USB_MUSB_MEDIATEK=y` `USB_INVENTRA_DMA=y` `PHY_MTK_MT6572_USB2_HANDOFF=y` | =y | 现 /init 用 ttyGS0 getty 作恢复通道；**建议暂留**（见 §2） |
| RTC | `RTC_CLASS=y` `RTC_DRV_MT6397=y` `RTC_HCTOSYS=y` `RTC_SYSTOHC=y` `RTC_INTF_*=y` | =y | 极小（~10KB），启动即校准系统时间；可模块化但没必要 |
| LED | `LEDS_CLASS=y` `LEDS_GPIO=y` `LEDS_PWM=y` `NEW_LEDS=y` `LEDS_TRIGGERS=y` | =y | 用户列"非必要"，当前 =y；可改 =m（低优先级） |
| HW RNG | `HW_RANDOM_MTK=y` `HW_RANDOM=y` | =y | 早期熵 |
| 其它 | `GPIO_CDEV=y` `REGULATOR_FIXED_VOLTAGE=y` `REGULATOR_GPIO=y` `EXTCON_USB_GPIO=y` `HWMON=y` `SENSORS_IIO_HWMON=y` `RFKILL_*` | =y | 基础设施 |

> 注意: **GPU/LIMA 不在当前内核里** — `# CONFIG_DRM is not set`、无 `CONFIG_LIMA`/`CONFIG_MALI`。显示路径是 simple-framebuffer（dts line 46），不是 DRM。早前 `boot_mainline_mt6572_lima_simplefb_*` 系列是实验版已弃。所以"GPU"当前 = 无，若日后要 LIMA 需另开 DRM（会显著加大 zImage，需重新评估 6 MiB 预算）。

### 1.2 非必要（可后加载 / 已 =m）

| 子系统 | CONFIG_* | 现状 | 实际 Z1 驱动 |
|---|---|---|---|
| WiFi | `CFG80211=m` `MAC80211=m` `WLAN_VENDOR_*=y`(空的 kconfig 开关) | =m | **树外** wlan_gen2.ko + mtk_stp_wmt_soc.ko + mtk_wmt_wifi_soc.ko + cfg80211.ko(配套) |
| BT | `BT=m` `BT_HCIUART=m` `BT_HCIVHCI=m` `BT_HIDP=m` `BT_BREDR=y` | =m | **树外** bluetooth.ko/ecc.ko/ecdh_generic.ko/hci_vhci.ko/hidp.ko/uhid.ko（BT_HCIUART_MTK 未开，走 vhci 桥路线）+ mtk_btif_drv.ko + mtk_stp_bt_soc.ko |
| USB host | `USB=y` `USB_SUPPORT=y` `USB_OTG=y`；EHCI/OHCI/XHCI/DWC2 全关 | 关 | 无主机功能（只做 device） |
| 音频 | `# CONFIG_SOUND is not set` | **完全缺失** | MT6323 codec / MT6572 AFE 无 mainline 支持，单独立项（CLAUDE.md §4 旁路） |
| RTC/LED | 见 §1.1 | =y | 可选后移（收益 <10KB，不建议为省这点折腾） |

> 关键点: `CFG80211=m` 意味着**内核没内编 cfg80211**，Z1 的 wlan_gen2 依赖的那份 cfg80211.ko 必须随 CONNSYS 一起部署（insmod 顺序里在最前）。这是与"内核已内编则可跳过"不同的坑。

### 1.3 遗留通用 =y（无 Z1 硬件对应，可关非本次重点）

来自多平台 defconfig 的残留，全部与 Z1 无关，关掉只省 zImage 体积（当前 3.78MB 已够用，非必要）:
`SERIAL_8250_OMAP` `SERIAL_AMBA_PL011` `SERIAL_CONEXANT_DIGICOLOR` `SERIAL_FSL_LPUART` `SERIAL_XILINX_PS_UART` `SERIAL_ST_ASC`、`FB_EFI`、`GPIO_OMAP` `GPIO_MPC8XXX` `GPIO_XILINX` `GPIO_PCA953X` `GPIO_PCF857X` `GPIO_STMPE`、`MMC_OMAP` `MMC_OMAP_HS`、`DW_DMAC` `PL330_DMA` `TI_EDMA` `TI_DMA_CROSSBAR` `FSL_EDMA` `XILINX_DMA` `DMA_OMAP`、`PINCTRL_MT2701/7623/7629/8127/8135`、`MTD_*`(eMMC 启动不需要) 等。**不影响本方案，仅存档备查。**

---

## 2. 必须编进内核 vs 可模块化 评估

| 驱动 | 判定 | 理由 |
|---|---|---|
| eMMC (MMC_MTK/MMC_BLOCK) | **必须 =y** | rootfs 就挂在这；模块化需两段 initramfs，收益 0 |
| PMIC (PMIC_WRAP/MFD_MT6397/REGULATOR_MT6323) | **必须 =y** | 电源轨全由它管，模块化 → eMMC 稳压缺失 → rootfs 挂不上 |
| UART console | **必须 =y** | 日志 + C3 桥登录 + earlycon |
| 时钟 (COMMON_CLK_MT6572) | **必须 =y** | 全 SoC 时钟门控 |
| WDT (MEDIATEK_WATCHDOG) | **必须 =y** | LK 喂的狗必须早停+接管，无空窗 |
| 按键 13 键 (mt6779-keypad/pmic-keys/goodix) | **必须 =y**（用户策略） | 用户明确"按键放 boot"，UI/交互 boot 期可用 |
| 显示 (FB_SIMPLE+fbcon+backlight) | **必须 =y**（用户策略） | 用户明确"显示放 boot" |
| 电池 (GENERIC_ADC_BATTERY+MT6323_AUXADC) | **必须 =y**（用户策略） | 用户明确"电池放 boot" |
| USB gadget (g_serial/ACM, ttyGS0) | **建议暂留 =y** | 它是当前 initramfs 的恢复/调试 console。若移出，boot 期失去 USB 登录通道。**可后移但要在 Alpine 稳定后再做**（g_serial 转 =m 需重新验证 gadget 拉起时序） |
| RTC (MT6397) | **可后移但没必要** | 体积极小，后移收益 <0.1%；保持 =y 最简单 |
| LED | **可后移** | 用户策略列非必要；LEDS_GPIO/PWM 可改 =m，低优先级 |
| WiFi (CONNSYS + cfg80211) | **模块化（已达成）** | 已 =m/树外 .ko，insmod 后 `iw scan` 已上板验证成功（WIFI_SCAN_SUCCESS_20260814） |
| BT (栈 + CONNSYS BT 通道) | **模块化（已达成）** | 已 =m/树外 .ko + vhci 桥用户态工具 |
| 音频 | **缺驱动，单独立项** | .config 无 SOUND；不属本次拆分范围 |

**结论**: 拆分是"ramdisk 内容"问题，不是"内核 .config"问题。boot 必要驱动当前全部 =y 且正确；非必要 WiFi/BT 已是模块形态，只是被错误地塞进了 boot ramdisk。

---

## 3. boot.img / ramdisk 体积测算（瘦身）

### 3.1 当前 boot.img 分段（`out/boot_mainline_connsys.img`，上板验证版）

| 段 | 大小 (B) | 说明 |
|---|---|---|
| 文件总大小 | **5,652,480** | ≤ 6 MiB (6,291,456)，富余 639,288 |
| kernel 段 (zImage + appended dtb) | 3,784,780 | zImage 3,764,736 + dtb 19,568 + 对齐 476；dtb 拼在 zImage 末尾（APPENDED_DTB） |
| ramdisk 段 (rs) | 1,863,368 | 512B MTK ROOTFS 头 + 1,862,856 XZ body |
| ramdisk 解压后 | 5,026,304 | 此版**无 BT 栈**（root/connsys 2,496,020 + iw 764,400 + 固件） |

### 3.2 最新 ramdisk（`out/ramdisk_mainline_connsys.gz`，含 BT 栈版）

| 项 | 大小 (B) |
|---|---|
| 文件 | 2,225,360 (= 512 头 + 2,224,848 XZ body) |
| 解压后 (cpio) | 8,018,944 |
| 解压后目录树 | 8,005,209 |

**各段解压后尺寸（逐文件实测）:**

| 段 | 解压后 (B) | 内容 |
|---|---|---|
| WiFi CONNSYS（不含 bt） | **2,496,020** | cfg80211.ko 491,872 + mtk_btif_drv.ko 113,984 + mtk_stp_wmt_soc.ko 369,696 + mtk_stp_bt_soc.ko 12,616 + mtk_wmt_wifi_soc.ko 14,360 + wlan_gen2.ko 958,904 + mtk_stp_launcher 522,212 + 2 个 shell 脚本 12,376 |
| BT 栈 ko | **742,452** | bluetooth.ko 641,676 + ecc.ko 36,876 + ecdh_generic.ko 9,304 + hci_vhci.ko 14,860 + hidp.ko 26,468 + uhid.ko 13,268 |
| BT 工具（静态 ARM） | **2,247,660** | stpbt-vhci-bridge 453,320 + stpbt-hci-test 448,296 + hci-localver 448,652 + btup-scan 448,460 + btif-lpbk-test 448,932 |
| iw（nl80211 静态 ARM） | **764,400** | /usr/bin/iw |
| 固件 | **218,904** | /lib/firmware/WIFI_RAM_CODE_MT6582 160,464 + /system/etc/firmware/ROMv1_patch_0_hdr.bin 37,360 + ROMv1_patch_1_hdr.bin 20,488 + WMT_SOC.cfg 80 + /etc/firmware/nvram/WIFI 512 |
| **小计 可移除** | **6,469,436** | 81% 的解压内容 |
| busybox（静态） | 1,534,121 | bin/busybox（含 switch_root/mdev/getty/watchdog） |
| init 脚本 | 1,652 | /init |
| 骨架 (etc + 空目录) | ~1,000 | |
| **小计 保留** | **1,535,773** | 19% |

### 3.3 瘦身实测（只留 busybox+init 后重打包）

| 方案 | XZ -9 --check=crc32 body (B) | +512 头 (B) |
|---|---|---|
| 现状（含 WiFi+BT+iw+固件） | 2,224,848 | 2,225,360 |
| **瘦身（仅 busybox+init）** | **925,584** | **926,096** |
| 瘦身（gzip -9 对照） | 1,069,037 | 1,069,549 |

### 3.4 结论：boot.img 能瘦多少

| 基准 | boot.img 大小 | 瘦身后 | 省 | 6 MiB 富余 |
|---|---|---|---|---|
| 最新版（ramdisk 含 BT 栈，2,225,360 B） | 6,012,928 (5.73 MiB) | **4,714,496 (4.50 MiB)** | **1,298,432 (1.24 MiB)** | 1,576,960 (1.50 MiB) |
| 上板验证版（ramdisk 无 BT 栈，1,863,368 B） | 5,652,480 (5.39 MiB) | **4,714,496 (4.50 MiB)** | **937,984 (0.89 MiB)** | 1,576,960 (1.50 MiB) |

计算: kernel 段 page 对齐 3,786,752 + 瘦身 ramdisk 926,096 → page 对齐至 4,714,496。与任务描述"ramdisk 约 2.2 MB"一致（即含 BT 栈版 2,225,360 B），**移出后 ramdisk 从 2.22 MB → 0.90 MB，boot.img 从 5.73 MiB → 4.50 MiB，净省约 1.24 MiB**。

> 富余的 1.50 MiB 用途建议：日后若想加回 LIMA/DRM（GPU）或把 USB gadget 改 configfs（需 `CONFIG_USB_CONFIGFS=y` + f_acm 走 configfs），有足够预算。

---

## 4. 驱动清单

### 4.1 Boot 必要驱动清单（.config 保持 =y，零改动）

```
CONFIG_MMC=y CONFIG_MMC_BLOCK=y CONFIG_MMC_MTK=y
CONFIG_EXT4_FS=y
CONFIG_MTK_PMIC_WRAP=y CONFIG_MFD_MT6397=y CONFIG_REGULATOR_MT6323=y
CONFIG_COMMON_CLK_MEDIATEK=y CONFIG_COMMON_CLK_MT6572=y
CONFIG_SERIAL_8250=y CONFIG_SERIAL_8250_CONSOLE=y CONFIG_SERIAL_8250_MT6577=y CONFIG_SERIAL_EARLYCON=y
CONFIG_WATCHDOG=y CONFIG_WATCHDOG_CORE=y CONFIG_MEDIATEK_WATCHDOG=y
CONFIG_INPUT=y CONFIG_INPUT_EVDEV=y CONFIG_KEYBOARD_MT6779=y CONFIG_KEYBOARD_MTK_PMIC=y CONFIG_KEYBOARD_GPIO=y CONFIG_TOUCHSCREEN_GOODIX=y CONFIG_INPUT_MATRIXKMAP=y
CONFIG_FB=y CONFIG_FB_SIMPLE=y CONFIG_FRAMEBUFFER_CONSOLE=y CONFIG_BACKLIGHT_CLASS_DEVICE=y CONFIG_BACKLIGHT_GPIO=y CONFIG_BACKLIGHT_PWM=y
CONFIG_POWER_SUPPLY=y CONFIG_GENERIC_ADC_BATTERY=y CONFIG_MEDIATEK_MT6323_AUXADC=y CONFIG_IIO=y
CONFIG_GPIOLIB=y CONFIG_PINCTRL_MT6572=y CONFIG_HW_RANDOM_MTK=y
CONFIG_USB_GADGET=y CONFIG_USB_G_SERIAL=<SERIAL> CONFIG_USB_F_SERIAL=<SERIAL> CONFIG_USB_F_ACM=y CONFIG_USB_MUSB_HDRC=y CONFIG_USB_MUSB_GADGET=y CONFIG_USB_MUSB_MEDIATEK=y CONFIG_USB_INVENTRA_DMA=y CONFIG_PHY_MTK_MT6572_USB2_HANDOFF=y
CONFIG_DEVTMPFS=y CONFIG_DEVTMPFS_MOUNT=y
```
（清单即现状，无需任何 .config 修改；boot ramdisk 只留 `bin/busybox` + `init`。）

### 4.2 Alpine 后加载驱动清单（非必要）

**源目录**（vermagic 全匹配，勿用旧 dirty 版）:
- `bridge_backups/products/products_connsys_modules_z1vermagic_20260814_015009/`
- `bridge_backups/products/products_bt_stack_20260814_204345/`
- 固件: `out/firmware_backup/firmware/` + `mainline_recon/wifi-nvram-WIFI.bin`

**WiFi/CONNSYS（6 ko + 1 用户态）** — 部署 `/lib/modules/7.0.0-rc7-z1+/`:

| .ko | 大小 (B) | 依赖 |
|---|---|---|
| cfg80211.ko | 491,872 | —（内核 CFG80211=m，必须随带） |
| mtk_btif_drv.ko | 113,984 | — |
| mtk_stp_wmt_soc.ko | 369,696 | mtk_btif_drv |
| mtk_stp_bt_soc.ko | 12,616 | mtk_stp_wmt_soc |
| mtk_wmt_wifi_soc.ko | 14,360 | mtk_stp_wmt_soc |
| wlan_gen2.ko | 958,904 | cfg80211, mtk_stp_wmt_soc, mtk_wmt_wifi_soc |
| mtk_stp_launcher（用户态） | 522,212 | 固件 patch 应答，func-on 必需 |

**BT 栈（6 ko + 5 工具）**:

| .ko | 大小 (B) | 依赖 |
|---|---|---|
| ecc.ko | 36,876 | — |
| ecdh_generic.ko | 9,304 | ecc |
| bluetooth.ko | 641,676 | ecdh_generic, libaes |
| hci_vhci.ko | 14,860 | — |
| hidp.ko | 26,468 | bluetooth |
| uhid.ko | 13,268 | — |

工具（静态 ARM，拷 `/usr/bin` 或 `/root/connsys/bt/tools`）: `stpbt-vhci-bridge`、`stpbt-hci-test`、`hci-localver`、`btup-scan`、`btif-lpbk-test`（各 ~448 KB）。

**iw 工具**: `iw` 764,400 B（nl80211，静态 ARM；wlan_gen2 只支持 nl80211 scan，iwlist 无用）。

**固件（必须一并部署，否则 insmod/启动失败）**:

| 路径（Alpine 上要建） | 文件 | 大小 (B) | 谁读 |
|---|---|---|---|
| /lib/firmware/WIFI_RAM_CODE_MT6582 | ← WIFI_RAM_CODE_SOC | 160,464 | wlan_gen2 request_firmware |
| /system/etc/firmware/WMT_SOC.cfg | WMT_SOC.cfg | 80 | mtk_stp_wmt_soc filp_open 绝对路径 |
| /system/etc/firmware/ROMv1_patch_0_hdr.bin | ← mt6572_82_patch_e1_0_hdr.bin | 37,360 | launcher -p /system/etc/firmware/ |
| /system/etc/firmware/ROMv1_patch_1_hdr.bin | ← mt6572_82_patch_e1_1_hdr.bin | 20,488 | launcher |
| /etc/firmware/nvram/WIFI | wifi-nvram-WIFI.bin | 512 | wlan_gen2 glLoadNvram（MAC <MAC>） |

> Alpine 没有 `/system` 目录，而 `z1_modprobe_connsys.sh` 里 launcher 参数写死 `-p /system/etc/firmware/` —— **要么在 Alpine 建 `/system/etc/firmware/` 目录，要么把脚本参数改成 launcher 实际路径**。此点须在部署时处理。

### 4.3 vermagic 匹配要求

- 运行内核: `7.0.0-rc7-z1+`（`uname -r` / `cat /proc/version`）
- 12 个 .ko 实测 vermagic 全部 = `7.0.0-rc7-z1+ SMP mod_unload ARMv7 p2v8`（`modinfo <ko> | grep vermagic` 已逐一批量核对）
- `CONFIG_MODULE_SIG` **未开** → 无签名门禁；只有 vermagic 一道关
- 纪律（CLAUDE.md §5.7）: ① 不要在 make 命令行设 `LOCALVERSION=`（会消 `+` 使 .ko 失配）；② 若改了 .ko 依赖的内核 ABI 必须重编对应 .ko；③ Alpine 部署后 insmod 前 `modinfo wlan_gen2.ko | grep vermagic` 复核
- 上板前可在 insmod 失败时 `cat /proc/version` 对比确认

### 4.4 insmod 脚本（顺序 = 依赖顺序）

沿用现成 `scripts/z1_modprobe_connsys.sh`（busybox-only 命令集，已上板验证）。顺序:
```
1  cfg80211.ko            （/sys/module/cfg80211 不存在才 insmod；内核是 =m，会走这里）
2  mtk_btif_drv.ko        （BTIF 传输，先于 WMT）
3  mtk_stp_wmt_soc.ko     （WMT/STP 控制面 + 固件下载；⚠️ 绝不可 rmmod → 挂死需 reboot）
4  mtk_stp_bt_soc.ko      （/dev/stpbt）
5  mtk_wmt_wifi_soc.ko    （/dev/wmtWifi）
6  wlan_gen2.ko           （wlan0）
7  建节点: mknod /dev/stpwmt c 190 0 / /dev/stpbt c 192 0 / /dev/wmtWifi c 155 0（mdev 没建时）
8  mtk_stp_launcher -m 3 -p /system/etc/firmware/ &（后台，WMT patch 应答）
9  等 dmesg "WMT HIF info added"（≤20s）+ "patch total num"（≤15s）→ 再 `echo 1 > /dev/wmtWifi`（func-on）
```
BT 栈在 wlan 之后按 `ecc → ecdh_generic → bluetooth → hci_vhci → hidp → uhid` 顺序 insmod（BT 走 vhci 桥，`stpbt-vhci-bridge` 用户态桥 tty/stpbt→vHCI）。

**Alpine 内触发**: OpenRC `/etc/local.d/connsys.start` + `rc-update add local default`，或手动 `sh /lib/modules/7.0.0-rc7-z1+/z1_modprobe_connsys.sh`。

### 4.5 部署变更清单（未来执行，本次未改）

| 动作 | 对象 |
|---|---|
| boot ramdisk 瘦身 | 用 `tools/repack_ramdisk_connsys.sh` 流程（去掉步骤 3/3.5/BT/iw 注入），或新建 `repack_ramdisk_slim.sh`；/init 换 switch_root 版 |
| 同步 dts initrd-end | `linux,initrd-end = 0x84100000 + body_size` 重编 dtb（repack 已自动做） |
| 移模块到 Alpine | `/lib/modules/7.0.0-rc7-z1+/` 12 .ko + launcher + 5 BT 工具 + iw + 脚本 |
| 建固件目录 | `/lib/firmware`、`/system/etc/firmware`、`/etc/firmware/nvram`（改 launcher `-p` 或建目录二选一） |
| 验 rootfs | fstab `/dev/block` 软链或改镜像 + inittab 补 `ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100`（见 ALPINE_BOOT_PATH_20260815 §3.4） |
| 刷写 | `tools/flash_partition.sh android out/system_alpine.img` + `tools/flash_with_retry.sh bootimg ...`；回滚保留 `1/boot.img` |

---

## 5. 结论

1. **驱动拆分不需要动 .config**——boot 必要驱动已全部 =y，非必要 WiFi/BT 已是模块形态。唯一要改的是 **boot.img 的 ramdisk 内容**：把 CONNSYS 12 .ko + launcher + 5 BT 工具 + iw + 固件（共 6.47 MB 解压 / 1.30 MB 压缩）从 boot ramdisk 移到 Alpine rootfs。
2. **boot.img 实测瘦身**：含 BT 栈版 5.73 MiB → 4.50 MiB（省 1.24 MiB）；当前上板版 5.39 MiB → 4.50 MiB（省 0.89 MiB）。6 MiB 分区富余从 0.61 MiB 涨到 **1.50 MiB**，给未来加回 GPU/DRM 或改 configfs USB 留足预算。
3. **vermagic 精确匹配**（全部 `7.0.0-rc7-z1+`）+ 无模块签名门禁，Alpine 下 insmod 无额外障碍；加载顺序/脚本已就绪（`scripts/z1_modprobe_connsys.sh`）。
4. 两个部署时易漏的坑: ① launcher 的 `/system/etc/firmware/` 绝对路径（Alpine 无 /system）；② Alpine fstab `/dev/block` 前缀 + inittab 补 ttyS0 getty。
