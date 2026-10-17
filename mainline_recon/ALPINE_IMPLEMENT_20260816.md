# Alpine 发行版启动实施 — initramfs /init 改造 + repack 注入 + 上板验证

> 2026-08-16。**只写脚本/分析，未碰硬件、未刷写、未改源树**（唯一落盘产物：
> `scripts/z1_alpine_switch_init.sh` 参考 /init + 本报告；repack 注入仅给出待粘贴代码块）。
> 前置文档: `mainline_recon/ALPINE_BOOT_PATH_20260815.md`（方案 A 详解）。
> 本文所有"已核对"事实均来自本地解包/实测，证据在 §1/§2。

---

## 0. 结论摘要 (TL;DR)

1. **`out/boot_alpine_auto.img` 的 /init 确认可用作模板**（挂 p4 + switch_root /sbin/init，busybox 静态 ARM 有 switch_root applet）。它与 mainline 的差异只有 3 处：/dev 用 tmpfs（mainline 必须改 devtmpfs）、等待只有 5s（mainline mmc probe ~8s 要拉到 30s）、挂 `-o ro`（应 rw）。见 §1.3。
2. **新 /init 已写好并过语法/打包干跑验证**: `scripts/z1_alpine_switch_init.sh`。核心：mount proc/sys/dev（devtmpfs，不盖内核自动挂的）→ 常驻喂狗 → （默认关）switch 前 ttyGS0 getty → 等 mmcblk0p4 30s → 建 /dev/block 软链 → mount p4 /newroot (ext4 rw) → bind proc/sys/dev → `exec switch_root -c /dev/ttyS0 /newroot /sbin/init`；失败兜底落恢复 shell（ttyS0/ttyGS0/tty0，与原一致）。见 §2。
3. **repack 注入 = 在 `tools/repack_ramdisk_connsys.sh` 加一个 `Z1_ALPINE_SWITCH=1` 段**：替换 /init、剪掉 connsys/iw/固件、跳过节食注入；输出 `out/ramdisk_alpine_switch.gz`。initrd-end 同步/dtb 重编/512B MTK 头 body-size 更新走既有 4-7 段，零改动。干跑证明：ramdisk 0.89MiB，boot.img 4.50MiB ≤ 6MiB，`validate_mtk_boot.py` 逻辑全兼容。见 §3 + §4。
4. **上板验证 3 步**（刷 p4 android + 刷 bootimg + 抓 log），期待 log 序列见 §5.3。

---

## 1. `out/boot_alpine_auto.img` 模板核对 (已完成)

### 1.1 解包（本地，未碰板子）

- boot.img: `ANDROID!`，ksz=0x32ec48，rsz=0x1051d6，pagesize=2048，dt_size=0。
  cmdline=`console=tty0 console=ttyMT1,921600n1 rdinit=/init androidboot.selinux=permissive`（3.4.67 时代，ttyMT1 921600）。
- ramdisk = **512B MTK ROOTFS 头 + 5B 填充(`ff ff ff ff ff`) + gzip cpio**。
  ⚠️ 注意这个旧图的头 magic 是 `88 88 16 58`（LE u32=0x58168888），**与现行 mainline 的 `88 16 88 58`(0x58881688) 不同**，且 body 前多 5 字节填充。
  **别照抄它的原始头**——新 ramdisk 一律按现行 `repack_ramdisk_connsys.sh` 的格式：`88 16 88 58` + body 直接 gzip/xz（无填充），这是已上板验证过的格式。
- busybox: 静态 ARM ELF，1,533,764B，含 `switch_root`/`mount`/`mdev`/`getty`/`watchdog` applet ✅。

### 1.2 模板 /init 原文（关键逻辑）

```sh
#!/bin/busybox sh
BB=/bin/busybox
$BB mount -t proc proc /proc 2>/dev/null
$BB mount -t sysfs sysfs /sys 2>/dev/null
$BB mount -t tmpfs tmpfs /dev 2>/dev/null
$BB mdev -s 2>/dev/null
# android_usb 切 ACM 段 (mainline 无 android_usb, [ -d ] 判空静默跳过)
for i in 1 2 3 4 5; do [ -b /dev/mmcblk0p4 ] && break; sleep 1; done   # ← 只等 5s
$BB mkdir -p /newroot
$BB mount -t ext4 -o ro /dev/mmcblk0p4 /newroot || { echo "mount fail, shell"; exec $BB sh; }  # ← ro
$BB mount --bind /proc /newroot/proc
$BB mount --bind /sys  /newroot/sys
$BB mount --bind /dev  /newroot/dev
exec $BB switch_root /newroot /sbin/init
```

