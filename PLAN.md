# 佳伯听力机 Z1 / V100 —— Linux 发行版移植编译计划

> 目标：让这台 MT6572 设备能开机进入一个**通用 Linux**（不是安卓），最小可用形态为"开机 → 串口/USB 控制台 → 能挂载根文件系统跑 shell"，逐步再补显示/Wi‑Fi/按键。

---

## 0. 一句话结论与路线选择

| 路线 | 可行性 | 工作量 | 风险 | 选用 |
|---|---|---|---|---|
| ① 主线 Linux（mainline kernel 6.x）上 MT6572 | ❌ 不可行 | 巨大 | SoC 在 mainline 无 clk/pinctrl/wdt/dtsi，几乎等于重写 BSP | 否 |
| ② pmOS / halium 既有 MT6572 包 | ⚠️ 无现成设备包 | 中 | 同样卡在显示/连接 combo 驱动 | 否（参考） |
| ③ **保留厂商 3.4.67 内核（binary）+ 自建 ARMv7 用户空间**（Buildroot/Alpine/Debian） | ✅ 最稳 | 小～中 | 主要风险是 initramfs pivot 与 cmdline；显示/按键后续再补 | **✅ 采用** |
| ④ 用 LOS14 源码重编内核 + Android rootfs | ✅ 已被大佬做过 | 中 | 是安卓不是通用 Linux；和你"想编译 Linux 发行版"的目标不符 | 否 |

**推荐路径③**：沿用设备里那颗已经为这块板子配好硬件（panel、mmc、uart、wmt combo）的 3.4.67 内核镜像，只换用户空间。这能在最短时间内让"开机进 Linux 控制台"成立，把移植不确定性收敛到用户态这一层；移植面一旦稳了，再按需逐个点亮 ST7785M/Wi‑Fi/按键。

---

## 1. 设备取证（已实测，来自 `/home/<user>/claude/6572/1` 备份）

### 1.1 硬件 / 系统
- SoC：MediaTek MT6572，双核 Cortex-A7 @1.3GHz，ARMv7-A + NEON，1GB RAM，4GB eMMC
- 屏幕：240×320，**ST7785M**（DSI, VDO mode, 变体 HSD），厂商内核只有这一个 panel 驱动编译进来
- 接口：Wi‑Fi 4 b/g/n（**MTK WCN combo，mt66xx 系列**，走 SDIO + WMT/STP），3.5mm，TF 卡，USB；无相机、无 modem
- 原厂系统：Android 4.4.2 (KK1.MP7)，fingerprint `Z1/j72_z1/j72_z1:4.4.2/KOT49H/...`
- LOS14 端口：GitHub `TestMT6572`，复用 Explay JoyTV / Micromax A102 的 MT6572 工程，**无 j72_z1 专用 device tree**，内核使用预编译二进制

