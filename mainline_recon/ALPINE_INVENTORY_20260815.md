# Alpine 材料只读盘点 — Z1 (MT6572) mainline 移植项目

> 盘点时间: 2026-08-15。范围: `tools/make_alpine_rootfs.sh` + `out/` 下全部 Alpine 相关产物。
> 性质: 只读盘点, 未改动任何文件。

## 0. 结论摘要

- **Alpine rootfs 材料是完整、可用的**: `system_alpine.img`(850 MiB ext4, Alpine 3.24.0 + busybox + OpenRC + apk, getty 已配)已生成, 体积 ≤ system 分区 900 MiB, 可刷 `android`(mmcblk0p4)。
- **boot_alpine_v*.img 全部用的是原厂 3.4.67 内核**(6 个镜像内核 md5 全同 `ca4b08de...`, XZ zImage 3,337,288 B, 解压 10,191,300 B), **不是 mainline 内核**。它们属于早期"保留 3.4.67 + Alpine"路线, 与当前 mainline 移植路线不直接兼容。
- **但 boot_alpine_auto.img 的 initramfs 模式(挂 mmcblk0p4 + switch_root /sbin/init)可直接移植为 mainline 内核的 Alpine 启动 ramdisk** —— 这是本盘点最有复用价值的产出。
- **eMMC 启动可用性**: 分区层面完全可行(p4=system 900MiB / bootimg 6MiB / 实测节点已确认); 阻塞不在 Alpine 材料, 而在当前 mainline 内核跳入即 reset(见 CLAUDE.md §4a, 与 Alpine 无关)。
- 已知缺口: `/etc/network/interfaces` 不存在(Z1 无有线网, WiFi 后补, 影响小); boot_alpine 早期版本(v3–v6)只是给 shell / 后台挂载, 只有 auto 版真正 switch_root。

---

## 1. 构建流程 (tools/make_alpine_rootfs.sh, 138 行)

脚本 2026-07-06 23:58, 流程为 **minirootfs tarball → qemu-user chroot 装包 → mke2fs -d 打成 ext4**。不做交叉编译(Alpine armv7 官方二进制现成)。

| 步骤 | 动作 | 产物/要点 |
|---|---|---|
| 1 | 拉 Alpine armv7 **minirootfs** (latest-stable, tuna/aliyun/dl-cdn 三镜像) | `out/alpine-armv7.tar.gz` (3.05 MiB, 515 条目) |
| 2 | 解到 `out/alpine_rootfs/` (WORK 目录) | 现存在, 1028 条目 |
| 3 | 拷 `qemu-arm-static`, 写 `/etc/apk/repositories` (tuna main+community) | chroot 环境 |
| 4 | chroot 内 `apk update` + 装: `openrc busybox-mdev-openrc util-linux e2fsprogs alpine-base openssh-sftp-server nano less`; `rc-update add devfs/dmesg/mdev sysinit` | rootfs 软件集 |
| 5 | 写 `/etc/fstab`: `mmcblk0p4=/`、`mmcblk0p6=/mnt/data`、`mmcblk0p5=/mnt/cache` + proc/sys/devtmpfs | 分区映射(与实测一致) |
| 6 | 写 `/etc/inittab`(busybox init + OpenRC): `rc sysinit/boot/default` + **ttyGS0 USB-ACM getty + tty0 屏幕 getty** | 串口/屏幕登录 |
| 7 | 写 `/etc/init.d/usb-acm`(android_usb gadget 切 ACM), `rc-update add usb-acm default` | USB gadget service |
| 8 | 卸载 bind, 删 qemu-arm-static | 清理 |
| 9 | `sudo mke2fs -t ext4 -L alpine_z1 -d "$WORK" -m 1 -b 4096 "$IMG" ${SIZE_MB}M`, SIZE_MB 默认 850 | **`out/system_alpine.img` (850 MiB)** |