### 1.3 结论：可用，但相对 mainline 有 3 处必须改

| 模板现状 | mainline 差异 | 新 /init 处理 |
|---|---|---|
| /dev 用 `tmpfs` + mdev | 内核 `CONFIG_DEVTMPFS_MOUNT=y` 在 /init 前已把 devtmpfs 挂上 /dev；tmpfs 会**盖掉** devtmpfs 节点，mmcblk0p4 只有再跑 mdev -s 才出现 | 用 devtmpfs（`mount -t devtmpfs devtmpfs /dev`，已挂载时是 EBUSY 空操作），mdev -s 兜底 |
| 等 p4 只 5s | mmc0 probe 实测 ~8s（boot log `mmc0: new MMC card` @8.0s） | 循环等 30s |
| `-o ro` 挂 p4 | `system_alpine.img` 是 mke2fs 干净镜像 | `-o rw` |

另外两点增强（非模板项）：
- **/dev/block 软链**（§2.4）：Alpine fstab 用 `/dev/block/mmcblk0pN`，mainline devtmpfs 给 `/dev/mmcblk0pN`。initramfs 建软链 → fstab 原样可用，**免重刷 p4**。
- **`switch_root -c /dev/ttyS0`**（§2.6）：switch 后 stdio 重定向到串口，Alpine init/OpenRC 输出 + 登录提示直接上 C3 桥。busybox 1.36.1 的 switch_root 支持 `-c`（已核对 help）。

---

## 2. 新 initramfs /init（方案 A）

### 2.1 文件

`/home/<user>/claude/6572/scripts/z1_alpine_switch_init.sh`（整体拷入 initramfs 的 `/init`，chmod 0755）。
已过 `busybox sh -n` 语法检查 ✅；已按 repack 同款流程干跑打包→解包验证（§4.4）✅。

### 2.2 逻辑流程（文件内注释同源）

```
#!/bin/busybox sh
1. mount proc/sys；/dev 用 devtmpfs（EBUSY 容忍）＋ mdev -s 兜底
2. [GETTY_BEFORE_SWITCH=1 时] 后台 ttyGS0 getty 等待循环（默认 0，见 §2.3）
3. 常驻喂狗：busybox watchdog -t 5 -T 10（后台）或 echo V 循环
4. 等 /dev/mmcblk0p4 最多 30s（每轮 mdev -s + sleep 1）
5. 失败兜底函数 recovery_shell()（ttyS0 exec + ttyGS0 循环 + tty0）
6. 建 /dev/block/mmcblk0p{4,5,6} 软链（Alpine fstab 免改）
7. mount -t ext4 -o rw /dev/mmcblk0p4 /newroot；失败 → recovery_shell()
   [ ! -x /newroot/sbin/init ] → recovery_shell()          （防 p4 不是 Alpine）
8. mkdir /newroot/{proc,sys,dev}；mount --bind proc/sys/dev
9. exec $BB switch_root -c /dev/ttyS0 /newroot /sbin/init
```

### 2.3 关键设计决策（为什么要这么做）

