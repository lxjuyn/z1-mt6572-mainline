# Alpine 落地完整计划 (ALPINE_FULL_PLAN_20260816)

> 汇总 2026-08-16 三份主报告 + 2026-08-15 三份参考, 形成可执行落地方案。**只读汇总, 未实施**。
> 主报告(全部存在, 无缺失):
> - `ALPINE_IMPLEMENT_20260816.md` — 块① initramfs switch_root 到 p4 (方案 A /init + repack 注入)
> - `ALPINE_ADB_20260816.md` — 块② Alpine 发行版内 ADB (OpenRC 服务 + 原厂静态 adbd)
> - `ALPINE_DRIVERS_20260816.md` — 块③ 非重要驱动部署 (WiFi CONNSYS + BT 栈 13 .ko)
> 参考(全部存在): `ALPINE_BOOT_PATH_20260815.md`(启动路径方案 A/B)、`DRIVER_SPLIT_PLAN_20260815.md`(必要/非必要拆分+瘦身测算)、`ADB_FULL_PLAN_20260815.md`(ADB 三段落地: 内核/设备/主机)。
> **三份主报告、三份参考全部齐全, 无缺失。**

---

## 0. 总览

**一句话**: 板子跑 mainline `7.0.0-rc7-z1+` 内核 + 最小 initramfs → `/init` 挂 p4 后 `switch_root` 进 Alpine 发行版; Alpine 里自动起 ADB gadget + 自动加载 WiFi/BT 非必要驱动。三大块共用**同一个** `system_alpine.img`(刷 p4)和**同一个** switch_root boot.img(刷 bootimg), 一次刷写全落地。

**依赖链**: 块①(Alpine 能起来)是块②(ADB)/块③(驱动)的前置 → 上板顺序 = 先验 boot 链 → 再验驱动 → 最后验 ADB。

| 块 | 内容 | 状态 (2026-08-16) | 关键产物 |
|---|---|---|---|
| ① 启动链 | initramfs switch_root 到 p4 | 新 /init 已写好并干跑验证, repack 注入待应用 | `out/ramdisk_alpine_switch.gz` (0.89MiB) → `out/boot_mainline_alpine.img` (预算 4.50MiB ≤ 6MiB) |
| ② ADB | Alpine 内自动 ADB | 内核已就绪(zImage 8/16 01:37 = ADB 版), OpenRC 服务脚本已写好, 待部署 | `/etc/init.d/adbd` + `/usr/local/sbin/adbd`(原厂静态 202968B) |
| ③ 驱动 | WiFi + BT 非必要部署 | 13 .ko vermagic 全匹配, 布局已定, OpenRC 服务已写好, 待部署 | `/lib/modules/7.0.0-rc7-z1+/` 13 ko + iw + BT 工具 + 固件 5 处 |

> **交叉关键事实**: 当前树 `fork_linux/arch/arm/boot/zImage`(8/16 01:37, sha `1eb98f3b…`)**
> **本身就是 ADB 版内核**(`CONFIG_USB_CONFIGFS=y` `USB_CONFIGFS_F_FS=y` `USB_CONFIGFS_ACM=y` `USB_F_FS=y` `USB_F_ACM=y`, `G_SERIAL` 已关, UDC=`musb-hdrc`)。所以块①的 boot.img **直接复用这个 zImage** 即同时获得 ADB 能力, **零内核重编、vermagic 不变**(`7.0.0-rc7-z1+`), 现有 CONNSYS .ko 不需重编。

---

## 1. 完整落地计划

### 1.1 块①: initramfs switch_root 到 p4 (方案 A)

**原理**(`ALPINE_BOOT_PATH` §2.1/§3.3): 内核 `init/main.c` 语义 — 只要 initramfs 里 `/init` 存在且可执行, 就跑它作 PID1, 不会去挂 `root=`. 所以 **cmdline 零改动**(`console=ttyS0,115200n8 root=/dev/ram panic=0 … rdinit=/init`), 挂 p4 的活全交给 /init. 模板 = 3.4.67 时代 `out/boot_alpine_auto.img` 的 /init(挂 p4 + switch_root /sbin/init), 已验证模式。

