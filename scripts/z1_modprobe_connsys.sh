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
# major 号与驱动源码一致: WMT_DEV_MAJOR=190, BT_DEV_MAJOR=192 (stp_chrdev_bt.c:47), WIFI_DEV_MAJOR=155 (wmt_chrdev_wifi.c:39)
[ -e /dev/stpwmt ] || mknod /dev/stpwmt c 190 0 2>/dev/null
[ -e /dev/stpbt ]  || mknod /dev/stpbt  c 192 0 2>/dev/null
[ -e /dev/wmtWifi ] || mknod /dev/wmtWifi c 155 0 2>/dev/null

# 6. WMT patch 守护进程 (func-on 必需)
#    launcher 应答芯片的 srh_patch 请求, 把 patch 文件名喂给内核 WMT;
#    没有它 → patchNum=0 → WiFi RAM code 在 func-on 入口死 (BT 不受影响)
LAUNCHER_PRESENT=0
if [ -x "$MODS_DIR/mtk_stp_launcher" ]; then
    LAUNCHER_PRESENT=1
    if pgrep -f mtk_stp_launcher >/dev/null 2>&1; then
        log "[connsys] mtk_stp_launcher 已在跑, 跳过"
    else
        log "[connsys] 启动 mtk_stp_launcher -m 3 -p /system/etc/firmware/ (后台)"
        "$MODS_DIR/mtk_stp_launcher" -m 3 -p /system/etc/firmware/ >/dev/null 2>&1 &
        log "[connsys] launcher 启动返回 pid $!"
    fi
else
    log "[connsys] 缺 $MODS_DIR/mtk_stp_launcher — WiFi func-on 将失败 (BT 仍可用)"
fi

# 6b. 等 launcher 就绪 (func-on 不撞 SET_STP_MODE 竞态窗口, CONNSYS 稳定性修复 D)
#    launcher 的 SET_STP_MODE ioctl 是 fire-and-forget (timeoutValue=0, 入队即返回),
#    无法直接观测"完成"; 用 wmtd 线程处理 HIF_CONF op 后必打的 dmesg 标志近似就绪:
#      "WMT HIF info added" = wmtInfoBit 已置 + hifType 已定 → func-on 不会撞 "no hif info"
#      "patch total num"    = launcher 首次上电 (LPBK func-on + patch 下载) 已完成
#    即使内核已按方案 C 默认 BTIF, 此等待仍给 sw_init 的 srh_patch (2s 超时) 留出
#    launcher 在线窗口。busybox 兼容: while + sleep 1 + dmesg | grep, 最多 ~20s。
if [ "$LAUNCHER_PRESENT" -eq 1 ]; then
    LAUNCHER_READY=0
    i=0
    while [ $i -lt 20 ]; do
        if dmesg | grep -q "WMT HIF info added"; then
            LAUNCHER_READY=1
            log "[connsys] launcher 就绪: 见 WMT HIF info added (第 ${i}s)"
            break
        fi
        sleep 1
        i=$((i + 1))
    done
    [ "$LAUNCHER_READY" -eq 1 ] || log "[connsys] 警告: 20s 未见 WMT HIF info added — func-on 靠内核默认 BTIF (方案C), launcher 可能未完成 SET_STP_MODE"

    i=0
    while [ $i -lt 15 ]; do
        if dmesg | grep -q "patch total num"; then
            log "[connsys] launcher 首次上电完成: 见 patch total num (第 ${i}s)"
            break
        fi
        sleep 1
        i=$((i + 1))
    done
fi

echo "=== 加载结果 (cat /proc/modules) ===" >> "$LOG"
cat /proc/modules | grep -aE "mtk_|wlan|wmt" >> "$LOG"

echo "=== 验证 ===" >> "$LOG"
echo "BTIF 传输: $(ls /sys/bus/platform/devices/ 2>/dev/null | grep -c btif) 个 btif 设备" >> "$LOG"
ls /dev/stpwmt /dev/stpbt /dev/wmtWifi 2>/dev/null >> "$LOG"
echo "WiFi: $(ls /sys/class/net/ 2>/dev/null | grep -c wlan) 个 wlan 接口 (需 func-on 后出现)" >> "$LOG"

log "[connsys] 完成. 日志: $LOG"
log "[connsys] 下一步: echo 1 > /dev/wmtWifi (WiFi func-on), 固件部署见 scripts/deploy_connsys_fw.sh"
