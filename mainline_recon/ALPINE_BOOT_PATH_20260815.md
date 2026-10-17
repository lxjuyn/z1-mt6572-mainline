# Alpine 从 p4 (system) 启动路径分析 — Z1 (MT6572) mainline 移植

> 只读分析，2026-08-15。未改任何源/镜像/脚本，只产出本报告。
> 依据: 当前内核 `7.0.0-rc7-z1+` 已在 `boot_mainline_connsys.img` 跑通(eMMC p4 枚举、initramfs /init 起 shell)，`system_alpine.img`(850MiB ext4, Alpine 3.24.0) 已就绪待刷 p4。
> 所有关键事实均已二次核对(解包 boot.img/ramdisk、读 .config/dts、抓 boot log)，见 §1 证据列。

---

## 0. 结论摘要 (TL;DR)

1. **推荐方案 A**: boot.img 的 initramfs 只做"挂载 p4 + `exec switch_root /newroot /sbin/init`"，保留 `root=/dev/ram rdinit=/init` cmdline 不动。这是**低风险、已验证模式**——`out/boot_alpine_auto.img` 的 `/init`(挂 mmcblk0p4 + switch_root /sbin/init) 就是现成模板，只需对 mainline 差异点做小改(见 §3.2)。
2. **方案 B (root=/dev/mmcblk0p4 直挂) 可行但有 2 个硬坑**:
   - 坑① (容易踩): **只要 initramfs 里 `/init` 存在且可执行，内核就跑它、根本不会 `prepare_namespace()` 去挂 `root=`**。所以光把 cmdline 的 `root=` 改成 p4 是**无效的**——必须同时移除/改名 initramfs 的 `/init`(或 `rdinit=/nonexistent`、或把 ramdisk 做空 `ramdisk_size≈0`，参考 `boot_mainline_konly.img` 先例)。
   - 坑②: eMMC probe ~8s，`mount_root` 若在 mmc0 probe 完成前执行会 panic；需 `rootwait` + 保证 MMC_MTK 非 async 或等 probe。
   - **6MB 限制下两方案都富余**(kernel 3.78MB + 最小 ramdisk)，但 A 在可靠性(有兜底 shell、可喂狗、cmdline 零改动)上明显更优。**建议 A 为主，B 留到 A 稳定后作为"更干净"的可选项。**
3. **CONNSYS .ko 放 Alpine 完全可行**: 全套 **12 个 .ko**(6 WiFi + 6 BT) vermagic 全部 `7.0.0-rc7-z1+`，与运行内核**精确匹配**；内核 `CONFIG_MODULE_SIG` 未开，无签名门禁。Alpine `/lib/modules` 现为空，需建 `7.0.0-rc7-z1+` 目录 + 用现成 `z1_modprobe_connsys.sh`(insmod 绝对路径，不需要 modules.dep)。`mtk_stp_launcher`/`iw`/`busybox` 全是**静态 ARM ELF**，Alpine 下直接可跑。
4. **必须先处理 2 个 Alpine rootfs 配置点才能从 p4 起来**:
   - `/etc/fstab` 全部用 `/dev/block/mmcblk0pN`，mainline 下节点是 **`/dev/mmcblk0pN`**(无 `/dev/block` 前缀)。**两个修法**(§3.4): 改镜像 fstab；或更省事——在 initramfs 里 `switch_root` 前建 `/dev/block/` 符号链接(`mkdir -p /dev/block; ln -s ../mmcblk0p4 /dev/block/mmcblk0p4`)，因 `/dev` 会被 bind 进 Alpine，fstab 原样可用，**免重刷 p4**。
   - inittab 只有 ttyGS0/tty0 getty，mainline 串口 console 是 **ttyS0**(115200)，需补一条 `ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100` 才能从 C3 桥登录。
5. **上板验证步骤**见 §5，全程不碰 download 线、不改现有回滚镜像。

---

## 1. 现状基线 (2026-08-15 二次核对)

