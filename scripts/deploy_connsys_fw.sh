#!/bin/sh
# deploy_connsys_fw.sh - install Z1 (MT6572) CONNSYS WiFi/BT firmware blobs
# for the out-of-tree mt6572-connsys-modules stack.
#
# The blobs are the ones the modules request by name (see the module README
# "Firmware" section; wlan driver tries WIFI_RAM_CODE_MT6582 first):
#
#   <ROOT>/lib/firmware/WIFI_RAM_CODE_MT6582   <- WIFI_RAM_CODE_SOC (request_firmware)
#   <ROOT>/lib/firmware/WIFI_RAM_CODE_SOC      <- same bytes, fallback name
#   <ROOT>/system/etc/firmware/mt6572_82_patch_e1_{0,1}_hdr.bin  (WMT patches)
#   <ROOT>/system/etc/firmware/ROMv1_patch_{0,1}_hdr.bin         (launcher alias)
#   <ROOT>/system/etc/firmware/WMT_SOC.cfg                       (WMT config)
#
# Usage:
#   scripts/deploy_connsys_fw.sh [ROOT]
#
#   ROOT   target rootfs root. Default "/" (live board). Use an SD-card rootfs
#          mount for staging (e.g. scripts/deploy_connsys_fw.sh /media/sd).
#   FW_SRC source directory with the stock firmware files. Defaults to the Z1
#          stock-firmware backup tree on the build host.

set -e

ROOT="${1:-/}"
FW_SRC="${FW_SRC:-/home/lxj/claude/6572/out/firmware_backup/firmware}"
LIBFW="${ROOT%/}/lib/firmware"
SYSFW="${ROOT%/}/system/etc/firmware"

# Firmware files this stack needs (all must exist in FW_SRC).
NEEDED="WIFI_RAM_CODE_SOC WMT_SOC.cfg mt6572_82_patch_e1_0_hdr.bin mt6572_82_patch_e1_1_hdr.bin"

echo "== CONNSYS firmware deploy =="
echo "  source : $FW_SRC"
echo "  target : $ROOT"

for f in $NEEDED; do
    if [ ! -f "$FW_SRC/$f" ]; then
        echo "ERROR: missing $FW_SRC/$f" >&2
        exit 1
    fi
done

mkdir -p "$LIBFW" "$SYSFW"

# Wi-Fi RAM image: the wlan driver requests WIFI_RAM_CODE_MT6582 first, then
# WIFI_RAM_CODE_<chipinfo>, then plain WIFI_RAM_CODE. Install it under the
# MT6582 name (stock MT6572 convention) and keep the SOC name as fallback.
cp "$FW_SRC/WIFI_RAM_CODE_SOC" "$LIBFW/WIFI_RAM_CODE_MT6582"
cp "$FW_SRC/WIFI_RAM_CODE_SOC" "$LIBFW/WIFI_RAM_CODE_SOC"
echo "  WIFI_RAM_CODE_MT6582 ($(wc -c < "$LIBFW/WIFI_RAM_CODE_MT6582") bytes)"

# WMT patch blobs: stock name + ROMv1 alias the launcher falls back to when
# Android system properties are absent (it derives ROMv<hwver>_patch*).
for i in 0 1; do
    cp "$FW_SRC/mt6572_82_patch_e1_${i}_hdr.bin" "$SYSFW/mt6572_82_patch_e1_${i}_hdr.bin"
    cp "$FW_SRC/mt6572_82_patch_e1_${i}_hdr.bin" "$SYSFW/ROMv1_patch_${i}_hdr.bin"
    echo "  patch mt6572_82_patch_e1_${i}_hdr.bin + ROMv1_patch_${i}_hdr.bin"
done

cp "$FW_SRC/WMT_SOC.cfg" "$SYSFW/WMT_SOC.cfg"
echo "  WMT_SOC.cfg"

sync
echo "== OK - CONNSYS firmware installed =="
echo "Next: connsys-up (insmod btif -> wmt -> bt, launcher, HCI test) then wifi-up."
