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

## 3. 顺带发现的既有缺陷：clean 构建必然断链（非 A9 引入）

现象：新 worktree 从 `ffdff0653` 检出后 `make clean` + 全量构建，链接报
`net/rfkill/core.o: undefined reference to rfkill_handler_init/exit`。

证据链：
- 主树 `fork_linux/net/rfkill/` 有 `input.o`（8-30 构建遗留），全量 clean 的 worktree **不生成** `input.o`；
- 两树 `net/rfkill/Makefile` 逐字节一致（`diff` 无输出）；
- `config RFKILL_INPUT` 仍在 `net/rfkill/Kconfig:21`，`.config` 也是 `=y`，`core.c` 三处 `#ifdef CONFIG_RFKILL_INPUT` 点亮了调用点，但 `include/config/rfkill/` 目录为空、`input.o` 未进对象列表。

结论：**这份提交在自身配置下无法干净构建**，主树此前是靠一个陈旧 `input.o` 混过链接的——正是本项目两次踩过的"陈旧产物伪装成有效产物"。
处置：关闭 `CONFIG_RFKILL_INPUT`（仅把按键事件映射成 rfkill 开关，Android 9 不用、Z1 也无对应实体键），使配置自洽可 clean 构建。提交 `a394377a9`。

## 4. 尚未证明 / 待板上定论

- `nm vmlinux` 与 `.ko` 的命中（证③）在本文件构建完成后补记。
- 探针的板级结论（binder/hwbinder/vndbinder/ashmem/timerfd/cgroup/xattr/seccomp/dma-heap）见 `A9_P1_FLASH_STEPS_*.md` 的判读表。
- `/dev/ion` 预期 FAIL(-2)：主线已删 ION，Pie gralloc 若坚持 ION ABI 则需要垫片，方案见 `A9_DMA_HEAP_CONTRACT.md`。
