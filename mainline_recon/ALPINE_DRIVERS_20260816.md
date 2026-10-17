# 非重要驱动部署到 Alpine — 完整方案 (Z1 MT6572 mainline)

> 只读分析, 2026-08-16。板子关机, 未改任何文件/源/镜像/固件, 仅产出本报告。
> 前置文档: `DRIVER_SPLIT_PLAN_20260815.md`(必要/非必要拆分 + 瘦身测算)、`ALPINE_BOOT_PATH_20260815.md`(p4 Alpine 启动方案 A)、`ALPINE_IMPLEMENT_20260816.md`(switch_root /init 已就绪)、`BT_ENABLE_PATH_20260814.md`、`WIFI_SCAN_SUCCESS_20260814.md`(上板已证 WiFi scan 8 AP)。
> 数据源(全部本地实测): `bridge_backups/products/products_connsys_modules_z1vermagic_20260814_015009/`、`bridge_backups/products/products_bt_stack_20260814_204345/`、`bridge_backups/products/products_iw_nl80211_20260814_144324/`、`out/firmware_backup/firmware/`、`mainline_recon/wifi-nvram-WIFI.bin`、`out/alpine_rootfs/`(WORK 目录, 与 system_alpine.img 内容一致)、`out/ramdisk_mainline_connsys.gz`(现上板版解包核对布局)、`fork_linux/.config`、`scripts/z1_modprobe_connsys.sh`、`scripts/z1_connsys_onboard_test.sh`。

---

## 0. 结论摘要 (TL;DR)

1. **必要/非必要划分确认** (沿用 DRIVER_SPLIT_PLAN §1/§2): boot 必要驱动(eMMC/rootfs/PMIC/时钟/console/WDT/按键13/显示/电池/USB gadget 恢复 console/RTC/HW RNG)当前全部 `=y` 且正确, **零 .config 改动**; 非必要 WiFi/BT 已是模块形态, 只是被塞在 boot ramdisk 里, 移到 Alpine 是"ramdisk 瘦身 + rootfs 部署"两个动作。
2. **部署主体 = 13 个 .ko**(6 CONNSYS + 7 BT 栈, **含 libaes.ko** — bluetooth.ko 依赖它; 任务描述"12 个"是 DRIVER_SPLIT_PLAN 的表漏了 libaes)+ `mtk_stp_launcher` + `iw` + 5 个 BT 用户态工具 + 5 处固件。**vermagic 全部 `7.0.0-rc7-z1+ SMP mod_unload ARMv7 p2v8`, 与运行内核精确匹配; `CONFIG_MODULE_SIG` 未开, `CONFIG_MODVERSIONS` 未开 → insmod 无签名/CRC 门禁**。
3. **Alpine 侧无兼容障碍**: .ko 是 Z1 自编 mainline 内核模块, 与 Alpine 3.24 自己的内核版本完全无关(Alpine 内核根本不会被加载, Z1 永远跑 `7.0.0-rc7-z1+` 内核)。Alpine 只提供 rootfs 用户态(busybox + OpenRC + 静态 ARM 工具全兼容)。**唯一注意: `z1_modprobe_connsys.sh` shebang 是 `#!/bin/bash`, 而 Alpine 没装 bash** — 脚本本体是纯 busybox/POSIX 语法(已 grep 无 bash 特性), 用 `sh 脚本` 调用即可, 或顺手 sed 成 `#!/bin/sh`。
4. **固件路径两处是内核/launcher 硬编码绝对路径**: ① `mtk_stp_wmt_soc.ko` 的 `wmt_conf.c` 编译进 `CUST_CFG_WMT_PREFIX="/system/etc/firmware/"` 读 `WMT_SOC.cfg`; ② `z1_modprobe_connsys.sh` 里 launcher 参数写死 `-p /system/etc/firmware/`。**Alpine 没有 `/system` 目录, 必须建 `/system/etc/firmware/`**(不能改路径, 除非重编 .ko 改宏——没必要)。另外 `wlan_gen2.ko` 用 filp_open 读 `/etc/firmware/nvram/WIFI`, `WIFI_RAM_CODE_MT6582` 走 request_firmware→`/lib/firmware`。
5. **开机自动加载推荐**: 自建 OpenRC 服务 `/etc/init.d/connsys` + `rc-update add connsys default`(比 local.d 更干净、可 stop、可看依赖); 备选 `/etc/local.d/connsys.start` + `rc-update add local default`(当前 default runlevel 只有 usb-acm, `local` 未启用, 两条路都要 rc-update 一次)。
6. **上板验证 4 步**: 刷 p4 android(system_alpine.img)→ 刷 bootimg(switch_root 版)→ 开机 OpenRC 日志看加载 → `iw dev wlan0 scan` + BT vhci 桥冒烟。参考基线: WiFi scan 8 AP 已在 ramdisk 版上板成功。

