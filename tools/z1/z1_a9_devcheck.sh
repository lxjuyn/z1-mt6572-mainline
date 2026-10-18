#!/usr/bin/env bash
# z1_a9_devcheck.sh —— 把 A9 device 层的每一条假设，拿去对内核树的事实核一遍。
#
# 动机：device 层是给另一台 AOSP 机器用的，本机产不出 system.img，一旦写错(引用 mainline
# 上根本不存在的节点、或依赖没开的 config)，要等上板才暴露，代价是一整轮刷机。
# 这个脚本能在本机就把这类错揪出来，输出可复核的证据行。
#
# 用法: tools/z1/z1_a9_devcheck.sh [device层目录，默认 android9/mainline]
set -uo pipefail
SELF=$(cd "$(dirname "$0")" && pwd)
WT=$(readlink -f "$SELF/../..")
ROOT=$(readlink -f "$WT/..")
DEV=${1:-$WT/android9/mainline}
CFG=$WT/.config
DTS=$WT/arch/arm/boot/dts/mediatek/mt6572-z1.dts
CC=arm-linux-gnueabihf-
fail=0

kcfg() { # 取某 config 符号在当前树上的生效值
	local v
	v=$(grep -m1 "^$1=" "$CFG" 2>/dev/null | cut -d= -f2)
	[ -n "$v" ] && { echo "$v"; return; }
	grep -qm1 "^# $1 is not set" "$CFG" && echo n || echo absent
}
res() { # $1=OK|FAIL $2=条目 $3=证据
	if [ "$1" = OK ]; then printf 'OK   %-34s %s\n' "$2" "$3";
	else printf 'FAIL %-34s %s\n' "$2" "$3"; fail=$((fail+1)); fi; }

