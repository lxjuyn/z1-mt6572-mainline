#!/bin/bash
# repack_ramdisk_connsys.sh — 把 CONNSYS 五模块 .ko + 加载脚本塞进 initramfs (保留 512B MTK ROOTFS 头)
# Z1 (MT6572) mainline 移植 — "非重要驱动 linux 启动后加载" (WiFi/BT 模块化)
# 产物: out/ramdisk_mainline_connsys.gz — 与固件版 ramdisk_mainline_fw.gz 并存
set -euo pipefail

SRC=${Z1_RAMDISK_SRC:-/home/lxj/claude/6572/out/forensic/linux/ramdisk.gz}
MODS_SRC=/home/lxj/claude/6572/bridge_backups/products/products_connsys_modules_20260812_022121/mt6572-connsys-modules
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
cp "$SCRIPT" "$TMP/rd/root/connsys/z1_modprobe_connsys.sh"
chmod +x "$TMP/rd/root/connsys/z1_modprobe_connsys.sh"

# 4. 重打包 cpio -H newc + gzip
(cd "$TMP/rd" && find . | cpio -o -H newc 2>/dev/null | gzip -9) > "$TMP/body_new.gz"

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

# 6. 校验
echo "=== 产物: $OUT ==="
ls -la "$OUT"
echo "=== 模块在不在 ==="
dd if="$OUT" bs=1 skip=512 2>/dev/null | gzip -dc 2>/dev/null | cpio -t 2>/dev/null | grep -aE "connsys" | head
echo "OK: CONNSYS 模块已入 ramdisk (共 $(dd if="$OUT" bs=1 skip=512 2>/dev/null | gzip -dc 2>/dev/null | cpio -t 2>/dev/null | grep -ac connsys) 项)"
