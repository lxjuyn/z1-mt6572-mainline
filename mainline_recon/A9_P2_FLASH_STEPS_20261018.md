# A9 P2：boot 分区体积工程 + 可复现变体出片（2026-10-18）

分支 `wip/a9-mainline`，基线 `main`=`ffdff0653`。上一轮 P1 出 A/B/C 三张探针镜像（未上板）。
本轮目标：把 boot 侧的 A9 硬约束收干净，让 **A9 ramdisk 一到就能立刻出可刷镜像**，
并修掉 P1 遗留的"变体不可复现"问题。

## 0. 一句话结论

zImage **4,772,352 → 4,555,264 B**（省 217,088 B），镜像余量 **217,088 → 434,176 B**，
A9 generic ramdisk 可用上限从 ~1.43 MB 提到 **~1.63 MB(1,710,711 B)**；
A9 符号门 26 项经**产物反抠**全部在位；新出两张可刷镜像 D1/D4，`validate` 均 `RESULT: VALID`。

## 1. 出片与指纹（详见 `A9_P2_MANIFEST.txt`）

| 镜像 | 大小 | sha256(前16) | 内容 |
|---|---|---|---|
| `out/boot_mainline_a9p2_D1_1b.img` | 5,857,280 | `13f75dcd7231e941` | 瘦身 zImage + dtb(1-bit) + androidboot + 探针 ramdisk |
| `out/boot_mainline_a9p2_D4_4b.img` | 5,857,280 | `464b1e281aea7cf4` | 同上，唯一差异 = `&mmc0 bus-width=<4>` + DAT1-3 pinmux |

boot 上限 6,291,456 → 两张余量均 **434,176 B**。`kernelrelease=7.0.0-rc7-z1+`，
`xt_quota2.ko`/`xt_IDLETIMER.ko` 与本轮 zImage **同编**，vermagic `7.0.0-rc7-z1+ SMP mod_unload ARMv7 p2v8`。
`linux,initrd-end=0x84237877`（探针 ramdisk body 1,276,023 不变）。
归档：`bridge_backups/products/a9p2_20261018_093818/`（zImage/dtb/.config/两镜像/ramdisk/ko）。

**建议刷 D1**（1-bit，与已验证启动链一致）；只有要看 eMMC 吞吐时才刷 D4，异常即刷回 D1，再不行刷 `1/boot.img`。

## 2. 本轮裁了什么、哪些没裁动（如实）

- 已从 `vmlinux.a` 内建对象中**确实消失**：MTD 整链（NAND/DENALI/BRCMNAND/SPI_NOR/FTL/BCH）、HID（含 hid-debug 96 KB）、
  `input/mouse`、`KEXEC/CRASH_DUMP/PROC_VMCORE`。
- **被 select 拖住、本轮未生效**：`CONFIG_ARCH_OMAP`、`CONFIG_SERIO`（mach-omap2 仍有 87 个内建对象）。
  要继续腾空间得从 select 链反向摘驱动，不是一行 `--set-val n` 能了事的，留给后续。
- **刻意不动 `CONFIG_DRM/DRM_LIMA`**：本轮实测发现 `CONFIG_SYSFB` 是无提示 bool，
  靠 `DRM=y` 一路 select 进来；把 DRM 降级成 `=m`，`SYSFB` 直接从配置里消失。
  本链 Android 显示入口是 LK 遗留 framebuffer + `FB_SIMPLE`，属硬依赖——
  **配置门当场把这次降级拦下**，没有拿显示路径换体积。
- 裁完必须 `make clean` 再全量重编（本项目反复踩增量编译用陈旧 .o 的坑）。

## 3. 验证方式（不是只看 .config）