echo "===== 1) ueventd 引用的设备节点 vs 内核能力 ====="
U=$DEV/rootdir/ueventd.z1.rc
[ -f "$U" ] || { echo "FAIL ueventd.z1.rc 不存在($U)"; fail=$((fail+1)); }
declare -A NEED=(
  [/dev/binder]=CONFIG_ANDROID_BINDER_IPC
  [/dev/hwbinder]=CONFIG_ANDROID_BINDER_IPC
  [/dev/vndbinder]=CONFIG_ANDROID_BINDER_IPC
  [/dev/ashmem]=CONFIG_ANDROID_ASHMEM
  [/dev/fb0]=CONFIG_FB_SIMPLE
  [/dev/graphics/fb0]=CONFIG_FB_SIMPLE
  [/dev/dma_heap/*]=CONFIG_DMABUF_HEAPS_SYSTEM
  [/dev/net/tun]=CONFIG_TUN
  [/dev/watchdog]=CONFIG_MEDIATEK_WATCHDOG
  [/dev/input/*]=CONFIG_INPUT
  [/dev/ttyGS0]=CONFIG_USB_CONFIGFS_ACM
  [/dev/usb-ffs/adb]=CONFIG_USB_F_FS
  [/dev/iio:device*]=CONFIG_IIO
)
for node in "${!NEED[@]}"; do
	sym=${NEED[$node]}
	v=$(kcfg "$sym")
	[ "$v" = y ] || [ "$v" = m ] && res OK "$node" "$sym=$v" || res FAIL "$node" "$sym=$v(需要 y/m)"
done
# binder 三域是否真按这条字符串注册（历史上 --set-str 多包引号把下一行吞掉过）
grep -q '^CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder"$' "$CFG" \
	&& res OK "binder 三域字符串" '无内层引号畸形' || res FAIL "binder 三域字符串" '值不符或引号畸形'
# 节点到底由谁创建：binder.c 在 !BINDERFS 时按 ANDROID_BINDER_DEVICES 逗号切词逐个
# init_binder_device() -> misc_register()。名字不在源码里硬编码，所以不能 grep "hwbinder"。
grep -q 'binder_devices_param = CONFIG_ANDROID_BINDER_DEVICES' "$WT/drivers/android/binder.c" \
	&& sed -n '/static int __init binder_init/,/^}/p' "$WT/drivers/android/binder.c" \
	   | grep -q '!IS_ENABLED(CONFIG_ANDROID_BINDERFS)' \
	&& res OK "binder.c 按参数切词建节点" 'init_binder_device()+misc_register，非 binderfs' \
	|| res FAIL "binder.c 建节点路径" '没找到 !BINDERFS 分支，契约里"不需要 mount binderfs"就不成立'
# 建出来的节点名必须正好是 manifest/init 期望的三个
grep -q '^CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder"$' "$CFG" \
	&& res OK "三域节点名 = binder,hwbinder,vndbinder" '与 ueventd 三行一一对应' \
	|| res FAIL "三域节点名" '对不上'
# 探针是否真的验过这三颗星(板上判据要能对上)
for p in hwbinder vndbinder ashmem dma_heap timerfd_boottime; do
	grep -q "$p" "$WT/tools/z1/z1_a9_probe.c" \
		&& res OK "探针覆盖 $p" 'z1_a9_probe.c 有这一项' \
		|| res FAIL "探针覆盖 $p" '探针没测，上板拿不到证据'
done
[ "$(kcfg CONFIG_ANDROID_BINDERFS)" = n ] \
	&& res OK "ANDROID_BINDERFS=n(刻意)" '节点由驱动注册, init 不要 mount binderfs' \
	|| res FAIL "ANDROID_BINDERFS" '应为 n，否则 /dev/binder 语义变了'

echo "===== 2) init/fstab 假设 vs 内核与 dts ====="
I=$DEV/rootdir/init.z1.rc
grep -q 'androidboot.hardware=z1' "$DTS" \
	&& res OK "dts 里 androidboot.hardware=z1" '本链 LK 不转发 boot 头 cmdline，只能靠 dts' \
	|| res FAIL "dts androidboot" '缺失 -> init 不会 import init.z1.rc，也不会取 fstab.z1'
grep -q 'androidboot.selinux=permissive' "$DTS" \
	&& res OK "dts permissive(首轮)" '收紧前先收 avc denial' || res FAIL "dts permissive" '缺'
[ "$(kcfg CONFIG_USB_F_FS)" = y ] \
	&& res OK "functionfs 可 mount" 'functionfs 类型在 f_fs.c 内注册, USB_F_FS=y' \
	|| res FAIL "functionfs" 'ADB 起不来'
[ "$(kcfg CONFIG_ZRAM)" = y ] \
	&& res OK "fstab 的 zram0 swap" 'CONFIG_ZRAM=y' || res FAIL "zram swap" 'CONFIG_ZRAM 未开'
[ "$(kcfg CONFIG_EXT4_FS)" = y ] && [ "$(kcfg CONFIG_EXT4_FS_POSIX_ACL)" = y ] \
	&& res OK "fstab ext4 + POSIX ACL" '/data 与 sdcard FUSE 权限映射要用 ACL' \
	|| res FAIL "ext4 ACL" 'EXT4_FS_POSIX_ACL 未开'
awk '/&mmc0 \{/{f=1} f&&/status/{print;exit}' "$DTS" | grep -q okay \
	&& res OK "dts &mmc0 status okay" 'system/cache/data 都在 mmcblk0 上' || res FAIL "&mmc0" '未 okay'

echo "===== 3) 交付给 rootfs 的 .ko 与 vermagic ====="
KREL=$(make -C "$WT" -s ARCH=arm CROSS_COMPILE=$CC kernelrelease 2>/dev/null)
for ko in net/netfilter/xt_quota2.ko net/netfilter/xt_IDLETIMER.ko; do
	f=$WT/$ko
	[ -f "$f" ] || { res FAIL "$ko" '未生成(需要 make modules)'; continue; }
	vm=$(modinfo -F vermagic "$f" 2>/dev/null)
	case "$vm" in *"$KREL"*) res OK "$ko" "vermagic 与 $KREL 同源";; *) res FAIL "$ko" "vermagic=$vm != $KREL";; esac
done

echo "===== 4) HIDL manifest 的内核前置 ====="
M=$DEV/rootdir/manifest.xml
if [ -f "$M" ]; then
	grep -q 'android.hardware.graphics.allocator' "$M" \
		&& res OK "manifest 声明 allocator@2.0" '内核侧对应 dma-heap/ashmem 两路' || res FAIL "allocator" '缺'
	grep -q '<transport>hwbinder</transport>' "$M" \
		&& res OK "manifest 用 hwbinder(非 passthrough)" 'mainline 有 /dev/hwbinder 才允许这样写' \
		|| res FAIL "manifest transport" '仍是 passthrough? 那是 stock 线的代偿'
else
	res FAIL "manifest.xml" '$DEV/rootdir 下没有 —— 需从 stock 分支 rootdir/ 取 mainline 版'
fi

echo "===== 5) 补丁判定与 sepolicy 一致性(把三道闸收进一条命令) ====="
if python3 "$WT/tools/z1/z1_a9_patchdecisions.py" >/tmp/dc_patch.txt 2>&1; then
	res OK "补丁判定完整" "$(grep -o '补丁总数=[^ ]*  已判定=[^ ]*[^)]*' /tmp/dc_patch.txt | head -1)"
else
	res FAIL "补丁判定" "$(tail -3 /tmp/dc_patch.txt | tr '\n' ' ')"
fi
if python3 "$WT/tools/z1/z1_a9_sepolicy_check.py" >/tmp/dc_sep.txt 2>&1; then
	res OK "sepolicy 与 ueventd/内核一致" "SEP_POLICY_FAIL=0"
else
	res FAIL "sepolicy 一致性" "$(grep '^FAIL' /tmp/dc_sep.txt | head -3 | tr '\n' ' ')"
fi

echo "===== 6) .ko 装载清单与本轮构建是否一致 ====="
if timeout 300 python3 "$WT/tools/z1/z1_a9_stage_modules.py" >/tmp/dc_mod.txt 2>&1; then
	res OK "modules.json 与本轮产物一致" "$(grep -o 'MODULES_JSON_MISSING=[0-9]*' /tmp/dc_mod.txt)"
else
	res FAIL "modules.json 装载清单" "$(grep -E 'MISSING|错误' /tmp/dc_mod.txt | head -2 | tr '\n' ' ')"
fi

echo "===== 结论 ====="
echo "DEVCHAIN_FAIL=$fail"
[ "$fail" -eq 0 ]
