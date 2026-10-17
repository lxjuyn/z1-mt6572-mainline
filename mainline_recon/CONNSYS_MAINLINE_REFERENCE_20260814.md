# MT6572 CONNSYS (WiFi/BT) mainline 参考调研报告

- 日期: 2026-08-14
- 调研方式: 联网搜索 (tavily + firecrawl) + 只读代码/文档分析。板子关机，未做任何硬件操作。
- 背景: Z1 用 MT6572 集成 CONNSYS（WiFi/BT 合一）。已移植 `mtk_btif_drv` / `mtk_stp_wmt_soc` / `mtk_stp_bt_soc` / `mtk_wmt_wifi_soc` / `wlan_gen2` 模块，insmod OK；固件 `WIFI_RAM_CODE_MT6582` + `WMT_SOC.cfg` + patch 已入 ramdisk。上次 `echo 1 > /dev/wmtWifi` func-on 因 **BTIF DMA Oops + EMI mapping fail** 失败，已修（btif ranges + EMI base 0xbe080000），未上板复测。
- 本报告回答 5 个问题，全部附 URL。

---

## 0. TL;DR（先看这里）

1. **没有现成的 "MT6572 CONNSYS mainline 跑通" 先例**。mt6572-mainline 上游 fork 的 README 明确写 "Connectivity: not existent in the upstream"——CONNSYS 移植是 Z1 自己的活。最接近的实战参考是 **BPI-R2 (MT7623) 论坛实测** 和 **postmarketOS Wiki（MT6589 / MT6577 两节）**，它们给出同一套 `mtk_stp_wmt_*` + `wlan_*` 栈的 func-on 时序。
2. **func-on 标准时序**（Android/postmarketOS 通用）：
   - 先 insmod `mtk_stp_wmt(_soc).ko` → `mtk_stp_bt(_soc).ko` → `mtk_wmt_wifi(_soc).ko` → `wlan_*.ko`；
   - `mknod /dev/wmtWifi c 153 0`（+ `/dev/stpwmt c 190 0`）；
   - 跑 launcher（CONNSYS 时代是 `wmt_loader` / `6620_launcher -p <fw_dir>`，**UART combo 时代**才是 `stp_uart_launcher -p <fw_dir> [-d /dev/ttyMT2]`）；
   - **最后** `echo 1 > /dev/wmtWifi`（STA）；`echo A > /dev/wmtWifi`（AP）；`echo 0` 关。
   - firmware 加载顺序：**先 ROM patch（`*_hdr.bin`，ALPS magic at +0x0C），后 WiFi RAM code（`WIFI_RAM_CODE_MT6582`，MTKE magic at +0x00）**。
3. **固件文件名（MT6572/MT6582 CONNSYS 实测）**：Wiko MT6592 私有文件清单里出现
   `/system/etc/firmware/mt6572_82_patch_e1_0_hdr.bin`、`mt6572_82_patch_e1_1_hdr.bin`、`WIFI_RAM_CODE_MT6582`、`WMT_SOC.cfg`。**Z1 的 patch 若叫 `ROMv1_patch_*` 也不矛盾**——BPI-R2 日志显示 launcher 会按 "前缀 + 版本 + _hdr.bin" 拼文件名（如 `ROMv2_lm_patch_1_0_hdr.bin`）。
4. **WMT_SOC.cfg 内容很简单**：就是几行 key=value 天线/coex 板级配置（实测例子只有 4 行：`coex_wmt_ant_mode=1`、`wmt_gps_lna_pin=0`、`wmt_gps_lna_enable=0`、`co_clock_flag=0`）。它不是固件路径配置；固件路径是 launcher 的 `-p` 参数/驱动里的路径宏。
5. **NVRAM**：`/data/nvram/APCFG/APRDEB/WIFI` 是 wlan 驱动直接读的二进制记录文件，MAC 在第 4 字节起 6 字节（mtkclient 写回逻辑），文件尾 2 字节是 checksum。mainline 部署三种办法：建 `/data` 放文件 / 用 `CONFIG_MTK_NVRAM_PATH` 类 config 改路径（postmarketOS 做法）/ 驱动里改 `BIN_FILE_PATH` 宏。
6. **ST7785M**：mainline 没有任何 ST7785M 驱动，`sitronix-st7789v.c` **不覆盖 ST7785M**（不同控制器）。MT6572 mainline DRM 是 DSI 路线（mt2701 兼容数据），DBI/DPI 是 DEAD。Z1 屏若走 DSI 要新写 panel 兼容项；若走 DBI/SPI 则 MT6572 主线无控制器驱动。
7. **复测建议重点**：BTIF base 参考值 MT6735/6797 = `0x1100C000`（MT6572 infrasys 区同域）；用 `/proc/driver/wmt_dbg`（`echo 0xDB9DB9`、`echo "7 9 1"`）开 WMT 详细日志；确认固件/NVRAM 路径宏；先单独验 BTIF（不联 wifi）再 func-on。

