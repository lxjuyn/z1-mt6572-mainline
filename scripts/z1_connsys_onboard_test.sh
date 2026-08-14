#!/bin/sh
# z1_connsys_onboard_test.sh — Z1 (MT6572) CONNSYS WiFi/BT func-on 上板一键验证 (V2, 2026-08-14)
#
# 用法 (板子开机进 busybox rootfs 后手动执行, 不刷写不碰 download 线):
#   sh /root/connsys/z1_connsys_onboard_test.sh
#   (脚本由 PC 端 tools/repack_ramdisk_connsys.sh 打进 ramdisk → /root/connsys/)
#
# busybox 兼容: 只用 echo/cat/insmod/mknod/ls/grep/printf/sleep/ifconfig + ash 内建
#   (本机 busybox 无 tee/lsmod; 有 ip 但本脚本统一用 ifconfig)
#   无线工具 iw (nl80211, 静态 ARM) 在 /usr/bin/iw — 脚本检测存在才执行 scan
#
# 流程 (每步 [OK]/[FAIL]):
#   1. /root/connsys 存在?
#   2. 跑 z1_modprobe_connsys.sh (insmod btif→wmt_soc→bt_soc→wmt_wifi_soc→cfg80211→wlan_gen2
#      并启动 mtk_stp_launcher -m 3 -p /system/etc/firmware/)
#   3. 等 launcher 就绪 (轮询 /proc/<pid>/cmdline 含 mtk_stp_launcher, 超时 20s)
#      — 注: launcher 是常驻 daemon, SET_STP_MODE + pwr-on(含 patch 下载)是启动期动作;
#        进程存活 + 后续 func-on 轮询 wlan0 是 busybox-safe 的就绪代理
#   4. echo 1 > /dev/wmtWifi (WiFi func-on)
#   5. 等 func-on 完成 (轮询 /sys/class/net/wlan*, 超时 30s)
#   6. ifconfig wlan0 up + iw scan (iw 存在则跑, 否则提示)
#   7. 蓝牙 /dev/stpbt 节点验证
#
# 依赖 (ramdisk 内, 由 repack_ramdisk_connsys.sh 部署):
#   /root/connsys/       5 个 .ko + cfg80211.ko + mtk_stp_launcher + 本脚本
#   /lib/firmware/WIFI_RAM_CODE_MT6582
#   /system/etc/firmware/{WMT_SOC.cfg,ROMv1_patch_0_hdr.bin,ROMv1_patch_1_hdr.bin}
#   /etc/firmware/nvram/WIFI   (缺 → 随机 MAC + 单播 RX 丢帧, scan 仍可测)

CONNSYS=/root/connsys
MODPROBE=$CONNSYS/z1_modprobe_connsys.sh
WIFI_DEV=/dev/wmtWifi
LOG=/tmp/connsys_onboard.log

ok()   { echo "  [OK]   $*"; }
fail() { echo "  [FAIL] $*"; }
step() { echo ""; echo "=== step $1: $2 ==="; }

: > "$LOG"

# wait_until <描述> <超时秒> <命令...> — 每 1s 轮询直到命令返回 0
wait_until() {
    desc=$1; tmo=$2; shift 2
    n=0
    while [ "$n" -lt "$tmo" ]; do
        if "$@"; then
            ok "$desc (${n}s)"
            return 0
        fi
        sleep 1
        n=$((n + 1))
    done
    fail "$desc (等 ${tmo}s 超时)"
    return 1
}

# launcher 进程在跑? 用 /proc 扫 (不依赖 pgrep)
launcher_alive() {
    for c in /proc/[0-9]*/cmdline; do
        [ -r "$c" ] || continue
        if grep -a 'mtk_stp_launcher' "$c" >/dev/null 2>&1; then
            return 0
        fi
    done
    return 1
}

# 有 wlan* 接口?
wlan_present() {
    ls /sys/class/net/ 2>/dev/null | grep '^wlan' >/dev/null
}

echo ""
echo "=== Z1 CONNSYS WiFi/BT 上板一键验证 (V2) ==="
echo "开机 uptime: $(cat /proc/uptime) s   日志: $LOG   (busybox 无 date, 用 uptime 计时)"

# --- 1 ---
step 1 "确认 $CONNSYS"
if [ -d "$CONNSYS" ]; then
    ok "$CONNSYS 存在"
else
    fail "$CONNSYS 不存在 — 先重建 ramdisk 部署 (PC 端 tools/repack_ramdisk_connsys.sh)"
    exit 1
fi

# --- 2 ---
step 2 "运行 z1_modprobe_connsys.sh (insmod 6 模块 + 启动 launcher)"
if [ -f "$MODPROBE" ]; then
    echo "  [RUN] sh $MODPROBE"
    sh "$MODPROBE"
    echo "  [INFO] 加载日志: /tmp/connsys_load.log"
else
    fail "未找到 $MODPROBE — 模块未加载, 继续检查现状"
fi

# 模块加载数量核对 (cat /proc/modules)
EXPECT="cfg80211 mtk_btif_drv mtk_stp_wmt_soc mtk_stp_bt_soc mtk_wmt_wifi_soc wlan_gen2"
n=0
for m in $EXPECT; do
    if grep "^$m " /proc/modules >/dev/null 2>&1; then
        n=$((n + 1))
    fi
