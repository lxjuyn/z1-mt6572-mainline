# Z1 v13.2a1 打包记录 (2026-08-27)

本文件仅记录打包事实; 完整取证上下文见工作区 `../mainline_recon/V13_PACKED_20260825.md`
(该文件不在本仓库跟踪范围)。

## 背景

v13.2a 上板验证: eMMC 完整枚举 + initramfs 解包 + /init 执行全部成功
(8cec21a62 的 msdc CMD1 pre-warm 修复有效), 但 userspace 输出全进屏幕,
串口无 userspace 输出。根因: cmdline `console=ttyMT0,921600n8` 与实际注册名
不匹配 —— 启动日志显示 `11005000.serial: ttyS0 at MMIO 0x11005000`,
8250 console 从未使能, /dev/console 落到屏幕。

## 改动 (仅 cmdline, 方案 A)

`console=ttyMT0,921600n8` → `console=ttyS0,921600n8`, 保持在 `console=tty0`
之后 (最后一个匹配的 console 进链表头, /dev/console 指向串口)。
内核、dtb、ramdisk 全部不变。无源码改动。

完整 cmdline:

    console=tty0 console=ttyS0,921600n8 root=/dev/ram panic=10 loglevel=7 clk_ignore_unused regulator_ignore_unused oops=panic sysrq_always_enabled rdinit=/init earlycon=mtk8250,0x11005000

## 产物

- `out/boot_mainline_v13_2a1.img` — 4,884,480 B (boot 分区 6MB 上限内)
- sha256: `d438dd8d972c521b2b2feda3934353be7e2a31da4ce1a95f251b32f3316f7e0d`
- 归档: `../bridge_backups/products/products_v13_2a1_20260827_012312/`

## 与 v13.2a 的逐字节对比

`cmp -l out/boot_mainline_v13_2a.img out/boot_mainline_v13_2a1.img`:
仅 176 字节差异, 全部位于 header 区 (字节 9–2053: cmdline 字段 + 联动的
boot header SHA1); kernel 段与 ramdisk 段与 v13.2a 逐字节一致。

## 源文件

- kernel 段 (zImage 3,789,312 B + appended dtb 19,748 B): 从
  `out/boot_mainline_v13_2a.img` 提取复用; 其中 zImage 与本树
  `arch/arm/boot/zImage` (8/26 03:23) cmp 一致
- ramdisk: `out/ramdisk_v11_extracted.gz` (1,069,377 B, 512B MTK ROOTFS 头, 原样)

## 偏差记录 (待排查)

本树当前 `arch/arm/boot/dts/mediatek/mt6572-z1.dtb` (8/26 03:28) 与 v13.2a
镜像内嵌 dtb 不一致: usb2-phy status 本树=disabled / 镜像内嵌=okay;
chosen/bootargs 字段亦不同 (linux,initrd-end 两者一致 = 0x84204f41)。
为保证 "与 v13.2a 完全一致" 纪律, 本轮 kernel 段直接从镜像提取, 未用本树
dtb。该分歧来源待后续排查。

## 待验证点 (上板)

1. 串口出现 `console [ttyS0] enabled`
2. userspace 输出落串口, getty 可交互
3. 回滚件: `../1/boot.img` (原厂); 上一可用 `out/boot_mainline_v13_2a.img`
