#!/bin/sh
# z1_adb_setup.sh — Z1 (MT6572) ADB gadget setup + adbd start (busybox-safe)
# 用法: sh /root/connsys/z1_adb_setup.sh
# 依赖: 内核 CONFIG_USB_CONFIGFS + USB_CONFIGFS_F_FS (ADB 版内核已编)
# 前置: 正常开机(拔 USB) -> 本脚本把 gadget 配置成 functionfs(adb) + acm(串口)
#       -> 起原厂静态 adbd -> 主机插 USB 后 adb devices 可见
# 注意: preloader LineState 坑 — 开机时别插 USB, 内核起来后再插

BB=/bin/busybox
CFG=/sys/kernel/config/usb_gadget
FUNCTIONFS_DIR=/dev/usb-ffs/adb
GADGET=z1

log() { echo "[adb-setup] $*"; }

# 1. 挂 configfs
$BB mount -t configfs none /sys/kernel/config 2>/dev/null || log "configfs 已挂载或挂载失败(继续)"

# 2. 清理旧 gadget (若存在)
if [ -d "$CFG/$GADGET" ]; then
  if [ -f "$CFG/$GADGET/UDC" ]; then
    echo "" > "$CFG/$GADGET/UDC" 2>/dev/null
  fi
  $BB rm -rf "$CFG/$GADGET" 2>/dev/null
fi

# 3. 建 gadget
mkdir -p "$CFG/$GADGET"
echo 0x0e8d > "$CFG/$GADGET/idVendor" 2>/dev/null   # MediaTek
echo 0x200e > "$CFG/$GADGET/idProduct" 2>/dev/null  # adb + acm
mkdir -p "$CFG/$GADGET/strings/0x409"
echo "Z1" > "$CFG/$GADGET/strings/0x409/manufacturer" 2>/dev/null
echo "Z1 hearing-aid" > "$CFG/$GADGET/strings/0x409/product" 2>/dev/null

# 4. functionfs (adb) + acm (串口) function
mkdir -p "$CFG/$GADGET/functions/ffs.adb"
mkdir -p "$CFG/$GADGET/functions/acm.usb0"

# 5. 配置 (建议 acm 串口保留 + ffs adb 主)
mkdir -p "$CFG/$GADGET/configs/c.1"
mkdir -p "$CFG/$GADGET/configs/c.1/strings/0x409"
echo "Z1 ADB" > "$CFG/$GADGET/configs/c.1/strings/0x409/configuration" 2>/dev/null
ln -s "$CFG/$GADGET/functions/ffs.adb" "$CFG/$GADGET/configs/c.1/" 2>/dev/null
ln -s "$CFG/$GADGET/functions/acm.usb0" "$CFG/$GADGET/configs/c.1/" 2>/dev/null

# 6. 挂 functionfs (adbd 硬编码路径 /dev/usb-ffs/adb)
mkdir -p "$FUNCTIONFS_DIR"
$BB mount -t functionfs adb "$FUNCTIONFS_DIR" 2>/dev/null || log "functionfs 挂载失败(检查内核 CONFIG_USB_F_FS)"

# 7. adbd Android 残留缓解
mkdir -p /system/bin /dev/log
ln -sf /bin/sh /system/bin/sh 2>/dev/null
grep -q "^shell:" /etc/passwd 2>/dev/null || echo "shell:x:2000:2000:shell:/data/local/tmp:/bin/sh" >> /etc/passwd 2>/dev/null

# 8. ★ 起 adbd 先于绑 UDC (f_fs 关键顺序修正)
#    mainline f_fs.c: ffs_do_functionfs_bind 在 !desc_ready 返回 -ENODEV;
#    desc_ready 要 adbd 打开 ep0 写入 ADB 描述符才置位。必须先起 adbd 后 echo UDC,
#    否则 gadget 不枚举。adbd 硬编码 /dev/usb-ffs/adb, 已在 step6 mount。
log "starting adbd (root, ALLOW_ADBD_ROOT)..."
/root/connsys/adbd 2>&1 &
ADBD_PID=$!
log "adbd started pid $ADBD_PID, waiting for desc_ready..."

# 8b. 绑 UDC (带重试, 等 adbd 写好描述符)
i=0
while [ $i -lt 20 ]; do
  echo musb-hdrc > "$CFG/$GADGET/UDC" 2>/dev/null && log "UDC 绑定成功" && break
  i=$((i+1))
  log "UDC 绑定重试 $i/20 (等 adbd desc_ready)..."
  sleep 1
done
[ $i -lt 20 ] || log "UDC 绑定失败(检查 adbd 是否写描述符 / /sys/class/udc/)"