---

## 1. MT6572 CONNSYS WiFi/BT 在 mainline/开源上的参考

### 1.1 结论性事实
- **mt6572-mainline 上游 fork 明确没有 CONNSYS**。其 README 平台状态表 "Connectivity: not existent in the upstream"（指没编进内核/没在主线），BT 状态 postmarketOS MT6572 页标 "Untested"。
  - https://github.com/mt6572-mainline/linux （README 平台状态表）
  - https://github.com/mt6572-mainline （org，还有 u-boot-pub / da-boot / buildroot / kernel-compat）
  - https://wiki.postmarketos.org/wiki/MediaTek_MT6572 （Mainline: yes；I²C: Works；Bluetooth: Untested）
- **没有其他开源仓库把 MT6572 的 CONNSYS 在 mainline 上点亮**。可搜到的 MT6572 主线资料集中在 SoC 基础（CPU/timer/UART/I2C/eMMC/DRM），WiFi/BT 全部缺位。

### 1.2 最值得抄的实战参考（同栈、不同 SoC）
- **BPI-R2 (MT7623) + MT6625L / CONNSYS** —— 真实 func-on log、launcher 输出、patch 拼名规则，全部可对照：
  - https://forum.banana-pi.org/t/bpi-r2-internal-wifi-bt-mt6625l-kernel/4307
    - 关键 log：`wmt_loader`（get/set device id 30243=0x7623）→ `stp_uart_launcher -p /etc/firmware &` → "run SOC chip flow" → "create pwr on thread ok" → "enter power on connsys flow" → `patch name pre-fix:ROMv2_lm_patch` / `fwVersion:0x8a00` / `/etc/firmware/ROMv2_lm_patch_1_0_hdr.bin`。
    - "**BTIF is needed for wifi and bt**"——BTIF 同时承载 WiFi 和 BT 数据，BTIF 挂 = WiFi 也起不来（正好对应 Z1 的 BTIF Oops 卡 WiFi func-on）。
  - https://forum.banana-pi.org/t/porting-openwrt-kernel-5-4-for-bpi-r2-status-report/10841 —— `wmt_loader` + `stp_uart_launcher` 要先于 bluetooth-init 调用；`mtk_wcn_wmt_msgcb_reg` / `stp_process_packet` 空回调等常见错。
- **postmarketOS Wiki WiFi 页（Mediatek 节）** —— 权威时序文档：
  - https://wiki.postmarketos.org/wiki/WiFi
    - **MT6589（Fairphone 1，CONNSYS 代）**：insmod `mtk_stp_wmt.ko / mtk_stp_uart.ko / mtk_stp_gps.ko / mtk_stp_bt.ko / mtk_wmt_wifi.ko` → `mknod /dev/stpwmt c 190 0`、`/dev/wmtWifi c 153 0` → `insmod wlan_mt6628.ko` → `6620_launcher -p /system/etc/firmware/` → `echo 1 > /dev/wmtWifi`。
    - **MT6577 (MT8317) + MT6628 combochip**：`CONFIG_MTK_WMT_FW_PATH="/lib/firmware/postmarketos/"` + `CONFIG_MTK_NVRAM_PATH=...` 改路径；`6620_launcher -m 1 -b 4000000 -p /lib/firmware/postmarketos/ -d /dev/ttyMT2 &`；然后 `echo -n '1' > /dev/wmtWifi`。也给出 launcher 源码出处：MediaDeb 的 `stp_uart_launcher.c`。
- **openwrt/openmptcprouter `wmt.init`** —— MTK 路由器侧完整的 func-on systemd/init 脚本（wmt_loader → stp_uart_launcher -p → sleep 3 → 检查 /dev/wmtWifi → echo 1/A）：
  - https://github.com/Ysurac/openmptcprouter/blob/develop/common/package/utils/wmt/files/wmt.init
  - https://github.com/Ysurac/openmptcprouter/blob/develop/common/package/utils/wmt/Makefile （`WMT_SOC.cfg` 安装到 `system/etc/firmware/`，同名拷成 `WMT.cfg`）
