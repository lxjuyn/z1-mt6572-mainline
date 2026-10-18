# A9 device 层 mainline 版：交接说明（给持 LOS16 源树的构建机）

配套内核：`wip/a9-mainline`，`kernelrelease=7.0.0-rc7-z1+`，接口契约见
`mainline_recon/A9_DMA_HEAP_CONTRACT.md`，撤/留补丁判定见
`mainline_recon/A9_SYSTEM_MIGRATION_CHECKLIST.md`。本目录是给那台机器**直接落进
`device/jiabo/z1/`** 的文件，不是可执行构建脚本。

## 1. 本目录里有什么

| 文件 | 状态 | 说明 |
|---|---|---|
| `rootdir/ueventd.z1.rc` | **本机新增** | 原 mainline 侧没有这份文件；stock 那份引用的 `/dev/ion`、`/dev/mali` 在 mainline 上不存在，而 mainline 需要的 `hwbinder/vndbinder/dma_heap/tun/watchdog/input` 没写 |
| `rootdir/init.z1.rc` | 取自 stock 分支 `rootdir/` | 已是 Treble 形态（`mount_all /fstab.z1`、watchdogd、configfs adb、`swapon_all`），未改内容 |
| `rootdir/init.z1.network.rc` | 同上 | 未改内容，落位前请核对是否仍引用 stock 专有接口 |
| `rootdir/fstab.z1` | 同上 | p4=system / p5=cache / p6=data + zram0；**p6 几何未实测，禁止加 format 标志** |
| `rootdir/manifest.xml` | 同上 | 用 `hwbinder` transport（mainline 有 /dev/hwbinder 才允许这样写） |
| `rootdir/z1_usb_setup.sh` | 同上 | ADB over configfs FunctionFS；内核侧 `USB_F_FS=y`，`functionfs` 文件系统类型在 `f_fs.c` 内注册 |

一致性由 `tools/z1/z1_a9_devcheck.sh` 在本机核过（`DEVCHAIN_FAIL=0`）：每个 `/dev/X` 对应
的内核能力、binder 三域由谁建节点、functionfs/zram/ext4 ACL/`&mmc0 okay`、`.ko` vermagic、
manifest transport、探针覆盖面。

## 2. 那台机器上要做的四件事

1. **撤掉 stock 3.4.67 的代偿补丁**（判定清单在 `A9_SYSTEM_MIGRATION_CHECKLIST.md` §1，15 份），
   核心是 `ro.z1.stock_kernel` 门控、configstore 本地 passthrough 工厂、镜像级禁用
   hwservicemanager/vndservicemanager、`timerfd CLOCK_BOOTTIME` 回落、`mmap_rnd_bits` ENOENT 放行、
   minijail 放宽 seccomp。mainline 上这些只会把 A9 拖回 stock 的假代偿路径。
2. **不要加 `mount binderfs`**。`CONFIG_ANDROID_BINDERFS=n` 是刻意的：binder.c 在
   `!IS_ENABLED(CONFIG_ANDROID_BINDERFS)` 时按 `CONFIG_ANDROID_BINDER_DEVICES` 逗号切词
   `init_binder_device()`→`misc_register()` 静态建 `/dev/binder`、`/dev/hwbinder`、`/dev/vndbinder`。
   节点名不在源码里硬编码，别用 grep 源码去验证。
3. **gralloc 保持 ashmem+fbdev 路线**：`device/jiabo/z1/graphics/gralloc/framebuffer.cpp`
   实际用的是 `cutils/ashmem.h` + `/dev/fb%u` + `mmap`，**不碰 ION**，所以第一阶段不需要
   ION→dma-heap 垫片；`CONFIG_ANDROID_ASHMEM=y`、`CONFIG_FB_SIMPLE=y`(LK 遗留 framebuffer) 已就位。
   ION 垫片(契约 §2 方案 A/B) 等上 lima/GBM 第二阶段再做。
4. 产出 **generic ramdisk**（first-stage init 那颗），并**连同目录内容**交回本机 —— 不要只给
   最终 `*.img.gz`。原因见下条。

## 3. ramdisk 交回的形状与出片命令

本链的 initrd 必须是 **512B MTK ROOTFS 头 + gzip cpio body**：头 magic `88 16 88 58`、
`0x08` 处 `ROOTFS`、`0x04` 处小端 u32 = body 字节数，且 dts `linux,initrd-end = 0x84100000 + body`。
所以请把 ramdisk 的**目录**打 tar 交回，本机重打带头版本：

```sh
cd <工作区根>/a9_wt
tar xf generic-ramdisk.tar -C /tmp/rd_a9
tools/z1/z1_repack_a9_ramdisk.sh /tmp/rd_a9 out/ramdisk_v13_7_acm.gz out/ramdisk_a9p4.gz
# 该脚本会打印 BODY_BYTES / INITRD_END / SELFTEST(头字段与 body 互镜像、cpio 清单比对)

A9_RAMDISK=out/ramdisk_a9p4.gz tools/z1/z1_a9_p2.sh pack 1b out/boot_mainline_a9p4_android.img
```

`pack` 会按**实际 body** 现算 initrd-end、回写 dts、重编 dtb、跑 6 MB 预算闸门、
打包后再从镜像里反解内嵌 DTB 复核（`VERIFY=OK` 才算交付）。

## 4. 预算（硬约束，先算再做）

P4 实测：zImage 4,612,608 + dtb 19,796/19,808 + 探针 ramdisk 1,276,824 → 镜像 5,914,624 B，
余量 376,832 B，即 **A9 generic ramdisk 上限 ≈ 1,654,944 B（带 512B MTK 头，约 1.58 MiB）**。
超过这个数，本机 `pack` 会**在打包前**拒绝 —— 因为实测发现 `mkbootimg` 装不下时不是报错，
而是静默把整个 ramdisk 丢掉（`ramdisk_size=0`，镜像反而"合规"），刷进去表现为 VFS panic，
看起来像内核回归。细节与三层封堵见 `mainline_recon/A9_PACK_GATE_20261018.md`。
若你们产出的 ramdisk 超过这个数，不要直接压 cpio，先按
`A9_BOOT_BUDGET.md` §3 的方向腾：把非首启必需项改 `=m` 随 `/vendor/lib/modules` 提供
（`xt_quota2`/`xt_IDLETIMER` 已经是 `=m`），或裁 ramdisk 里的调试件；
**不要裁 A9 符号门**（`z1_a9_slim.sh` 的 `ANDROID_ON`/`GATE_Y` 会让裁错立刻红）。

## 5. 仍未闭合（别当成已完成）

- 内核面 P2/P3/P4 **全部未上板**。本机只有 CH340(`/dev/ttyUSB0`)，无 C3 桥、无 MTK 下载口在位。
- system.img / generic ramdisk 不在本机 —— 这是"可刷写安卓镜像"目前唯一缺的输入。
- 触摸节点 `&i2c1/touchscreen@5d` 仍是 `status="disabled"`，等板上 `i2cprobe` 取证
  （见 `TOUCH_CST836U_20260829.md`）；A9 首轮输入面靠 `KEYBOARD_MTK_PMIC`（已 =y）。
- WiFi(mt6620) 与 S6 的 LCM/FTS 路线另评，不在本轮。
