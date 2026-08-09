#!/bin/bash
# 经 C3 桥向 Z1 ttyMT0 shell 发命令并抓回显。
# 用法: z1cmd.sh "命令" [抓秒数]
set -u
CMD="${1:?usage: z1cmd.sh command [seconds]}"; WAIT="${2:-4}"
PORT="${C3_PORT:-}"
BAUD="${Z1_BAUD:-115200}"
if [ -z "$PORT" ]; then
    link=
    for pat in 'usb-Espressif_USB_JTAG_serial_debug_unit_*' \
               'usb-GigaDevice_*' 'usb-1a86_*' 'usb-QinHeng_*'; do
        link=$(find /dev/serial/by-id -maxdepth 1 -type l -name "$pat" 2>/dev/null | head -n1)
        [ -n "$link" ] && break
    done
    [ -n "$link" ] && PORT=$(readlink -f "$link")
fi
[ -n "$PORT" ] && [ -c "$PORT" ] || { echo "ERROR: 未找到 USB 串口 (Espressif/GigaDevice/1a86/QinHeng)" >&2; exit 1; }
# 当前 Z1 console=115200(GD32/CH340 直连); C3 桥 USB-SJAG 端也保持 115200。可用 Z1_BAUD 覆盖。
stty -F "$PORT" "$BAUD" raw -echo 2>/dev/null
printf '\r\n\r\n' > "$PORT"
sleep 0.2
timeout 1 tr -d '\000' < "$PORT" >/dev/null 2>&1 || true
printf '%s\r\n' "$CMD" > "$PORT"
timeout "$WAIT" tr -d '\000' < "$PORT" 2>/dev/null | tr -cd '\11\12\15\40-\176' || true