| 项 | 值 | 证据 |
|---|---|---|
| 运行内核 | `7.0.0-rc7-z1+` #91 SMP Fri Aug 14 01:46:36 CST 2026 | `z1_boot_mainline_20260815_140539.log` 首行 |
| boot.img | `out/boot_mainline_connsys.img` 5,652,480B ≤ 6MiB，ANDROID! magic，appended DTB (dt_size=0 无独立 dt 区) | 本报告解包 header: ksz=0x39c04c rsz=0x1c6ec8 page=2048 dt=0 |
| cmdline | `console=tty0 console=ttyS0,115200n8 root=/dev/ram panic=0 ignore_loglevel clk_ignore_unused rdinit=/init` | boot log `Kernel command line:` + `z1_make_bootimg.sh` 硬编码 |
| initrd 地址 | dts `linux,initrd-start = <0x84100000>`, `end = <0x8431f2d0>` (start+body_size 严格匹配) | `mt6572-z1.dts:41-42` |
| eMMC | `mmc0: new MMC card` @ ~8.0s，`mmcblk0: p1 < p5 p6 > p2 p3 p4`，**p4 = 921,600KiB = 900MiB (system)** | boot log + `z1_emmc_test_233012.log` |
| 串口 | `11005000.serial: <SERIAL> ... ST16650V2` @ 115200 (不是 ttyMT1/921600) | boot log |
| USB ACM | `g_serial gadget.0: Gadget Serial v2.4` → `/dev/ttyGS0` (mainline f_acm/g_serial，**非** android_usb) | boot log |
| initramfs | 512B MTK ROOTFS 头 (`0x58881688` + offset 4 body 大小 LE u32) + XZ cpio；/init 是 busybox 恢复 shell(USB ACM getty + 喂狗 + 落 shell)，**不含 p4 挂载/switch_root** | `ramdisk_mainline_connsys.gz` 解包 |
| busybox (initramfs) | 1,533,764B **静态 ARM**，applets 含 `switch_root`(实测 `busybox` 输出 applet 表) | 本报告 strings + 运行输出 |
| Alpine rootfs | `out/system_alpine.img` 850MiB ext4，Alpine 3.24.0 + busybox init + OpenRC；`/sbin/init → /bin/busybox` | `ALPINE_INVENTORY_20260815.md` + 本报告 mount 核对 |
| WDT | `CONFIG_MEDIATEK_WATCHDOG=y`，`mediatek_timer_init` 早停 LK 喂的狗(`mediatek.c:35-58`)，init 里再喂 `/dev/watchdog` 兜底 | `.config` + `mediatek.c` |
| 模块支持 | `CONFIG_MODULES=y` `MODULE_UNLOAD=y` **无 CONFIG_MODULE_SIG**（无需签名），`CONFIG_RD_{GZIP,XZ,...}` 全 =y，`CONFIG_EXT4_FS=y` `CONFIG_MMC_MTK=y` | `.config` |
| 分区名 | GPT 用 MTK 名: **bootimg / recovery / android / cache / usrdata**；android = system = mmcblk0p4 | `flash_partition.sh` 注释 + `PLAN.md §1.3` |

无害告警（不用管）: boot log 有 `mmcblk0: p6 size 4292549119 extends beyond EOD`——p6(usrdata) 的 GPT size 字段超出 eMMC 实际容量，**不要用 p6**；与 p4 无关。

关键点：**当前 initramfs 的 /init 已在做 "getty + 喂狗 + 起 USB ACM"**，但没有 p4 挂载。过渡到 Alpine 只需把 /init 改成 "挂 p4 + switch_root"，其余(喂狗/getty)可在 switch 前保留。

---

## 2. 方案 A vs 方案 B 评估

### 2.1 方案 A — initramfs 挂 p4 + switch_root

```
boot.img: [kernel+appended-dtb][initramfs(最小 busybox)]
cmdline:  console=ttyS0,115200n8 root=/dev/ram rdinit=/init   ← 不动
/init:    mount proc/sys → 喂狗 → 等 /dev/mmcblk0p4 → mount p4 /newroot
          → mount --bind /proc /sys /dev → exec switch_root /newroot /sbin/init
```