---

## 1. 必要/非必要驱动划分确认 (读 DRIVER_SPLIT_PLAN_20260815.md)

### 1.1 Boot 必要驱动(保持 =y, 不部署 Alpine)

| 子系统 | CONFIG_* | 为什么必须内核 |
|---|---|---|
| eMMC/存储 | `MMC=y` `MMC_BLOCK=y` `MMC_MTK=y` | rootfs p4 就挂在这; 模块化需两段 initramfs, 收益 0 |
| rootfs | `EXT4_FS=y` | p4 = ext4 Alpine |
| PMIC | `MTK_PMIC_WRAP=y` `MFD_MT6397=y` `REGULATOR_MT6323=y` | eMMC 稳压/各电源轨; 模块化 → rootfs 挂不上 |
| 时钟 | `COMMON_CLK_MEDIATEK=y` `COMMON_CLK_MT6572=y` | 全 SoC 时钟门控 |
| console | `SERIAL_8250=y` `SERIAL_8250_MT6577=y` `SERIAL_EARLYCON=y` | UART0 日志/登录/earlycon |
| WDT | `MEDIATEK_WATCHDOG=y` | LK 喂的狗早停+接管, 无空窗 |
| 按键 13 | `KEYBOARD_MT6779` `MTK_PMIC` `TOUCHSCREEN_GOODIX` `INPUT_MATRIXKMAP` | 用户策略"按键放 boot" |
| 显示 | `FB_SIMPLE` `FRAMEBUFFER_CONSOLE` `BACKLIGHT_*` | 用户策略"显示放 boot" |
| 电池 | `GENERIC_ADC_BATTERY` `MT6323_AUXADC` `IIO` | 用户策略"电池放 boot" |
| USB gadget | `USB_G_SERIAL` `USB_F_ACM` `MUSB_MEDIATEK` `PHY_MT6572_USB2_HANDOFF` | 现 /init 的恢复 console(ttyGS0); **建议暂留 =y**, Alpine 稳定后再移 |
| RTC / LED / HW RNG / GPIO | `RTC_DRV_MT6397` `LEDS_GPIO/PWM` `HW_RANDOM_MTK` `PINCTRL_MT6572` | 体积极小, 保持 =y 最简单 |

### 1.2 非必要驱动(部署 Alpine 后 insmod)