- **Gemini PDA (MT6797) 开源主线项目** —— 文档把 CONNSYS 固件格式/内容讲得很清楚（ROM patch = ALPS magic，WiFi RAM code = MTKE magic，WMT_SOC.cfg = plaintext board config），还写了 "WMT Firmware-Push Protocol"：
  - https://github.com/bsg100/gemini-linux/blob/main/blockers.md
- **MT6589 mainline (TeamYogaBlade2 / Lenovo YOGA)** —— 同代 CONNSYS，还没亮 WiFi，但可跟进：
  - https://github.com/TeamYogaBlade2 （README 里直接挂了 `https://github.com/mt6572-mainline`、`https://github.com/Swer15/mt6582-mainline`、`https://github.com/mediatek-mainline`）
  - https://github.com/Swer15/mt6582-mainline
- **MT6577 mainline（arzam16）** —— 老 MTK 主线化、bootimg/ROOTFS 头处理、ATAG 处理，作为"旧 LK 跳主线内核"的对照（Z1 同类坑）：
  - https://github.com/arzam16/linux-mt6577
  - https://github.com/arzam16/empty-sixty-five （MT65xx 主线笔记；含 MT6572 CPU 频率/寄存器基址定位方法、`IO_VIRT_TO_PHYS` 说明；搜寄存器用 `mediatek/platform/mt65xx/.../mt_reg_base.h` + `mt_clock_manager.h`）

### 1.3 可当"参考实现"的驱动源码（非 mainline，但同栈）
- Android Googlesource `kernel/mediatek`（3.18 分支）`drivers/misc/mediatek/connectivity/` 全套组合/SOC 驱动源码（gl_init.c 等）：
  - https://android.googlesource.com/kernel/mediatek/+/android-mtk-3.18/drivers/misc/mediatek/connectivity/
- launcher 源码：
  - https://github.com/MediaDeb/android_hardware_mediatek/blob/master/combo_tool/src/stp_uart_launcher.c
  - https://github.com/Envcc/MediaTek-MTK-UART-enable （含 stp_uart_launcher.c 的 diff，默认宏：`CUST_COMBO_WMT_DEV "/dev/stpwmt"`、`CUST_COMBO_STP_DEV "/dev/ttyMT2"`、`CUST_COMBO_PATCH_PATH "/vendor/firmware"`、`CUST_COMBO_CFG_FILE "/system/vendor/firmware/WMT.cfg"`）
- openwrt LEDE 里 Ralink/MTK 驱动对 NVRAM 文件的读法（`BIN_FILE_PATH "/data/nvram/APCFG/APRDEB/WIFI"`）：
  - https://github.com/coolsnowwolf/lede/blob/master/package/lean/mt/drivers/mt7615d/src/mt_wifi/embedded/include/eeprom.h

---

## 2. WiFi func-on 正确时序、launcher 参数、WMT_SOC.cfg、patch 命名

### 2.1 标准时序（三份来源互相印证）
`postmarketOS Wiki` + `BPI-R2 论坛` + `openmptcprouter wmt.init`：

```
# 1) 模块（CONNSYS 用 *_soc 变体，即 Z1 的 mtk_stp_wmt_soc 等）
insmod mtk_stp_wmt_soc.ko
insmod mtk_stp_bt_soc.ko
insmod mtk_wmt_wifi_soc.ko
insmod wlan_gen2.ko            # 或 wlan_mt*.ko

# 2) 设备节点
mknod /dev/stpwmt  c 190 0
mknod /dev/wmtWifi c 153 0

# 3) launcher（CONNSYS/SOC 时代；UART combo 才带 -d /dev/ttyMT2）
wmt_loader                          # 或 6620_launcher -p <fw_dir>
stp_uart_launcher -p <fw_dir> &     # 必须常驻后台（UART 路径）

# 4) func-on（这几秒会看到 launcher/fw 下载日志）
echo 1 > /dev/wmtWifi      # STA
# echo A > /dev/wmtWifi   # AP 模式
# echo 0 > /dev/wmtWifi   # 关
```