**优点**
- **已验证模式**：`out/boot_alpine_auto.img` 的 /init 就是"挂 p4 + switch_root /sbin/init"，本报告已解包核对(§3.2 引全文)。
- **cmdline 零改动**：内核仍 `root=/dev/ram rdinit=/init`，不碰 `root=`/`rootfstype=`/`rootwait`，`z1_make_bootimg.sh` 硬编码的 cmdline 直接复用。
- **有兜底**：p4 挂载失败 → 落恢复 shell(现 /init 就是这个逻辑)，不会 panic。
- **可保留喂狗 + USB 节点**：进 Alpine 前 WDT 持续喂、ttyGS0 已建，Alpine 起来即有 console。
- **6MB 压力小**：最小 initramfs 只需静态 busybox(switch_root+mount+sh) ≈ 1.5MB；kernel 3.78MB + ramdisk 余量充足。
- **不动 eMMC probe 时序**：`/init` 里等 `/dev/mmcblk0p4`(实测 mmc0 probe ~8s)，比内核 mount_root 的 wait_for_device_probe 更可控。

**缺点**
- initramfs 里要有一个能 switch_root 的 busybox(现 ramdisk 已有 `bin/switch_root`，可直接复用)。
- 多一步 "挂载失败兜底" 逻辑(简单 shell)。

### 2.2 方案 B — cmdline `root=/dev/mmcblk0p4` 直挂

```
boot.img: [kernel+appended-dtb][无 /init 或空 ramdisk]
cmdline:  console=ttyS0,115200n8 root=/dev/mmcblk0p4 rootwait rootfstype=ext4 panic=0 ...
```

**优点**
- 内核 `prepare_namespace → mount_root` 直挂 p4，语义最标准。
- initramfs 可缩到空，6MB 余量更大。

**缺点 / 坑(对本 Z1 现实)**
1. **坑① (最关键): rdinit 抢占**。`fork_linux/init/main.c:1707-1713`——只要 `ramdisk_execute_command`(默认 `/init`，`rdinit=` 可覆盖)存在且 `init_eaccess()` 可执行，内核就**跳过 `prepare_namespace()`**，直接跑 initramfs 的 `/init` 作 PID1(实测当前 /init 打印 `[init] uname:` 即为这条路径)。**因此只改 `root=` 无效**，必须满足其一才走 mount_root:
   - 打包时不带 ramdisk(`ramdisk_size=0`，`boot_mainline_konly.img` 先例 rsz=1 近空，但 mainline 下未验证过 LK 行为)；
   - 或 initramfs 里删掉/改名 `/init`；
   - 或 cmdline 加 `rdinit=/nonexistent`(eaccess 失败 → fallback)。
2. **坑② eMMC probe 时序**：mmc0 probe 实测 ~8s(`[8.02] mmc0: new MMC card`)。内核 `mount_root` 前会 `wait_for_device_probe()`(do_mounts.c)，但 MMC_MTK 是否 async 未确认；加 `rootwait` 可强制等待，但那是"等多久不知道"的盲等。若 root 挂载 panic → bootloop。
3. **丢失进 Alpine 前的喂狗窗口**：mount_root 阶段无用户态喂狗。`mediatek_timer_init` 已早停狗 + `mtk_wdt` probe 再启，但 root 挂载失败就无恢复 shell。
4. **无恢复路径**：p4 挂载失败 → kernel panic → bootloop，必须重新刷 boot.img 回滚。

### 2.3 6MB 限制下哪个更优

两方案都远小于 6MB 上限。**方案 A 更优**：可靠性(兜底 shell、可喂狗)、改动面小(cmdline/打包脚本零改动)、模式已验证(3.4.67 时代 `boot_alpine_auto.img` 同款逻辑)。方案 B 的"干净"收益在内核稳定 boot 之后才值得。

**建议**: 以 A 为主路线；B 作为 A 稳定后的可选项(届时把 boot.img 里 initramfs 缩到只有 switch_root，或验证空 ramdisk 的 LK 行为)。

---

## 3. 当前 initramfs → Alpine 过渡改造

### 3.1 现状 /init (解包 `ramdisk_mainline_connsys.gz`: 512B 头 + XZ cpio body)