1. **/dev 用 devtmpfs，不用 tmpfs**。内核 `CONFIG_DEVTMPFS_MOUNT=y` 时 devtmpfs 在 subsys_initcall（早于 initramfs 解包）已挂 /dev，mmcblk0p4/ttyGS0/watchdog 节点由内核随设备注册自动生成。老 init 的 `tmpfs /dev` 会把这层藏掉，逼着每轮 mdev -s。新 init 直接不盖它，等待循环只需 `[ -b /dev/mmcblk0p4 ]` 就能看到真实节点。
2. **GETTY_BEFORE_SWITCH 默认 0**。Alpine 自己的 `/etc/inittab` 已有 `ttyGS0::respawn:/sbin/getty -L 115200 ttyGS0 vt100`，switch 后马上会再起一个；switch 前先起会与它抢 termios——正是 connsys init 修过的"双 getty 竞态"（`>` 续行 bug）。要跨 switch 不间断 USB shell 才置 1。
3. **喂狗保留到 switch**。LK 武装的 HW WDT 已被 `mediatek_timer_init` 早停，但任何驱动在 Alpine init 接管前重新武装就会咬人（~19.7s/~30.5s）。busybox `watchdog` 是常驻守护进程（fork 后台），switch_root 后若还活着继续喂；死了也无妨（早停的狗不会自己武装）。
4. **`-o rw` 挂 p4**。`system_alpine.img` 是 mke2fs 干净 ext4。initramfs busybox 无 fsck.ext4，脏 ext4 时 init 内无法 fsck；恢复 shell 里可 `mount -o ro` 抢救。
5. **switch 前建 `/dev/block` 软链**：`/dev` 会被 bind 进 Alpine，软链随 devtmpfs 一起进，fstab 的 `/dev/block/mmcblk0p4` 原样可挂 → **不用重刷 p4**（省 850MiB 重刷）。
6. **`switch_root -c /dev/ttyS0`**：把 PID1 stdio 重定向到串口（C3 桥），确保 Alpine init/OpenRC 日志可见；`/dev/ttyS0` 在 bind 进 Alpine 的 devtmpfs 里存在（串口驱动已 probe，boot log 有 `11005000.serial: <SERIAL>

### 2.4 失败兜底（与现 connsys init 的恢复 shell 完全一致）

- **触发点**：p4 节点 30s 超时 / ext4 挂载失败 / `/newroot/sbin/init` 不可执行。
- **行为**：ttyGS0 等待循环 getty（USB ACM）+ tty0 getty（屏幕）+ `exec getty -L -n -l /bin/sh 115200 ttyS0 vt100`（C3 桥串口，主入口）。
- 喂狗在第 3 段已常驻，兜底路径不重复起。

### 2.5 关于 switch_root 与后台进程

busybox switch_root 假定自身是唯一进程（PID 1），**不主动 kill** 旧 root 的后台进程；它 `chroot + move NEW_ROOT 到 / + 删除旧 / 内容`。喂狗守护进程（open /dev/watchdog fd）在 switch 后仍持有效 fd，继续喂——这正是我们想要的；残留为"孤儿进程"无害。GETTY_BEFORE_SWITCH=1 的 getty 同理存活（所以默认 0 更干净）。

### 2.6 cpio uid/gid 说明（已验证，非阻塞）

干跑实测：`cpio -idmu` 以非 root 提取会把所有条目存成 uid=1000（内核 `initramfs.c` 解包**忽略** cpio 的 uid/gid，一律 root），现有 `ramdisk_mainline_connsys.gz` 的 /init 也是 uid 1000 且上板正常。若要让 `validate_mtk_boot.py --require-init-root` 通过，可在 repack 的 cpio 命令加 `--owner=0:0`（GNU cpio 2.15 已实测支持，产出 uid/gid=0）。**功能上非必须**。

---

## 3. repack 注入方案（Z1_ALPINE_SWITCH=1 段）

### 3.0 思路

复用 `tools/repack_ramdisk_connsys.sh` 的成熟骨架（拆 512B MTK 头 → 解 body → 塞内容 → 重打 cpio+xz → 回写头 body-size → **同步 dts initrd-end + 重编 dtb** → 校验）。Alpine 版只换两件事：
1. `/init` 换成 §2 的 switch_root 版；
2. **不打包** connsys 模块/iw/固件/adb/BT（这些部署到 Alpine rootfs，见 `ALPINE_BOOT_PATH_20260815.md` §4），ramdisk 保持最小。

base ramdisk 用默认 `out/forensic/linux/ramdisk.gz`（512B MTK 头 + gzip cpio，busybox 1.36.1 静态，switch_root/mount/mdev/getty/watchdog 全有），与既有默认一致，无需另找。

### 3.1 对 `tools/repack_ramdisk_connsys.sh` 的改动（4 处）

**① 顶部 OUT 默认值**（Z1_ALPINE_SWITCH=1 时输出专用名）：
```bash
OUT=${Z1_RAMDISK_OUT:-/home/<user>/claude/6572/out/ramdisk_mainline_connsys.gz}
if [ "${Z1_ALPINE_SWITCH:-0}" = "1" ] && [ -z "${Z1_RAMDISK_OUT:-}" ]; then
    OUT=/home/<user>/claude/6572/out/ramdisk_alpine_switch.gz
