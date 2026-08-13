#!/bin/bash
# z1_modprobe_connsys.sh — Z1 (MT6572) CONNSYS WiFi/BT 模块加载 (linux 启动后 insmod)
#
# 加载顺序 (依赖关系): btif → wmt → bt → cfg80211 → wlan_gen2
# - mtk_stp_wmt_soc.ko 在 insmod 时做 WMT init 并注册 STP 传输 (依赖 btif 已就位)
# - mtk_stp_bt_soc.ko 注册 BT 通道 /dev/stpbt
# - wlan_gen2.ko 的 platform probe 只把 WLAN 回调注册进 WMT, 硬件在 func-on 才碰
#
# 用法: sh z1_modprobe_connsys.sh [模块目录]
# 默认模块目录: /root/connsys (ramdisk 部署位置)
# 环境变量 Z1_MODS_DIR 可覆盖
#
# ⚠️ busybox 限制: 无 tee / 无 lsmod, 一律改用 echo 重定向 + cat /proc/modules
# 只用 busybox 有的命令: cat/echo/insmod/mknod/ls/grep/[/:/test

set -u
MODS_DIR="${Z1_MODS_DIR:-/root/connsys}"
LOG=/tmp/connsys_load.log
: > "$LOG"

log() { echo "$@" >> "$LOG"; echo "$@"; }

log "[connsys] 模块目录: $MODS_DIR"
[ -d "$MODS_DIR" ] || { echo "ERROR: $MODS_DIR 不存在" >> "$LOG"; echo "ERROR: $MODS_DIR 不存在"; exit 1; }

# 前置: cfg80211 (wlan 需要), 通常内核已内编则跳过
if ! ls /sys/module/cfg80211 >/dev/null 2>&1; then
    log "[connsys] 加载 cfg80211..."
    insmod "$MODS_DIR/cfg80211.ko" 2>/dev/null || echo "  (cfg80211 内核内编或加载失败, 继续)" >> "$LOG"
fi

load() {
    ko="$1"
    if [ -f "$MODS_DIR/$ko" ]; then
        log "[connsys] insmod $ko ..."
        if insmod "$MODS_DIR/$ko" >> "$LOG" 2>&1; then
            log "  OK: $ko"
        else
            rc=$?
            log "  FAIL: $ko (rc=$rc) — 继续下一个, 记录错误"
        fi
    else
        log "[connsys] 缺失 $ko (跳过)"
    fi
}

# 1. BTIF 传输 (PIO+APDMA, 绑定 mediatek,btif)
load mtk_btif_drv.ko

# 2. WMT/STP 控制面 + 固件下载 (绑定 mediatek,mt6572-consys)
#    ⚠️ 绝不可 rmmod, 会不可中断挂死 → 需 reboot
load mtk_stp_wmt_soc.ko

# 3. BT 通道 /dev/stpbt
load mtk_stp_bt_soc.ko

# 4. WiFi func ctrl /dev/wmtWifi
load mtk_wmt_wifi_soc.ko

# 5. cfg80211 wlan0 (绑定 mediatek,wifi)
load wlan_gen2.ko

# 创建设备节点 (若 mdev 没自动建)
[ -e /dev/stpwmt ] || mknod /dev/stpwmt c 190 0 2>/dev/null
[ -e /dev/stpbt ]  || mknod /dev/stpbt  c 191 0 2>/dev/null
[ -e /dev/wmtWifi ] || mknod /dev/wmtWifi c 195 0 2>/dev/null

echo "=== 加载结果 (cat /proc/modules) ===" >> "$LOG"
cat /proc/modules | grep -aE "mtk_|wlan|wmt" >> "$LOG"

echo "=== 验证 ===" >> "$LOG"
echo "BTIF 传输: $(ls /sys/bus/platform/devices/ 2>/dev/null | grep -c btif) 个 btif 设备" >> "$LOG"
ls /dev/stpwmt /dev/stpbt /dev/wmtWifi 2>/dev/null >> "$LOG"
echo "WiFi: $(ls /sys/class/net/ 2>/dev/null | grep -c wlan) 个 wlan 接口 (需 func-on 后出现)" >> "$LOG"

log "[connsys] 完成. 日志: $LOG"
log "[connsys] 下一步: echo 1 > /dev/wmtWifi (WiFi func-on), 固件部署见 scripts/deploy_connsys_fw.sh"