- **WiFi CONNSYS 6 .ko**: `cfg80211.ko`(内核 `CFG80211=m` 未内编, **必须随带**)、`mtk_btif_drv.ko`、`mtk_stp_wmt_soc.ko`、`mtk_stp_bt_soc.ko`、`mtk_wmt_wifi_soc.ko`、`wlan_gen2.ko`
- **BT 栈 7 .ko**: `bluetooth.ko`、`ecc.ko`、`ecdh_generic.ko`、`libaes.ko`(bluetooth 依赖, **DRIVER_SPLIT_PLAN 表漏列**)、`hci_vhci.ko`、`hidp.ko`、`uhid.ko`
- **用户态**: `mtk_stp_launcher`(WMT patch 应答, func-on 必需)、`iw`(nl80211 scan)、BT 工具 5 个(`stpbt-vhci-bridge` 必需 / `stpbt-hci-test` / `hci-localver` / `btup-scan` / `btif-lpbk-test`)
- **固件 5 处**: `/lib/firmware/WIFI_RAM_CODE_MT6582`、`/system/etc/firmware/{WMT_SOC.cfg, ROMv1_patch_0_hdr.bin, ROMv1_patch_1_hdr.bin}`、`/etc/firmware/nvram/WIFI`

### 1.3 上板已证基线

- `WIFI_SCAN_SUCCESS_20260814.md`: ramdisk 版加载后 `iw dev wlan0 scan` 发现 8 个真实 AP(SSID/信道/信号全有)。WiFi 硬件栈完整可用。
- BT 栈(bluetooth/ecc/ecdh/libaes/vhci/hidp/uhid + 5 工具)已编译完成归档, 依赖顺序已在 BT_STACK_BUILT 文档确认,**尚未上板验证**(vhci 桥路线是 PAP5500 参考验证过的接法)。
- 已知小问题: `dev_addr_check` WARNING(wlan0 up 时 dev_addr 全 0), 不影响 scan/连接。

---

## 2. 部署目标 — Alpine rootfs 文件布局

> 目标 rootfs: `out/alpine_rootfs/`(WORK 目录) → 打包 `out/system_alpine.img`(850MiB ext4) → 刷 p4(android 分区)。当前 `out/alpine_rootfs/lib/modules/` **不存在**(需建), `/lib/firmware` 存在但为空, `/etc/firmware` 不存在, `/system` 不存在。

### 2.1 模块目录 `/lib/modules/7.0.0-rc7-z1+/`(全部平铺)

| 文件 | 大小 (B) | 来源 | modinfo depends |
|---|---|---|---|
| cfg80211.ko | 491,872 | products_connsys_modules_.../cfg80211.ko | — |
| mtk_btif_drv.ko | 113,984 | .../btif/mtk_btif_drv.ko | — |
| mtk_stp_wmt_soc.ko | 369,696 | .../conn_soc/mtk_stp_wmt_soc.ko | mtk_btif_drv |
| mtk_stp_bt_soc.ko | 12,616 | .../conn_soc/mtk_stp_bt_soc.ko | mtk_stp_wmt_soc |
| mtk_wmt_wifi_soc.ko | 14,360 | .../conn_soc/mtk_wmt_wifi_soc.ko | mtk_stp_wmt_soc |
| wlan_gen2.ko | 958,904 | .../wlan/wlan_gen2.ko | cfg80211,mtk_stp_wmt_soc,mtk_wmt_wifi_soc |
| bluetooth.ko | 641,676 | products_bt_stack_.../ko/bluetooth.ko | ecdh_generic,libaes |
| ecc.ko | 36,876 | .../ko/ecc.ko | — |
| ecdh_generic.ko | 9,304 | .../ko/ecdh_generic.ko | ecc |
| libaes.ko | 11,656 | .../ko/libaes.ko | — |
| hci_vhci.ko | 14,860 | .../ko/hci_vhci.ko | —(符号来自 bluetooth, 须后 insmod) |
| hidp.ko | 26,468 | .../ko/hidp.ko | bluetooth |
| uhid.ko | 13,268 | .../ko/uhid.ko | — |
| mtk_stp_launcher | 522,212 | products_connsys_modules_.../mtk_stp_launcher | (用户态) |
| z1_modprobe_connsys.sh | 5,512 | scripts/z1_modprobe_connsys.sh | (脚本) |

**模块小计**: 13 .ko = 2,719,336 B; +launcher+脚本 ≈ 3,247,060 B。