要点：
- **launcher 必须一直跑着**（`&` 后台），func-on 时它负责往 CONNSYS 传固件。
- `echo 1` 后等待数秒，dmesg 会出现 WMT 固件下载/初始化日志，之后 `wlan0`/`ap0` 出现。
- 在 Z1 的 mainline 移植里，模块是 insmod 的、固件已进 ramdisk，**func-on 应由内核侧 WMT 驱动完成固件下载**（不再需要用户态 launcher），所以 BTIF DMA 是关键路径。

### 2.2 launcher 参数（实测）
| 参数 | 含义 | 实测值 |
|---|---|---|
| `-p <path>` | 固件/patch 目录 | `-p /etc/firmware` / `-p /system/etc/firmware/` / `-p /lib/firmware/postmarketos/` |
| `-m <mode>` | 模式（combo 用） | `-m 1` |
| `-b <baud>` | UART 波特率 | `-b 4000000` |
| `-d <dev>` | STP UART 设备 | `-d /dev/ttyMT2`（仅外部 combo；CONNSYS 不需要） |
| `-c <cfg>` | 覆盖 WMT.cfg 路径 | 默认 `CUST_COMBO_CFG_FILE` |

源码默认宏出处：Envcc/MediaTek-MTK-UART-enable 的 `stp_uart_launcher.c`。

### 2.3 WMT_SOC.cfg 内容
实测（LineageOS mt6735-common 的 `configs/WMT_SOC.cfg` 全文仅 4 行）：
```
coex_wmt_ant_mode=1
wmt_gps_lna_pin=0
wmt_gps_lna_enable=0
co_clock_flag=0
```
- 来源：https://raw.githubusercontent.com/LineageOS/android_device_cyanogen_mt6735-common/cm-14.1/configs/WMT_SOC.cfg
- Gemini 项目描述一致：**"WMT_SOC.cfg — plaintext board config (coex antenna mode, GPS LNA pin disabled, co_clock_flag=0); trivial, no parsing concerns"**（bsg100/gemini-linux blockers.md）。
- 结论：**Z1 已入 ramdisk 的 WMT_SOC.cfg 只要内容合法（key=value、无损坏）即可**，它不是 func-on 卡点的常见原因。它决定天线共存/GPS LNA 行为。

### 2.4 patch 文件命名（ROMv1_patch_* 实际用法）
- **拼名规则**（BPI-R2 实测 log）：launcher 打印 `patch name pre-fix:ROMv2_lm_patch`，然后按 `前缀 + <ver> + _hdr.bin` 拼出实际文件 `ROMv2_lm_patch_1_0_hdr.bin`。所以 MT6582/72 的 `ROMv1_patch_MT6582` 对应的实际文件可能是 `ROMv1_patch_MT6582_<ver>_hdr.bin`。
- **MT6572/82 CONNSYS 实测文件名**（Wiko MT6592 私有清单，同一代 CONNSYS 硅片）：
  `/system/etc/firmware/mt6572_82_patch_e1_0_hdr.bin`、`mt6572_82_patch_e1_1_hdr.bin`、`WIFI_RAM_CODE_MT6582`、`WMT_SOC.cfg`
  来源：https://github.com/thiz11/mediatek/blob/master/config/wiko/proprietary-files.txt
- **固件二进制格式**（Gemini 项目实测）：
  - ROM patch 文件：**`ALPS` magic at offset 0x0C**，多段 offset/length 表，带 build 时间戳。
  - WiFi RAM code（`WIFI_RAM_CODE_6797` / `WIFI_RAM_CODE_MT6582`）：**`MTKE` magic at offset 0x00**，WiFi RAM code 在 ROM patch 之后加载。
  - 来源：https://github.com/bsg100/gemini-linux/blob/main/blockers.md
- 验证建议：`xxd WIFI_RAM_CODE_MT6582 | head` 应见 `MTKE`；patch 文件 +0x0C 应见 `ALPS`。

---

## 3. MT6572 NVRAM 结构（/data/nvram/APCFG/APRDEB/WIFI）

### 3.1 位置与来源
- 路径：`/data/nvram/APCFG/APRDEB/WIFI`（老 Android）；新机在 `nvdata` 分区内镜像 `/mnt/vendor/nvdata/APCFG/APRDEB/WIFI`。
- NVRAM 整个分区（`nvram.img`）有 TOC：把路径字符串映射到偏移。mtkclient 的解析逻辑：
  - `wifi_offset = nvram.find(b'/mnt/vendor/nvdata/APCFG/APRDEB/WIFI')`；`wifi_size = unpack('V', nvram, wifi_offset - 4)[1]`；`wifi_offset = toc_size + unpack('V', nvram, wifi_offset - 8)[1]`（toc_size=0x20000）。
  - 来源：https://github.com/bkerler/mtkclient/blob/main/mtkclient/Library/mtk_crypto.py
