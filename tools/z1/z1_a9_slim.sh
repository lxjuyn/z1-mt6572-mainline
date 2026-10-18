#!/bin/bash
# A9 boot 分区体积工程：裁掉与 Z1 无关的内建平台代码，同时守住 A9 符号门。
# 用法: z1_a9_slim.sh [apply|build|check]
#   apply -> 改写 .config + olddefconfig + 配置门校验 + diffconfig
#   build -> make clean 后重编 zImage/dtb（改过 config 必须 clean，否则陈旧 .o 静默复用）
#   check -> 只跑配置门校验
set -u
SELF=$(cd "$(dirname "$0")" && pwd)
WT=${Z1_A9_WT:-$(readlink -f "$SELF/../..")}
BAK=${Z1_BAK:-$(readlink -f "$SELF/../../.." )/bridge_backups}
ARCH=arm
XCF="CROSS_COMPILE=arm-linux-gnueabihf-"
TS=$(date +%Y%m%d_%H%M%S)

# 关闭：与 Z1(MT6572 单板, eMMC, 无 PS/2/鼠标/NAND/OMAP) 无关的内建件
# 依据 vmlinux.a 内建 TEXT 归因: OMAP 271K / MTD+NAND 150K+ / HID 167K / input-mouse 102K / SERIO ~120K
OFF=(
  CONFIG_ARCH_OMAP CONFIG_MACH_OMAP_GENERIC
  CONFIG_MACH_MT2701 CONFIG_MACH_MT6582 CONFIG_MACH_MT6589 CONFIG_MACH_MT6592
  CONFIG_MACH_MT7623 CONFIG_MACH_MT7629 CONFIG_MACH_MT8127 CONFIG_MACH_MT8135
  CONFIG_MTD
  CONFIG_HID CONFIG_HID_GENERIC CONFIG_USB_HID CONFIG_HID_BATTERY_STRENGTH
  CONFIG_INPUT_MOUSE CONFIG_INPUT_JOYSTICK CONFIG_INPUT_TABLET
  CONFIG_SERIO CONFIG_SERIO_LIBPS2 CONFIG_SERIO_PCIPS2
  CONFIG_KEXEC CONFIG_KEXEC_CORE CONFIG_CRASH_DUMP CONFIG_PROC_VMCORE
)
# 改模块候选：本轮**不动 DRM**。
# 实测发现 CONFIG_SYSFB 是无提示 bool，靠 DRM=y 一路 select 进来；把 DRM 降级成 =m 会让
# SYSFB 从 .config 消失（配置门当场抓到）。本链的显示入口是 LK 遗留 framebuffer + FB_SIMPLE，
# 属于 Android 的硬依赖，未上板复验前不拿它换体积。若将来要裁 DRM，必须显式 --set-val CONFIG_SYSFB y
# 并单独出一个变体验证 /dev/fb0 仍在。
MOD=()
# A9 硬前置，必须保持 =y（出处 mainline_recon/A9_CONFIG_GATE.md §1）
GATE_Y=(
  CONFIG_ANDROID_BINDER_IPC CONFIG_ANDROID_ASHMEM
  CONFIG_SECURITY CONFIG_SECURITY_NETWORK CONFIG_SECURITY_SELINUX
  CONFIG_SECURITY_SELINUX_BOOTPARAM CONFIG_SECURITY_SELINUX_DEVELOP CONFIG_SECURITYFS
  CONFIG_AUDIT CONFIG_AUDITSYSCALL
  CONFIG_BPF CONFIG_BPF_SYSCALL CONFIG_SECCOMP CONFIG_SECCOMP_FILTER
  CONFIG_MEMCG CONFIG_CGROUP_FREEZER CONFIG_CGROUP_CPUACCT CONFIG_CPUSETS
  CONFIG_CGROUP_SCHED CONFIG_FAIR_GROUP_SCHED CONFIG_CGROUP_DEVICE CONFIG_CGROUP_BPF
  CONFIG_NAMESPACES CONFIG_UTS_NS CONFIG_IPC_NS CONFIG_PID_NS CONFIG_NET_NS
  CONFIG_FUSE_FS CONFIG_EXT4_FS CONFIG_EXT4_FS_SECURITY CONFIG_TMPFS CONFIG_TMPFS_XATTR
  CONFIG_BLK_DEV_DM CONFIG_DM_VERITY CONFIG_BLK_DEV_LOOP
  CONFIG_DMABUF_HEAPS CONFIG_DMABUF_HEAPS_SYSTEM CONFIG_DMABUF_HEAPS_CMA
  CONFIG_DMA_SHARED_BUFFER CONFIG_SYNC_FILE
  CONFIG_KEYS CONFIG_FS_ENCRYPTION CONFIG_ZRAM CONFIG_ZSMALLOC
  CONFIG_NETFILTER CONFIG_NETFILTER_ADVANCED CONFIG_NETFILTER_XTABLES CONFIG_IP_NF_IPTABLES
  CONFIG_NF_CONNTRACK CONFIG_IPV6 CONFIG_XFRM CONFIG_XFRM_USER
  CONFIG_IKCONFIG CONFIG_IKCONFIG_PROC
  CONFIG_FB CONFIG_FB_CORE CONFIG_FB_SIMPLE CONFIG_SYSFB CONFIG_VT
  CONFIG_INPUT CONFIG_INPUT_TOUCHSCREEN CONFIG_MMC CONFIG_MMC_BLOCK CONFIG_RTC_CLASS CONFIG_PSTORE
  CONFIG_GENERIC_IRQ_CHIP CONFIG_RFKILL CONFIG_RFKILL_INPUT
  CONFIG_USB CONFIG_USB_GADGET CONFIG_USB_CONFIGFS CONFIG_USB_F_FS CONFIG_USB_CONFIGFS_F_FS
)
GATE_M=(CONFIG_NETFILTER_XT_MATCH_QUOTA2 CONFIG_NETFILTER_XT_TARGET_IDLETIMER CONFIG_TOUCHSCREEN_EDT_FT5X06)
# A9 netd/VpnService/data 挂载真正需要的符号（本轮实测发现基线里是缺的）：
# netd 开机即下发 iptables 规则，缺表/缺 match 会让 netd 起不来，进而拖死 system_server。
# 逐项依据见 mainline_recon/A9_NETD_NETFILTER.md
ANDROID_ON=(
  # 基线里 NETDEVICES 是关的：TUN(VpnService 要的 /dev/net/tun)、NET_CORE 与所有网卡驱动
  # 都挂在 `if NETDEVICES` 下，所以 A9 一侧完全起不来。注意 loopback 不在此列 ——
  # drivers/net/Makefile 第 23 行是 `obj-$(CONFIG_NET) += loopback.o`，lo 一直都在，
  # 不要因为"没有 lo"这种错误理由去开这个符号。
  CONFIG_NETDEVICES
  CONFIG_NET_CORE
  CONFIG_TUN
  CONFIG_NETFILTER_XTABLES_LEGACY
  CONFIG_FS_POSIX_ACL CONFIG_EXT4_FS_POSIX_ACL
  CONFIG_IP_NF_IPTABLES_LEGACY CONFIG_IP6_NF_IPTABLES_LEGACY
  CONFIG_IP_NF_FILTER CONFIG_IP_NF_MANGLE CONFIG_IP_NF_NAT CONFIG_IP_NF_RAW
  CONFIG_IP_NF_TARGET_REJECT CONFIG_IP_NF_TARGET_MASQUERADE CONFIG_IP_NF_TARGET_REDIRECT
  CONFIG_IP6_NF_IPTABLES CONFIG_IP6_NF_FILTER CONFIG_IP6_NF_MANGLE CONFIG_IP6_NF_NAT
  CONFIG_IP6_NF_RAW CONFIG_IP6_NF_MATCH_AH CONFIG_IP6_NF_MATCH_IPV6HEADER
  CONFIG_IP6_NF_MATCH_FRAG CONFIG_IP6_NF_MATCH_HL CONFIG_IP6_NF_MATCH_RT
  CONFIG_IP6_NF_TARGET_REJECT CONFIG_IP6_NF_TARGET_MASQUERADE
  CONFIG_NF_CONNTRACK_MARK CONFIG_NF_CONNTRACK_EVENTS CONFIG_NF_CONNTRACK_TIMESTAMP
  CONFIG_NF_CONNTRACK_TIMEOUT CONFIG_NF_CONNTRACK_ZONES CONFIG_NF_CONNTRACK_SECMARK
  CONFIG_NF_SOCKET_IPV4 CONFIG_NF_SOCKET_IPV6
  CONFIG_NETFILTER_XT_MATCH_ADDRTYPE CONFIG_NETFILTER_XT_MATCH_COMMENT
  CONFIG_NETFILTER_XT_MATCH_CONNMARK CONFIG_NETFILTER_XT_MATCH_CONNTRACK
  CONFIG_NETFILTER_XT_MATCH_MAC CONFIG_NETFILTER_XT_MATCH_MARK
  CONFIG_NETFILTER_XT_MATCH_MULTIPORT CONFIG_NETFILTER_XT_MATCH_OWNER
  CONFIG_NETFILTER_XT_MATCH_SOCKET CONFIG_NETFILTER_XT_MATCH_TCPMSS
  CONFIG_NETFILTER_XT_MATCH_CGROUP CONFIG_NETFILTER_XT_MATCH_LIMIT
  CONFIG_NETFILTER_XT_TARGET_CONNMARK CONFIG_NETFILTER_XT_TARGET_DSCP
  CONFIG_NETFILTER_XT_TARGET_LOG CONFIG_NETFILTER_XT_TARGET_MARK
  CONFIG_NETFILTER_XT_TARGET_TCPMSS CONFIG_NETFILTER_XT_TARGET_NFLOG
  CONFIG_NETFILTER_XT_NAT
  CONFIG_MEDIATEK_WATCHDOG
)
# 静默/被 select 的派生项：不主动写，只验证真的在（写了也会被 olddefconfig 抹掉）
ANDROID_EXPECT=(CONFIG_NF_NAT CONFIG_NF_NAT_MASQUERADE CONFIG_NF_DEFRAG_IPV4 CONFIG_NF_DEFRAG_IPV6
  CONFIG_NETFILTER_XTABLES CONFIG_IP_NF_IPTABLES CONFIG_NF_CONNTRACK CONFIG_WATCHDOG_CORE
  CONFIG_INET CONFIG_XFRM CONFIG_XFRM_USER)