`/tmp/ramdisk_connsys/init` 已读，关键内容:
```sh
busybox mount -t proc proc /proc
busybox mount -t sysfs sysfs /sys
busybox mount -t tmpfs tmpfs /dev
busybox mdev -s
# android_usb 切 ACM 段 (mainline 无 android_usb, [ -d ] 判空静默跳过)
setsid getty -n -l /bin/sh 115200 ttyGS0 vt100 &   # USB ACM (mainline 由 g_serial 提供 ttyGS0)
setsid getty -n -l /bin/sh 0 tty0 vt100 &           # framebuffer
( watchdog -t 5 -T 10 /dev/watchdog || echo V > /dev/watchdog ) &   # WDT 兜底
exec getty -n -l /bin/sh 0 console vt100            # 落 shell
```
→ **无 p4 挂载、无 switch_root**。busybox 有 `switch_root`/`switch_root_setsid` applet(已核实)。

### 3.2 方案 A 的 /init 改造点 (对 auto 模板 + mainline 差异)

现成模板 `out/boot_alpine_auto.img` 解包出的 `/init`(已读原文，核心逻辑如下):
```sh
#!/bin/busybox sh
BB=/bin/busybox
$BB mount -t proc proc /proc
$BB mount -t sysfs sysfs /sys
$BB mount -t tmpfs tmpfs /dev
$BB mdev -s
# android_usb 切 ACM (mainline 段可整段删)
for i in 1 2 3 4 5; do [ -b /dev/mmcblk0p4 ] && break; sleep 1; done
$BB mkdir -p /newroot
$BB mount -t ext4 /dev/mmcblk0p4 /newroot || { echo "mount fail, shell"; exec $BB sh; }
$BB mount --bind /proc /newroot/proc
$BB mount --bind /sys  /newroot/sys
$BB mount --bind /dev  /newroot/dev
exec $BB switch_root /newroot /sbin/init
```

**相对 auto 模板的 mainline 差异 / 建议**:
1. `/dev` 用 `devtmpfs`(`mount -t devtmpfs devtmpfs /dev`；内核 `CONFIG_DEVTMPFS_MOUNT=y` 会自动挂)——mmcblk0p4 以真实节点出现；保留 `mdev -s` 兜底。**不要再用 `tmpfs`**，否则 devtmpfs 节点被 tmpfs 盖掉。
2. 等待循环拉长到 10s(实测 mmc0 probe ~8s)；等满仍无则落 shell。
3. **挂载用 `rw`**(auto 模板是 `-o ro`，只适合首次验证)。`system_alpine.img` 是 mke2fs 干净镜像，rw 挂载安全；若担心脏挂，可先 `ro` 挂 + `e2fsck -f /dev/mmcblk0p4` 再 remount rw。
4. 保留喂狗行(进 Alpine 前 WDT 持续喂)；`android_usb` 段整段删(mainline 无此 sysfs)。
5. (可选)若不想改 Alpine 镜像的 fstab，可在 switch_root 前建 `/dev/block` 符号链接(见 §3.4 备选)。
6. 串口 getty 目标改 **ttyS0**(§3.4 ②)。

**打包**: 复用 `tools/z1_make_bootimg.sh` + `tools/repack_ramdisk_connsys.sh` 流程。新 initramfs 仍为 **512B MTK ROOTFS 头(`\x88\x16\x88\x58`) + XZ cpio**(`xz -9 --check=crc32`，内核 `CONFIG_RD_XZ=y`)，头 offset 4 写 body 大小 LE u32，**dts `linux,initrd-end` 必须同步为 `0x84100000 + body_size`** 并重编 dtb(repack 脚本已自动做这步；打包后跑 `tools/validate_mtk_boot.py` 复核)。

### 3.3 为什么方案 A 不用改内核 cmdline

`init/main.c` 语义(已核对):
- `ramdisk_execute_command` 默认 `/init`(`rdinit=` 可覆盖)，行 1707-1712: 若 `/init` 存在且可执行(`init_eaccess()` 返回 0)，置空 `ramdisk_execute_command` 并调 `prepare_namespace()` 的 fallback **不会发生**。
- 行 1610-1617: 直接 `run_init_process("/init")` 作 PID1。

所以 `root=/dev/ram` 只是占位，真正挂 root 的活交给 /init 的 switch_root。boot.img header 与 dts bootargs 两处 cmdline 都可保持不动。

### 3.4 Alpine rootfs 必改 2 项 (否则起不来/登不上)

从 `out/alpine_rootfs/etc/` 实测(mount 核对 `system_alpine.img` 一致):

