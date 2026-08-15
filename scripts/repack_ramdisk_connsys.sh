#!/bin/bash
# repack_ramdisk_connsys.sh — 把 CONNSYS 五模块 .ko + 加载脚本塞进 initramfs (保留 512B MTK ROOTFS 头)
# Z1 (MT6572) mainline 移植 — "非重要驱动 linux 启动后加载" (WiFi/BT 模块化)
# 产物: out/ramdisk_mainline_connsys.gz — 与固件版 ramdisk_mainline_fw.gz 并存
set -euo pipefail

SRC=${Z1_RAMDISK_SRC:-/home/lxj/claude/6572/out/forensic/linux/ramdisk.gz}
# 模块源 — 用 z1vermagic 产物: 全部 ko vermagic=7.0.0-rc7-z1+ 匹配 CONFIG_LOCALVERSION="-z1" 内核
#   (旧默认 products_connsys_modules_rebuilt_20260814_004806 的 ko 是 7.0.0-rc7-g2cd361bd40c7-dirty,
#    对 z1+ 内核 insmod 会被拒载 — 上板成功用的就是本 z1vermagic 目录, 见 WIFI_FUNC_ON_SUCCESS_20260814.md)
MODS_SRC=${Z1_MODS_SRC:-/home/lxj/claude/6572/bridge_backups/products/products_connsys_modules_z1vermagic_20260814_015009}
FW_SRC=${Z1_FW_SRC:-/home/lxj/claude/6572/out/firmware_backup/firmware}
# iw (nl80211, 静态 ARM) — wlan_gen2 只支持 nl80211 scan, iwlist/WEXT 报不支持
#   (iw_arm_static_stripped = products_iw_nl80211_20260814_144324, sha256 38b5be3c...)
IW_BIN=${Z1_IW_BIN:-/home/lxj/claude/6572/bridge_backups/products/products_iw_nl80211_20260814_144324/iw_arm_static_stripped}
# 备选: 老 wifi_tools 产物目录 (iwconfig/iwlist/iwpriv, WEXT 用; 仅当上面 iw 缺失时试 $TOOLS_SRC/iw)
TOOLS_SRC=${Z1_TOOLS_SRC:-/home/lxj/claude/6572/bridge_backups/products/products_wifi_tools_20260814_131306/bin}
SCRIPT=/home/lxj/claude/6572/scripts/z1_modprobe_connsys.sh
OUT=${Z1_RAMDISK_OUT:-/home/lxj/claude/6572/out/ramdisk_mainline_connsys.gz}
TMP=$(mktemp -d /tmp/rdkc_XXXXXX)
trap 'rm -rf "$TMP"' EXIT

# 1. 拆 512B MTK ROOTFS 头
head -c 512 "$SRC" > "$TMP/header"
dd if="$SRC" bs=1 skip=512 2>/dev/null > "$TMP/body.gz"
gzip -t "$TMP/body.gz"

# 2. 解 body → busybox rootfs
mkdir -p "$TMP/rd"
gzip -dc "$TMP/body.gz" | (cd "$TMP/rd" && cpio -idmu 2>/dev/null)
[ -x "$TMP/rd/bin/busybox" ] || { echo "ERROR: 非 busybox rootfs"; exit 1; }

# 2.5 喂狗注入 (Z1_WATCHDOG_FEED=1 时) — 消除 LK 遗留 HW WDT 饿死复位 (~19.7s/~30.5s)
#    常驻循环喂狗 (P6_EOD_VERDICT 判定: 一次性 echo V 不常驻 → WDT 30.5s 仍咬死)
#    busybox watchdog -t 5 -T 10 若可用且不退出则常驻; 否则 while 循环 echo V 常驻
if [ "${Z1_WATCHDOG_FEED:-0}" = "1" ]; then
    if python3 - "$TMP/rd/init" <<'PYEOF'