GATE_N=(CONFIG_ANDROID_BINDERFS CONFIG_ION CONFIG_ARM_APPENDED_DTB_NOT)
# ARM_APPENDED_DTB 是本项目启动链的命门，必须 =y，单独校验
MUST_Y_LIFELINE=(CONFIG_ARM_APPENDED_DTB CONFIG_MACH_MT6572 CONFIG_ARCH_MEDIATEK)

st() { # y|m|n|missing ; 符号整体不可见(依赖关闭)也报 n，由调用方的期望值决定死活
  local v
  v=$(grep -m1 "^$1=" "$WT/.config" 2>/dev/null | cut -d= -f2)
  if [ -n "$v" ]; then echo "$v"; return; fi
  grep -q "^# $1 is not set$" "$WT/.config" 2>/dev/null && echo n || echo missing
}

gate_check() {
  local bad=0 s
  for s in "${GATE_Y[@]}" "${MUST_Y_LIFELINE[@]}" "${ANDROID_ON[@]}"; do
    [ "$(st "$s")" = y ] || { echo "GATE-FAIL(expect y) $s=$(st "$s")"; bad=$((bad+1)); }
  done
  for s in "${ANDROID_EXPECT[@]}"; do
    v=$(st "$s"); [ "$v" = y ] || echo "GATE-WARN(派生项未生效) $s=$v"
  done
  for s in "${GATE_M[@]}"; do
    [ "$(st "$s")" = m ] || { echo "GATE-FAIL(expect m) $s=$(st "$s")"; bad=$((bad+1)); }
  done
  for s in "${GATE_N[@]}"; do
    [ "$s" = CONFIG_ARM_APPENDED_DTB_NOT ] && continue
    v=$(st "$s"); [ "$v" = n ] || [ "$v" = missing ] || { echo "GATE-FAIL(expect n) $s=$v"; bad=$((bad+1)); }
  done
  grep -q '^CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder"$' "$WT/.config" \
    || { echo "GATE-FAIL ANDROID_BINDER_DEVICES 值不符或引号畸形(A9_CONFIG_GATE 陷阱1)"; bad=$((bad+1)); }
  grep -q '^CONFIG_LOCALVERSION="-z1"$' "$WT/.config" \
    || { echo "GATE-FAIL LOCALVERSION 变了，vermagic 会失配"; bad=$((bad+1)); }
  grep -qE '^(CONFIG_LOCALVERSION_AUTO=n|# CONFIG_LOCALVERSION_AUTO is not set)$' "$WT/.config" \
    || { echo "GATE-FAIL LOCALVERSION_AUTO 非 n（会让 vermagic 尾巴多变）"; bad=$((bad+1)); }
  echo "GATE_FAIL_COUNT=$bad"
  [ "$bad" -eq 0 ]
}

