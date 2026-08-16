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

# 1. 挂 configfs (带验证)
if [ ! -d /sys/kernel/config ]; then
  $BB mount -t configfs configfs /sys/kernel/config 2>/dev/null || {
    log "FATAL: configfs 挂载失败"
    exit 1
  }
fi
if [ ! -d /sys/kernel/config/usb_gadget ]; then
  log "FATAL: 内核未开 CONFIG_USB_CONFIGFS 或 configfs 挂载异常"
  exit 1
fi
log "configfs 就绪"

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
if [ ! -x /root/connsys/adbd ]; then
  log "FATAL: /root/connsys/adbd 不存在或不可执行"
  exit 1
fi
log "starting adbd (root, ALLOW_ADBD_ROOT)..."
/root/connsys/adbd 2>&1 &
ADBD_PID=$!
sleep 1
if ! kill -0 $ADBD_PID 2>/dev/null; then
  log "FATAL: adbd 启动失败 (pid $ADBD_PID 已退出)"
  exit 1
fi
log "adbd started pid $ADBD_PID, waiting for desc_ready..."

# 8b. 绑 UDC (带重试, 等 adbd 写好描述符)
#     UDC 名称 = DTS 设备节点名 (11100000.usb), 不是驱动名 (musb-hdrc)
UDC_NAME="11100000.usb"
if [ ! -d /sys/class/udc ]; then
  log "FATAL: /sys/class/udc 不存在 (内核未开 USB gadget UDC 框架)"
  exit 1
fi
log "可用 UDC: $(ls /sys/class/udc/ 2>/dev/null)"
i=0
while [ $i -lt 20 ]; do
  # 检查 adbd 是否还活着
  if ! kill -0 $ADBD_PID 2>/dev/null; then
    log "adbd 已退出，重启..."
    /root/connsys/adbd 2>&1 &
    ADBD_PID=$!
    sleep 1
  fi
  echo "$UDC_NAME" > "$CFG/$GADGET/UDC" 2>/dev/null && log "UDC 绑定成功 ($UDC_NAME)" && break
  i=$((i+1))
  log "UDC 绑定重试 $i/20 (等 adbd desc_ready)..."
  sleep 1
done
[ $i -lt 20 ] || log "UDC 绑定失败(检查 adbd 是否写描述符 / /sys/class/udc/)"

# 9. 验证 /dev/ttyGS0 创建
if [ $i -lt 20 ]; then
  sleep 2  # 等内核创建设备节点
  if [ -c /dev/ttyGS0 ]; then
    log "✅ USB gadget 配置成功: /dev/ttyGS0 就绪"
    log "   ADB: 主机插 USB 后运行 'adb devices'"
    log "   串口: cat /dev/ttyGS0"
  else
    log "⚠️  UDC 绑定成功但 /dev/ttyGS0 未创建"
    log "诊断:"
    log "  ls /sys/class/udc/ → $(ls /sys/class/udc/ 2>/dev/null)"
    log "  cat $CFG/$GADGET/UDC → $(cat $CFG/$GADGET/UDC 2>/dev/null)"
    log "  dmesg | tail -20 | grep -i 'udc\|gadget\|acm'"
  fi
fi