import sys
path = sys.argv[1]
src = open(path).read()
marker = "# Drop to a fallback shell"
inject = """# Feed the watchdog so the LK-armed HW WDT doesn't bite (~19.7s/~30.5s reboot).
# Persistent loop: one-shot echo V dies with its subshell -> WDT still bites.
if [ -e /dev/watchdog ]; then
  $BB echo "[init] feeding watchdog /dev/watchdog (persistent loop)"
  # busybox watchdog (daemon) if usable, else fallback echo V loop
  if $BB watchdog -t 5 -T 10 /dev/watchdog >/dev/null 2>&1; then
    :
  else
    (while true; do $BB echo V > /dev/watchdog 2>/dev/null; $BB sleep 5; done) &
  fi
fi

"""
if marker in src and "feeding watchdog" not in src:
    open(path, 'w').write(src.replace(marker, inject + marker, 1))
    print("[repack] persistent watchdog feed injected into init")
else:
    print("[repack] init watchdog feed already present or marker missing")
PYEOF
    then
        :
    fi
fi

# 2.6 getty 修复 (Z1_GETTY_FIX=1 时) — shell 卡死根因修复 R2
#    /dev/console 交互 shell 内核强制 O_NONBLOCK+noctty → 改 ttyS0 真设备 + CLOCAL
#    恢复 ttyGS0 等待循环 (gadget 异步 probe 需等节点)
if [ "${Z1_GETTY_FIX:-0}" = "1" ]; then
    if python3 - "$TMP/rd/init" <<'PYEOF'
import sys
path = sys.argv[1]
src = open(path).read()

ttygs0_marker = "# Console on USB ACM (ttyGS0) and on framebuffer (tty0 if present)"
ttygs0_wait = """# Console on USB ACM (ttyGS0) and on framebuffer (tty0 if present)
# Single ttyGS0 getty (root cause of '>' continuation: two gettys raced termios).
# Loop until gadget node appears (USB plug any time), stty the tty (pin ICANON/
# ICRNL/ECHO so ash line editor commits on CR/LF), then getty with setsid.
(
  i=0
  while [ $i -lt 600 ]; do
    if [ -e /dev/ttyGS0 ]; then
      $BB stty -F /dev/ttyGS0 115200 icanon icrnl onlcr echo isig ixon opost 2>/dev/null
      exec setsid $BB getty -L -n -l /bin/sh 115200 ttyGS0 vt100
    fi
    $BB mdev -s 2>/dev/null
    $BB sleep 1
    i=$((i+1))
  done
) </dev/null >/dev/null 2>&1 &
"""
# 5.1: delete the old direct getty line (was racing the wait-loop getty)
old_direct = "$BB setsid $BB sh -c 'exec $BB getty -L -n -l /bin/sh 115200 ttyGS0 vt100' </dev/ttyGS0 >/dev/ttyGS0 2>&1 &\n"
if old_direct in src:
    src = src.replace(old_direct, "", 1)

# 5.1b: also tolerate the non -L variant of the old direct line
old_direct2 = "$BB setsid $BB sh -c 'exec $BB getty -n -l /bin/sh 115200 ttyGS0 vt100' </dev/ttyGS0 >/dev/ttyGS0 2>&1 &\n"
if old_direct2 in src:
    src = src.replace(old_direct2, "", 1)

if ttygs0_marker in src and "wait for /dev/ttyGS0" not in src:
    src = src.replace(ttygs0_marker, ttygs0_wait, 1)

src = src.replace("getty -n -l /bin/sh 115200 ttyGS0", "getty -L -n -l /bin/sh 115200 ttyGS0")

old_fb = "exec $BB getty -n -l /bin/sh 0 console vt100 2>/dev/null"
new_fb = "exec $BB getty -L -n -l /bin/sh 115200 ttyS0 vt100 2>/dev/null"
if old_fb in src:
    src = src.replace(old_fb, new_fb, 1)

