# A9 内核配置门（mainline 7.0-rc7 / wip/a9-mainline）

分支：`wip/a9-mainline`，基线 `main` = `ffdff0653`（v13.5 已验证可启动配置）。
本文件记录"为了让 Android 9 userspace 有可能跑起来，内核必须提供什么"，以及**在动手过程中挖到的四个真实陷阱**（都已落在提交里）。

## 1. 符号清单（目标值）

| 组 | 符号 | 目标 | A9 里谁用它 |
|---|---|---|---|
| IPC | `ANDROID_BINDER_IPC` | y | 全系统 binder |
| | `ANDROID_BINDER_DEVICES` | `"binder,hwbinder,vndbinder"` | HIDL 需要 hwbinder/vndbinder 三个域 |
| | `ANDROID_BINDERFS` | **n（刻意保持）** | 见陷阱 3 |
| 内存 | `ANDROID_ASHMEM` | y | gralloc / `ashmem_create_region()`（主线已删，见步骤 4 移植） |
| | `KEYS`、`FS_ENCRYPTION` | y | keystore / FBE（stock 3.4.67 两者皆无） |
| 安全 | `SECURITY`、`SECURITY_NETWORK`、`SECURITY_SELINUX(+BOOTPARAM,DEVELOP)`、`SECURITYFS`、`AUDIT`、`AUDITSYSCALL` | y | init 加载 sepolicy、`setxattr security.*` 标签 |
| | `BPF_SYSCALL` + `SECCOMP_FILTER` | y | minijail / zygote seccomp（见陷阱 2） |
| cgroup | `MEMCG`、`CGROUP_FREEZER`、`CGROUP_CPUACCT`、`CPUSETS`、`CGROUP_SCHED`+`FAIR_GROUP_SCHED`、`CGROUP_DEVICE`、`CGROUP_BPF` | y | `libprocessgroup`（cpuset/freezer 是 ANR 与后台治理地基） |
| 命名空间 | `UTS_NS`、`IPC_NS`、`PID_NS`、`NET_NS` | y | zygote/app 隔离 |
| 存储 | `FUSE_FS`、`EXT4_FS_SECURITY`、`TMPFS_XATTR` | y | vold sdcard FUSE 视图、标签挂载 |
| | `BLK_DEV_DM`、`DM_VERITY`、`BLK_DEV_LOOP` | y | dm-verity / ASEC（stock 3.4.67 无 verity 目标） |
| | `DMABUF_HEAPS(_SYSTEM,_CMA)` | y | ION 缺位下的缓冲区来源，见 `A9_DMA_HEAP_CONTRACT.md` |
| 网络 | `NETFILTER`、`NETFILTER_ADVANCED`、`NETFILTER_XTABLES`、`IP_NF_IPTABLES`、`NF_CONNTRACK`、`IPV6`、`XFRM`、`XFRM_USER` | y | netd / tethering / VPN |
| | `NETFILTER_XT_MATCH_QUOTA2`、`NETFILTER_XT_TARGET_IDLETIMER` | **m** | Pie named quota 与唤醒标签（=m 省 boot 空间，netd 按需加载） |
| 内存回收 | `ZRAM`、`ZSMALLOC`、`LZ4_*`、`LZO_COMPRESS` | y | 1GB RAM 设备的 swap |
| 自证 | `IKCONFIG`、`IKCONFIG_PROC` | y | 板端 `/proc/config.gz` 直接证明编译配置，避免"编进去没有"扯皮 |

## 2. 挖到的陷阱（都已进提交）

1. **配置管线本身就不可复现（早于 A9）**：`z1_defconfig` 1365 行里有 519 条 `=y` 在工作 `.config` 未生效，`.config` 有 1222 条不在 defconfig —— 用 defconfig 重建会得到另一个内核。
   处置：从已验证 `.config` 反推 `savedefconfig`（444 行），并验证往返 `scripts/diffconfig` **差异 0 行**。提交 `3d1316611`。
2. **`.config` 有自相矛盾条目**：`SECCOMP_FILTER=y` 但依赖它的 `BPF_SYSCALL` 未开，`olddefconfig` 会静默降级。A9 的 minijail 依赖 SECCOMP_FILTER。
   处置：显式开 `BPF_SYSCALL`。提交 `10b9b05c6`。
3. **`ANDROID_BINDERFS` 必须保持关闭**：6.x/7.x 上若开 binderfs，`/dev/binder` 不再是驱动自动创建的静态节点，而要挂载 binderfs 并由 init 建节点（Android 10+ 的 `dev/binderfs` 路线）。本轮目标是最小改动拿到桌面，因此显式 `=n`，并在探针里用 `/proc/filesystems` 反查该事实。
4. **`BLK_DEV_DM` 藏在 `if MD` 里**：未开 `CONFIG_MD` 时该符号根本不出现，`--enable` 也留不住。SELinux 同理需要 `SECURITY_NETWORK`（`security/selinux/Kconfig:4` 的依赖是 `SECURITY_NETWORK && AUDIT && NET && INET`）。依赖补齐后必须**重新 `--enable`**，因为上一轮 `olddefconfig` 已把它们清掉。

