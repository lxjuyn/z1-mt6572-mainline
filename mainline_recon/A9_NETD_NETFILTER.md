# A9 网络面缺口：legacy iptables 门没开（2026-10-18 实测定位并修复）

## 1. 结论

本机 mainline 配置里 `CONFIG_IP_NF_IPTABLES=y`、`CONFIG_NETFILTER_ADVANCED=y` 看着"网络都齐了"，
但**一个 iptables 表都没有**：本树(7.0-rc7)里 `IP_NF_FILTER / IP_NF_MANGLE / IP_NF_NAT / IP_NF_RAW`
以及 IPv6 的对应项，全部 `depends on IP_NF_IPTABLES_LEGACY`，而最上层的
`NETFILTER_XTABLES_LEGACY`（bool，`depends on !PREEMPT_RT`）没开，于是整条链在 olddefconfig 里
被静默抹掉 —— `.config` 里连符号都不出现（不是 `is not set`，是 ABSENT，所以按常规 grep
"is not set" 的审计法会漏过去）。

A9 的 `netd` 开机就通过 `iptables-restore` 下发 `filter/nat/mangle` 三张表的规则；
缺表 = `netd` 起不来 = `system_server` 拖死。stock 线从来不被这个问题咬到，
因为原厂 3.4.67 时代的 Kconfig 没有这个 legacy 门。

## 2. 同批挖到的另外两个 Android 专属缺口

| 缺口 | 后果 | 为什么之前没暴露 |
|---|---|---|
| `# CONFIG_NETDEVICES is not set` | `TUN`/`NET_CORE` 与所有网卡驱动都挂在 `if NETDEVICES` 下 → `/dev/net/tun` 不存在，VpnService 直接废 | 探针/busybox ramdisk 不需要 tun。**注意 loopback 不在这条链上**：`drivers/net/Makefile` 第 23 行是 `obj-$(CONFIG_NET) += loopback.o`，`lo` 一直都在，别拿"没有 lo"当开这个符号的理由 |
| `CONFIG_MEDIATEK_WATCHDOG` 未开 | `device/jiabo/z1/rootdir/init.z1.rc` 里 `service watchdogd /sbin/watchdogd` 依赖 `/dev/watchdog`；LK 会留下已使能的硬件 WDT | 调试 ramdisk 不跑 watchdogd。该符号 `default ARCH_MEDIATEK`，但被 `drivers/watchdog/Kconfig` 的菜单可见性挡了一下，实际没进配置 |

另外顺手补了 `CONFIG_FS_POSIX_ACL` + `CONFIG_EXT4_FS_POSIX_ACL`（A9 `/data` 与 sdcard FUSE
的权限映射要用 ACL，stock 线那份是 `is not set`）。

## 3. 已修正内容

`tools/z1/z1_a9_slim.sh` 新增 `android` 动作（52 项，逐项注明"谁用它"），并入配置门：
`NETDEVICES/NET_CORE/TUN`、`NETFILTER_XTABLES_LEGACY`、`IP_NF_{LEGACY,FILTER,MANGLE,NAT,RAW,TARGET_*}`、
`IP6_NF_*` 同族、`NF_CONNTRACK_{MARK,EVENTS,TIMESTAMP,TIMEOUT,ZONES,SECMARK}`、`NF_SOCKET_{IPV4,IPV6}`、
netd 用到的 `xt match/target`（owner/mac/multiport/comment/conntrack/connmark/socket/cgroup/limit/tcpmss、
MARK/CONNMARK/DSCP/LOG/NFLOG/TCPMSS/NAT）与 `MEDIATEK_WATCHDOG`、ACL 两项。
派生项（`NF_NAT`、`NF_NAT_MASQUERADE` 这类无提示 bool）不主动写，只在校验里看是否真被 select 出来。

代价：zImage `4,555,264 → 4,608,512`（+53,248 B）；boot 余量 `434,176 → 380,928 B`；
A9 generic ramdisk 可用上限 **1,659,052 B**。全部内建，不留给运行期模块加载。

## 4. device 层交付物（本机新产出）

`android9/mainline/rootdir/`：从 stock 分支取回 mainline 那套（`init.z1.rc`、
`init.z1.network.rc`、`fstab.z1`、`manifest.xml`、`z1_usb_setup.sh`），并**补上缺失的
`ueventd.z1.rc`** —— 原仓里 mainline 侧根本没有这份文件，只有 stock 那份（里面全是
`/dev/ion`、`/dev/mali` 这类 mainline 上不存在的节点，而 mainline 需要的
`hwbinder/vndbinder/dma_heap/tun/watchdog/input` 一个都没写）。

## 5. `tools/z1/z1_a9_devcheck.sh`（本机可执行的证据）

把 device 层的每条假设拿去对内核事实：
- ueventd 引用的每个 `/dev/X` → 对应 CONFIG 符号是否 y/m；
- binder 三域到底谁建节点：核实 `binder.c` 里 `binder_devices_param = CONFIG_ANDROID_BINDER_DEVICES`
  且 `binder_init()` 有 `!IS_ENABLED(CONFIG_ANDROID_BINDERFS)` 分支去 `init_binder_device()`→`misc_register()`。
  **注意不能 grep "hwbinder"**——名字来自配置字符串，源码里没有这个名字（我第一版就这么误判过）；
- `androidboot.hardware=z1`/permissive 必须在 **dts**（本链 LK 不转发 boot 头 cmdline）；
- `functionfs` 能否 mount（`USB_F_FS=y`，类型在 `f_fs.c` 内注册）；zram、ext4 ACL、`&mmc0 status okay`；
- `xt_quota2.ko`/`xt_IDLETIMER.ko` 存在且 vermagic 与 `kernelrelease` 同源；
- manifest 用 `hwbinder` transport 的内核前置成立，且探针确实覆盖 hwbinder/vndbinder/ashmem/dma_heap/timerfd_boottime。

当前结果：`DEVCHAIN_FAIL=0`（A/B/C/D 四轮里第一次全绿）。
