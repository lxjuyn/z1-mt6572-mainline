#!/bin/bash
# z1_deploy_alpine_rootfs.sh — 把 Z1 非必要驱动 + ADB 部署到 Alpine rootfs (out/alpine_rootfs/)
# 依赖: out/alpine_rootfs/ (Alpine WORK 目录, 与 system_alpine.img 一致)
# 部署: 13 .ko + launcher + iw + BT 工具 + 固件 5 处 + connsys OpenRC 服务 + adbd OpenRC 服务
set -euo pipefail

ROOTFS=/home/lxj/claude/6572/out/alpine_rootfs
MODS=/home/lxj/claude/6572/bridge_backups/products/products_connsys_modules_z1vermagic_20260814_015009
BT=/home/lxj/claude/6572/bridge_backups/products/products_bt_stack_20260814_204345
IW=/home/lxj/claude/6572/bridge_backups/products/products_iw_nl80211_20260814_144324/iw_arm_static_stripped
FW=/home/lxj/claude/6572/out/firmware_backup/firmware
NVRAM=/home/lxj/claude/6572/mainline_recon/wifi-nvram-WIFI.bin
ADBD=/home/lxj/claude/6572/out/boot_extracted/rd/sbin/adbd
VER=7.0.0-rc7-z1+

echo "部署到 Alpine rootfs: $ROOTFS"
[ -d "$ROOTFS" ] || { echo "ERROR: $ROOTFS 不存在"; exit 1; }

# 1. 建模块目录 + 拷 13 .ko
MODDIR="$ROOTFS/lib/modules/$VER"
mkdir -p "$MODDIR"
for ko in "$MODS"/btif/*.ko "$MODS"/conn_soc/*.ko "$MODS"/wlan/*.ko "$MODS"/cfg80211.ko "$BT"/ko/*.ko; do
    [ -f "$ko" ] && cp "$ko" "$MODDIR/" && echo "  ko $(basename "$ko")"
done

# 2. 固件 5 处
mkdir -p "$ROOTFS/lib/firmware" "$ROOTFS/system/etc/firmware" "$ROOTFS/etc/firmware/nvram"
cp "$FW/WIFI_RAM_CODE_SOC" "$ROOTFS/lib/firmware/WIFI_RAM_CODE_MT6582"
cp "$FW/WMT_SOC.cfg" "$ROOTFS/system/etc/firmware/WMT_SOC.cfg"
cp "$FW/mt6572_82_patch_e1_0_hdr.bin" "$ROOTFS/system/etc/firmware/ROMv1_patch_0_hdr.bin"
cp "$FW/mt6572_82_patch_e1_1_hdr.bin" "$ROOTFS/system/etc/firmware/ROMv1_patch_1_hdr.bin"
cp "$NVRAM" "$ROOTFS/etc/firmware/nvram/WIFI"
echo "  固件 5 处"

# 3. 用户态工具
mkdir -p "$ROOTFS/usr/local/sbin" "$ROOTFS/usr/bin"
cp "$MODS/mtk_stp_launcher" "$ROOTFS/usr/local/sbin/mtk_stp_launcher" 2>/dev/null && chmod +x "$ROOTFS/usr/local/sbin/mtk_stp_launcher"
cp "$IW" "$ROOTFS/usr/bin/iw" && chmod +x "$ROOTFS/usr/bin/iw"
for t in "$BT"/tools/*; do [ -f "$t" ] && [ -x "$t" ] && cp "$t" "$ROOTFS/usr/local/sbin/" && chmod +x "$ROOTFS/usr/local/sbin/$(basename "$t")"; done
cp /home/lxj/claude/6572/scripts/z1_modprobe_connsys.sh "$ROOTFS/usr/local/sbin/z1_modprobe_connsys.sh"
chmod +x "$ROOTFS/usr/local/sbin/z1_modprobe_connsys.sh"
sed -i 's|^#!/bin/bash|#!/bin/sh|' "$ROOTFS/usr/local/sbin/z1_modprobe_connsys.sh"
echo "  用户态工具"

# 4. ADB
cp "$ADBD" "$ROOTFS/usr/local/sbin/adbd" && chmod +x "$ROOTFS/usr/local/sbin/adbd"
cp /home/lxj/claude/6572/scripts/z1_adb_setup.sh "$ROOTFS/usr/local/sbin/" 2>/dev/null && chmod +x "$ROOTFS/usr/local/sbin/z1_adb_setup.sh"
# Android 残留缓解
mkdir -p "$ROOTFS/system/bin" "$ROOTFS/dev/log" "$ROOTFS/data/local/tmp"
ln -sf /bin/sh "$ROOTFS/system/bin/sh" 2>/dev/null || true
grep -q "^shell:" "$ROOTFS/etc/passwd" 2>/dev/null || echo "shell:x:2000:2000:shell:/data/local/tmp:/bin/sh" >> "$ROOTFS/etc/passwd"
grep -q "^shell:" "$ROOTFS/etc/group" 2>/dev/null || echo "shell:x:2000:" >> "$ROOTFS/etc/group"
echo "  ADB adbd + setup"

# 5. inittab 补 ttyS0 getty
grep -q "ttyS0::respawn" "$ROOTFS/etc/inittab" 2>/dev/null || echo "ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100" >> "$ROOTFS/etc/inittab"
echo "  inittab ttyS0 getty"

echo "=== 部署完成 ==="
echo "ko 数: $(ls "$MODDIR"/*.ko | wc -l)"
echo "固件: $(ls "$ROOTFS/lib/firmware/" "$ROOTFS/system/etc/firmware/" "$ROOTFS/etc/firmware/nvram/" | wc -l) 文件"
echo "下一步: 重打 system_alpine.img + 刷 p4 + 刷 switch_root boot.img + 上板验证"