case "${1:-apply}" in
  apply)
    cd "$WT" || exit 1
    mkdir -p "$BAK/a9p2_${TS}"
    cp .config "$BAK/a9p2_${TS}/config.before_slim_${TS}"
    for s in "${OFF[@]}"; do ./scripts/config --set-val "$s" n; done
    for s in "${MOD[@]}"; do ./scripts/config --set-val "$s" m; done
    # ARCH 必须以字面量传给 make；写成 "$ARCH"=arm 会展开成 `make arm=arm`，
    # 于是 olddefconfig 按宿主 x86_64 重生成整份 .config（本轮实测踩过）。
    make ARCH=arm $XCF olddefconfig >/dev/null 2>&1 || { echo "olddefconfig 失败"; exit 2; }
    if grep -qE '^CONFIG_X86_64=|^CONFIG_X86_GENERICARCH=|^CONFIG_MACH_X86=' .config; then
      echo ">>> 架构漂移：.config 里出现 x86 符号，判定 olddefconfig 跑错了架构，拒绝继续"
      cp "$BAK/a9p2_${TS}/config.before_slim_${TS}" .config
      exit 4
    fi
    cp .config "$BAK/a9p2_${TS}/config.after_slim_${TS}"
    echo "===== diffconfig (before -> after) ====="
    ./scripts/diffconfig "$BAK/a9p2_${TS}/config.before_slim_${TS}" .config
    echo "===== A9 配置门校验 ====="
    gate_check || { echo ">>> 回归失败；before 副本: $BAK/a9p2_${TS}/config.before_slim_${TS}"; exit 3; }
    echo "BACKUP_DIR=$BAK/a9p2_${TS}"
    ;;
  android)
    cd "$WT" || exit 1
    mkdir -p "$BAK/a9p3_${TS}"
    cp .config "$BAK/a9p3_${TS}/config.before_android_${TS}"
    for s in "${ANDROID_ON[@]}"; do ./scripts/config --set-val "$s" y; done
    make ARCH=arm $XCF olddefconfig >/dev/null 2>&1 || { echo "olddefconfig 失败"; exit 2; }
    if grep -qE '^CONFIG_X86_64=' .config; then echo ">>> 架构漂移，回滚"; cp "$BAK/a9p3_${TS}/config.before_android_${TS}" .config; exit 4; fi
    cp .config "$BAK/a9p3_${TS}/config.after_android_${TS}"
    echo "===== diffconfig (before -> after android) ====="
    ./scripts/diffconfig "$BAK/a9p3_${TS}/config.before_android_${TS}" .config
    echo "===== 配置门校验(含 A9 网络必需项) ====="
    gate_check || { echo ">>> 有符号没留住，见上面 diff；before 副本 $BAK/a9p3_${TS}"; exit 3; }
    echo "BACKUP_DIR=$BAK/a9p3_${TS}"
    ;;
  build)
    cd "$WT" || exit 1
    gate_check || { echo "配置门未过，拒绝编译"; exit 3; }
    echo "=== make clean ==="
    make ARCH=arm $XCF clean >/dev/null 2>&1
    # 单目标 `make mt6572-z1.dtb` 在这棵树报"没有规则"（AGENTS §3.1 记过的坑），改用 dtbs。
    # 另外绝不能把 make 的输出管道给 tail —— 那会让 $? 变成 tail 的退出码，失败被吞掉。
    echo "=== 重编 dtbs + zImage ==="
    make ARCH=arm $XCF -j"$(nproc)" dtbs zImage
    rc=$?
    echo "MAKE_RC=$rc"
    [ "$rc" -eq 0 ] || { echo ">>> 编译失败，不产出镜像"; exit 5; }
    ls -l arch/arm/boot/zImage arch/arm/boot/dts/mediatek/mt6572-z1.dtb
    sha256sum arch/arm/boot/zImage arch/arm/boot/dts/mediatek/mt6572-z1.dtb
    ;;
  check)
    cd "$WT" || exit 1; gate_check
    ;;
  *) echo "用法: $0 [apply|android|build|check]"; exit 1 ;;
esac