fi
```

**② 跳过节食注入**（2.5 喂狗 / 2.6 getty fix 的 python 注入对替换后的 /init 无意义，还浪费）：
```bash
if [ "${Z1_WATCHDOG_FEED:-0}" = "1" ] && [ "${Z1_ALPINE_SWITCH:-0}" != "1" ]; then
if [ "${Z1_GETTY_FIX:-0}" = "1" ] && [ "${Z1_ALPINE_SWITCH:-0}" != "1" ]; then
```

**③ 新增 2.7 段**（插在 2.6 之后、第 3 段"塞 CONNSYS 模块"之前）：
```bash
# 2.7 Alpine switch init (Z1_ALPINE_SWITCH=1)
#   用 scripts/z1_alpine_switch_init.sh 整体替换 /init (方案A: 挂 p4 + switch_root)
#   该脚本自带喂狗/恢复 shell，不再需要 2.5/2.6 注入。
if [ "${Z1_ALPINE_SWITCH:-0}" = "1" ]; then
    ALPINE_INIT=${Z1_ALPINE_INIT:-/home/<user>/claude/6572/scripts/z1_alpine_switch_init.sh}
    if [ ! -f "$ALPINE_INIT" ]; then
        echo "ERROR: Z1_ALPINE_SWITCH=1 但缺 $ALPINE_INIT"; exit 1
    fi
    cp "$ALPINE_INIT" "$TMP/rd/init"
    chmod 0755 "$TMP/rd/init"
    echo "[repack] Alpine switch /init installed (from $ALPINE_INIT)"
fi
```

**④ 跳过内容注入**（Alpine 版不需要 connsys 模块/固件/iw/launcher/adb/BT，剪掉保持最小；模块进 Alpine rootfs）：
把第 3 段（塞模块）、3.4b（ADB）、3.5（固件/iw/BT）整段包一层：
```bash
if [ "${Z1_ALPINE_SWITCH:-0}" != "1" ]; then
    ... (原 3 / 3.4b / 3.5 全部内容原样) ...
fi
```
（或偷懒：注入后 `rm -rf "$TMP/rd/root/connsys" "$TMP/rd/usr/bin/iw" "$TMP/rd/lib/firmware" "$TMP/rd/system/etc/firmware" "$TMP/rd/etc/firmware"`。推荐 gating，省得白拷。）

**其余零改动**：第 4 段重打 cpio + `xz -9 --check=crc32`、第 5 段更新 512B 头 body-size、第 7 段同步 `mt6572-z1.dts` 的 `linux,initrd-end` + 重编 dtb、第 6 段校验——Alpine ramdisk 尺寸不同，initrd-end 必须重同步，这段逻辑**原样生效**，绝不能跳过。

### 3.2 使用命令（PC 上，板子关机）

```bash
cd /home/<user>/claude/6572
# 1) 重建 alpine 版 ramdisk (base = out/forensic/linux/ramdisk.gz)
Z1_ALPINE_SWITCH=1 bash tools/repack_ramdisk_connsys.sh        # → out/ramdisk_alpine_switch.gz

# 2) 打包 boot.img (z1_make_bootimg.sh 内容校验会对缺固件/iw/connsys 告警, 非致命不退出)
tools/z1_make_bootimg.sh \
  fork_linux/arch/arm/boot/zImage \
  fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dtb \
  out/ramdisk_alpine_switch.gz \
  out/boot_mainline_alpine.img

# 3) 复核 (appended dtb + initrd-end + MTK ROOTFS 头 + ≤6MiB)
tools/validate_mtk_boot.py out/boot_mainline_alpine.img \
  --dtb fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dtb
