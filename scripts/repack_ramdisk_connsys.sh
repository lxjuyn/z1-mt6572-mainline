#!/bin/bash
# repack_ramdisk_connsys.sh — 把 CONNSYS 五模块 .ko + 加载脚本塞进 initramfs (保留 512B MTK ROOTFS 头)
# Z1 (MT6572) mainline 移植 — "非重要驱动 linux 启动后加载" (WiFi/BT 模块化)
# 产物: out/ramdisk_mainline_connsys.gz — 与固件版 ramdisk_mainline_fw.gz 并存
set -euo pipefail

SRC=${Z1_RAMDISK_SRC:-/home/lxj/claude/6572/out/forensic/linux/ramdisk.gz}
MODS_SRC=${Z1_MODS_SRC:-/home/lxj/claude/6572/bridge_backups/products/products_connsys_modules_rebuilt_20260814_004806}
FW_SRC=${Z1_FW_SRC:-/home/lxj/claude/6572/out/firmware_backup/firmware}
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
# 只留 scan 必需的 iwlist; iwconfig 删 (busybox 自带 ifconfig 可替代 up), 省 ~500KB 让 boot.img ≤6MB
if [ -d "$TOOLS_SRC" ]; then
    mkdir -p "$TMP/rd/usr/bin"
    for t in iwlist; do
        if [ -f "$TOOLS_SRC/$t" ]; then
            cp "$TOOLS_SRC/$t" "$TMP/rd/usr/bin/$t"
            chmod +x "$TMP/rd/usr/bin/$t"
            echo "  工具 $t (静态 ARM)"
        fi
    done
fi

# 4. 重打包 cpio -H newc + 压缩 (默认 xz -9e 更小, 内核 CONFIG_RD_XZ=y 支持解压; Z1_RAMDISK_COMPRESS=gzip 可回退)
COMP=${Z1_RAMDISK_COMPRESS:-xz}
case "$COMP" in
    gzip) (cd "$TMP/rd" && find . | cpio -o -H newc 2>/dev/null | gzip -9) > "$TMP/body_new.gz" ;;
    xz)   (cd "$TMP/rd" && find . | cpio -o -H newc 2>/dev/null | xz -9e -c) > "$TMP/body_new.gz" ;;
    *) echo "ERROR: 未知压缩方式 $COMP (gzip/xz)"; exit 1 ;;
esac
echo "  body 压缩: $COMP"

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