### 2.2 工具目录

| 文件 | 大小 (B) | 部署位置 | 说明 |
|---|---|---|---|
| iw | 764,400 | `/usr/local/bin/iw`(或 /usr/bin) | nl80211 静态 ARM, scan 必需; wlan_gen2 只支持 nl80211 |
| stpbt-vhci-bridge | 453,320 | `/usr/local/bin/`(或 /root/connsys/bt/tools) | BT hci0 必需(/dev/stpbt ↔ /dev/vhci 泵) |
| stpbt-hci-test | 448,296 | 同上 | HCI_Reset 冒烟 |
| hci-localver | 448,652 | 同上 | HCI Read Local Version |
| btup-scan | 448,460 | 同上 | 裸 HCI inquiry |
| btif-lpbk-test | 448,932 | 同上 | BTIF 块 loopback |

### 2.3 固件(5 处, 全部必须)

| Alpine 路径 | 源文件 | 大小 (B) | 谁读 | 读法 |
|---|---|---|---|---|
| `/lib/firmware/WIFI_RAM_CODE_MT6582` | out/firmware_backup/firmware/WIFI_RAM_CODE_SOC | 160,464 | wlan_gen2 | request_firmware → `/lib/firmware`(CONFIG_FW_LOADER=y 已核) |
| `/system/etc/firmware/WMT_SOC.cfg` | out/firmware_backup/firmware/WMT_SOC.cfg | 80 | mtk_stp_wmt_soc | **filp_open 绝对路径**(wmt_conf.c:462 拼接 `CUST_CFG_WMT_PREFIX`, 缺则 insmod 失败, 上板实测过) |
| `/system/etc/firmware/ROMv1_patch_0_hdr.bin` | out/firmware_backup/firmware/mt6572_82_patch_e1_0_hdr.bin | 37,360 | mtk_stp_launcher | launcher `-p /system/etc/firmware/` 参数(脚本写死) |
| `/system/etc/firmware/ROMv1_patch_1_hdr.bin` | out/firmware_backup/firmware/mt6572_82_patch_e1_1_hdr.bin | 20,488 | mtk_stp_launcher | 同上 |
| `/etc/firmware/nvram/WIFI` | mainline_recon/wifi-nvram-WIFI.bin | 512 | wlan_gen2 | filp_open(含 MAC <MAC>) |

**固件小计**: 218,904 B。
**部署体积合计(解压后)**: ≈ 6.47 MB(与 DRIVER_SPLIT_PLAN §3.2 移出的 6,469,436 B 一致, 因为现 boot ramdisk 正是这套内容)。

> 可选:`/etc/firmware/wifi.cfg`(wlan_gen2 的 CFG_FILE, strings 里 `/data/misc/wifi.cfg`→`/etc/firmware/wifi.cfg` 兜底链)。现 ramdisk 没放也 scan 成功, 非必须; 缺则用默认配置。

---

## 3. 开机自动加载方案

### 3.1 推荐: OpenRC 服务 `/etc/init.d/connsys`(或 `z1_connsys_alpine.sh`)

Alpine 用 OpenRC(init.d/local 服务存在但**未启用**, 需 rc-update)。自建服务最干净:

```
# /etc/init.d/connsys  (chmod +x, 内容见下)
#!/sbin/openrc-run
description="Z1 CONNSYS WiFi/BT modules (non-essential, load after boot)"
depend() { after localmount; }

start() {
    ebegin "Loading Z1 CONNSYS WiFi/BT modules"
    # 用 insmod 绝对路径, 不需要 modules.dep; 顺序 = 依赖顺序
    sh /lib/modules/7.0.0-rc7-z1+/z1_modprobe_connsys.sh
    eend $?
    # WiFi func-on (可选, 自动拉起 wlan0)
    [ -w /dev/wmtWifi ] && echo 1 > /dev/wmtWifi 2>/dev/null
    return 0
}
stop() {
    # ⚠️ mtk_stp_wmt_soc 绝不可 rmmod(挂死需 reboot) — stop 只做提示, 不 rmmod
    ewarn "CONNSYS modules are not unloadable (mtk_stp_wmt_soc rmmod would hang); reboot to unload"
    return 0
}
```