open(path, 'w').write(src)
print("[repack] getty R2 fix applied: ttyGS0 wait + getty -L ttyS0")
PYEOF
    then
        :
    fi
fi

# 3. 塞 CONNSYS 模块到 /root/connsys
mkdir -p "$TMP/rd/root/connsys"
for ko in \
    "$MODS_SRC/btif/mtk_btif_drv.ko" \
    "$MODS_SRC/conn_soc/mtk_stp_wmt_soc.ko" \
    "$MODS_SRC/conn_soc/mtk_stp_bt_soc.ko" \
    "$MODS_SRC/conn_soc/mtk_wmt_wifi_soc.ko" \
    "$MODS_SRC/wlan/wlan_gen2.ko"; do
    [ -f "$ko" ] || { echo "ERROR: 缺 $ko"; exit 1; }
    cp "$ko" "$TMP/rd/root/connsys/"
done
# cfg80211.ko (wlan_gen2 insmod 依赖) + mtk_stp_launcher (WMT patch 应答, func-on 必需)
#   放在 MODS_SRC 同级: cfg80211.ko 在 $MODS_SRC/cfg80211.ko, launcher 在 $MODS_SRC/mtk_stp_launcher
for extra in \
    "$MODS_SRC/cfg80211.ko" \
    "$MODS_SRC/mtk_stp_launcher"; do
    [ -f "$extra" ] || { echo "WARN: 缺可选件 $extra (跳过)"; continue; }
    cp "$extra" "$TMP/rd/root/connsys/"
    echo "  已加 $extra → /root/connsys/$(basename "$extra")"
done
cp "$SCRIPT" "$TMP/rd/root/connsys/z1_modprobe_connsys.sh"
chmod +x "$TMP/rd/root/connsys/z1_modprobe_connsys.sh"

# 上板一键验证脚本 → /root/connsys/ (下次开机直接 sh /root/connsys/z1_connsys_onboard_test.sh)
ONBOARD=/home/lxj/claude/6572/scripts/z1_connsys_onboard_test.sh
if [ -f "$ONBOARD" ]; then
    cp "$ONBOARD" "$TMP/rd/root/connsys/z1_connsys_onboard_test.sh"
    chmod +x "$TMP/rd/root/connsys/z1_connsys_onboard_test.sh"
    echo "  脚本 z1_connsys_onboard_test.sh → /root/connsys/"
else
    echo "WARN: 缺上板验证脚本 $ONBOARD — 需手工推送或用 PC 端跑"
fi

# 3.5 塞 CONNSYS 固件 (WMT_init 读 /system/etc/firmware/WMT_SOC.cfg, wlan request_firmware WIFI_RAM_CODE_*)
#    WMT_SOC.cfg 缺失 → wmt_conf_read_file 失败 → wmt_lib_init(-1) → mtk_stp_wmt_soc insmod 失败 (上板实测)
mkdir -p "$TMP/rd/lib/firmware" "$TMP/rd/system/etc/firmware"
# WIFI RAM code: wlan_gen2 请求顺序 WIFI_RAM_CODE_MT6582 → WIFI_RAM_CODE_SOC → WIFI_RAM_CODE
# 只放 MT6582 (首选名), 内容与 SOC 相同, 删重复份省 ~160KB 让 boot.img ≤6MB
if [ -f "$FW_SRC/WIFI_RAM_CODE_SOC" ]; then
    cp "$FW_SRC/WIFI_RAM_CODE_SOC" "$TMP/rd/lib/firmware/WIFI_RAM_CODE_MT6582"
    echo "  固件 WIFI_RAM_CODE_MT6582 ← WIFI_RAM_CODE_SOC"