- BT 的 `BT_Addr` 同目录（`.../APRDEB/BT_Addr`），MAC 在偏移 0（XDA 教程：BT_Addr 第一行 00-05 列写 MAC）。

### 3.2 WIFI 文件记录结构
综合 mtkclient 写回逻辑 + XDA/16rd 维修资料：
- **MAC 写在文件内偏移 4 起、6 字节**（mtkclient `wf.seek(wifi_offset + 4); wf.write(mac)`）。
- **文件最后 2 字节是 checksum**（`checksum_2b`，`(0xAA << 8) + (value & 0xFF)`，对"全文件减最后 2 字节"求和/异或）。
- 前面 4 字节区域含版本/LID 信息：MTK cgen 体系里该 LID 为 `AP_CFG_RDEB_FILE_WIFI_LID`，版本号宏 `AP_CFG_RDEB_FILE_WIFI_LID_VERNO`（升级时 +1 触发 ConvertFunc 保留 MAC）。
- 来源：
  - mtkclient `mtk_crypto.py`（写回 MAC@+4 + 2B checksum）
  - https://bbs.16rd.com/thread-60673-1-1.html （"升级WIFI Nvram，WIFI MAC 不丢失"：改 `AP_CFG_RDEB_FILE_WIFI_LID_VERNO`、`CFG_FILE_WIFI_REC_SIZE`/`REC_TOTAL`、`WIFI_ConvertFunc` 保留 MAC）
  - XDA NVRAM err 0x10 修复帖：https://xdaforums.com/t/solved-wifi-nvram-warning-err-0x10.2197378/ 、https://xdaforums.com/t/mtk-problem-nvram-wifi-nvram-warning-err-0x10-problem-fixed.3682079 （格式化了 /data 或换机导致 WIFI 文件丢失/损坏 → `NVRAM WARNING: ERR 0x10` → MAC 随机化）
- **驱动侧读取**：wlan 驱动直接 `open("/data/nvram/APCFG/APRDEB/WIFI")` 读整文件当 eeprom/校准源（Ralink/MTK 驱动 `#define BIN_FILE_PATH "/data/nvram/APCFG/APRDEB/WIFI"`，见 coolsnnowwolf/lede eeprom.h）。文件缺失/长度不符 → 校验失败 → MAC 随机。

### 3.3 mainline 部署方式
三种（按成熟度排）：
1. **在 rootfs/ramdisk 造 `/data` 目录**，把原厂 `WIFI`（含 MAC）原样放 `/data/nvram/APCFG/APRDEB/WIFI`。驱动不改路径即可读到。最简单。
2. **驱动加 `CONFIG_MTK_WMT_FW_PATH` / `CONFIG_MTK_NVRAM_PATH` 类 kconfig**（postmarketOS 对 MT6577 下游内核的做法），把固件+NVRAM 指到 `/lib/firmware/postmarketos/`。参考 https://wiki.postmarketos.org/wiki/WiFi （MT6577 节）及 arzam16 的 kernel 提交（`CONFIG_MTK_WMT_FW_PATH`、`CONFIG_MTK_NVRAM_PATH`）。
3. **改驱动内硬编码路径宏**（如 `BIN_FILE_PATH`）为 ramdisk 里真实存在的路径。
- 注意：Z1 的 wlan_gen2 是 MTK 闭源风格驱动，NVRAM 读取位置要看具体实现（可能走 `filp_open` 直接读文件，也可能走 Android NVRAM API——后者 mainline 必须绕开，改为直接读文件）。

---

## 4. ST7785 系列 panel (ST7785M) 在 mainline DRM 的情况

### 4.1 ST7785M 是什么
- Sitronix ST7785M：**240RGBx320、262K 色、a-Si TFT 单芯片驱动 IC**（小尺寸彩屏驱动）。
- 来源：https://bbs.16rd.com/forum.php?mod=forumdisplay&fid=663 （"ST7785M datasheet ... 240RGBx320 Resolution and 262K color"）
- 注意：ST7785M ≠ ST7789V ≠ ST7735R。ST7789V 是 240x320、MIPI-DBI/SPI；ST7785M 是更早的一代，寄存器/初始化序列不同。

