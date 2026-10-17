# PACKAGE_NOTE v13.3b (2026-08-29, 评审修复后重打, 覆盖同名镜像; host-side, no flash)

## 产物
- boot_mainline_v13_3.img          4,884,480 B  (≤6MB boot 分区上限)
  sha256: 814ef070eb823d63acec76fc402e5d94c23da1f74861487589a6c25a5875f84c
  头部 ANDROID! magic 已验
- ramdisk_v13_3_acm.gz             1,070,030 B  (512B MTK ROOTFS 头 + gzip-cpio body)
- init_v13_3_acm.sh                新 /init 源文件 (可读副本)
- 旧版备份: bridge_backups/ramdisk_v13_3_pre_review_fix_20260829_015349.gz
  (v13.3 原版: sha256 599a3298... 时代的 ramdisk)

## ⚠️ 警示 (修复 2)
勿从 fork_linux main 分支重编 zImage —— main 缺 eMMC CMD1 预热修复
(只在 wip/v13.2a-msdc-fix@8cec21a62), 且 main 的 drivers/mmc/host/mtk-sd.c
mtime 晚于已验证 zImage, 增量重编会静默丢预热修复、复现 CMD1 硬挂。
重编前必须切该分支或 cherry-pick(需用户确认)。本镜像复用已验证 zImage, 安全。

## 与上一版 (8/27 v13.3) 的差异 (评审修复)
1. /init 补全 [acm] 自报: gadget dir / VID-PID / configs/c.1 创建成功失败分支;
   最关键: `ln -s acm.usb0 -> c.1` 补 `[acm] acm linked into c.1` /
   `[acm] acm link FAILED` (此前 symlink 失败静默, 绑定必挂但无法定位根因)。
2. tty0 后台 getty 加 `[ -e /dev/tty0 ]` 守卫, 不存在时跳过不报错。
3. dtb: linux,initrd-end 0x84205148 -> 0x842051ce (随 ramdisk 尺寸重算)。
4. zImage 未变 (与源树 8/26 03:23 产物及 v13.2a 内嵌逐字节一致, 预热修复不丢);
   cmdline 与上轮逐字一致 (console=ttyS0,921600n8, 评审"应改 ttyMT0"为误报, 未改)。

## 关键数值
- NEWLEN (新 ramdisk body): 1,069,518 B (0x1051ce); 旧 1,069,384 (+134)
- 512B 头长度字段 (偏移 0x04-0x07, LE): ce 51 10 00 = 1,069,518 ✓ 与 body 一致
- 新 linux,initrd-end = 0x84100000 + 1,069,518 = 0x842051ce
  (dts fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts, 已提交推送)
- ramdisk 文件结构: [512B 头: 8816 8858 | len(LE) | "ROOTFS"@0x08] + gzip(1f8b)
- 配对表 (AGENTS.md §3.2):
  ramdisk_v11_extracted.gz body 1,068,865 -> 0x84204f41
  ramdisk_v13_3_acm.gz v13.3 body 1,069,384 -> 0x84205148
  ramdisk_v13_3_acm.gz v13.3b body 1,069,518 -> 0x842051ce

## 打包命令 (实际执行)
```
cd /home/<user>/claude/6572
cat fork_linux/arch/arm/boot/zImage fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dtb > /tmp/zImage_appended_v133b
./build/mtkbootimg/mkbootimg --kernel /tmp/zImage_appended_v133b \
  --ramdisk out/ramdisk_v13_3_acm.gz \
  --base 0x10000000 --kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
  --pagesize 2048 --mtk 1 \
  --cmdline "console=tty0 console=ttyS0,921600n8 root=/dev/ram panic=10 loglevel=7 clk_ignore_unused regulator_ignore_unused oops=panic sysrq_always_enabled rdinit=/init earlycon=mtk8250,0x11005000" \
  -o out/boot_mainline_v13_3.img
```
cmdline 注意: console 是 ttyS0 (8250), 不是 ttyMT0。

## dtb / 全链验证结果
- mmc@11120000 (mmc0): status okay
- usb@11100000:        status okay
- phy@11110800:        status okay
- linux,initrd-end = <0x842051ce> (源树与镜像内嵌双重确认)
- 与 v13.2a 内嵌 dtb 反编译 diff 仅 initrd-end 一行
- 镜像内嵌 ramdisk 与 out/ramdisk_v13_3_acm.gz 逐字节一致; 头长度字段 1,069,518
- zImage 段与 v13.2a 内嵌 + 源树产物逐字节一致 (eMMC 预热修复不丢)

## 已知风险 (只记录不改脚本)
z1_auto_switch.sh / grab_acm.sh / tools/acm_capture.sh 硬编码 /dev/ttyACM0;
gadget (0e8d:2006) 上线后与 C3 桥同时在位会抢口, 可能把桥命令打进 gadget root
shell。z1_capture_mainline.sh / z1cmd.sh by-id 选口安全。首次验证先确认桥口。

## 刷写与回滚 (待用户唤醒设备后, 勿今晚执行)
tools/flash_with_retry.sh bootimg out/boot_mainline_v13_3.img /home/<user>/claude/6572/1/boot.img 8
回滚件: ~/claude/6572/1/boot.img (原厂); 上一可用 out/boot_mainline_v13_2a1.img。
验证: 串口见 [acm] 自报(含 acm linked into c.1); 主机插线后 lsusb 应现
0e8d:2006 + /dev/ttyACM* (注意与桥口区分)。