关键点:
- **默认 `SIZE_MB=850` → 镜像 850 MiB**, 脚本末尾用 `0x38400000` (900 MiB) 校验 `fits system`。
- fstab 用 `/dev/block/mmcblk0p4`, boot_alpine initramfs 里用 `/dev/mmcblk0p4`(Android 上通常都有, 但 initramfs 侧需 `mdev -s` 建节点, 见 §5)。
- USB-ACM service 走的是 `android_usb` 老 gadget 接口(`/sys/class/android_usb/android0`), 仅 MTK 原厂 3.4 内核有; **mainline 内核没有 android_usb**, 该 service 在 mainline 下会 `[ -d ] || return 0` 静默跳过, 需改 configfs 或靠 mainline 侧 f_acm 已起(CLAUDE.md §路C 已编 usb-gadget)。

## 2. out/alpine-armv7.tar.gz 内容

- 大小 3,196,124 B (3.05 MiB), 515 条目(纯 minirootfs, 未装包)。
- 关键路径均在: `./sbin/init`、`./bin/sh`、`./bin/busybox`、`./etc/inittab`。
- 自带 `etc/alpine-release` = **3.24.0**; 自带 stock inittab(`::sysinit:/sbin/openrc sysinit` / `boot` / `default` + tty1/tty2/tty3 getty)。
- 即 Alpine 官方 armv7 minirootfs, 后续经 chroot 装包变成 §3 的 alpine_rootfs。

## 3. out/alpine_rootfs/ 解包目录 (WORK, 1028 条目)

| 检查项 | 结果 |
|---|---|
| init / systemd | `/sbin/init -> /bin/busybox`(symlink, busybox init); **无 /init、无 systemd** (`usr/lib/systemd` 不存在) |
| busybox | `/bin/busybox` 存在 (709,868 B), applets 齐全(ash/cat/mount/mdev/getty/switch_root 等) |
| /etc/inittab | 存在, OpenRC 三段 + ttyGS0 getty + tty0 getty(见 §1 步6) |
| /etc/network/interfaces | **不存在**(`/etc/network/` 只有 if-*.d 钩子目录, 无 interfaces 文件); 另有 `etc/hostname=localhost`、`etc/resolv.conf`(920B) |
| apk | `/sbin/apk` 80,468 B, `/etc/apk/{repositories,world,keys,arch}` 完整 |
| OpenRC | `/sbin/openrc` 29,956 B + openrc-run + `/etc/init.d/` 全套(acpid..syslog 等 40+ 项), `rc-update add` 的 sysinit 三项已生效 |
| 版本 | `etc/alpine-release` = 3.24.0; os-release `Alpine Linux v3.24` |
| 遗留 | 无 qemu-arm-static(步8 已删) |
| dev/proc/sys | 空目录(运行时靠 fstab 的 devtmpfs/proc/sysfs 挂) |

结论: 目录是 §1 脚本的 WORK 输出, 与 system_alpine.img 内容一致(见 §4), 可作改 rootfs 的源。

## 4. out/system_alpine.img (+ .bak)

| 项 | 值 |
|---|---|
| 大小 | 891,289,600 B = **850.0 MiB** |
| 文件系统 | `file`: ext4, volume name `alpine_z1`, UUID 00bcf5ec-fc44-46d1-8d22-618c56451852 |
| superblock (debugfs) | features: has_journal ext_attr resize_inode dir_index filetype extent flex_bg sparse_super large_file huge_file uninit_bg dir_nlink extra_isize; **无 64bit**(对 3.4 老内核 ext4 友好); state=clean; 217,600 blk × 4096 |
| 分区适配 | ≤ system 分区 900 MiB (0x38400000) ✓ |
| 内容核对 (debugfs) | `/sbin/init` symlink→busybox ✓, `/sbin/apk` 80,468 B ✓, `/sbin/openrc`+`openrc-run` ✓, `/etc/inittab`(ttyGS0/tty0 getty) ✓, `/etc/fstab`(mmcblk0p4=/) ✓, 根目录 bin/dev/etc/lib/... 全在 |
| `.bak` 差异 | `system_alpine.bak` 同为 850 MiB ext4, 但 superblock 带 **64bit** feature(比 `.img` 多 64bit), 其余同; 实际刷写应选**无 64bit 的 `.img`**(老内核不支持 64bit) |