### 4.2 mainline 有没有驱动
- **没有 ST7785M 驱动**。代码搜索（grep.app）`ST7785` 只有 FPGA/Verilog 工程（ModRetro chromatic_fpga），没有 Linux 内核驱动。
- `drivers/gpu/drm/panel/panel-sitronix-st7789v.c` 只认 `sitronix,st7789v` 兼容串，**不覆盖 ST7785M**，且它是 **9-bit SPI / MIPI-DBI 面板驱动**（不是 SoC DSI 侧）。
- 相关驱动清单：
  - `drivers/gpu/drm/panel/panel-sitronix-st7789v.c`（ST7789V，9-bit SPI）：https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/panel/panel-sitronix-st7789v.c
  - `drivers/staging/fbtft/fb_st7789v.c`（ST7789V fbdev，staging）：`modinfo fb_st7789v` → `alias spi:st7789v`
  - 新 mipi-dbi 版 `drivers/gpu/drm/sitronix/st7789v.c`（2026 初入上游，支持多款 240x320 面板，8-bit SPI+D/C GPIO）：https://lwn.net/Articles/1059005
- 结论：**ST7785M 要么自己写 panel 驱动/兼容项，要么确认 Z1 屏实际控制器是 ST7789V 兼容**（很多小厂模组丝印 ST7785 实际是 ST7789 兼容或反之——建议读屏 ID / 对照原厂 LCD 驱动初始化序列）。

### 4.3 MT6572 mainline DRM 现状（mt6572-mainline fork 实测）
- DRM：🟡 partial，`mediatek,mt6572-mmsys`，目前 fallback 到 **mt2701 plat data**（需自己数据，只有 1 个 rdma）。
- **DSI**：`mediatek,mt6572-dsi` 🟡 partial（需 upstreaming）；**DSI PHY** `mediatek,mt2701-mipi-tx` 🟢 OK。
- **DBI**：🔴 DEAD（"no known device with DBI display"）；**DPI**：🔴 DEAD。
- 来源：https://github.com/mt6572-mainline/linux （README 平台状态表）
- 所以 MT6572 主线的屏路线是 **DSI**；SPI/DBI/并行口控制器在主线上没有。

### 4.4 对 Z1 的建议
1. 先确认 Z1 屏接的是 **MT6572 的 DSI**（MT6572 手机上 LCM 走 DSI 是主流；eevblog 帖子也确认 3G-IoT 的 MIPI 屏：https://www.eevblog.com/forum/embedded-computing/orange-pi-3g-iot-a-need-connect-a-cheap-screen-via-mipi/ ）。
2. 若 DSI：在 `drivers/gpu/drm/panel/` 加一个面板驱动（或复用 sitronix-st7789v 风格，换成 ST7785M 的 init 序列 + 新 compatible），挂在 `mediatek,mt6572-dsi` 下；沿用 mt2701 兼容数据把 DRM 点亮。
3. 若屏其实是 DBI/SPI 命令模式（ST7785 系原生支持）：MT6572 主线 DBI 控制器是 DEAD，只能自己写（或走 tinydrm/mipi-dbi 但缺 SoC 侧控制器驱动），成本高。
4. 别指望 `sitronix-st7789v.c` 直接覆盖 ST7785M——寄存器不同。

---

## 5. 对我们 Z1 当前 BTIF/EMI 修复后 func-on 复测的额外建议

### 5.1 BTIF / EMI 修复的核对参考
- **BTIF base 参考值**：MT6735 / MT6797 的 `BTIF_BASE = 0x1100C000`（LK `mt_reg_base.h`）：
  - https://github.com/littlekernel/lk/blob/master/platform/mediatek/mt6735/include/platform/mt_reg_base.h
  - https://github.com/littlekernel/lk/blob/master/platform/mediatek/mt6797/include/platform/mt_reg_base.h
