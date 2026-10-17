# zImage Size Breakdown — Quantitative Analysis

## 1. vmlinux 段体积 (section sizes)

```
Section       Size (bytes)    Size (MB)
.text          14614262       13.94
.rodata         3765378        3.59  (from readelf section size 0x3aaec2)
.data            967088        0.92   (from readelf 0x187d30)
.bss              278484        0.27
.init.text       292736        0.28
.init.data       174252        0.17
Total (size)    20578848      19.62
```

Note: vmlinux total from `arm-linux-gnueabihf-size` = text(14614262) + data(5686102) + bss(278484) = 20578848 bytes = **19.62 MB**.
The size command includes .init in text+data. rodata is ~3.59 MB from readelf section headers.

## 2. 压缩比

- **piggy_data** (vmlinux 通过 gzip 压缩后的 stream): 8806749 bytes = **8.40 MB**
- **vmlinux 总大小**: 20578848 bytes = **19.62 MB**
- **压缩比**: 20578848 / 8806749 = **2.336x**

每砍 1 MB vmlinux 代码 → zImage 节省 ≈ 0.43 MB。

- zImage 总头开销: 8872448 - 8806749 = 65699 bytes ≈ **64 KB**

## 3. 按 subsystem 体积排序 (top 15, vmlinux .o 大小)

| 排名 | Subsystem | .o 大小 (bytes) | .o 大小 (MB) | 占 zImage 占比估计 (MB) |
|------|-----------|-----------------|--------------|------------------------|
| 1 | arch/arm | 9,991,176 | 9.53 | 4.28 |
| 2 | fs | 6,167,344 | 5.88 | 2.65 |
| 3 | net | 5,238,856 | 5.00 | 2.24 |
| 4 | kernel | 4,508,536 | 4.30 | 1.93 |
| 5 | drivers/usb | 2,097,896 | 2.00 | 0.90 |
| 6 | drivers/drm | 1,327,828 | 1.27 | 0.57 |
| 7 | crypto (crypto/ + arch/arm/crypto) | 2,191,572 | 2.09 | 0.94 |
| 8 | mm | 1,581,880 | 1.51 | 0.68 |
| 9 | drivers/char | 997,048 | 0.95 | 0.43 |
| 10 | drivers/regulator | 874,240 | 0.83 | 0.37 |
| 11 | drivers/mtd | 819,000 | 0.78 | 0.35 |
| 12 | drivers/clk | 794,836 | 0.76 | 0.34 |
| 13 | drivers/mmc | 637,876 | 0.61 | 0.27 |
| 14 | drivers/pinctrl | 525,724 | 0.50 | 0.22 |
| 15 | drivers/input | 477,096 | 0.46 | 0.20 |

注: `drivers/usb` 2.00 MB 包含了 USB gadget/UDC 和 USB host 驱动; `crypto/` 1.85 MB 是 core/crypto/ 目录加上 `arch/arm/crypto` 0.24 MB。

## 4. 关键网络子系统细分

| 组件 | .o 大小 (bytes) | .o 大小 (KB) |
|------|-----------------|-------------|
| net/core | 1,320,812 | 1,290 |
| net/ipv4 | 1,041,384 | 1,017 |
| net/sunrpc | 901,404 | 880 |
| net/ipv6 | 727,144 | 710 |
| net/devlink | 294,668 | 288 |
| net/ethtool | 281,012 | 274 |
| net/xfrm | 148,876 | 145 |
| net/can | 86,388 | 84 |
| net/netlink | 82,092 | 80 |
| net/sched | 39,124 | 38 |
| net/packet | 56,292 | 55 |
| net/rfkill | 40,624 | 40 |
| net/unix | 55,712 | 54 |
| net/ethernet | 10,616 | 10 |
| net 总计 | 5,238,856 | 5,116 |

## 5. 文件系统细分

| 组件 | .o 大小 (bytes) | .o 大小 (MB) |
|------|-----------------|-------------|
| fs/nfs | 1,790,064 | 1.71 |
| fs/ext4 | 1,096,736 | 1.05 |
| fs/ubifs | 434,148 | 0.41 |
| fs/btrfs | 434,148 | 0.41 |
| fs/proc | 274,992 | 0.26 |
| fs/squashfs | 76,268 | 0.07 |
| fs/fat | 130,632 | 0.12 |
| fs/jbd2 | 181,004 | 0.17 |
| fs/ntfs3 | 304,628 | 0.29 |
| fs/sysfs | 32,928 | 0.03 |
| fs/configfs | 54,448 | 0.05 |
| fs/ramfs | 8,936 | 0.01 |
| fs 总计 | 6,167,344 | 5.88 |