## 5. boot_alpine_v*.img 盘点 (6 个, 全是 6,291,456 B = 恰好 6 MiB)

统一结构(**Android bootimg**, magic `ANDROID!`, page_size=2048, kernel@0x10008000, ramdisk@0x11000000, **dt=0B** 无 dtb):
- **kernel**: 全部 md5 相同 `ca4b08de32c2ec142410c374508b53c0` —— 即**原厂 Z1 3.4.67 内核**(XZ 压缩 zImage 3,337,288 B; 解压 10,191,300 B, 内含 `Linux version 3.4.67 (hongyao@hongyao-PowerEdge-R630) ... #1 SMP PREEMPT Tue Aug 23 12:05:53 CST 2022`)。与 PLAN.md §1.2 记录的 stock kernel 尺寸完全一致。**非 mainline**。
- **ramdisk**: MTK ROOTFS blob 头(`0x58881688` magic + `ROOTFS` 名, v6 为 `0x58881688`/`0x58168888` 字节序差异)包一个 **gzip'd busybox 新c initramfs**(解压 ~1.54 MB), 内含 `/init` + `bin/busybox` + `dev etc newroot proc sbin sys tmp usr` 空骨架。
- cmdline(MTK 头 offset 0x40): `console=tty0 console=ttyMT1,921600n1 rdinit=/init [androidboot.selinux=permissive]`(auto/v3/v5 带 selinux=permissive, v5b/v6 不带)。

各版本 `/init` 行为差异(演化史):

| 版本 | init 行为 |
|---|---|
| v3 | 挂 mmcblk0p4 ext4 → `chroot /newroot /sbin/init`(真正试图进 Alpine), 失败落 recovery shell(ttyGS0 + console getty) |
| v4 | 先起 ttyGS0 getty 保 PID1 存活, 后台挂 mmcblk0p4 到 /newroot(提示手动 `chroot /newroot /bin/sh`), 无 switch_root |
| v5 | 同 v4(getty + 后台挂载), 加了 USB gadget `iProduct="Z1"` + sleep+mdev 建 ttyGS0 节点 |
| v5b | 同 v5, 后台挂载前 `sleep 3` 延迟 |
| v6 | 同 v5b(getty + 延迟后台挂载), 无 switch_root |
| **auto** | **完整自动启动**: 挂 mmcblk0p4 **ro** → 若无 ttyGS0 则自动往 `/newroot/etc/inittab` 追加 getty → `exec switch_root /newroot /sbin/init`; 挂载失败落 shell。**这是可复用的模板** |

## 6. 分区表实测依据 (eMMC 可用性前提)

- `tools/flash_partition.sh`: GPT 分区名是 MTK 名 `bootimg / recovery / android / cache / usrdata`(非 AOSP boot/system/userdata)。
- `PLAN.md §1.3` scatter: bootimg @0x2980000 **6 MiB**; android(system) @0x4340000 **900 MiB** (0x38400000); cache 226 MiB; usrdata ~2.5 GB。
- `mainline_recon/z1_emmc_test_233012.log` **实测节点**:
  - `/dev/mmcblk0p1`(1KiB) / p2(10MiB) / p3(10MiB) / **p4 = 921,600 KiB = 900 MiB (system)** / p5 = 226 MiB (cache) / p6 = 2.55 GiB (usrdata); 另有 boot0/boot1/rpmb。
  - onboard_verify.sh 第 3 步即在 p4 挂 ext4 读 build.prop(Android system), 佐证 p4=system。

## 7. eMMC 启动可用性评估