启用: `rc-update add connsys default`(在 chroot 里 `sudo chroot out/alpine_rootfs /sbin/rc-update add connsys default`)。

### 3.2 备选: `/etc/local.d/connsys.start` + `rc-update add local default`

```
# /etc/local.d/connsys.start  (chmod +x)
#!/bin/sh
sh /lib/modules/7.0.0-rc7-z1+/z1_modprobe_connsys.sh
echo 1 > /dev/wmtWifi 2>/dev/null || true
```
启用: `rc-update add local default`(`local` 服务现不在任何 runlevel, 必须先加)。

### 3.3 关于 `z1_modprobe_connsys.sh` 的 3 个 Alpine 适配点

1. **shebang**: 现为 `#!/bin/bash`, Alpine 无 bash。脚本本体纯 POSIX/busybox 兼容(已核无 `local`/`[[`/数组/`<<<`), **用 `sh 脚本` 调用即可**; 或部署时 sed 首行 `#!/bin/sh`(可选)。
2. **模块目录**: 脚本默认 `MODS_DIR=/root/connsys`, 支持 `Z1_MODS_DIR` 环境变量覆盖(代码只读 `Z1_MODS_DIR`, **不读位置参数**, 用法注释与实际不符)。部署 Alpine 时:
   - 方案 i(推荐): 启动脚本里 `export Z1_MODS_DIR=/lib/modules/7.0.0-rc7-z1+` 再调 `sh z1_modprobe_connsys.sh`;
   - 方案 ii: 直接把 13 .ko + launcher + 脚本原样放 `/root/connsys/`(与 ramdisk 布局一致, 脚本零改动)。
   - ⚠️ 两种都要保证 launcher 同目录(`$MODS_DIR/mtk_stp_launcher`)。
3. **固件路径 `/system/etc/firmware/`**: 建目录即可, 脚本/内核已按此路径读(见 §2.3)。`wlan_gen2.ko` 的 `/etc/firmware/nvram/WIFI` 也需建。

### 3.4 BT 栈自动加载(可选, 与 WiFi 分离)

BT 栈不走 z1_modprobe_connsys.sh(它只处理 CONNSYS 6 个)。若开机即要 BT, 在 OpenRC 服务里加第二段(顺序 = modinfo depends, 见 BT_STACK_BUILT §3):

```
# BT 栈 (在 CONNSYS 之后)
for m in ecc.ko ecdh_generic.ko libaes.ko bluetooth.ko hci_vhci.ko hidp.ko uhid.ko; do
    insmod /lib/modules/7.0.0-rc7-z1+/$m 2>/dev/null
done
# vhci 桥 (hci0 创建 + PSM 治理, 必需; 后台)
/usr/local/bin/stpbt-vhci-bridge > /tmp/bridge.log 2>&1 &
```
`stpbt-vhci-bridge` open `/dev/stpbt` 触发 BT func-on。若只想先保 WiFi, BT 段留手动命令即可(避免开机即耗电/未验证的通道)。

---

## 4. insmod 可行性评估(与 Alpine 内核版本无关)