- MT6572 的 infrasys 外设区在 `0x11000000` 域，BTIF 应在附近；**用 `0x1100C000` 对照 Z1 源码里的 BTIF base**（3.4.67 的 `mt_reg_base.h`，或 drsami/mt6572_kernel：https://github.com/drsami/mt6572_kernel ）。
- **EMI base 0xbe080000**：这是虚拟/IO 映射地址（不是寄存器物理地址）。MTK 3.4 内核 IO 映射用 `IO_VIRT_TO_PHYS`（如 `0xC0000000 | (v & 0x0fffffff)`），`0xbe080000` 需确认在 mainline 里对应的 ioremap 正确、且 dma-ranges/`device_node` 的 `dma_coherent` 配好。BTIF DMA Oops 的典型根因就是 **DMA 描述符/数据缓冲被映射到错误地址**（EMI 窗口没开或基址错）。
- 核对方法参考：arzam16/empty-sixty-five 的"搜索寄存器地址"章节（`mt_reg_base.h` + `mt_clock_manager.h`，`IO_VIRT_TO_PHYS` 换算）：https://github.com/arzam16/empty-sixty-five

### 5.2 复测清单（按顺序）
1. 先确认 5 个模块 insmod 后 `/dev/stpwmt`、`/dev/wmtWifi`、`/proc/driver/wmt_dbg`、`/proc/wmt` 等节点存在。
2. **单独验证 BTIF**（不触发 wifi）：读 `/sys/kernel/debug/btif/*` 或直接触发一次 BTIF 读写，确认不再 Oops（上板后先跑这一步，确认 BTIF DMA 通，再 func-on）。
3. 确认固件路径宏指向 ramdisk 里真实的 `WIFI_RAM_CODE_MT6582` + patch（若驱动有 `CONFIG_MTK_WMT_FW_PATH` 之类就用它，否则改硬编码路径宏）。文件头校验：`WIFI_RAM_CODE_MT6582` → `MTKE`；patch `*_hdr.bin` +0x0C → `ALPS`。
4. 确认 NVRAM：把原厂 `/data/nvram/APCFG/APRDEB/WIFI`（含真实 MAC）放到 ramdisk 的 `/data/nvram/APCFG/APRDEB/WIFI`；或设 `CONFIG_MTK_NVRAM_PATH`。否则 MAC 随机 / `NVRAM WARNING: ERR 0x10`。
5. `echo 1 > /dev/wmtWifi`，立即抓 dmesg（重点看 BTIF DMA、`[WMT]`、patch 下载、`chip id`、RAM code 加载日志）。成功后 `ip link` 应出现 `wlan0`，`iw dev` / `iwlist wlan0 scan` 验证。

### 5.3 调试手段（来自 KOBO/koreader 的真实用法）
- 启用 WMT 调试命令：`echo "0xDB9DB9" >/proc/driver/wmt_dbg`
- 关 LPBK（loopback 测试）：`echo "7 9 0" >/proc/driver/wmt_dbg`；开 LPBK（会阻塞 ~1.3s）：`echo "7 9 1" >/proc/driver/wmt_dbg`
- 然后 `echo 1 >/dev/wmtWifi`。
- 来源：https://github.com/koreader/koreader/blob/master/platform/kobo/enable-wifi.sh （注释直接引用 `wmt_dbg_func_ctrl @ modules/connectivity/wmt_mt66xx/common_main/linux/wmt_dbg.c`）
- 这套在 MTK CONNSYS 栈（wlan_gen4m / wmt_mt66xx）里被验证有效；Z1 的 `wlan_gen2`/wmt_soc 栈若有同款 `/proc/driver/wmt_dbg` 就能用。

### 5.4 已知坑（避免重复踩）
- **BTIF 同时服务 WiFi 和 BT**：BTIF 修不好，BT 也不可能好。先 BTIF 后 func-on。
- **`/dev/wmtWifi` 没出现** = `mtk_wmt_wifi(_soc).ko` 没 probe 或 chrdev 没注册；检查 `mknod` 主次号（postmarketOS 用 `c 153 0`）。
- **func-on 无任何日志** = 固件路径错（找不到 `WIFI_RAM_CODE_MT6582` / patch）或 WMT 层还没起来；不是 BTIF 问题。
- **MAC 随机 / `NVRAM WARNING ERR 0x10`** = NVRAM 文件缺失或路径不对（见 §3.3）。
- **patch 版本不匹配**：`fwVersion` 与 `WIFI_RAM_CODE` 的版本/`hw_code`（0x6572）要对上；不同 3.4 编译的固件与 7.0 内核之间一般固件通用（同硅片），但 patch 前缀要按 Z1 实际 ramdisk 文件核对（`mt6572_82_patch_e1_*` 或 `ROMv1_patch_MT6582_*`）。

---

## 6. 完整 URL 清单（按用途）