**新 /init 已落盘**: `scripts/z1_alpine_switch_init.sh`(busybox sh, 已 `sh -n` 语法检查 + 干跑打包解包验证)。逻辑:
```
1. mount proc/sys; /dev 用 devtmpfs(不盖内核 CONFIG_DEVTMPFS_MOUNT 自动挂的层, EBUSY 容忍)+ mdev -s 兜底
2. [GETTY_BEFORE_SWITCH=1 时] 后台 ttyGS0 getty(默认 0, 避免与 Alpine inittab 双 getty 竞态)
3. 常驻喂狗: busybox watchdog -t 5 -T 10 后台
4. 等 /dev/mmcblk0p4 最多 30s(实测 mmc0 probe ~8s; 每轮 mdev -s + sleep 1)
5. 失败兜底 recovery_shell(): ttyS0 exec + ttyGS0 循环 + tty0 getty(与现 connsys init 一致)
6. 建 /dev/block/mmcblk0p{4,5,6} 软链 → Alpine fstab 原样可用, 免重刷 p4
7. mount -t ext4 -o rw /dev/mmcblk0p4 /newroot(失败→recovery_shell); [ ! -x /newroot/sbin/init ]→recovery_shell
8. mkdir /newroot/{proc,sys,dev}; mount --bind proc/sys/dev
9. exec switch_root -c /dev/ttyS0 /newroot /sbin/init   (busybox 1.36.1 支持 -c, stdio 上 C3 桥)
```

**repack 注入**(`ALPINE_IMPLEMENT` §3, 待应用到 `tools/repack_ramdisk_connsys.sh` 4 处):
- ① 顶部 OUT 默认值: `Z1_ALPINE_SWITCH=1` 且未设 `Z1_RAMDISK_OUT` → 输出 `out/ramdisk_alpine_switch.gz`
- ② 2.5 喂狗 / 2.6 getty fix 的 python 注入在 `Z1_ALPINE_SWITCH=1` 时跳过
- ③ 新增 2.7 段: 用 `scripts/z1_alpine_switch_init.sh` 整体替换 /init (chmod 0755)
- ④ 第 3 段塞模块 / 3.4b ADB / 3.5 固件/iw/BT 整段 `if [ Z1_ALPINE_SWITCH != 1 ]` gating(剪掉, 保持 ramdisk 最小)
- 其余零改动: 第 4 段 cpio + `xz -9 --check=crc32`、第 5 段回写 512B MTK 头(`88 16 88 58`) body-size、**第 7 段同步 dts `linux,initrd-end` + 重编 dtb**(ramdisk 尺寸变了, 这段绝不能跳过)、第 6 段校验。
- base ramdisk = 默认 `out/forensic/linux/ramdisk.gz`(busybox 1.36.1 静态, 有 switch_root/mount/mdev/getty/watchdog applet)。

**干跑已验证**(`/tmp/alpine_switch_dryrun/`): 新 /init 装入 7554B、MTK 头 body-size 回写 `0xe2834` 一致、ramdisk 928,308B (0.89MiB)、boot.img 预算 **4.50MiB ≤ 6MiB**、xz crc32 + `CONFIG_RD_XZ=y` ✅。

**Alpine rootfs 必改 2 项**(改 `out/alpine_rootfs/`, 打包前做):
1. `/etc/inittab` 补 `ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100`(必须, 否则 C3 桥登不上)
2. `/etc/fstab` 不改 — `/dev/block/*` 软链由新 /init 建好带进 Alpine(§.2 第 6 段)

### 1.2 块②: Alpine 内 ADB

**内核侧零改动**(`ALPINE_ADB` §2): 当前树 zImage 即 ADB 版; UDC=`musb-hdrc`; `CONFIG_USB_G_SERIAL` 已关(不抢 UDC)。

**adbd 零编译**: `out/boot_extracted/rd/sbin/adbd`(202968B, static ARM EABI5, 无 INTERP/NEEDED)。strings 铁证自带 FunctionFS(`/dev/usb-ffs/adb/ep0..ep2` + `[ usb_init - using FunctionFS ]`), 先走 functionfs, 失败才退 `/dev/android_adb_enable`(mainline 无, 正好用不上)。

**核心方案** = `/etc/init.d/adbd` OpenRC 服务(脚本全文见 `ALPINE_ADB` §6, 已写好):
```
挂 configfs → 清旧 gadget → 建 0e8d:200e(z1) gadget → functions/ffs.adb + acm.usb0
→ configs/c.1 挂两 function → mount -t functionfs adb /dev/usb-ffs/adb(adbd 硬编码路径, 必须此名)
→ 起 adbd(start-stop-daemon, pidfile /run/adbd.pid)
→ echo musb-hdrc > UDC(带 20 次重试)
```