fi
# WMT patch (launcher 喂内核, 只留 ROMv1_patch_N_hdr.bin fallback 名, 删原名重复份省 ~58KB)
if [ -f "$FW_SRC/mt6572_82_patch_e1_0_hdr.bin" ] && [ -f "$FW_SRC/mt6572_82_patch_e1_1_hdr.bin" ]; then
    cp "$FW_SRC/mt6572_82_patch_e1_0_hdr.bin" "$TMP/rd/system/etc/firmware/ROMv1_patch_0_hdr.bin"
    cp "$FW_SRC/mt6572_82_patch_e1_1_hdr.bin" "$TMP/rd/system/etc/firmware/ROMv1_patch_1_hdr.bin"
    echo "  固件 ROMv1_patch_{0,1}_hdr.bin (删原名重复份)"
fi
# WMT_SOC.cfg (内核 filp_open 绝对路径读)
if [ -f "$FW_SRC/WMT_SOC.cfg" ]; then
    cp "$FW_SRC/WMT_SOC.cfg" "$TMP/rd/system/etc/firmware/WMT_SOC.cfg"
    echo "  固件 WMT_SOC.cfg"
fi
# Wi-Fi NVRAM (wlan_gen2 glLoadNvram 读 /etc/firmware/nvram/WIFI, 512B; 缺 → 随机 MAC + 单播 RX 丢帧)
# 提取自 stock NVRAM 分区 offset 0x20000, md5 a6182f634a2554bc881e87fdf111ecd1, 真实 MAC 20:72:0d:39:0d:1f
NVRAM_SRC=${Z1_NVRAM_SRC:-/home/lxj/claude/6572/mainline_recon/wifi-nvram-WIFI.bin}
if [ -f "$NVRAM_SRC" ]; then
    mkdir -p "$TMP/rd/etc/firmware/nvram"
    cp "$NVRAM_SRC" "$TMP/rd/etc/firmware/nvram/WIFI"
    echo "  固件 NVRAM WIFI (512B, MAC 20:72:0d:39:0d:1f)"
fi
# 测试无线工具 (静态 ARM) → /usr/bin — 上板测 WiFi scan 用
# 优先 nl80211 iw (wlan_gen2 只支持 nl80211 scan); iwlist 是 WEXT 不支持 scan, 去掉省空间
IW_SRC=""
if [ -f "$IW_BIN" ]; then
    IW_SRC="$IW_BIN"
elif [ -f "$TOOLS_SRC/iw" ]; then
    IW_SRC="$TOOLS_SRC/iw"
fi
if [ -n "$IW_SRC" ]; then
    mkdir -p "$TMP/rd/usr/bin"
    cp "$IW_SRC" "$TMP/rd/usr/bin/iw"
    chmod +x "$TMP/rd/usr/bin/iw"
    echo "  工具 iw ← $IW_SRC ($(stat -c%s "$IW_SRC") B, nl80211 静态 ARM)"
else
    echo "WARN: 缺 iw (nl80211) — 上板无法 scan (iwlist/WEXT 对 wlan_gen2 不支持). 用 Z1_IW_BIN 指定"
