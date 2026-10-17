# PACKAGE_NOTE_s6_v8 — lima GPU 内核镜像（任务 #50，2026-08-30）

## 一句话

= s6_v7（回放时序修复版）+ **新编含 DRM + lima（Mali-400 GPU）的 zImage**（任务 #49），
ramdisk / dtb 完全复用，仅换内核。

## 输入件（打包前逐件哈希核对，全部一致）

| 输入件 | 大小 | sha256 |
|---|---|---|
| `fork_linux/arch/arm/boot/zImage`（新，含 DRM+lima，CONFIG_DRM=y CONFIG_DRM_LIMA=y） | 3,920,384 B | `2895a552bdfebf2a0a2ba395143f3466a1f7c7c3f17b7b81011e51e59e2da533` |
| `fork_linux/arch/arm/boot/dts/mediatek/mt6572-s6.dtb`（不变，initrd-end 0x842362fb 与下方 ramdisk 已配对） | — | `0dfd07afa72c7dd1a5896c7c75d89c94691b16ee1fb17ec7300545ce27553753` |
| `out/ramdisk_v13_7_acm.gz`（不变，v13.7：回放先行阻塞 + stty -echo） | 1,271,035 B（body 0x1362fb，与 dtb initrd-end 0x842362fb 配对一致） | `f19ccb0f8269d626ac23fdecf0c5867df1d7b7b439942d198d6cadfdc45d360c` |

## 变化点（相对 s6_v7）

- **唯一变化 = kernel 段**：新 zImage 启用 `CONFIG_DRM=y` + `CONFIG_DRM_LIMA=y`（+ 依赖
  DRM_GEM_SHMEM_HELPER / DRM_SCHED / DRM_BRIDGE / DRM_PANEL），为 Mali-400 GPU（MT6572）上
  lima 驱动做准备。
- ramdisk、dtb、cmdline、打包参数与 s6_v7 完全一致。

## 打包命令（已执行）

```
cd /home/<user>/claude/6572
CMDLINE="console=tty0 console=ttyS0,921600n8 root=/dev/ram panic=10 loglevel=7 clk_ignore_unused regulator_ignore_unused oops=panic sysrq_always_enabled rdinit=/init earlycon=mtk8250,0x11005000"
cat fork_linux/arch/arm/boot/zImage fork_linux/arch/arm/boot/dts/mediatek/mt6572-s6.dtb > /tmp/zImage_appended_s6v8
./build/mtkbootimg/mkbootimg --kernel /tmp/zImage_appended_s6v8 \
  --ramdisk out/ramdisk_v13_7_acm.gz \
  --base 0x10000000 --kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
  --pagesize 2048 --mtk 1 --cmdline "$CMDLINE" \
  -o out/boot_mainline_s6_v8.img
```

（提醒：本启动链 LK 不转发 boot 头 cmdline，console 等引导参数以 dts chosen bootargs 为准；
头内 cmdline 仅语义自洽。）

## 产物

- `out/boot_mainline_s6_v8.img` sha256 **`5c306361a7c8002dda9e4607ef72beb2fe434e35c59f17f94efccc2e62fc607b`**
- 大小 **5,214,208 B**（~5.0 MB，≤ 6MB boot 分区上限；布局：2048B android 头 + kernel 段
  3,939,889 B（512B MTK KERNEL 头 + 3,939,377 B 拼接体）+ ramdisk 段 1,271,035 B，均页对齐）

## 全链自验结果（实测，全部通过）

1. 大小 5,214,208 ≤ 6,291,456；头部 8 字节 = `ANDROID!` ✓
2. 剥 512B MTK KERNEL 头（magic `88 16 88 58`，长度字段 3,939,377）后，kernel 段
   逐字节 == `cat(新zImage, mt6572-s6.dtb)`（拼接体 sha256
   `b24fb48b8f2b2e6f2d5b256bfec24b739a73c8977e273aaf1deea09a22d03d03`）✓
3. ramdisk 段逐字节 == `out/ramdisk_v13_7_acm.gz`，且与 s6_v7 的 ramdisk 段相同 ✓
4. dtb `linux,initrd-start=0x84100000` / `linux,initrd-end=0x842362fb`，与 ramdisk body
   （1,271,035 − 512B MTK 头 = 1,270,523 = 0x1362fb）配对一致 ✓
5. 与 s6_v7（44de3ea5…）diff：差异仅在 android 头 `kernel_size` 字段（3808817→3939889）、
   头内 SHA1 id 域、及 kernel 段本体；ramdisk 段及其后内容完全一致 ✓
   （kernel 变大 131,072 B，镜像由 5,083,136 → 5,214,208，仍在分区上限内）
6. kernel/ramdisk 段尾 padding 均全零 ✓

## 上板验证判据（刷写由他人执行，本文件只留判据）

- 串口/ACM 表现与 s6_v7 一致：先完整回放（[pstore]/[klog] 横幅 + dmesg）再出 `~ #` 提示符，
  输 `id` 有回应。
- **lima 专项**：
  - `ls /dev/dri/` 应出现 `card0`（若 LCM 面板节点未就绪，可能只有渲染节点或空目录，见下）
  - `dmesg | grep -i lima` 应见 lima probe 记录（`lima 13000000.gpu: ...` 类）
  - `cat /sys/class/drm/card0/device/*`（uevent/vendor 等）应有内容
- **退化容忍**：若 lima probe 失败（缺时钟/电源/面板节点均可能），**不影响串口交互与
  userspace**——本轮目标是确认新内核（含 DRM）能正常启动，GPU 失败只记录日志继续取证。
- 回滚件：`out/boot_mainline_s6_v7.img`（44de3ea5…）

## 归档

`bridge_backups/products/products_s6_v8_20260830_032212/`（boot img + zImage + dtb +
ramdisk + SHA256SUMS）。