**★ f_fs 顺序修正(本项目最重要新发现)**: mainline `f_fs.c:3509` `ffs_do_functionfs_bind()` 在 `!desc_ready` 时返回 `-ENODEV`; `desc_ready` 要等 adbd 打开 ep0 写入描述符(`f_fs.c:4266` `ffs_ready()`)才置位。**必须先起 adbd、后绑 UDC**。现成 `scripts/z1_adb_setup.sh`(:step8 绑 UDC 在 :step9 起 adbd 之前)顺序反了, 照抄会 ENODEV → 不枚举。社区 3 份同构脚本(tonyho/adbd-linux、NXP i.MX、VisionFive2)也都是"先 daemon 后 UDC", 佐证。

**Android 残留缓解**(adbd 硬依赖, 不能省): `mkdir -p /system/bin && ln -sf /bin/sh /system/bin/sh`(必做); `echo 'shell:x:2000:2000:shell:/data/local/tmp:/bin/sh' >> /etc/passwd` + `/etc/group` + `mkdir -p /data/local/tmp`(防降权路径残缺); `mkdir -p /dev/log`(建议); `export ADB_EXTERNAL_STORAGE=/mnt/data`(adb pull 落点, p6 已挂 fstab); `ro.adb.secure` 缺省→大概率免 RSA(Z1 原厂就是 0)。

**★ 最大不确定 = adbd 是否降权到 shell(uid 2000)**: 无 property 服务时 `ro.debuggable=""` + `ro.secure="1"` → `should_drop_privileges()` 大概率返回真 → `setgid(2000);setuid(2000)` → adb shell 不能 insmod/读 /dev(与"测驱动"冲突, **root 是目标**)。上板 `adb shell id` 一测便知。兜底 3 级:
- A(先试): 原厂 adbd 直接跑, `id` = root → 收工
- B(二进制补丁, ~1-2h): 按 Harrison/Weiske 技法 Ghidra 定位本 adbd 的 `setgid(2000)/setuid(2000)`(搜 `mov rN,#0x7d0` + `bl setuid`), `bl` 处 4 字节改 `00 BF`
- C(重编, ~半天-1天): meta-oe scarthgap `android-tools_5.1.1.r37.bb` + `-DALLOW_ADBD_ROOT=1`(含 `0008-allow-adbd-root` 补丁), 行为确定
- D(假 property 层, 可选): 精确匹配 bionic prop_area 格式, 复杂度不低于 B/C, 默认不做

**部署**: `cp out/boot_extracted/rd/sbin/adbd out/alpine_rootfs/usr/local/sbin/adbd`(变量 `adbd=` 可经 `/etc/conf.d/adbd` 覆盖); init.d 脚本 chmod 0755; `ln -sf /etc/init.d/adbd out/alpine_rootfs/etc/runlevels/default/adbd`; `rm -f out/alpine_rootfs/etc/runlevels/default/usb-acm`(no-op 服务, mainline 下 android_usb 不存在)。

**主机侧**(`ADB_FULL_PLAN` §阶段3, 5 分钟): 建 `/etc/udev/rules.d/99-z1-adb.rules`(`0e8d:0c01`/`0e8d:200e` → MODE="0666" GROUP="plugdev" SYMLINK z1-adb); 或 `groupadd adbusers && usermod -aG adbusers $USER`(修 50-android.rules 引用的不存在组); `udevadm control --reload-rules && udevadm trigger`。连接流程: 正常开机(拔 USB)→ 内核完全起来**后再插 USB**(避 preloader LineState)→ `adb kill-server; adb start-server` → `adb devices -l` → `adb shell`。

### 1.3 块③: 非重要驱动部署

**划分确认**(`DRIVER_SPLIT_PLAN` §1/§2): boot 必要驱动(eMMC/rootfs/PMIC/时钟/console/WDT/按键13/显示/电池/USB gadget 恢复 console/RTC/HW RNG)当前全部 `=y` 且正确, **零 .config 改动**。非必要 WiFi/BT 已是模块形态, 只是被塞在 boot ramdisk 里; 移到 Alpine = "ramdisk 瘦身(块①已自动做)+ rootfs 部署"。

**部署主体** = **13 个 .ko**(6 CONNSYS + 7 BT 栈,**含 libaes.ko** — bluetooth.ko 依赖它; 旧计划"12 个"是 DRIVER_SPLIT_PLAN 表漏了 libaes) + `mtk_stp_launcher` + `iw` + 5 个 BT 用户态工具 + 5 处固件。**vermagic 全部 `7.0.0-rc7-z1+ SMP mod_unload ARMv7 p2v8` 精确匹配; `CONFIG_MODULE_SIG` 未开、`CONFIG_MODVERSIONS` 未开 → insmod 无签名/CRC 门禁**。Alpine 3.24 内核根本不会被加载(Z1 永远跑自编内核), .ko 与 Alpine 内核版本无任何耦合。