fi
# 蓝牙栈 (内核 BT 模块 + 用户态工具) → /root/connsys/bt — 上板测 BT (vhci 桥路线)
#   ko: bluetooth/ecc/ecdh_generic/hci_vhci/hidp/uhid (vermagic -z1+)
#   tools: stpbt-vhci-bridge / stpbt-hci-test / hci-localver / btup-scan / btif-lpbk-test
BT_SRC=${Z1_BT_SRC:-/home/lxj/claude/6572/bridge_backups/products/products_bt_stack_20260814_204345}
if [ -d "$BT_SRC/ko" ]; then
    mkdir -p "$TMP/rd/root/connsys/bt/ko" "$TMP/rd/root/connsys/bt/tools"
    for ko in "$BT_SRC"/ko/*.ko; do
        [ -f "$ko" ] && cp "$ko" "$TMP/rd/root/connsys/bt/ko/" && echo "  BT ko $(basename "$ko")"
    done
    for t in "$BT_SRC"/tools/*; do
        if [ -f "$t" ] && [ -x "$t" ]; then
            cp "$t" "$TMP/rd/root/connsys/bt/tools/"
            chmod +x "$TMP/rd/root/connsys/bt/tools/$(basename "$t")"
            echo "  BT tool $(basename "$t")"
        fi
    done
fi

# 4. 重打包 cpio -H newc + 压缩 (默认 xz, 内核 CONFIG_RD_XZ=y; 必须 --check=crc32, xz 默认 CRC64 内核解不了)
#    Z1_RAMDISK_COMPRESS=gzip 可回退
COMP=${Z1_RAMDISK_COMPRESS:-xz}
case "$COMP" in
    gzip) (cd "$TMP/rd" && find . | cpio -o -H newc 2>/dev/null | gzip -9) > "$TMP/body_new.gz" ;;
    xz)   (cd "$TMP/rd" && find . | cpio -o -H newc 2>/dev/null | xz -9 --check=crc32 -c) > "$TMP/body_new.gz" ;;
    *) echo "ERROR: 未知压缩方式 $COMP (gzip/xz)"; exit 1 ;;
esac
echo "  body 压缩: $COMP (xz 用 --check=crc32, 内核兼容)"

# 5. 512B 头前置拼回 — 更新 MTK ROOTFS 头的 body 大小字段 (offset 4-8, LE u32)
#    否则 LK 只读原 body 长度, 重打包更大的 body 被截断 → VFS mount 失败
python3 - "$TMP/header" "$TMP/body_new.gz" <<'EOF'
import struct, sys
hdr_path, body_path = sys.argv[1], sys.argv[2]
body = open(body_path, 'rb').read()
hdr = bytearray(open(hdr_path, 'rb').read())
assert hdr[0:4] == b'\x88\x16\x88\x58', f"unexpected MTK header magic: {hdr[0:4].hex()}"
hdr[4:8] = struct.pack('<I', len(body))
open(hdr_path, 'wb').write(hdr)
print(f"updated MTK ROOTFS body-size field: {len(body)} (0x{len(body):x})")
EOF
cat "$TMP/header" "$TMP/body_new.gz" > "$OUT"

# 7. 同步 Z1 dts 的 chosen linux,initrd-end —— 内核按 DT 区间读 initrd,
#    body 变大后必须同步 end, 否则内核只读前一段 → gzip 截断 → VFS panic.
#    (agent 定案: dts mt6572-z1.dts:42 硬编码 0x8420ac7d 只匹配原版 body)
DTS=${Z1_DTS:-/home/lxj/claude/6572/fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts}
INITRD_START=0x84100000
BODY_LEN=$(stat -c%s "$TMP/body_new.gz")
NEW_END=$(( INITRD_START + BODY_LEN ))
NEW_END_HEX=$(printf '0x%x' "$NEW_END")
echo "initrd-end: $INITRD_START + $BODY_LEN = $NEW_END_HEX"
if grep -q "linux,initrd-end" "$DTS"; then
    sed -i "s|linux,initrd-end = <0x[0-9a-fA-F]*>;|linux,initrd-end = <$NEW_END_HEX>;|" "$DTS"
    echo "updated $DTS initrd-end → $NEW_END_HEX"
    # 重编 dtb
    make -C /home/lxj/claude/6572/fork_linux ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- dtbs 2>&1 | grep -iE "mt6572-z1|error" | head -2
    echo "dtb rebuilt"
else
    echo "WARN: no linux,initrd-end in $DTS"
fi

# 6. 校验
echo "=== 产物: $OUT ==="
ls -la "$OUT"
echo "=== 模块在不在 ==="
dd if="$OUT" bs=1 skip=512 2>/dev/null | gzip -dc 2>/dev/null | cpio -t 2>/dev/null | grep -aE "connsys" | head
echo "OK: CONNSYS 模块已入 ramdisk (共 $(dd if="$OUT" bs=1 skip=512 2>/dev/null | gzip -dc 2>/dev/null | cpio -t 2>/dev/null | grep -ac connsys) 项)"