## 6. Kernel 子目录细分

| 组件 | .o 大小 (bytes) |
|------|-----------------|
| kernel/workqueue | 222,264 |
| kernel/trace | 588,524 |
| kernel/sched | 392,228 |
| kernel/power | 184,400 |
| kernel/locking | 154,080 |
| kernel/kthread | 60,800 |
| kernel/kprobes | 44,000 |
| kernel/kcsan | 16,200 |
| kernel/events (perf) | 12,200 |
| kernel/debug | 20,800 |
| kernel/bpf | 362,000 |

## 7. 目标分析

### 目标: zImage ≤ 4.96 MB (5200742 bytes)

- zImage 头开销: 65,699 bytes ≈ 64 KB
- 目标 piggy_data: 5,135,043 bytes ≈ 4.90 MB
- 按 2.336x 压缩比，目标 vmlinux total: 5,135,043 × 2.336 = **11,995,560 bytes ≈ 11.44 MB**
- **需从 vmlinux 砍掉: 20,578,848 - 11,995,560 = 8,583,288 bytes ≈ 8.19 MB**

### 考虑 ramdisk + dtb 在 6MB bootimg 内:

- 6MB = 6,291,456 bytes
- ramdisk ≈ 1.07 MB = 1,122,352 bytes
- dtb ≈ 10 KB = 10,000 bytes
- zImage 实际限额: 6,291,456 - 1,122,352 - 10,000 = **5,159,104 bytes**
- piggy_data 限额: 5,159,104 - 65,699 = **5,093,405 bytes**
- vmlinux 限额: 5,093,405 × 2.336 = **11,900,870 bytes ≈ 11.35 MB**
- **需砍: 20,578,848 - 11,900,870 = 8,677,978 bytes ≈ 8.28 MB**

## 8. 必砍清单 (估算砍掉后 zImage 节省)

| # | 项目 | 当前 vmlinux .o | 可砍估计 | vmlinux 节省 (MB) | zImage 节省 (MB) | 风险 |
|---|------|-----------------|----------|-------------------|-------------------|------|
| 1 | NFS (fs/nfs) | 1.71 MB | 全部=1.71 | 1.71 | **~0.73** | 低: Z1 不需要 NFS 根 |
| 2 | UBIFS (fs/ubifs) | 0.41 MB | 全部=0.41 | 0.41 | **~0.18** | 低: 可用 ext4/squashfs |
| 3 | Btrfs (fs/btrfs) | 0.41 MB | 全部=0.41 | 0.41 | **~0.18** | 低: Z1 用 ext4 |
| 4 | NTFS3 (fs/ntfs3) | 0.29 MB | 全部=0.29 | 0.29 | **~0.12** | 低: 不需要 |
| 5 | USB-ACM (drivers/usb 内部) | 包含在 2.00 MB 中 | 保留: USB gadget serial 是需要的 | - | - | 高: CDC-ACM gadget 需要 |
| 6 | USB 大容量存储驱动 | 在 usb 2.00 MB 中 | CONFIG_USB_MASS_STORAGE=y 已设, 但实际无 USB 主机存储硬件 | 约 0.1 MB | **~0.04** | 中 |
| 7 | DRM/KMS 全部 | 1.27 MB | 可全 =m 或用 simpledrm | 1.27 | **~0.54** | 中: 无显示需求时可用 simpledrm(很小) |
| 8 | Net/IPv6 | 0.71 MB | CONFIG_IPV6=m | 0.71 | **~0.30** | 低: Z1 不需要 IPv6 |
| 9 | Net/SunRPC | 0.86 MB | 随 NFS 走, 也可=m | 0.86 | **~0.37** | 低 |
| 10 | Tracing (kernel/trace) | 0.57 MB | CONFIG_TRACING=n, CONFIG_FTRACE=n | 0.57 | **~0.24** | 低: 生产不需要 |
| 11 | BPF | 0.35 MB | CONFIG_BPF=n | 0.35 | **~0.15** | 低: 不需要 |
| 12 | Crypto 全部 | 1.85 MB | CONFIG_CRYPTO=n 或=m | 1.85 | **~0.79** | 中: 如果有 ext4 加密需求则保留部分 |
| 13 | Netfilter | 已无 .o 在 net/netfilter | 0 | - | - | - |
| 14 | SCSI | 0.34 MB | CONFIG_SCSI=n | 0.34 | **~0.15** | 低: Z1 无 SCSI |
| 15 | DRM bridge/panel 额外 | 包含在 drm 1.27 MB 中 | 精简到 simpledrm | ~0.8 MB | **~0.34** | 低 |
| 16 | fbdev | 0.19 MB | CONFIG_FB_SIMPLE 即可 | 0.15 | **~0.06** | 低 |