**目录布局**(目标 rootfs `out/alpine_rootfs/`):
- `/lib/modules/7.0.0-rc7-z1+/`(当前不存在, 需建; 全部平铺):
  - CONNSYS 6: `cfg80211.ko`(491,872, **内核 CFG80211=m 未内编, 必须随带**)、`mtk_btif_drv.ko`(113,984)、`mtk_stp_wmt_soc.ko`(369,696)、`mtk_stp_bt_soc.ko`(12,616)、`mtk_wmt_wifi_soc.ko`(14,360)、`wlan_gen2.ko`(958,904)
  - BT 栈 7: `bluetooth.ko`(641,676)、`ecc.ko`(36,876)、`ecdh_generic.ko`(9,304)、`libaes.ko`(11,656)、`hci_vhci.ko`(14,860)、`hidp.ko`(26,468)、`uhid.ko`(13,268)
  - `mtk_stp_launcher`(522,212, WMT patch 应答, func-on 必需) + `z1_modprobe_connsys.sh`
- `/usr/local/bin/`: `iw`(764,400, nl80211) + BT 5 工具 `stpbt-vhci-bridge`(必需)/`stpbt-hci-test`/`hci-localver`/`btup-scan`/`btif-lpbk-test`(各 ~448K, 全静态 ARM)
- **固件 5 处(全必须, 缺则 insmod/启动失败)**:
  - `/lib/firmware/WIFI_RAM_CODE_MT6582` ← `out/firmware_backup/firmware/WIFI_RAM_CODE_SOC`(160,464; wlan_gen2 request_firmware)
  - `/system/etc/firmware/WMT_SOC.cfg` ← 同名源(80B; **mtk_stp_wmt_soc.ko 编译进 `CUST_CFG_WMT_PREFIX="/system/etc/firmware/"` filp_open 绝对路径, 缺则 insmod 失败 — 上板实测过**)
  - `/system/etc/firmware/ROMv1_patch_0_hdr.bin` ← `mt6572_82_patch_e1_0_hdr.bin`(37,360; launcher `-p /system/etc/firmware/` 参数写死)
  - `/system/etc/firmware/ROMv1_patch_1_hdr.bin` ← `mt6572_82_patch_e1_1_hdr.bin`(20,488)
  - `/etc/firmware/nvram/WIFI` ← `mainline_recon/wifi-nvram-WIFI.bin`(512B, MAC <MAC>; wlan_gen2 filp_open)
  - **坑**: Alpine 没有 `/system`, 必须建 `/system/etc/firmware/` 目录(不能改路径, 除非重编 .ko 改宏)
- 部署体积 ≈ 6.47MB 解压, 850MiB p4 富余充裕。

**开机自动加载**: `/etc/init.d/connsys` OpenRC 服务(已写好, `ALPINE_DRIVERS` §3.1)+ `rc-update add connsys default`。start = `sh /lib/modules/7.0.0-rc7-z1+/z1_modprobe_connsys.sh` + `echo 1 > /dev/wmtWifi`; **stop 只 ewarn 不 rmmod**(`mtk_stp_wmt_soc.ko` 绝不可 rmmod, 挂死需 reboot)。备选 `/etc/local.d/connsys.start` + `rc-update add local default`(但 local 服务现不在任何 runlevel, 要先加)。

**`z1_modprobe_connsys.sh` 3 个 Alpine 适配点**:
1. shebang `#!/bin/bash` → Alpine 无 bash, 脚本本体纯 busybox/POSIX, **一律 `sh 脚本` 调用**(别写 `./`)
2. 模块目录: 脚本默认 `MODS_DIR=/root/connsys`, **只读 `Z1_MODS_DIR` 环境变量不读位置参数** → 启动脚本里 `export Z1_MODS_DIR=/lib/modules/7.0.0-rc7-z1+`(或把 13 ko + launcher + 脚本原样放 `/root/connsys/` 零改动, 二选一, 都要保证 launcher 同目录)
3. 固件 `/system/etc/firmware/`: 建目录即可

**insmod 顺序**(脚本已控, 依赖顺序): CONNSYS = cfg80211 → mtk_btif_drv → mtk_stp_wmt_soc → mtk_stp_bt_soc → mtk_wmt_wifi_soc → wlan_gen2 → 建节点(mknod /dev/stpwmt c 190 0 / /dev/stpbt c 192 0 / /dev/wmtWifi c 155 0) → launcher `-m 3 -p /system/etc/firmware/ &` → 等 `WMT HIF info added`/`patch total num` → `echo 1 > /dev/wmtWifi`。**BT 栈在 wlan 之后**: ecc → ecdh_generic → libaes → bluetooth → hci_vhci → hidp → uhid(hci_vhci 无 depends 但符号来自 bluetooth, 必须后 insmod)→ `stpbt-vhci-bridge /dev/stpbt ↔ /dev/vhci` 后台(触发 BT func-on)。