**分区适配 — 全部 ✓**
- `boot_alpine_v*.img` 恰好 6 MiB ≤ bootimg 6 MiB。
- `system_alpine.img` 850 MiB ≤ system 900 MiB。

**内容 — 基本可用, 两条路线各说各的**

路线 A: **保留 3.4.67 原厂内核(早期路线, 现已弃用转向 mainline)**
- 刷 `boot_alpine_auto.img` → bootimg; 刷 `system_alpine.img` → android(system, mmcblk0p4)。
- auto initramfs 挂 p4 → switch_root /sbin/init → busybox init + OpenRC + ttyGS0/tty0 getty。这条当时是完整闭环(除主会话后来转 mainline 未再刷)。

路线 B: **当前 mainline 内核 + Alpine rootfs(本项目现路线)**
- 内核换成 mainline zImage(dtb 须 appended, 见 CLAUDE.md §路C); rootfs 仍是 `system_alpine.img`(内核无关, 直接可用)。
- **boot_alpine_auto 的 initramfs 是现成模板**: 把 `/init`(挂 mmcblk0p4 + 自动加 ttyGS0 getty + switch_root)打进 mainline ramdisk, 即可实现 mainline 内核 → Alpine rootfs 全链。注意 MTK ROOTFS blob 包装风格与 mainline boot 图一致(out/forensic/linux/ramdisk.gz 同为 ROOTFS blob), 可沿用打包方式。
- **mainline 下差异点**:
  1. android_usb gadget 不存在 → rootfs 内 `/etc/init.d/usb-acm` 无效; 但 mainline 已编 f_acm(configfs, /dev/ttyGS0), inittab 里 ttyGS0 getty 直接可用。
  2. mainline 串口 console 是 ttyMT0 @ 921600, 不是 ttyMT1; getty 建议补一条 ttyMT0 或在 cmdline 处理。
  3. 阻塞是 mainline 内核跳入即 reset(§4a, DEBUG_LL 版待刷), 与 Alpine 材料无关。

**缺口/风险**
- `/etc/network/interfaces` 缺失 → `rc-service networking` 无配置可起(Z1 无 eth0, WiFi 后走 wlan + wpa_supplicant, 影响小)。
- initramfs 内 `-b /dev/mmcblk0p4` 依赖 mdev 建节点; stock 内核路径 OK, mainline 用 devtmpfs 也 OK, 但应在上板验证。
- `system_alpine.bak` 带 64bit ext4 feature, 老内核不支持; **刷写务必用 `system_alpine.img`(无 64bit)**。

## 8. 材料清单总表

| 文件 | 大小 | 类型 | 状态 |
|---|---|---|---|
| `out/alpine-armv7.tar.gz` | 3.05 MiB | Alpine 3.24.0 armv7 minirootfs | 构建输入, 完整 |
| `out/alpine_rootfs/` | 1028 条目 | 解包 rootfs (WORK) | 完整, 可改源 |
| `out/system_alpine.img` | 850 MiB | ext4, `alpine_z1`, 无64bit | **可刷 system (mmcblk0p4)** |
| `out/system_alpine.bak` | 850 MiB | ext4, 带64bit | 备份, 勿刷 |
| `out/boot_alpine_auto.img` | 6 MiB | stock 3.4.67 内核 + Alpine 启动 initramfs (switch_root) | 路线A 可用; mainline 下取 initramfs 模板 |
| `out/boot_alpine_v3.img` | 6 MiB | stock 内核 + chroot 版 init | 实验 |
| `out/boot_alpine_v4/v5/v5b/v6.img` | 各 6 MiB | stock 内核 + getty/后台挂载版 init | 实验(仅 shell) |
| `tools/make_alpine_rootfs.sh` | 5.4 KiB | 重建脚本 | 可重跑重造 rootfs |
| 分区依据 | — | PLAN.md §1.3 + z1_emmc_test_233012.log | 实测 p4=system 900MiB |