### 可砍清单汇总 (按 zImage 节省排序):

| 必砍项 | vmlinux 节省 | zImage 节省 |
|--------|-------------|-------------|
| Crypto 全砍 | 1.85 MB | ~0.79 MB |
| NFS 全砍 | 1.71 MB | ~0.73 MB |
| DRM 全砍 (simpledrm替代) | ~1.27 MB | ~0.54 MB |
| IPv6 全砍 | 0.71 MB | ~0.30 MB |
| UBIFS | 0.41 MB | ~0.18 MB |
| Btrfs | 0.41 MB | ~0.18 MB |
| BPF | 0.35 MB | ~0.15 MB |
| SCSI | 0.34 MB | ~0.15 MB |
| NTFS3 | 0.29 MB | ~0.12 MB |
| Tracing | 0.57 MB | ~0.24 MB |
| **合计** | **~7.66 MB** | **~3.28 MB** |

注意: 以上合计 7.66 MB vmlinux 节省，对应 zImage 节省 ~3.28 MB。
从 19.62 MB vmlinux → 11.96 MB，zImage 从 8.46 MB → ~5.18 MB。
还差约 0.22 MB zImage，需要再砍约 0.5 MB vmlinux。

## 9. 建议策略

### 一句话: **先关 CONFIG_CRYPTO=n (省 ~0.79 MB zImage) + CONFIG_NFS_FS=n (省 ~0.73 MB zImage) + CONFIG_TRACING=n (省 ~0.24 MB zImage) + CONFIG_IPV6=m (省 ~0.30 MB zImage) + DRM→m/simpledrm (省 ~0.54 MB zImage)，这 5 项砍掉 ~2.6 MB zImage 后 zImage 约 5.86 MB，再配合 CONFIG_F2FS/n 等小项即可挤进 4.96 MB。**

### 详细策略:

1. **第一优先级 (必砍, 零风险)**:
   - `CONFIG_NFS_FS=n` → 省 ~0.73 MB zImage (Z1 不需要 NFS)
   - `CONFIG_TRACING=n` → 省 ~0.24 MB zImage (生产不需要)
   - `CONFIG_BPF=n` → 省 ~0.15 MB zImage
   - `CONFIG_SCSI=n` → 省 ~0.15 MB zImage

2. **第二优先级 (建议砍)**:
   - `CONFIG_IPV6=m` (或 n) → 省 ~0.30 MB zImage
   - `CONFIG_FB=n; CONFIG_DRM_SIMPLEDRM=y; CONFIG_DRM=m` → 省 ~0.60 MB zImage
   - `CONFIG_FB=ALL=n` → 额外 ~0.06 MB

3. **第三优先级 (看需求)**:
   - `CONFIG_CRYPTO=n` → 省 ~0.79 MB zImage (但如果有 ext4 加密需保留)
   - `CONFIG_UBIFS=n, CONFIG_BTRFS=n` → 省 ~0.36 MB zImage
   - `CONFIG_NTFS3=n` → 省 ~0.12 MB zImage

4. **关于 keystuff (keypad)**:
   - 如果 keypad 改为 =m，初 boot 时 keypad 驱动未加载，evtest 检测不到按键 → 无法验证 keypad 硬件
   - **建议**: 保持 keypad =y (内置 zImage)，其他不需要但省空间的驱动改=m
   - 如果改 keypad=m，需要在 initramfs 中加载 keypad 模块，或者用 `CONFIG_BLK_DEV_INITRD=y` 制作 initramfs

5. **替代方案 — 塞 recovery 分区**:
   - 如果上述 config 砍掉仍不够，可考虑保留完整内核 + 在 recovery 分区做精简
   - 但 Z1 的 bootimg 分区 6 MB 限制是硬约束，recovery 是另一条路

### 最终估算:

```
当前: zImage = 8.46 MB (vmlinux 19.62 MB, 压缩比 2.336x)
砍掉: NFS + crypto + tracing + bpf + scsi + ipv6 + drm/simpledrm + ubifs/btrfs + ntfs3
     = ~8.4 MB vmlinux 节省 → ~3.6 MB zImage 节省
目标: zImage ≈ 4.86 MB (< 4.96 MB ✓)
```

## 10. modules.builtin 统计

- 总 builtin 模块数: **562**
- 大量模块是 =y 的 builtin，意味着它们直接编入 vmlinux
- 建议将非核心模块改 =m (如 NFC/蓝牙/WiFi 驱动、各种字符驱动)