done
echo "  [INFO] /proc/modules 已加载 $n/6 期望模块"
if [ "$n" -ge 5 ]; then
    ok "模块加载充分 (≥5, 含 cfg80211)"
else
    fail "模块不足 (需 ≥5) — cat /tmp/connsys_load.log 看 insmod 失败项"
fi

# --- 3 ---
step 3 "等 mtk_stp_launcher 就绪"
if wait_until "launcher 进程存活" 20 launcher_alive; then
    echo "  [INFO] launcher 启动期动作 (SET_STP_MODE + pwr-on patch 下载) 约 1-2s, 再等 2s ..."
    sleep 2
    ok "launcher 就绪 (若 func-on 仍失败, 见 step 5 排查 — 竞态细节见 mainline_recon/LAUNCHER_TIMING_20260814.md)"
else
    fail "launcher 未起 — WiFi func-on 会死 (patch 无人喂, BT 仍可用). 查: $CONNSYS/mtk_stp_launcher 存在? 手动: $CONNSYS/mtk_stp_launcher -m 3 -p /system/etc/firmware/ &"
fi

# --- 4 ---
step 4 "WiFi func-on (echo 1 > $WIFI_DEV)"
if [ -e "$WIFI_DEV" ]; then
    ok "$WIFI_DEV 节点存在"
else
    fail "$WIFI_DEV 缺失 — 应 mknod c 155 0 (z1_modprobe_connsys.sh 已建), 检查 mdev"
fi
echo "  [ACTION] echo 1 > $WIFI_DEV"
if echo 1 > "$WIFI_DEV" 2>&1; then
    ok "func-on 指令已下发"
else
    fail "写入 $WIFI_DEV 失败 — 节点/驱动未就绪"
fi

# --- 5 ---
step 5 "等 func-on 完成 (wlan0 接口注册)"
if wait_until "wlan0 接口注册" 30 wlan_present; then
    echo "  无线接口: $(ls /sys/class/net/ | grep '^wlan')"
else
    echo "  排查 (按序):"
    echo "    1. cat /tmp/connsys_load.log — insmod 是否全 OK / launcher 启动返回?"
    echo "    2. ls /system/etc/firmware/ /lib/firmware/ — ROMv1_patch_* / WMT_SOC.cfg / WIFI_RAM_CODE_MT6582 在不在"
    echo "    3. ls /etc/firmware/nvram/ — WIFI (NVRAM, 缺则 wlan0 上不了)"
    echo "    4. dmesg | grep -iE 'wmt|wlan|wifi' — 内核错误 (本机 busybox 有 dmesg)"
fi

# --- 6 ---
step 6 "ifconfig wlan0 up + iw scan"
WLAN0=$(ls /sys/class/net/ 2>/dev/null | grep '^wlan' | head -1)
if [ -n "$WLAN0" ]; then
    if ifconfig "$WLAN0" up 2>&1; then
        ok "ifconfig $WLAN0 up"
    else
        fail "ifconfig $WLAN0 up 失败 — NVRAM/驱动状态问题"
    fi
else
    fail "无 wlan* 接口, 跳过 up + scan"
fi

IW=""
for p in /usr/bin/iw /bin/iw /sbin/iw; do
    if [ -x "$p" ]; then IW="$p"; break; fi
done
if [ -n "$WLAN0" ] && [ -n "$IW" ]; then
    echo "  [RUN] $IW dev $WLAN0 scan (nl80211, 静态 ARM; wlan_gen2 只支持 nl80211 scan)"
    $IW dev "$WLAN0" scan 2>&1 | grep -aE 'BSS |SSID:|signal:|freq:|last seen'
    echo "  [INFO] scan 摘要已过滤 (BSS/SSID/signal/freq). 连接: $IW dev $WLAN0 connect <SSID> ; 查看: $IW dev"
elif [ -n "$WLAN0" ]; then
    echo "  [提示] 未找到 iw (静态 iw 应在 /usr/bin/iw, 由 repack_ramdisk_connsys.sh 部署). 备选: iwlist $WLAN0 scan (WEXT, wlan_gen2 可能报不支持)"
else
    echo "  [提示] wlan0 未起, 跳过 scan"
fi

# --- 7 ---
step 7 "蓝牙节点 /dev/stpbt"
if [ -e /dev/stpbt ]; then
    ok "/dev/stpbt 存在 (major 192)"
    echo "  [INFO] 节点存在 ≠ 已上电: BT func-on 需 open /dev/stpbt 且 mtk_wcn_stp_is_ready()."
    echo "        下一步 (不在本脚本): stpbt-hci-test / BlueZ hciattach 走 stpbt 发 HCI_Reset"
else
    fail "/dev/stpbt 缺失 — 应 mknod c 192 0 (z1_modprobe_connsys.sh 已建), BT 不可用"
fi

echo ""
echo "=== 汇总 ==="
if wlan_present; then
    echo "[OK]   WiFi 链路完整: insmod → launcher → func-on → wlan0 → ifconfig up → iw scan"
    echo "[OK]   BT 节点: $([ -e /dev/stpbt ] && echo 在 || echo 缺)"
    exit 0
else
    echo "[FAIL] wlan0 未起 — 上板主目标未达成; 见 step 5 排查项"
    exit 1
fi