```

### 3.3 备选：独立脚本

若不想动 connsys repack，可把 `tools/repack_ramdisk_connsys.sh` 复制为 `tools/repack_ramdisk_alpine_switch.sh`，删掉 2.5/2.6/3/3.4b/3.5，加 2.7，其余原样。两条路效果相同；本报告按任务要求以 `Z1_ALPINE_SWITCH=1` 段为主线。

### 3.4 干跑验证（已做，证明机制闭环）

在 `/tmp/alpine_switch_dryrun/` 完整复刻了 repack 流程（拆头→换 init→cpio+xz→回写 body-size）并回解验证：

| 项 | 结果 |
|---|---|
| 新 /init 装入 | `-rwxr-xr-x` 7554B，解包回读一致 ✅ |
| MTK 头 body-size 回写 | `0xe2834`(927796)，解包校验一致 ✅ |
| ramdisk 产物 | 928,308B (0.89MiB) |
| boot.img 预算 | kernel 0x39c04c(3.78MiB) + ramdisk 0.89MiB + 头 ≈ **4.50MiB ≤ 6MiB** ✅ |
| 压缩 | xz `--check=crc32`，内核 `CONFIG_RD_XZ=y` ✅ |
| cpio /init mode | 0755 ✅（uid 1000 不影响内核，见 §2.6） |
| switch_root -c 支持 | forensic busybox 1.36.1 help 确认 ✅ |

> 干跑未改 dts、未写 out/ 产物、未碰板子。

---

## 4. 前置：Alpine rootfs 必改项（沿用 ALPINE_BOOT_PATH §3.4，刷 p4 前做）

1. **`/etc/inittab` 补 ttyS0 getty**（必须，否则 C3 桥登不上）：
   ```
   ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100
   ```
   （ttyGS0/tty0 保留。）
2. **`/etc/fstab` 不用改**：`/dev/block/*` 软链由新 /init 建好带进 Alpine（§2.4）。若图省事也可直接改镜像 fstab 为 `/dev/mmcblk0p4`，二选一。
3. **CONNSYS 部署**（WiFi/BT，可选）：建 `/lib/modules/7.0.0-rc7-z1+/`，拷 12 .ko + 固件 + `scripts/z1_modprobe_connsys.sh`（insmod 绝对路径，不需要 modules.dep）。改动前备份 `out/alpine_rootfs/`。

---

## 5. 上板验证步骤（全程只读，刷写才插 download 线）

> 板子当前关机。以下第 1/2 步由用户起 mtkclient + 插 download 线后执行；第 3 步拔线电池开机。

### 5.1 刷 p4 (system, 分区名 `android`)
```bash
cd /home/<user>/claude/6572
tools/flash_partition.sh android out/system_alpine.img      # 850MiB ≤ 900MiB ✓; .bak(64bit)勿刷
```
> 若 §4 改了 `out/alpine_rootfs/`，需先重打 `system_alpine.img` 再刷。

### 5.2 打包并刷 boot.img
```bash
Z1_ALPINE_SWITCH=1 bash tools/repack_ramdisk_connsys.sh
tools/z1_make_bootimg.sh fork_linux/arch/arm/boot/zImage \
  fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dtb \
  out/ramdisk_alpine_switch.gz out/boot_mainline_alpine.img
tools/flash_with_retry.sh bootimg out/boot_mainline_alpine.img ~/claude/6572/1/boot.img 8
```

### 5.3 抓 log（拔 download 线 + 电池开机，C3 桥 USB-CDC 全程在）
```bash
./z1_capture_mainline.sh 600
```
**期待顺序**（对照 `z1_boot_mainline_20260815_204801.log` 已确认的前段）：
```
Kernel command line: ... console=ttyS0,115200n8 ... rdinit=/init
mmcblk0: p6 size 4292549119 extends beyond EOD, truncated   ← ~0.7s, 无害
[init] Alpine switch-root init starting
[init] kernel: Linux version 7.0.0-rc7-z1+ ...
[init] feeding watchdog /dev/watchdog (persistent loop)
[init] waiting for /dev/mmcblk0p4 (1s/30s)                  ← mmc0 probe ~8s, 约 7-8 行
[init] creating /dev/block symlinks for Alpine fstab
[init] mounting /dev/mmcblk0p4 (ext4 rw) -> /newroot
[init] switch_root /newroot /sbin/init ...
Welcome to Alpine Linux ...                                  ← busybox init/OpenRC
... ttyS0 login:                                            ← C3 桥登录提示 (需 §4 inittab)
```

### 5.4 登录验证
- C3 桥（ttyS0, 115200）登录，或 USB ACM ttyGS0。
- `mount | grep ' / '` → 应见 `/dev/mmcblk0p4 on / type ext4 (rw,...)`。
- `ls -l /dev/block/` → `mmcblk0p4`/`mmcblk0p5`/`mmcblk0p6` 软链存在。
- `cat /etc/inittab` → 有 ttyS0 getty；`cat /etc/fstab` → `/dev/block/...` 原样可用。
- WiFi/BT: `sh /lib/modules/7.0.0-rc7-z1+/z1_modprobe_connsys.sh` → `echo 1 > /dev/wmtWifi` → `iw dev wlan0 scan`。

### 5.5 失败判读
| log 现象 | 含义 | 处置 |
|---|---|---|
| switch_root 后一行都没有 | `-c /dev/ttyS0` 打不开或 Alpine init 崩 | 看是否 /dev/ttyS0 缺失（改用无 `-c` 版）；Alpine busybox init 缺 inittab 首行等 |
| `FATAL: /dev/mmcblk0p4 never appeared` | 本 boot eMMC probe 失败 | 落恢复 shell（ttyS0 getty 可用）；`dmesg` 查 mmc 错误 |
| `FATAL: mount ... failed` | p4 非 ext4 / 未刷入 | 落恢复 shell；核对 `flash_partition.sh android` 成功 |
| `FATAL: /newroot/sbin/init not executable` | p4 里不是 Alpine | 刷回 `system_alpine.img` |
| ~19.7s/~30.5s 周期复位 | 喂狗死在 switch_root 且 WDT 被重武装 | Alpine 侧加喂狗（local.d 起 busybox watchdog）；确认 `mediatek_timer_init` 早停 |
| p6 EOD 告警 | 已知无害 | 忽略 |

---

## 6. 风险清单

| 风险 | 概率 | 缓解 |
|---|---|---|
| initrd-end 未同步 → initramfs 截断 VFS panic | 中 | repack 第 7 段自动同步 dts + 重编 dtb；打包后 `validate_mtk_boot.py` 复核 |
| Alpine inittab 无 ttyS0 → C3 桥登不上 | 高(若漏) | §4 必加 |
| mmc probe > 30s | 低 | 实测 ~8s；等待循环已留 30s 余量；超时落恢复 shell 可手动查 |
| WDT 在 switch 窗口咬死 | 低 | 常驻喂狗 + `mediatek_timer_init` 早停 + mtk_wdt probe 接管 |
| 双 getty 竞态（若 GETTY_BEFORE_SWITCH=1） | 中(仅当置1) | 默认 0，交给 Alpine inittab |
| switch_root 后遗留 initramfs 进程 | 必然(无害) | 喂狗守护存活反而安全；孤儿进程不占资源冲突 |
| p4 脏 ext4 | 低 | system_alpine.img 干净；脏时恢复 shell 里 `mount -o ro` 抢救 |
| /dev/block 软链漏 → fstab 挂载失败 | 中(若漏) | 新 init 第 6 段已内置；上板后 `ls /dev/block/` 复核 |
| cpio uid=1000 | 无(内核忽略) | `--owner=0:0` 可选加 |

---

## 7. 文件 / 命令速查

| 项 | 路径 |
|---|---|
| 新 /init 参考脚本 | `scripts/z1_alpine_switch_init.sh`（本任务落盘产物） |
| 本报告 | `mainline_recon/ALPINE_IMPLEMENT_20260816.md` |
| 方案 A 前置分析 | `mainline_recon/ALPINE_BOOT_PATH_20260815.md` |
| auto 模板 | `out/boot_alpine_auto.img`（解包 /init 见 §1.2） |
| base ramdisk | `out/forensic/linux/ramdisk.gz`（512B MTK 头 + gzip cpio, busybox 1.36.1） |
| 现行 ramdisk | `out/ramdisk_mainline_connsys.gz`（xz, 含 connsys, 回滚用） |
| Alpine 镜像 | `out/system_alpine.img`（850MiB ext4, 刷 p4=android） |
| repack 注入 | `tools/repack_ramdisk_connsys.sh` 加 §3.1 的 4 处（待应用） |
| 打包 | `tools/z1_make_bootimg.sh`（cmdline `root=/dev/ram ... rdinit=/init` 不动） |
| 校验 | `tools/validate_mtk_boot.py --dtb ...` |
| 刷 p4 / boot | `tools/flash_partition.sh android ...` / `tools/flash_with_retry.sh bootimg ...` |
| 回滚 | `out/boot_mainline_connsys.img` / `~/claude/6572/1/boot.img` |
| 干跑验证 | `/tmp/alpine_switch_dryrun/`（临时目录，未留产物） |

### 关键约束提醒
- **cmdline 零改动**：`root=/dev/ram rdinit=/init` 保留（`init/main.c` 语义：/init 存在即被跑，switch_root 是 PID1 干的事）。boot.img header 与 dts bootargs 两处 cmdline 都不动。
- **vermagic 纪律**：本任务不重编内核，不改 .config，.ko vermagic 保持 `7.0.0-rc7-z1+` 不动。
- 改动 repack 脚本前先备份到 `bridge_backups/`（CLAUDE.md 规则）。