## 3. 我自己引入的坑（已修正，并附误判过程）

**症状**：`make clean` 全量构建在链接期报 `net/rfkill/core.o: undefined reference to rfkill_handler_init/exit`，后又报 `drivers/irqchip/irq-omap-intc.o` / `arch/arm/mach-omap2/prm_common.o` 未定义 `irq_gc_*`、`irq_alloc_generic_chip`。

**我最初的结论是错的**：当时判断为"本仓既有缺陷，主树靠陈旧 input.o 混过链接"，并提交关闭 `CONFIG_RFKILL_INPUT`（`a394377a9`）。该结论已作废。

**真实根因（可复现证据）**：我在符号门里执行了
`scripts/config --set-str CONFIG_ANDROID_BINDER_DEVICES '"binder,hwbinder,vndbinder"'` ——
多包了一层引号，`--set-str` 自己会加引号，结果 `.config` 与 `z1_defconfig` 里写成
`CONFIG_ANDROID_BINDER_DEVICES="\"`。同步出的 `include/config/auto.conf` 第 17 行因此变成
以反斜杠结尾的续行，把**第 18 行的下一个符号吞成续行**：

```
CONFIG_ANDROID_BINDER_DEVICES=\
CONFIG_GENERIC_IRQ_CHIP=y        <- 被上一行吞掉
```

于是出现极隐蔽的分裂状态：`include/generated/autoconf.h`（C 宏，直接来自 .config）里有
`CONFIG_RFKILL_INPUT`/`CONFIG_GENERIC_IRQ_CHIP`，驱动的 `#ifdef` 生效并调用符号；而 make 变量为空，
`obj-$(CONFIG_...) += xxx.o` 不成立，`input.o`/`generic-chip.o` 根本不参与编译 → 链接必断。
判定手法（记下来，下次直接用）：在子目录 Makefile 里临时插一行
`$(info CHIP=[$(CONFIG_GENERIC_IRQ_CHIP)])`，看子 make 的真实取值；第一次给出 `CHIP=[]`
而 `auto.conf` 明明有该行，即锁定为"变量未展开"而非"配置没开"。

**修正**：值改为不带内层引号的 `'binder,hwbinder,vndbinder'`；`olddefconfig` 后子 make 视角
`CHIP=[y]`，`kernel/irq/generic-chip.o` 与 `net/rfkill/input.o` 均正常生成；
`CONFIG_RFKILL_INPUT` 恢复为 `y`（先前那次关闭属于误诊，Android 用不用它与此缺陷无关）。
**教训**：`scripts/config` 写字符串型符号时绝不能再套引号；字符串行一旦畸形，破坏面是"下一行"，
症状会出现在完全无关的子系统上。

## 4. 尚未证明 / 待板上定论

- `nm vmlinux` 与 `.ko` 的命中（证③）在本文件构建完成后补记。
- 探针的板级结论（binder/hwbinder/vndbinder/ashmem/timerfd/cgroup/xattr/seccomp/dma-heap）见 `A9_P1_FLASH_STEPS_*.md` 的判读表。
- `/dev/ion` 预期 FAIL(-2)：主线已删 ION，Pie gralloc 若坚持 ION ABI 则需要垫片，方案见 `A9_DMA_HEAP_CONTRACT.md`。


## 5. 陷阱 2 已被证伪（2026-10-18 P7 更正，保留原文不删）

上面 §2 陷阱 2 说"SECCOMP_FILTER 依赖 BPF_SYSCALL，必须显式开"。**在 7.0.0-rc7 上这不成立**：
`arch/Kconfig:660` 里 `config SECCOMP_FILTER` 是 `def_bool y`，
`depends on HAVE_ARCH_SECCOMP_FILTER && SECCOMP && NET` —— 没有 BPF_SYSCALL。
该依赖是早期 Android common kernel(3.18/4.x)的事实。

因此本树可以摘掉整个 eBPF（`BPF_SYSCALL`/`CGROUP_BPF`/`NETFILTER_BPF_LINK`/`BPF_EVENTS`/
`BPF_STREAM_PARSER`/`BPF_PRELOAD`/`DEBUG_INFO_BTF`），换来 zImage **-266,240 B**。
A9 侧不受影响的理由：minijail/zygote 用的是经典 BPF(`SO_FILTER`/`sock_fprog`，
产物里 `sk_filter*` 11 个符号仍在)；`bpfloader` 与 cgroup-bpf 是 Android 10+ 的东西；
Pie 的 netd 走 iptables + `xt_quota2`(本仓 =m)。板上由探针 `seccomp_filter` 项与
`[a9cfg]` 段自证。**GATE_Y 里的 CGROUP_BPF 已移出，GATE_N 加入 BPF_SYSCALL/CGROUP_BPF。**