| 检查项 | 实测结果 | 结论 |
|---|---|---|
| 运行内核 vermagic | `kernel.release` = `7.0.0-rc7-z1+`(fork_linux/include/config/kernel.release, 已固定) | — |
| 13 个 .ko vermagic | 全部 `7.0.0-rc7-z1+ SMP mod_unload ARMv7 p2v8`(modinfo 逐一批量核) | **精确匹配**, 唯一硬门槛 |
| `CONFIG_MODULE_SIG` | `.config: # CONFIG_MODULE_SIG is not set` | **无签名门禁** |
| `CONFIG_MODVERSIONS` | `.config: # CONFIG_MODVERSIONS is not set` | **无符号 CRC 门禁** |
| `CONFIG_MODULE_UNLOAD` | `=y` | mod_unload 在 vermagic 里, 匹配 |
| Alpine 内核版本 | Alpine 3.24 自带内核**不会被加载**(Z1 只跑自编 7.0.0-rc7-z1+), .ko 与 Alpine 内核无任何耦合 | **与 Alpine 内核无关** |
| firmware 加载 | `CONFIG_FW_LOADER=y`, USER_HELPER=n → request_firmware 直接读 `/lib/firmware`, 无需用户态 helper | 放对路径即可 |
| 用户态工具 | iw/launcher/BT 工具全 **静态 ARM ELF**(file 实测 statically linked), 不依赖 musl/glibc 动态库 | Alpine 直接可跑 |
| busybox applet | Alpine busybox 含 `insmod/mknod/pgrep/dmesg/sleep/grep/cat/echo/ls`(sbin/bin 符号链接已核) | 脚本命令全有 |

**结论**: insmod 只有 vermagic 一道关, 已匹配; Alpine 内核版本差异无影响。部署纪律(CLAUDE.md §5.7): 不设 `LOCALVERSION=` 环境变量; 改内核 ABI 必须重编对应 .ko; insmod 前 `modinfo wlan_gen2.ko | grep vermagic` 复核 + `cat /proc/version`。

---

## 5. 上板验证步骤(板子开机后)

> 前置: 板子已能进 Alpine(刷 `system_alpine.img` 到 p4 + switch_root 版 bootimg, 见 ALPINE_IMPLEMENT §5)。以下用 C3 桥 ttyS0 或 USB ACM ttyGS0 登录。

### 5.1 模块加载验证
```sh
# 自动加载已配好时, 直接看日志; 手动则:
export Z1_MODS_DIR=/lib/modules/7.0.0-rc7-z1+
sh /lib/modules/7.0.0-rc7-z1+/z1_modprobe_connsys.sh
cat /proc/modules | grep -aE "mtk_|wlan|wmt|bluetooth|vhci|hidp|uhid"
ls /dev/stpwmt /dev/stpbt /dev/wmtWifi        # 节点(脚本 mknod)
dmesg | grep -E "WMT HIF info added|patch total num"   # launcher 就绪标志
```

### 5.2 WiFi 上板验证(基线: ramdisk 版 scan 8 AP)
```sh
echo 1 > /dev/wmtWifi                          # func-on
sleep 3
ip link set wlan0 up   # 或 ifconfig wlan0 up (busybox)
iw dev wlan0 scan | grep -E "SSID|freq|signal" # 期待列出 AP
iw dev wlan0 link
```

### 5.3 BT 上板验证(首次, 未验证过)
```sh
for m in ecc.ko ecdh_generic.ko libaes.ko bluetooth.ko hci_vhci.ko hidp.ko uhid.ko; do
  insmod /lib/modules/7.0.0-rc7-z1+/$m
done
ls /dev/vhci
/usr/local/bin/stpbt-vhci-bridge > /tmp/bridge.log 2>&1 &
sleep 3
hciconfig -a 2>/dev/null || cat /sys/class/bluetooth/hci0/...   # 或 btup-scan 冒烟
/usr/local/bin/hci-localver    # HCI Read Local Version
```

### 5.4 回滚
- p4: 重刷原厂 system 镜像(若有)或保留现 system_alpine.img 前先 `cp` 到 `bridge_backups/`。
- boot: `1/boot.img`(原厂零修改 6MB)。
- 每步改 rootfs 前先备份带时间戳到 `bridge_backups/`。

---

## 6. 部署变更清单(未来执行, 本次未改)