**上板已证基线**: WiFi `iw dev wlan0 scan` 8 AP(ramdisk 版); BT 栈编译完成归档但**尚未上板验证**(首次)。已知 `dev_addr_check` WARNING(wlan0 up 时 dev_addr 全 0)不影响 scan/连接。

---

## 2. 分步执行清单(每步产物 + 上板验证点)

> 纪律: 每步改前备份 `bridge_backups/`(带时间戳); 每步修复完立即 `git add/commit/push`; 产物归档 `bridge_backups/products/`; 全程默认 make, 不设 `LOCALVERSION=`。

### Phase 0 — PC 上准备(板子关机, 只读检查 + 落盘)

| # | 动作 | 命令/对象 | 产物 | 验证点(PC 侧) |
|---|---|---|---|---|
| 0.1 | 备份 | `cp -a out/alpine_rootfs bridge_backups/alpine_rootfs_pre_adbd_$(date +%Y%m%d_%H%M)`; 备份 `tools/repack_ramdisk_connsys.sh` | 备份目录 | 存在 |
| 0.2 | 确认 zImage 是 ADB 版 | `sha256sum fork_linux/arch/arm/boot/zImage` = `1eb98f3b…`(8/16 01:37); `.config` grep `CONFIG_USB_CONFIGFS` | — | 匹配即 ADB 内核, 零重编 |
| 0.3 | repack 脚本加 `Z1_ALPINE_SWITCH=1` 段 | `tools/repack_ramdisk_connsys.sh` 按 §1.1 的 4 处改动 | 改后脚本 | `sh -n` 通过 |
| 0.4 | 建 alpine 版 ramdisk | `Z1_ALPINE_SWITCH=1 bash tools/repack_ramdisk_connsys.sh` | `out/ramdisk_alpine_switch.gz`(0.89MiB) | 解包回读 /init 是新版; MTK 头 `88 16 88 58` body-size 一致 |
| 0.5 | 打包 boot.img | `tools/z1_make_bootimg.sh fork_linux/arch/arm/boot/zImage fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dtb out/ramdisk_alpine_switch.gz out/boot_mainline_alpine.img` | `out/boot_mainline_alpine.img` | `validate_mtk_boot.py out/boot_mainline_alpine.img --dtb fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dtb` → VALID; ≤6MiB |
| 0.6 | 归档 + git | 产物复制进 `bridge_backups/products/`; commit+push | 归档 | 存在 |
| 0.7 | rootfs: inittab 补 ttyS0 | 改 `out/alpine_rootfs/etc/inittab` 追加 `ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100` | 改后 inittab | grep ttyS0 |
| 0.8 | rootfs: 部署 adbd | `sudo cp out/boot_extracted/rd/sbin/adbd out/alpine_rootfs/usr/local/sbin/adbd` | `/usr/local/sbin/adbd` | `file` static ARM; sha 与源一致 |
| 0.9 | rootfs: 装 init.d/adbd | 写 `/etc/init.d/adbd`(ALPINE_ADB §6 全文) chmod 0755; `ln -sf /etc/init.d/adbd out/alpine_rootfs/etc/runlevels/default/adbd`; `rm -f out/alpine_rootfs/etc/runlevels/default/usb-acm` | 服务 + runlevel 软链 | `ls -l` 软链在 |
| 0.10 | rootfs: 建驱动目录 + 拷模块/工具/固件 | 建 `/lib/modules/7.0.0-rc7-z1+/` `/system/etc/firmware/` `/etc/firmware/nvram/`; 拷 13 .ko + launcher + 脚本 + iw + BT 5 工具 + 固件 5 处(§1.3) | 目录树 | `find` 核对数量; `modinfo` 抽验 vermagic=`7.0.0-rc7-z1+` |
| 0.11 | rootfs: 装 init.d/connsys | 写 `/etc/init.d/connsys`(ALPINE_DRIVERS §3.1, 含可选 BT 段) chmod 0755; `ln -sf /etc/init.d/connsys out/alpine_rootfs/etc/runlevels/default/connsys` | 服务 + runlevel 软链 | 软链在; `sh -n` |
| 0.12 | 重打 system_alpine.img | `make_alpine_rootfs.sh` 流程末尾 `mke2fs -d out/alpine_rootfs`(850MiB, 无 64bit) | `out/system_alpine.img` | `file`/`ls -l` ≤900MiB; 挂载抽查 /init.d/connsys |
| 0.13 | 主机 udev | 建 `/etc/udev/rules.d/99-z1-adb.rules`(0c01/200e → 0666); `udevadm control --reload-rules && udevadm trigger` | 规则文件 | `lsusb` 测试在 ADB 验证段 |
| 0.14 | 归档 + git | 全部产物 + 改脚本 + 本计划 | 提交 | push 成功 |

