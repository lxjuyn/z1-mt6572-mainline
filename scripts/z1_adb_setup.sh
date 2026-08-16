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

# 0. 前置检查: 内核配置
if [ -f /proc/config.gz ]; then
  if ! zcat /proc/config.gz | grep -q "CONFIG_USB_CONFIGFS=y"; then
    log "FATAL: 内核未开 CONFIG_USB_CONFIGFS"
    exit 1
  fi
  if ! zcat /proc/config.gz | grep -q "CONFIG_USB_CONFIGFS_F_FS=y"; then
    log "FATAL: 内核未开 CONFIG_USB_CONFIGFS_F_FS (ADB 必需)"
    exit 1
  fi
  if ! zcat /proc/config.gz | grep -q "CONFIG_USB_CONFIGFS_ACM=y"; then
    log "FATAL: 内核未开 CONFIG_USB_CONFIGFS_ACM (串口必需)"
    exit 1
  fi
  log "内核配置检查通过"
else
  log "警告: /proc/config.gz 不存在，跳过内核配置检查"
fi

# 1. 挂 configfs (带验证)
if [ ! -d /sys/kernel/config ]; then
  $BB mount -t configfs configfs /sys/kernel/config 2>/dev/null || {
    log "FATAL: configfs 挂载失败"
    log "检查: dmesg | grep configfs"
    exit 1
  }
fi
if [ ! -d /sys/kernel/config/usb_gadget ]; then
  log "FATAL: 内核未开 CONFIG_USB_CONFIGFS 或 configfs 挂载异常"
  log "检查: ls /sys/kernel/config/"
  exit 1
fi
log "configfs 就绪"

# 2. 清理旧 gadget (若存在)
if [ -d "$CFG/$GADGET" ]; then
  log "清理旧 gadget..."
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
$BB mount -t functionfs adb "$FUNCTIONFS_DIR" 2>/dev/null || {
  log "FATAL: functionfs 挂载失败"
  log "检查: 内核 CONFIG_USB_F_FS 是否启用"
  log "      dmesg | grep functionfs"
  exit 1
}

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
  log "检查: ls -l /root/connsys/adbd"
  log "      ldd /root/connsys/adbd (检查依赖库)"
  exit 1
fi
log "adbd started pid $ADBD_PID, waiting for desc_ready..."

# 8b. 检查 UDC 是否存在
#     UDC 名称 = "musb-hdrc" (musb_core.c:95 MUSB_DRIVER_NAME)
#     不是 DTS 设备节点地址 (11100000.usb)
UDC_NAME="musb-hdrc"
if [ ! -d /sys/class/udc ]; then
  log "FATAL: /sys/class/udc 不存在 (内核未开 USB gadget UDC 框架)"
  log "检查: CONFIG_USB_GADGET=y CONFIG_USB_MUSB_GADGET=y"
  exit 1
fi

AVAILABLE_UDCS=$(ls /sys/class/udc/ 2>/dev/null)
log "可用 UDC: $AVAILABLE_UDCS"

if [ -z "$AVAILABLE_UDCS" ]; then
  log "FATAL: 无可用 UDC"
  log "检查: dmesg | grep -i 'musb\\|udc\\|phy'"
  log "      ls /sys/class/udc/ (应为空)"
  log "      MUSB probe 是否成功? (dmesg | grep 'musb.*probe')"
  exit 1
fi

# 检查目标 UDC 是否存在
if ! echo "$AVAILABLE_UDCS" | grep -q "^$UDC_NAME$"; then
  log "FATAL: 目标 UDC '$UDC_NAME' 不存在"
  log "可用 UDC: $AVAILABLE_UDCS"
  log "检查: dmesg | grep -i musb"
  log "      MUSB 驱动是否正确加载?"
  exit 1
fi
log "目标 UDC '$UDC_NAME' 存在 ✓"

# 8c. 绑 UDC (带重试, 等 adbd 写好描述符)
i=0
while [ $i -lt 20 ]; do
  # 检查 adbd 是否还活着
  if ! kill -0 $ADBD_PID 2>/dev/null; then
    log "adbd 已退出，重启..."
    /root/connsys/adbd 2>&1 &
    ADBD_PID=$!
    sleep 1
  fi

  # 尝试绑定 UDC
  echo "$UDC_NAME" > "$CFG/$GADGET/UDC" 2>/dev/null && {
    log "UDC 绑定成功 ($UDC_NAME)"
    break
  }

  i=$((i+1))
  log "UDC 绑定重试 $i/20 (等 adbd desc_ready)..."
  sleep 1
done

if [ $i -ge 20 ]; then
  log "FATAL: UDC 绑定失败"
  log "诊断:"
  log "  adbd pid: $ADBD_PID (alive: $(kill -0 $ADBD_PID 2>/dev/null && echo yes || echo no))"
  log "  UDC 状态: $(cat $CFG/$GADGET/UDC 2>/dev/null || echo 'empty')"
  log "  functionfs 挂载: $(mount | grep functionfs)"
  log "  dmesg 最后 10 行:"
  dmesg | tail -10 | grep -i 'udc\|gadget\|acm\|ffs' | while read line; do
    log "    $line"
  done
  log "可能原因:"
  log "  1. adbd 未写入描述符 (检查 /dev/usb-ffs/adb/ep0 是否被打开)"
  log "  2. UDC 名称错误 (应为 'musb-hdrc', 不是 '11100000.usb')"
  log "  3. MUSB gadget 模式未初始化 (检查 dmesg | grep musb)"
  exit 1
fi

# 9. 验证 /dev/ttyGS0 创建
sleep 2  # 等内核创建设备节点
if [ -c /dev/ttyGS0 ]; then
  log "✅ USB gadget 配置成功: /dev/ttyGS0 就绪"
  log "   ADB: 主机插 USB 后运行 'adb devices'"
  log "   串口: cat /dev/ttyGS0"
  log "   UDC 状态: $(cat $CFG/$GADGET/UDC)"
  log "   gadget 配置: ls $CFG/$GADGET/"
else
  log "⚠️  UDC 绑定成功但 /dev/ttyGS0 未创建"
  log "诊断:"
  log "  ls /sys/class/udc/ → $(ls /sys/class/udc/ 2>/dev/null)"
  log "  UDC 绑定状态 → $(cat $CFG/$GADGET/UDC 2>/dev/null)"
  log "  ACM function 链接 → $(ls -l $CFG/$GADGET/configs/c.1/ 2>/dev/null | grep acm)"
  log "  dmesg 相关日志:"
  dmesg | grep -i 'acm\|gadget\|udc\|ttyGS' | tail -10 | while read line; do
    log "    $line"
  done
  log "可能原因:"
  log "  1. ACM function 未正确链接到 config"
  log "  2. 内核 CONFIG_USB_F_ACM 未启用"
  log "  3. devtmpfs 未挂载 (ls /dev/ 看有无 ttyGS*)"
  log "  4. MUSB VBUS 检测失败 (检查 dmesg | grep 'vbus\\|DEVCTL')"
fi

log "配置完成"