### 1.2 启动链与镜像格式（实测 boot.img / recovery.img）
```
preloader (EMMC_BOOT_1, 0x0, 12MB)
   → LK "Little Kernel"（lk.bin, uboot 分区 @0x2920000, 384KB）
   → Android boot.img（bootimg 分区 @0x2980000, 6MB）
        ├─ MTK 自定义头(magic 88 16 88 58) + 标准 Android boot header
        │   name='j72_Z1_V0.1_WEL'   page_size=2048
        │   kernel load=0x10008000   ramdisk load=0x11000000
        ├─ kernel（XZ 压缩，3 337 288 B；解出 10 191 300 B）
        │   Linux version 3.4.67 (hongyao@hongyao-PowerEdge-R630)
        │   gcc 4.7, #1 SMP PREEMPT Tue Aug 23 2022
        └─ ramdisk（gzip cpio，~标准 Android initramfs）
   注意：MTK header 把 ramdisk_size/dt_size 字段当别用，**不要信这两个字段**；
        按 page_size=2048 在 kernel 后页对齐处找 gzip magic。
```
- **没有 DTB**：LK 不传 device tree，硬件描述写死在内核 `board` 文件里（`mtk-tpd`、`st7785m_qvga_dsi_vdo_hsd` 等）。
- **LK 默认 cmdline**（`strings lk.bin`）：`console=tty0 console=ttyMT1,921600n1 root=/dev/ram androidboot.serialno=<SERIAL>
  → `ttyMT1` 是 MTK UART，yk 板上通常**没有引出**；显示控制台走 `console=tty0`（framebuffer）。
- LK 里没有 fastboot/recovery 按键触发（大佬为这做了修改版 `lk_Z1.bin` 把 Boot Menu 引出来，用复读/跟读键选）。

### 1.3 分区表（实测 scatter + recovery.fstab）
```
preloader   EMMC_BOOT_1  0x0          12MB        (bootloader)
mbr/ebr1    EMMC_USER    0xC00000     ——          (分区表)
pro_info    EMMC_USER    0xD00000     3MB
nvram       EMMC_USER    0x1000000    5MB         (校准/RF 等—千万别动)
protect_f/s EMMC_USER    0x1500000   各10MB
seccfg                   0x2900000   128KB
uboot(lk)                0x2920000   384KB       ===== 可刷自定义 LK =====
bootimg                  0x2980000   6MB         ===== 我们的 Linux boot.img =====
recovery                 0x2F80000   6MB(stock)/12MB(LOS)
sec_ro                   0x3580000   256KB
logo                     0x3640000   3MB
expdb                    0x3940000   10MB
android(system)          0x4340000   900MB       ===== 放我们的 rootfs（或拆 userdata）=====
cache                    0x3C740000  226MB
usrdata                  0x4A940000  ~2.5GB      ===== 大空间，rootfs 优选 =====
```
- 设备块节点：`/dev/block/mmcblk0p4=system`, `p5=cache`, `p6=userdata`（十一条 MBR 表）。
- ⚠️ **绝对不要在 SP Flash Tool 里选"全部格式化并下载"**——会清掉 nvram/校准，设备基本就废了（fuquan 博客原文警告）。

---

## 2. 已确认的决策点（用户拍板 + 实测验证）

- **rootfs 放 `system` 分区**（900MB，`mmcblk0p4`）。
  ⚠️ 连带风险：原厂 system 里带 Wi‑Fi/BT 的 WMT 固件 + 连接性 blob，必须**先从备份 `system.img` 抠出固件目录留底**再覆盖；否则阶段 3 补 Wi‑Fi 时找不到固件。**阶段 0 必加一步"抽 system 固件"**。
  ⚠️ 另一建议（已被用户否决，但记录在案）：放 `userdata`(2.5GB) 更稳——不碰原厂 system、固件不丢、回退方便。本次按 system 执行。
- **控制台先走 USB 虚拟串口** —— 实测**可行**：
  - 解出的内核 `/tmp/kx.out` 里 `f_acm`/`acm_bind`/`acm_function_bind_config`/`g_serial`/`u_serial`/`f_mass` 都在，ADB/MTP/accessory 均 compiled-in。
  - MTK 用的是 Android 经典 `/sys/class/android_usb/android0/...` 接口（不是新 configfs），原厂 ramdisk `init.modem.rc` 已用过 `/dev/ttyACM0`，证明 ACM 通路在内核里是活的。
  - 原厂默认 `persist.sys.usb.config=mass_storage`。我们在 initramfs 里写 `/sys/class/android_usb/android0/enable 0 → functions acm → enable 1`，`/dev/ttyGS0` 起来后 getty 顶上。
- **用户态用 Alpine armv7**（apk，社区对老 ARM 友好；注意 musl + 3.4 内核，避免 systemd）。

> 下文"决策点"小节已并入，不再单列。

---

## 3. 实施路线（分阶段，每段都可独立验证）

### 阶段 0 · 护航与回退（必做，0.5–1 天）
1. 备份：`/home/<user>/claude/6572/1` 已是全量 SP 备份，**复制一份到异地**；尤其 `NVRAM`、`PROTECT_F/S`、`preloader_j72_z1.bin`、`lk.bin`、`MT6572_Android_scatter.txt` 不可再得。
2. **抽 system 里的 Wi‑Fi/BT 固件**（rootfs 选 system 后必须先做）：
   ```
   # 从 ext4 原 system.img 里抠连接性 blob
   debugfs -R 'ls /system/etc/firmware' system.img                 # 看 WMT/WCN 固件目录
   debugfs -R 'ls /system/lib/modules'      system.img            # wmt/wlan .ko
   debugfs -R 'ls /system/vendor'           system.img            # 厂商 blob
   # 把 /system/etc/firmware /system/bin/{wmt_loader,stp_dump3,wmt_loader,...} /system/lib/modules 全 dump 出来, 存到 userdata 备份盘外
   ```
   留底，阶段 3 补 Wi‑Fi 时回灌到 Alpine rootfs 的 `/lib/firmware`、`/lib/modules`。
3. 准备刷写工具：`mtkclient`（开源，单分区刷写，不强制全擦）优先；SP Flash Tool 备用。
4. 自定义 LK：**先刷大佬修改版 `lk_Z1.bin`**（按键进 Boot Menu），再上我们的实验，否则刷错只能 SP Flash 上电救。
5. 自定义 scatter：以本机实测的 `MT6572_Android_scatter.txt` 为准，不套别人的。

### 阶段 1 · 让 3.4.67 内核先跑起来验证（1 天）
目标：不改内核，把现有 boot.img 的 kernel 区段抠出来，配一个**最简自定义 initramfs**重新打包刷入，让 USB ACM 串口能拿到内核日志和 shell。

1. 抠出内核：
   ```
   python3 tools/extract_boot.py boot.img   # 输出 kernel.xz / ramdisk.gz
   unxz kernel.xz                            # 得到 vmlinuz(实为解出来的 Image+zImage)
   ```
   实测 xz 流在 `boot.img` 内偏移 0x800(kernel page) + 0x3d34 处，第二个 stream 才是完整可解的。
2. 自建最简 initramfs（Alpine 的 `mkinitfs` 或静态 busybox 都行；起步用静态 busybox 最稳）：
   - 用 `musl` 静态编 busybox（init + sh + mount + mdev + getty）
   - `init` 脚本：
     ```
     mount -t proc proc /proc
     mount -t sysfs sysfs /sys
     mount -t tmpfs tmpfs /dev
     # 切 USB gadget 为 ACM 串口（MTK android_usb 接口）
     echo 0 > /sys/class/android_usb/android0/enable
     echo "acm" > /sys/class/android_usb/android0/functions
     echo 1 > /sys/class/android_usb/android0/enable
     # 起控制台：ttyGS0(USB ACM) + tty0(屏) 兜底
     exec /sbin/getty -L ttyGS0 115200 vt100
     ```
   - `find . | cpio -H newc -o | gzip > initramfs.gz`
3. 用 mtkbootimg 重新打包（关键：**`--mtk 1`**；base 0x10000000；pagesize 2048；kernel load 0x10008000；ramdisk 0x11000000；cmdline 透传 LK 已设的，或追加 `rdinit=/init`）：
   ```
   mtkbootimg --mtk 1 --kernel kernel.bin --ramdisk initramfs.gz \
              --base 0x10000000 --pagesize 2048 \
              --cmdline 'console=tty0 console=ttyMT1,921600n1 rdinit=/init' \
              -o boot_linux.img
   ```
   > BoardConfig 里写的是 `BOARD_KERNEL_BASE := 0x10000000`（不是 header 里 kernel load 的 0x10008000；后者是 base+0x8000）。打包时按 0x10000000 设 base。
4. 刷：用 `mtkclient` 只写 `bootimg` 分区（`boot.img`），**不碰 nvram/preloader/lk/recovery/system**。开机后插 USB，PC 端应枚举出 `/dev/ttyACM0`，screen/tty登陆进去。

   **判定通过标准**：PC `dmesg` 看到 `cdc_acm` 枚举、`/dev/ttyACM0` 能进 busybox shell、`uname -a` 显示 `3.4.67`。
   **若 USB ACM 不出**：先开盖按 fbcon 看屏幕内核 banner 是否起来（区分"内核没起"和"gadget 没切成功"两种情况）。

### 阶段 2 · 挂载真根文件系统（0.5 天）
1. 用 Alpine armv7 出一个 system 上能跑的 rootfs：
   ```
   # PC 上：apk add --root ./rootfs --arch armv7 --initdb \
   #        busybox openrc alpine-base musl util-linux
   mke2fs -t ext4 -d ./rootfs -L alpine_system system_alpine.img ...
   ```
   大小受 system 分区 900MB 限制，可裁成 ~400–600MB；apk 后续可随用随装。
2. `mtkclient` 把 `system_alpine.img` 写入 `android`(system) 分区——**原厂 system 镜像被覆盖，固件已先在阶段 0 抠出留底**。
3. boot.img 的 initramfs 改成"transition ramfs"：
   ```
   mount /dev/block/mmcblk0p4 /newroot   # system 分区现在装 Alpine
   exec switch_root /newroot /sbin/init  # Alpine 用 OpenRC, 不用 systemd
   ```
4. Alpine 的 `/etc/fstab`：
   ```
   /dev/block/mmcblk0p4   /            ext4   noatime             0 1
   /dev/block/mmcblk0p6   /mnt/data    ext4   noatime,nosuid      0 0
   /dev/block/mmcblk0p5   /mnt/cache   ext4   noatime             0 0
   /dev/mmcblk1p1         /mnt/sd      vfat   noatime             0 0   # TF
   none                   /proc        proc   defaults            0 0
   none                   /sys         sysfs  defaults            0 0
   none                   /dev         tmpfs  mode=0755            0 0
   ```
5. OpenRC 里把 USB ACM getty 写成一个 service，开机默认开机即活。

   **判定通过标准**：开机后从 system 上的 Alpine rootfs 起 OpenRC，USB `/dev/ttyACM0` 能登录 getty、能 `apk add`（先得有网络，见阶段 3 Wi‑Fi）。

### 阶段 3 · 补齐"算力/外设"用户态（按需，逐项）
按"先易后难、先验证后下手"：

| 子项 | 难度 | 依赖 | 备注 |
|---|---|---|---|
| USB gadget / ADB shell 走控制台 | 易 | 内核已编 | 拿 `init.usb.rc` 思路，配 g_serial / g_mass_storage |
| mSD/TF 卡挂载 | 易 | 内核已编 | `mtk-msdc.1` 口，vfat |
| framebuffer + ASCII 控制台满屏 | 中 | 内核 `st7785m` 驱动已在 | 应能直接 `fbcon`，先验证 `cat /dev/urandom > /dev/fb0` 出花屏 |
| 触屏/物理按键 evdev | 中 | `mtk-tpd` 已在内核 | 校准可能要改 dts/board，但 board 文件改动需重编内核 |
| Wi‑Fi（MTK WCN combo） | **难** | 固件 + `wmt_loader` + `wlan_mtk` 模块 | 固件原本在 system 分区；需抽出 `WMT.cfg`/`ROM`/`RAM`，用户态跑 `wmt_loader`+`stp_dump`；走 `wpa_supplicant_8_mt66xx`。**最耗时**，可后置 |
| 音频（3.5mm 听力机核心功能） | **难** | MTK `mtkaudio` blob + ALSA | 需 libxlog/libmtk_symbols shim，依赖厂商 blob |
| 自带电池/低电量/充电 | 中 | `init.charging.rc` 路径 | 走 `boot_mode` sysfs |

> 阶段 3 各项彼此独立，**Wi‑Fi 和音频是最难、最依赖厂商 blob 的**；如果目标只是"能开机的 Linux"，这两项可以最后做甚至不做。

### 阶段 4（可选）· 真正"重编内核"
若有需要改内核（例如调 panel/按键/wifi），可拉 `MTKKernel/android_kernel_mt6572`（有 `muse72_s4_kk_debug_defconfig`)作起点：
1. 找/反推 j72_z1 的 board 文件差异（panel 名、gpio、partition、memory），diff 参考机。
2. `export ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-`，`make muse72_s4_kk_debug_defconfig` 后改 panel/分区项 → `make -j` zImage。
3. 用阶段 1 的 mtkbootimg 流程打包。
> 重编内核门槛明显高于路线③，**仅在阶段 3 的硬骨头非改内核不可时再上**。

---

## 4. 工具链 / 仓库清单

| 用途 | 来源 | 备注 |
|---|---|---|
| MTK 自定义 boot 打包 | `TestMT6572/android_device_Explay_JoyTV` 的 `mtkbootimg/` | 必须用 `--mtk 1` |
| 4.4.2 ramdisk/init.rc 参考 | 本备份已解开（见下） | `factory_init.project.rc` 显示 `/dev/ttyMT2` 是 BT/FM/GPS 口 |
| LOS14 设备树/BoardConfig 参考 | `TestMT6572/android_device_Explay_JoyTV` | 唯一含 `mtkbootimg`、`recovery.fstab`、`BoardConfig.mk` 的 j72_z1 最近参考 |
| 厂商内核源（备选/重编） | `MTKKernel/android_kernel_mt6572` | defconfig: `muse72_s4_kk_debug_defconfig` |
| 单分区刷写工具 | `mtkclient`（GitHub，开源） | 避免全擦，安全 |
| 根文件系统 | Buildroot / Alpine armv7 / Debian armhf | 起步用 Buildroot |

### 已经实测可从备份里取出的"现成物"
- 完整可解 XZ 内核（`/tmp/kx.out`，`Linux 3.4.67`）
- 解开的原厂 ramdisk（`/tmp/rd/`）：含 `fstab`、`fstab.nand`、`init.project.rc`、`init.charging.rc`、`default.prop`
- LK 默认 cmdline、panel 名、wmt combo 驱动符号
- `MT6572_Android_scatter.txt`（本机实测分区表）

---

## 5. 风险与红线

- 🔴 **绝不**对 nvram / pro_info / protect_{f,s} / preloader 做擦写——校准和 RF 一旦丢，这台机器基本不可恢复。
- 🔴 SP Flash Tool **不要选"全部格式化并下载"**（fuquan 原文强警告）。
- 🟠 自定义 LK 没刷进去前，**先有大佬的修改版 LK（lk_Z1.bin）**做兜底，否则刷错只能靠 SP Flash 上电救。
- 🟠 MTK boot header 与标准 Android 不同，pack 时**必须带 `--mtk 1`**，否则内核不认、黑屏。
- 🟡 3.4.67 内核与新 systemd/glibc 的一些新 syscall 不兼容，用户态优先用 musl + 旧的 init（busybox init/OpenRC），避免 systemd 新特性踩坑。
- 🟡 显示/Wi‑Fi/音频依赖厂商 blob，**移植到通用 Linux 后这三项是主要不确定源**，应在阶段 3 排期里显式降级为"尽力而为"。

---

## 6. 推荐第一步（具体可立刻动手）

决策已定（system 分区 / USB ACM / Alpine armv7）。我按这个搭：

1. `tools/extract_boot.py` —— 解出 kernel.xz + ramdisk.gz（已实测路径：xz 流在 boot.img 内 0x800+0x3d34）
2. `tools/dump_system_firmware.sh` —— 从 `system.img` 抠 WMT/WCN 固件 + 连接性二进制 + .ko 模块，留底（rootfs 选 system 后必做）
3. `initramfs/` —— 最简 busybox initramfs：切 USB ACM gadget + getty ttyGS0（initramfs 内嵌, 验证内核先跑通）
4. `pack.sh` —— mtkbootimg 打包（base 0x10000000 / pagesize 2048 / `--mtk 1` / cmdline）
5. `rootfs/` —— Alpine armv7 生成脚本，出 ≤900MB 的 `system_alpine.img`，含 OpenRC、ACM getty service、fstab

先把 1+2+3+4 跑通刷 `bootimg` 分区验证阶段 1（USB ACM 出 shell），再上 5 覆盖 system 进阶段 2。

---

*本文档基于对 `/home/<user>/claude/6572/1` 全量备份的实测取证 + GitHub `TestMT6572` 工程源 + 联网调研整合。所有"实测"标注的参数均为直接解析镜像所得。*