### Phase 1 — 上板刷写(用户起 mtkclient + 插 download 线, 两态分开)

| # | 动作 | 命令 | 上板验证点 |
|---|---|---|---|
| 1.1 | 刷 p4(system) | `tools/flash_partition.sh android out/system_alpine.img` | 刷写无错(避开 `.bak` 64bit 版) |
| 1.2 | 刷 bootimg | `tools/flash_with_retry.sh bootimg out/boot_mainline_alpine.img ~/claude/6572/1/boot.img 8` | 写入成功; 失败自动回滚兜底 |

### Phase 2 — 拔线电池开机 + 抓 log + Alpine 内验证

| # | 动作 | 命令 | 上板验证点 |
|---|---|---|---|
| 2.1 | 抓 log | `./z1_capture_mainline.sh 600` | 期待顺序: `earlycon`/`Linux version 7.0.0-rc7-z1+` → `11005000.serial: <SERIAL> → `mmc0: new MMC card` → `[init] Alpine switch-root init starting` → `[init] mounting /dev/mmcblk0p4` → `switch_root` → `Welcome to Alpine Linux` → `ttyS0 login:` |
| 2.2 | 登录 | C3 桥 ttyS0(115200)或 USB ACM ttyGS0 | 能登录 |
| 2.3 | 验 root | `mount \| grep ' / '` | `/dev/mmcblk0p4 on / type ext4 (rw,...)` |
| 2.4 | 验软链 | `ls -l /dev/block/` | mmcblk0p4/p5/p6 软链存在 |
| 2.5 | 验服务注册 | `rc-update show \| grep -E 'adbd|connsys|usb-acm'` | adbd/connsys 在 default; usb-acm 已移除 |
| 2.6 | 驱动加载 | `rc-service connsys start` 或 `export Z1_MODS_DIR=/lib/modules/7.0.0-rc7-z1+; sh /lib/modules/7.0.0-rc7-z1+/z1_modprobe_connsys.sh` | `/proc/modules` 见 6 ko; `ls /dev/stpwmt /dev/stpbt /dev/wmtWifi`; dmesg `WMT HIF info added` |
| 2.7 | ADB 起 | `rc-service adbd start` | ebegin/eend 0 + pidfile; `/sys/kernel/config/usb_gadget/z1/UDC` = musb-hdrc; `/dev/usb-ffs/adb/` 有 ep0/ep1/ep2; `ps \| grep adbd` |
| 2.8 | 主机连 ADB | 内核起来后插 USB → `lsusb`(0e8d:200e) → `adb kill-server; adb start-server` → `adb devices -l` | 显示 `device`(免认证); `adb shell id` 判 root/shell |

---

## 3. 明天早上上板验证顺序(含 ADB 测蓝牙)

> 前提: 用户插 download 线 + 起 mtkclient; **刷写才插 USB, 正常开机拔掉**。每步间判读失败表(见 §4)。以 C3 桥 ttyS0 为主入口; **下半夜注意 ESP 端口是掌机不是 C3, 别往 303a:1001 发数据**。

```
[1] 刷 p4:   tools/flash_partition.sh android out/system_alpine.img
[2] 刷 boot: tools/flash_with_retry.sh bootimg out/boot_mainline_alpine.img ~/claude/6572/1/boot.img 8
[3] 拔 download 线 + 电池开机; ./z1_capture_mainline.sh 600
    判读: 期待 "Welcome to Alpine Linux" + "ttyS0 login:" (若 0 输出/循环, 见 §4 表)
[4] C3 桥 ttyS0 登录; 验 mount / = ext4 rw + ls /dev/block/
[5] WiFi(基线 scan 8 AP): rc-service connsys start → echo 1 > /dev/wmtWifi → sleep 3
    → ip link set wlan0 up → iw dev wlan0 scan | grep -E "SSID|freq|signal"
    (期待 ≥1 AP, 对照 WIFI_SCAN_SUCCESS_20260814 的 8 AP)