**① `/etc/fstab` — 设备节点名错**
```
# 现状 (错):                    # 改法1: 直接改镜像          # 改法2 (免重刷 p4): initramfs 建软链
/dev/block/mmcblk0p4  /         /dev/mmcblk0p4  /            mkdir -p /dev/block
/dev/block/mmcblk0p6  /mnt/data /dev/mmcblk0p6  /mnt/data    ln -s ../mmcblk0p4 /dev/block/mmcblk0p4
/dev/block/mmcblk0p5  /mnt/cache /dev/mmcblk0p5  /mnt/cache   ln -s ../mmcblk0p6 /dev/block/mmcblk0p6
```
mainline devtmpfs 生成 `/dev/mmcblk0p4`，无 `/dev/block` 目录。root 由 initramfs 挂好再 switch，fstab 的 `/` 行影响 OpenRC `localmount`/remount；p5/p6 不改则挂载失败(不影响启动，但 `/mnt/data` 不可用)。**改法2 最省事**: 在 initramfs `/init` 的 switch_root 前建软链，因 `/dev` 会被 bind 进 Alpine，fstab 原样即可，**不用重刷 p4**。

**② `/etc/inittab` — 补 ttyS0 串口 getty**
```
# 现状: ttyGS0::respawn:... + tty0::respawn:...
# 追加:
ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100
```
mainline 串口 console = `ttyS0` @ 115200(实测 boot log `11005000.serial: <SERIAL> ACM，g_serial)/tty0(framebuffer) 保留。这一项**必须改**，否则 C3 桥 UART 上没登录提示。

> 注意: 改的是 `out/alpine_rootfs/`(WORK 目录)或重打包 `system_alpine.img` 后重刷 p4。改文件前先备份(`bridge_backups/`)。

---

## 4. 非必要驱动 (CONNSYS .ko) 部署到 Alpine

### 4.1 结论: 直接可用，vermagic 精确匹配，无需签名

运行内核 vermagic = `7.0.0-rc7-z1+`，CONNSYS 全套 12 个 .ko 也是 `7.0.0-rc7-z1+`。内核 `CONFIG_MODULE_SIG` 未开，**无签名/vermagic 之外的门禁**，insmod 可直接过。`mtk_stp_launcher`/`iw` 是**静态 ARM**，Alpine 下直接跑。

### 4.2 vermagic 实测表 (12 个 .ko，全部 = `7.0.0-rc7-z1+ SMP mod_unload ARMv7 p2v8`)

| .ko | 用途 | 依赖 |
|---|---|---|
| cfg80211.ko | WiFi 上层 (nl80211) | — |
| mtk_btif_drv.ko | BTIF 传输 (PIO+APDMA) | — |
| mtk_stp_wmt_soc.ko | WMT/STP 控制面 + 固件下载 | mtk_btif_drv |
| mtk_stp_bt_soc.ko | BT 通道 /dev/stpbt | mtk_stp_wmt_soc |
| mtk_wmt_wifi_soc.ko | WiFi func ctrl /dev/wmtWifi | mtk_stp_wmt_soc |
| wlan_gen2.ko | WiFi 主驱动 (wlan0) | cfg80211, mtk_stp_wmt_soc, mtk_wmt_wifi_soc |
| bluetooth.ko | BT 栈 | ecdh_generic, libaes |
| ecc.ko | BT ECC | — |
| ecdh_generic.ko | BT ECDH | ecc |
| hci_vhci.ko | BT vhci 桥 | — |
| hidp.ko | BT HID | bluetooth |
| uhid.ko | BT UHID | — |

源目录: `bridge_backups/products/products_connsys_modules_z1vermagic_20260814_015009/`(btif/ + conn_soc/ + wlan/ + cfg80211.ko + mtk_stp_launcher)，BT 栈 `products_bt_stack_20260814_204345/ko`。上板已在用的副本就是当前 connsys ramdisk 里的 `/root/connsys/`(12 .ko 全在)。

### 4.3 部署方式 (推荐: 方式一，直接用现成脚本)

**方式一 (推荐，不需要 modules.dep)**
1. Alpine rootfs 建 `/lib/modules/7.0.0-rc7-z1+/`，拷入 12 个 .ko + `mtk_stp_launcher` + `z1_modprobe_connsys.sh`(脚本用 `insmod 绝对路径`，不依赖 modules.dep；busybox/静态 binary 均可跑)。
2. 加 OpenRC 服务 `/etc/init.d/connsys`(或 `/etc/local.d/connsys.start` + `rc-update add local default`)，内容 `sh /lib/modules/7.0.0-rc7-z1+/z1_modprobe_connsys.sh`。
3. **固件目录必须一并部署**(脚本按绝对路径读，缺则 insmod 失败——上板实测 WMT_SOC.cfg 缺 → `mtk_stp_wmt_soc` insmod 失败):
   ```
   /lib/firmware/WIFI_RAM_CODE_MT6582        ← out/firmware_backup/firmware/WIFI_RAM_CODE_SOC (160464B)
   /system/etc/firmware/WMT_SOC.cfg          ← out/firmware_backup/firmware/WMT_SOC.cfg (80B)
   /system/etc/firmware/ROMv1_patch_0_hdr.bin ← out/firmware_backup/firmware/mt6572_82_patch_e1_0_hdr.bin
   /system/etc/firmware/ROMv1_patch_1_hdr.bin ← out/firmware_backup/firmware/mt6572_82_patch_e1_1_hdr.bin
   /etc/firmware/nvram/WIFI                  ← mainline_recon/wifi-nvram-WIFI.bin (512B, MAC <MAC>)
   ```
   这些目录 Alpine rootfs 目前**都不存在**，需在 rootfs 里建好。launcher 参数固定 `-m 3 -p /system/etc/firmware/`(脚本里写死)。
4. 与当前上板验证链路(wifi scan 已成功，`WIFI_FUNC_ON_SUCCESS_20260814.md`)完全一致，改动最小。

**方式二 (标准 distro 布局)**
- 建 `/lib/modules/7.0.0-rc7-z1+/` 放 .ko，Alpine 内跑 `depmod`(busybox 有 depmod applet)生成 modules.dep 后 `modprobe`。
- 更"正规"，但 MTK 驱动依赖顺序特殊(btif→wmt→bt→cfg80211→wlan_gen2)，depmod 依赖解析不保证按序，**仍建议方式一的脚本控顺序**。

### 4.4 vermagic 漂移纪律 (CLAUDE.md §5.7 生效)

- `.config` 已固定 `CONFIG_LOCALVERSION="-z1"` + `LOCALVERSION_AUTO=n` → 内核重编(改 config/dts)不漂 vermagic，.ko 不会因重编失效。
- 绝不在 make 命令行设 `LOCALVERSION=`(会消掉 `+` 使 .ko 失配)。
- 若改了 .ko 依赖的内核 ABI(函数签名/结构体/导出符号)，必须重编对应 .ko。
- 部署到 Alpine 后，insmod 前先 `modinfo wlan_gen2.ko | grep vermagic` 复核运行内核 `cat /proc/version`。

---

## 5. 上板验证步骤（全程只读 + 刷写时才插 download 线）

> 前提: ① 按 §3.4 处理 Alpine rootfs(改 fstab/inittab 或走软链方案)；② 用户插 download 线起 mtkclient。

1. **刷 p4 (system, 分区名 `android`)**:
   ```
   tools/flash_partition.sh android out/system_alpine.img     # 850MiB ≤ 900MiB ✓；.bak 带 64bit 勿刷
   ```
2. **打包并刷 boot.img (方案A initramfs)**:
   ```
   tools/z1_make_bootimg.sh fork_linux/arch/arm/boot/zImage \
     fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dtb \
     out/ramdisk_alpine_switch.gz out/boot_mainline_alpine.img
   tools/flash_with_retry.sh bootimg out/boot_mainline_alpine.img ~/claude/6572/1/boot.img 8
   ```
   (z1_make_bootimg.sh 已自动: cat 拼 dtb → mkbootimg --mtk 1；ramdisk 换成 switch_root 版后 initrd-end 同步由 repack 流程负责)
3. **拔 download 线 + 电池开机，抓 log**:
   ```
   ./z1_capture_mainline.sh 600
   ```
   期待顺序: `earlycon/Linux version` → `11005000.serial: <SERIAL> → `mmc0: new MMC card` → initramfs `[init] mount p4` → `switch_root` → Alpine busybox init `Welcome to Alpine Linux` → `ttyS0` getty 提示。
4. **登录并验驱动**:
   - ttyS0 getty 登录 (115200，C3 桥)；或 USB ACM ttyGS0。
   - `mount | grep /` 应见 `/dev/mmcblk0p4 on / type ext4`。
   - `ls /lib/modules/7.0.0-rc7-z1+/` 看 .ko。
   - `sh /lib/modules/7.0.0-rc7-z1+/z1_modprobe_connsys.sh` 或 `rc-service connsys start` → `echo 1 > /dev/wmtWifi` → `iw dev wlan0 scan` 复现 scan(对照 `WIFI_SCAN_SUCCESS_20260814.md` 的 8 AP)。
5. **回归**: 保留原 `boot_mainline_connsys.img` 与 `1/boot.img` 回滚；bootloop 时先核对 `mainline_recon/z1_driver_supervisor_loop.log`。

### 风险清单
| 风险 | 概率 | 缓解 |
|---|---|---|
| initrd-end 不同步 → initramfs 截断 VFS panic | 中 | repack 脚本自动同步 dts `linux,initrd-end`；打包后 `validate_mtk_boot.py` 复核 |
| eMMC p4 probe 慢于等待循环 | 低 | initramfs 循环等 10s(实测 ~8s) |
| Alpine fstab `/dev/block/` 未处理 → 挂载失败 | 高(若漏) | §3.4① 改 fstab 或 initramfs 建 /dev/block 软链 |
| inittab 无 ttyS0 getty → C3 桥登不上 | 高(若漏) | §3.4② 必加 |
| WDT 在 switch 窗口咬死 | 低 | init 持续喂狗 + mtk_wdt probe 接管 + mediatek_timer_init 早停 |
| cfg80211 `regulatory.db` 缺失告警 | 低 | scan 不受影响(已验证)；Alpine 可 `apk add wireless-regdb` 或接受 WARN |
| p6 GPT size 越界告警 | 无 | 不碰 p6，仅告警 |
| 误刷 `system_alpine.bak`(64bit ext4) | 低 | 只刷 `system_alpine.img`(无 64bit) |

---

## 6. 材料/命令速查

| 文件 | 路径 |
|---|---|
| Alpine rootfs WORK | `out/alpine_rootfs/` (改 fstab/inittab/模块的源) |
| Alpine 镜像 | `out/system_alpine.img` (850MiB, 无 64bit, 可刷) / `.bak`(64bit, 勿刷) |
| boot auto 模板 init | `out/boot_alpine_auto.img` 解包 `/init` (挂 p4 + switch_root) |
| 当前 initramfs | `out/ramdisk_mainline_connsys.gz` (512B ROOTFS 头 + XZ, 含 12 .ko + 固件 + launcher + iw) |
| CONNSYS 模块源 | `bridge_backups/products/products_connsys_modules_z1vermagic_20260814_015009/` |
| BT 栈源 | `bridge_backups/products/products_bt_stack_20260814_204345/` |
| 固件源 | `out/firmware_backup/firmware/` (WMT_SOC.cfg / ROMv1_patch_* / WIFI_RAM_CODE_SOC) |
| NVRAM | `mainline_recon/wifi-nvram-WIFI.bin` (512B) |
| 加载脚本 | `scripts/z1_modprobe_connsys.sh` (12 .ko insmod 顺序 + launcher) |
| 打包脚本 | `tools/z1_make_bootimg.sh` / `tools/repack_ramdisk_connsys.sh` / `tools/validate_mtk_boot.py` |
| 分区刷写 | `tools/flash_partition.sh` (android→p4) / `tools/flash_with_retry.sh` (bootimg) |
| 回滚 | `~/claude/6572/1/boot.img` (原厂) / `out/boot_mainline_connsys.img` (当前验证版) |

### 关键代码位置
- initrd/init 分支: `fork_linux/init/main.c:1610-1617` (run_init_process(rdinit)) + `:1707-1712` (init_eaccess 决定是否 prepare_namespace)
- root 挂载: `fork_linux/init/do_mounts.c:480-485` (initrd_load + mount_root)
- WDT 早停: `fork_linux/arch/arm/mach-mediatek/mediatek.c:35-58`
- dts initrd 地址: `fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts:41-42`