1. `scripts/extract-ikconfig vmlinux` 从**产物**反抠配置，逐项核对 A9 硬前置 26 项
   （binder/ashmem/SELinux/BPF_SYSCALL+SECCOMP_FILTER/cgroup 族/命名空间族/FUSE/EXT4_FS_SECURITY/
   DM_VERITY/LOOP/dma-heap/KEYS/FS_ENCRYPTION/ZRAM/IPV6/XFRM_USER/IKCONFIG_PROC/
   FB+FB_SIMPLE+SYSFB/MACH_MT6572/ARM_APPENDED_DTB/GENERIC_IRQ_CHIP/RFKILL_INPUT）全 `=y`。
2. `scripts/diffconfig before after` 人审 248 行变更，确认没有波及 A9 网络/存储项。
3. 打完镜像**从镜像里反解内嵌 DTB** 复核 `bus-width`/`initrd-end`/`androidboot` ——
   注意 dtc 输出是 `<0x01>` 形式，必须按数值比，按字符串比会假失败（本轮就踩过一次）。

## 4. 勘误：P1 的镜像 C 不是"A + androidboot"

P1 台账写的是 "C 变体 = A + `androidboot.*`（1-bit 保持不变）"。本轮把 A/B/C 三张镜像的内嵌 DTB
逐个反解实测：**A=bus-width 1，B=4，C=4**。即 C 实际是 **B + androidboot**——
出 B 时 dts 留在 4-bit 状态，C 只是在其上追加了 bootargs，于是 C 相对 A 有两处变量，
A/B/C 差分归因被破坏。P2 的 `z1_a9_p2.sh` 因此把变体做成显式参数并在打包后反解复核。

## 5. 工具入库

- `tools/z1/z1_a9_slim.sh`：裁剪 + `olddefconfig` + A9 配置门回归（不过门就拒绝编译）。
  内含两条本轮新踩的坑：`make "$ARCH"=arm` 会展开成 `make arm=arm` 从而按宿主 x86 重生成整份
  `.config`；`make` 输出管道给 `tail` 会把失败退出码吞掉。
- `tools/z1/z1_a9_p2.sh`：`pack <1b|4b> <镜像>` / `budget` / `check`。
  ramdisk 用 `A9_RAMDISK=` 覆盖，脚本按实际 body **现算 initrd-end、改写 dts、重编 dtb、复核镜像内嵌 DTB**，
  并强制 boot 分区 6 MB 预算闸门——A9 generic ramdisk 一到，一条命令出可刷 A9 boot.img。

## 6. 板上判读（刷 D1 后看串口）

与 P1 同：读 `RESULT_STEP_*`（binder/hwbinder/vndbinder、ashmem、timerfd BOOTTIME、cgroup 族、
tmpfs xattr、securityfs 挂载、dma-heap 分配、`ion_dev`/`sw_sync_dev` 预期 FAIL、
`binderfs` 预期 ABSENT）与 `[a9cfg]` 段。**额外加一条本轮专属判据**：

```sh
time dd if=/dev/mmcblk0p4 of=/dev/null bs=1M count=32
```

用来给 D1(1-bit) 定吞吐基线，之后与 D4 对差。若 D1 启动异常，先按 v13.7/ s6_v8 的老路子读
`~ #` 提示符是否出现，再怀疑本轮裁剪。

## 7. 仍未闭合的（阻塞不在本机）

本机缺 A9 userspace：LOS16 源树与 S1D–S1I 产物都在另一台设备，GitHub Releases 里没有任何 a9 件
（本轮实测确认远端 6 个分支最新仍是 `wip/a9-mainline=1dd39867a`，无新增）。
A9 的 `generic ramdisk`（first-stage init）+ `system.img` 必须在那台机器上产出，
判据与撤补丁清单见 `A9_SYSTEM_MIGRATION_CHECKLIST.md`，接口契约见 `A9_DMA_HEAP_CONTRACT.md`。
ramdisk 一到本机，`A9_RAMDISK=<文件> tools/z1/z1_a9_p2.sh pack 1b out/boot_mainline_a9p3.img` 即出片。