[6] BT 首次上板(未验证过):
    insmod 顺序: ecc → ecdh_generic → libaes → bluetooth → hci_vhci → hidp → uhid
    ls /dev/vhci → /usr/local/bin/stpbt-vhci-bridge > /tmp/bridge.log 2>&1 &
    sleep 3 → /usr/local/bin/hci-localver (HCI Read Local Version 冒烟)
    (可选) btup-scan 裸 HCI inquiry
[7] ADB: rc-service adbd start → (内核起来后再)插 USB
    lsusb 期望 0e8d:200e → adb kill-server; adb start-server → adb devices -l
    adb shell id     ★ 关键判定: uid=0(root) → 直接可用; uid=2000(shell) → §1.2 兜底 B/C
[8] 用 ADB 复测驱动(含 ADB 测蓝牙) — 前提: [7] 得到 root shell:
    adb shell 'rc-service connsys status'
    adb shell 'echo 1 > /dev/wmtWifi'
    adb shell 'iw dev wlan0 scan | grep -E "SSID|freq|signal"'
    adb shell 'insmod /lib/modules/7.0.0-rc7-z1+/ecc.ko'  ... (BT 7 ko)
    adb shell '/usr/local/bin/stpbt-vhci-bridge > /tmp/bridge.log 2>&1 &'
    adb shell 'hci-localver'    # ADB 通道上跑 BT 冒烟
    adb pull /proc/cmdline /tmp/z1_cmdline; adb push /tmp/test.bin /tmp/   # 验 push/pull 通路
