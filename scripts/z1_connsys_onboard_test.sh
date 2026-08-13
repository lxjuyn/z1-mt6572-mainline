#!/bin/sh
# z1_connsys_onboard_test.sh — Z1 (MT6572) CONNSYS WiFi/BT func-on 上板一键验证
#
# ⚠️ 上板脚本: 板子开机进 busybox rootfs 后手动执行 (不刷写, 不碰 download 线):
#   sh z1_connsys_onboard_test.sh
#
# busybox 兼容: 无 tee/lsmod/od, 只用 echo/cat/insmod/mknod/ls/grep/printf/df/mount/sleep
#   (sleep 是 busybox 自带; command -v 是 ash 内建, 用于探测 iw/ip)
#
# 依赖: /root/connsys/ 下须有 CONNSYS 模块 + z1_modprobe_connsys.sh
#   (由 PC 端 tools/repack_ramdisk_connsys.sh 打进 ramdisk 部署)
#
# 流程:
#   ① /root/connsys 存在?
#   ② 跑 z1_modprobe_connsys.sh (insmod: cfg80211→btif→wmt→bt→wifi→wlan_gen2)
#   ③ cat /proc/modules 验证 ≥5 模块 (含 cfg80211)
#   ④ /dev/stpwmt /dev/stpbt /dev/wmtWifi 节点
#   ⑤ echo 1 > /dev/wmtWifi (WiFi func-on)
#   ⑥ 等 5s 后 ls /sys/class/net/ 看 wlan0
#   ⑦ wlan0 出现 → 下一步 (iw/ip); 没出现 → 排查提示

ok()   { echo "  [OK]   $*"; }
fail() { echo "  [FAIL] $*"; }
step() { echo ""; echo "=== step $1/7: $2 ==="; }

echo ""
echo "=== Z1 CONNSYS WiFi/BT 上板一键验证 (开始) ==="

# --- ① ---
step 1 "确认 /root/connsys"
if [ -d /root/connsys ]; then
    ok "/root/connsys 存在"
else
    fail "/root/connsys 不存在 — 先重建 ramdisk 部署 (PC 端 tools/repack_ramdisk_connsys.sh)"
    exit 1
fi

# --- ② ---
step 2 "运行 z1_modprobe_connsys.sh (insmod 6 模块)"
MODPROBE=/root/connsys/z1_modprobe_connsys.sh
if [ -f "$MODPROBE" ]; then
    echo "  [RUN] sh $MODPROBE"
    sh "$MODPROBE"
    echo "  加载日志: /tmp/connsys_load.log (cat 查看 insmod 失败原因)"
else
    fail "未找到 $MODPROBE — 模块未加载, 继续检查现状"
fi

# --- ③ ---
step 3 "验证模块加载 (cat /proc/modules)"
EXPECT="cfg80211 mtk_btif_drv mtk_stp_wmt_soc mtk_stp_bt_soc mtk_wmt_wifi_soc wlan_gen2"
n=0
for m in $EXPECT; do
    if cat /proc/modules | grep -q "^$m "; then
        ok "模块 $m 已加载"
        n=$((n + 1))
    else
        fail "模块 $m 未加载"
    fi
done
echo "  已加载 $n/6 个期望模块"
if [ "$n" -ge 5 ]; then
    ok "模块加载充分 (≥5, 含 cfg80211)"
else
    fail "模块不足 (需 ≥5) — cat /tmp/connsys_load.log 看 insmod 失败项"
fi

# --- ④ ---
step 4 "设备节点 (/dev/stpwmt /dev/stpbt /dev/wmtWifi)"
for d in /dev/stpwmt /dev/stpbt /dev/wmtWifi; do
    if ls "$d" >/dev/null 2>&1; then
        ok "$d 存在"
    else
        fail "$d 缺失 — z1_modprobe_connsys.sh 应已 mknod (major 190/192/155), 或 mdev 未建"
    fi
done

# --- ⑤ ---
step 5 "WiFi func-on"
echo "  [ACTION] echo 1 > /dev/wmtWifi"
if echo 1 > /dev/wmtWifi 2>&1; then
    ok "func-on 指令已下发"
else
    fail "写入 /dev/wmtWifi 失败 — 设备节点/驱动未就绪"
fi
echo "  [WAIT] 等 5 秒 (WMT 固件下载 + wlan0 注册) ..."
sleep 5

# --- ⑥ ---
step 6 "检查无线接口 (ls /sys/class/net/)"
WLAN=""
for i in $(ls /sys/class/net/ 2>/dev/null); do
    case "$i" in
        wlan*) WLAN="$WLAN $i" ;;
    esac
done
if [ -n "$WLAN" ]; then
    ok "无线接口出现:$WLAN"
else
    fail "无 wlan* 接口 — func-on 失败; 查 dmesg WMT 错误 / mtk_stp_launcher / 固件"
fi

# --- ⑦ ---
step 7 "下一步"
if [ -n "$WLAN" ]; then
    echo "  [OK] WiFi func-on 成功. 配置网络:"
    if command -v iw >/dev/null 2>&1; then
        echo "    iw dev wlan0 scan | grep SSID"
        echo "    iw dev wlan0 connect <SSID>"
    else
        echo "    (busybox 无 iw, 用 ip/ifconfig)"
    fi
    if command -v ip >/dev/null 2>&1; then
        echo "    ip link set wlan0 up"
        echo "    ip addr add 192.168.1.x/24 dev wlan0"
    else
        echo "    (busybox 无 ip, 试 ifconfig wlan0 up)"
    fi
else
    echo "  wlan0 未起, 排查顺序:"
    echo "    1. cat /tmp/connsys_load.log — insmod 是否全 OK"
    echo "    2. ls /system/etc/firmware/ — mt6572_82_patch_e1_{0,1}_hdr.bin + WMT_SOC.cfg 是否在"
    echo "    3. mtk_stp_launcher 是否在跑 (z1_modprobe_connsys.sh 已启动它; func-on 需要它喂 patch)"
    echo "    4. dmesg | grep -iE 'wmt|wlan|wifi' 看内核错误"
fi
echo ""
echo "=== Z1 CONNSYS 上板验证结束 ==="