| # | 动作 | 对象 |
|---|---|---|
| 1 | 建目录 | `/lib/modules/7.0.0-rc7-z1+/`、`/system/etc/firmware/`、`/etc/firmware/nvram/` |
| 2 | 拷 13 .ko + launcher | → `/lib/modules/7.0.0-rc7-z1+/` |
| 3 | 拷 `z1_modprobe_connsys.sh` | → `/lib/modules/7.0.0-rc7-z1+/`(或 /root/connsys); 用 `sh` 调用 |
| 4 | 拷 iw | → `/usr/local/bin/iw` chmod 0755 |
| 5 | 拷 BT 5 工具 | → `/usr/local/bin/` chmod 0755 |
| 6 | 拷固件 5 处 | §2.3 表, 严格用源文件 |
| 7 | 写 OpenRC 服务 | `/etc/init.d/connsys`(含 BT 段可选)+ `rc-update add connsys default` |
| 8 | 打包 | `make_alpine_rootfs.sh` 流程重打 `out/system_alpine.img`(mke2fs -d out/alpine_rootfs, ≤900MiB) |
| 9 | 刷写 | `tools/flash_partition.sh android out/system_alpine.img` + 刷 switch_root bootimg |
| 10 | 上板验证 | §5; BT 首验记 `bridge_backups/products/` 归档 |

**同步提醒(非本任务)**: boot ramdisk 瘦身(移出这套 6.47MB)与 `linux,initrd-end` 同步见 DRIVER_SPLIT_PLAN §4.5 / ALPINE_IMPLEMENT §3, 与本部署是配套的两个动作。

---

## 7. 坑与注意(部署时别踩)

1. **`/system/etc/firmware/` 必须建**: `wmt_conf.c` 编译进 ko 的 `CUST_CFG_WMT_PREFIX` + launcher 脚本参数都是绝对路径, 不能省。缺 `WMT_SOC.cfg` → `mtk_stp_wmt_soc.ko` insmod 失败(上板实测过)。
2. **shebang 是 bash, Alpine 无 bash**: 脚本本体 busybox 兼容, 一律 `sh 脚本` 调用; 别写成 `./z1_modprobe_connsys.sh`。
3. **`Z1_MODS_DIR` 是环境变量, 不是位置参数**: 脚本用法注释说 `[模块目录]` 但代码只读环境变量, 覆盖时要 `export Z1_MODS_DIR=...`。
4. **`mtk_stp_wmt_soc.ko` 绝不可 rmmod**(不可中断挂死需 reboot), OpenRC stop 只提示。
5. **BT 栈与 WiFi 依赖顺序**: CONNSYS = btif→wmt_soc→bt_soc→wmt_wifi_soc→cfg80211→wlan_gen2; BT = ecc→ecdh_generic→libaes→bluetooth→hci_vhci→hidp→uhid(hci_vhci 无 depends 字段但符号来自 bluetooth, 必须后 insmod)。
6. **launcher 是常驻 daemon**: 脚本后台启动后继续跑, func-on 靠它应答 patch; 缺它 → patchNum=0 → WiFi RAM code 在 func-on 死(BT 不受影响)。
7. **vermagic 纪律**: 不设 `LOCALVERSION=`; 改内核 ABI 重编 .ko; insmod 前 modinfo 复核。
8. **`dev_addr_check` WARNING**: wlan0 up 时 dev_addr 全 0, 已知不影响 scan/连接, 可后修。
9. **mdev/udev**: Alpine busybox 默认 mdev; 脚本已带 mknod 兜底建 `/dev/stpwmt|stpbt|wmtWifi`(major 190/192/155), 不依赖 hotplug。
10. **fstab `/dev/block` 前缀**: 与驱动部署无关, 但 Alpine 挂 p5/p6 前需 initramfs 建 `/dev/block` 软链(ALPINE_BOOT_PATH §3.4), 别漏。