[9] 回归/收尾: 结果归档 bridge_backups/products/ + git 上传; 保留回滚镜像
```

**上板验证点速记**: ① Alpine 起来(Welcome + login) ② root = ext4 rw + /dev/block 软链 ③ WiFi scan 复现 8 AP ④ BT hci0 首次冒烟 ⑤ adb devices + `adb shell id` root 判定 ⑥ 用 adb shell 通道复测 WiFi/BT/push-pull。

**执行顺序决策**: 先跑 [5] WiFi 确认 CONNSYS 部署正确(基线已知), 再 [6] BT 首验(新通道), 再 [7][8] ADB(依赖 root 判定)。若 [7] `adb shell id` = shell(非 root), **先做 §1.2 兜底 B(二进制补丁)或 C(meta-oe 重编)再进 [8]**, 否则 adb shell 内 insmod 会因权限失败。

---

## 4. 失败判读表(合并三大块)

| 现象 | 含义 | 处置 |
|---|---|---|
| switch_root 后一行都没有 | `-c /dev/ttyS0` 打不开或 Alpine init 崩 | 改用无 `-c` 版 /init; 查 /dev/ttyS0; Alpine inittab 首行 |
| `FATAL: /dev/mmcblk0p4 never appeared` | 本 boot eMMC probe 失败 | 落恢复 shell(ttyS0 getty 可用); `dmesg` 查 mmc |
| `FATAL: mount ... failed` | p4 非 ext4 / 未刷入 | 核对 `flash_partition.sh android` 成功 |
| `FATAL: /newroot/sbin/init not executable` | p4 里不是 Alpine | 重刷 `system_alpine.img` |
| ~19.7s/~30.5s 周期复位 | 喂狗死在 switch_root 且 WDT 被重武装 | Alpine 侧 local.d 起 busybox watchdog; 确认 `mediatek_timer_init` 早停 |
| initramfs 截断 VFS panic | initrd-end 不同步 | repack 第 7 段自动同步; 打包后 validate 复核 |
| `rc-service adbd start` 的 `UDC bind failed` | f_fs desc_ready 没置位(adbd 没起/functionfs 挂载失败) | `ps \| grep adbd`; `ls /dev/usb-ffs/adb`; `dmesg \| grep -i ffs`; 确认内核是 ADB 版 |
| `adb devices` 空 | gadget 没枚举/权限 | `lsusb`; 主机 udev(99-z1-adb.rules); `dmesg` 看 musb |
| `adb devices` 显示 `offline` | 旧 adbd(4.x) vs 新 adb 34 握手没完 | `adb kill-server` 重试; 换官方 platform-tools; 看设备 dmesg |
| `adb devices` 显示 `unauthorized` | RSA 被要求(ro.adb.secure 被读成 1) | 推 `~/.android/adbkey.pub` → 设备 `/data/misc/adb/adb_keys` + `rc-service adbd restart` |
| `adb shell id` = `uid=2000(shell)` | adbd 降权(§1.2 最大不确定) | §1.2 兜底 B(二进制补丁)或 C(meta-oe 重编) |
| `adb shell` 报 `/system/bin/sh: not found` | §5.1 symlink 没建 | 检查 init.d/adbd step7; 手动 `ln -sf /bin/sh /system/bin/sh` + restart |
| `mtk_stp_wmt_soc.ko` insmod 失败 | `/system/etc/firmware/WMT_SOC.cfg` 缺(上板实测过) | 核对固件 5 处部署(§1.3) |
| `iw dev wlan0 scan` 0 AP | func-on 没通/launcher 没起 | 查 `dmesg \| grep -E "WMT HIF info added\|patch total num"`; 确认 launcher 后台在跑 |
| BT hci0 不出现 | vhci 桥没起/BT 栈顺序错 | 确认 7 ko insmod 顺序; `ls /dev/vhci`; `/tmp/bridge.log` |
| `ttyGS0` getty 刷屏 | 只配 adb 没配 acm, ttyGS0 不存在 | init.d/adbd 保留 `acm.usb0`(已含) |
| `mmcblk0: p6 ... extends beyond EOD` | 已知无害(p6 GPT 越界, 别用 p6) | 忽略 |
| `dev_addr_check` WARNING | wlan0 up 时 dev_addr 全 0 | 已知, 不影响 scan/连接, 可后修 |

---

## 5. 回滚

| 层 | 回滚 |
|---|---|
| boot | `~/claude/6572/1/boot.img`(原厂零修改 6MB); 或 `out/boot_mainline_connsys.img`(现上板验证版, 含 connsys ramdisk) |
| p4 | 重刷原厂 system 镜像(若有); 刷前 `cp out/system_alpine.img bridge_backups/` |
| repack 脚本 | 0.1 步备份恢复 |
| Alpine rootfs | 0.1 步备份 `alpine_rootfs_pre_adbd_*` 恢复 |

---

## 6. 关键文件索引

| 项 | 路径 |
|---|---|
| 新 /init(块①) | `scripts/z1_alpine_switch_init.sh`(已落盘, 干跑验证) |
| ADB 内核(zImage 8/16 01:37) | `fork_linux/arch/arm/boot/zImage`(= `out/boot_mainline_z1_adb.img` kernel 段减 dtb) |
| 原厂静态 adbd | `out/boot_extracted/rd/sbin/adbd`(同 `out/forensic/{live,rd_stock_extract}/rd/sbin/adbd`) |
| base ramdisk | `out/forensic/linux/ramdisk.gz`(512B MTK 头 + gzip cpio, busybox 1.36.1) |
| 现上板 ramdisk | `out/ramdisk_mainline_connsys.gz`(回滚用) |
| 模块源 | `bridge_backups/products/products_connsys_modules_z1vermagic_20260814_015009/`、`products_bt_stack_20260814_204345/`、`products_iw_nl80211_20260814_144324/` |
| 固件源 | `out/firmware_backup/firmware/` + `mainline_recon/wifi-nvram-WIFI.bin` |
| 加载脚本 | `scripts/z1_modprobe_connsys.sh`(shebang bash, 用 sh 调用; 只读 `Z1_MODS_DIR`) |
| 现成 ADB setup(顺序需修正) | `scripts/z1_adb_setup.sh`(先 UDC 后 adbd = 反, 勿照抄; 用 init.d/adbd 版) |
| repack/打包/校验 | `tools/repack_ramdisk_connsys.sh`(待加段)/`tools/z1_make_bootimg.sh`/`tools/validate_mtk_boot.py` |
| 刷写 | `tools/flash_partition.sh android`(p4) / `tools/flash_with_retry.sh bootimg` |
| 抓 log | `./z1_capture_mainline.sh` |
| 主报告 | `ALPINE_IMPLEMENT_20260816.md`、`ALPINE_ADB_20260816.md`、`ALPINE_DRIVERS_20260816.md` |
| 参考 | `ALPINE_BOOT_PATH_20260815.md`、`DRIVER_SPLIT_PLAN_20260815.md`、`ADB_FULL_PLAN_20260815.md` |

**关键纪律提醒**: ① cmdline 零改动(`root=/dev/ram rdinit=/init`); ② vermagic `7.0.0-rc7-z1+` 不动, 全程默认 make, 不设 `LOCALVERSION=`; ③ 改前备份 + 每步 git; ④ 刷写才插 USB, 开机拔掉; ⑤ boot.img 必须用 ADB 版 zImage(当前树)打包; ⑥ 打包后必跑 `validate_mtk_boot.py`(initrd-end 同步是自动的但必须复核)。