| 用途 | URL |
|---|---|
| MT6572 主线 fork（无 CONNSYS） | https://github.com/mt6572-mainline/linux ；https://github.com/mt6572-mainline |
| postmarketOS MT6572 页 | https://wiki.postmarketos.org/wiki/MediaTek_MT6572 |
| postmarketOS WiFi 页（Mediatek 节，权威时序） | https://wiki.postmarketos.org/wiki/WiFi |
| BPI-R2 MT7623 CONNSYS 实测 | https://forum.banana-pi.org/t/bpi-r2-internal-wifi-bt-mt6625l-kernel/4307 |
| BPI-R2 OpenWrt 移植帖 | https://forum.banana-pi.org/t/porting-openwrt-kernel-5-4-for-bpi-r2-status-report/10841 |
| openwrt wmt.init（完整 func-on 脚本） | https://github.com/Ysurac/openmptcprouter/blob/develop/common/package/utils/wmt/files/wmt.init |
| launcher 源码（MediaDeb） | https://github.com/MediaDeb/android_hardware_mediatek/blob/master/combo_tool/src/stp_uart_launcher.c |
| launcher 默认宏（Envcc） | https://github.com/Envcc/MediaTek-MTK-UART-enable |
| MT6572/82 CONNSYS 固件文件名（Wiko 清单） | https://github.com/thiz11/mediatek/blob/master/config/wiko/proprietary-files.txt |
| WMT_SOC.cfg 内容实测 | https://raw.githubusercontent.com/LineageOS/android_device_cyanogen_mt6735-common/cm-14.1/configs/WMT_SOC.cfg |
| CONNSYS 固件格式（ALPS/MTKE/WMT_SOC.cfg 语义） | https://github.com/bsg100/gemini-linux/blob/main/blockers.md |
| Android mediatek kernel 驱动源码 | https://android.googlesource.com/kernel/mediatek/+/android-mtk-3.18/drivers/misc/mediatek/connectivity/ |
| NVRAM WIFI 文件解析（mtkclient） | https://github.com/bkerler/mtkclient/blob/main/mtkclient/Library/mtk_crypto.py |
| 驱动读 NVRAM 的 BIN_FILE_PATH | https://github.com/coolsnowwolf/lede/blob/master/package/lean/mt/drivers/mt7615d/src/mt_wifi/embedded/include/eeprom.h |
| WIFI NVRAM 升级/保留 MAC（16rd） | https://bbs.16rd.com/thread-60673-1-1.html |
| NVRAM err 0x10 修复 | https://xdaforums.com/t/solved-wifi-nvram-warning-err-0x10.2197378/ ；https://xdaforums.com/t/mtk-problem-nvram-wifi-nvram-warning-err-0x10-problem-fixed.3682079 |
| BT MAC 修改法（XDA） | https://xdaforums.com/t/guide-to-solve-mtk-no-imei-no-baseband-no-backup.2695909 |
| wmt_dbg 调试命令（koreader） | https://github.com/koreader/koreader/blob/master/platform/kobo/enable-wifi.sh |
| BTIF base 参考（MT6735/6797=0x1100C000） | https://github.com/littlekernel/lk/blob/master/platform/mediatek/mt6735/include/platform/mt_reg_base.h ；https://github.com/littlekernel/lk/blob/master/platform/mediatek/mt6797/include/platform/mt_reg_base.h |
| MT65xx 主线笔记（寄存器定位/IO_VIRT_TO_PHYS） | https://github.com/arzam16/empty-sixty-five |
| MT6577 主线（bootimg/ROOTFS/ATAG） | https://github.com/arzam16/linux-mt6577 |
| MT6589 mainline（TeamYogaBlade2） | https://github.com/TeamYogaBlade2 ；https://github.com/Swer15/mt6582-mainline |
| MT6572 3.4 下游内核（drsami） | https://github.com/drsami/mt6572_kernel |
| ST7789V panel 驱动（主线上游） | https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/panel/panel-sitronix-st7789v.c |
| ST7789V mipi-dbi 新驱动（2026） | https://lwn.net/Articles/1059005 |
| ST7785M datasheet 提及 | https://bbs.16rd.com/forum.php?mod=forumdisplay&fid=663 |
| MT6572 MIPI 屏（eevblog） | https://www.eevblog.com/forum/embedded-computing/orange-pi-3g-iot-a-need-connect-a-cheap-screen-via-mipi/ |
| MT6572 U-Boot（kclinx） | https://github.com/kclinx/mediatek-mt6572-u-boot |
