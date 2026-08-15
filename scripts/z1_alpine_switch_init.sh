#!/bin/busybox sh
# z1_alpine_switch_init.sh — Alpine 从 p4 (system) 启动的 initramfs /init (方案 A)
# Z1 (MT6572) mainline 移植 — 2026-08-16
#
# 用途: 替换原 "busybox 恢复 shell" /init。逻辑:
#   mount proc/sys/dev (devtmpfs, 不盖掉内核自动挂的) → 常驻喂狗 →
#   (可选)switch 前起 ttyGS0 getty → 等 /dev/mmcblk0p4 最多 30s →
#   建 /dev/block 软链 (Alpine fstab 免改) → mount p4 /newroot (ext4 rw) →
#   mount --bind proc/sys/dev → exec switch_root -c /dev/ttyS0 /newroot /sbin/init
# 失败兜底: p4 节点超时 / 挂载失败 / /newroot/sbin/init 缺失 → 落恢复 shell
#           (ttyS0 串口 + ttyGS0 USB-ACM + tty0 屏幕, 与现恢复 shell 一致)
#
# 参考: mainline_recon/ALPINE_BOOT_PATH_20260815.md §3.2
# 打包注入: tools/repack_ramdisk_connsys.sh 的 Z1_ALPINE_SWITCH=1 段 (见
#           mainline_recon/ALPINE_IMPLEMENT_20260816.md §4)
# 本文件整体拷入 initramfs 的 /init (chmod 0755, root:root)。

BB=/bin/busybox

# ============================================================
# 1. kernel 伪文件系统
#    /dev 用 devtmpfs: 内核 CONFIG_DEVTMPFS_MOUNT=y 在 /init 之前已把
#    devtmpfs 挂上 /dev, 块设备节点(mmblk0p4 等) 由内核自动生成。
#    绝不能再挂 tmpfs 盖住它 (老 init 的做法: tmpfs 会把 devtmpfs 节点藏掉,
#    mmcblk0p4 只有再跑一次 mdev -s 才出现)。
#    下面这条在已挂载时是 EBUSY 空操作 (2>/dev/null || true), 兜底用。
# ============================================================
$BB mount -t proc proc /proc 2>/dev/null
$BB mount -t sysfs sysfs /sys 2>/dev/null
$BB mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
$BB mdev -s 2>/dev/null

# ============================================================
# 2. (可选) switch 前起 ttyGS0 (USB ACM) getty
#    默认 0: Alpine 自己的 /etc/inittab 已有
#        ttyGS0::respawn:/sbin/getty -L 115200 ttyGS0 vt100
#    switch 后马上会再起一个, switch 前先起会与它抢 termios
#    (正是 connsys init 修过的双 getty 竞态)。除非要跨 switch 不间断的
#    USB shell, 否则保持 0。
# ============================================================
GETTY_BEFORE_SWITCH=0
if [ "$GETTY_BEFORE_SWITCH" = "1" ]; then
  (
    i=0
    while [ $i -lt 300 ]; do
      if [ -e /dev/ttyGS0 ]; then
        $BB stty -F /dev/ttyGS0 115200 icanon icrnl onlcr echo isig ixon opost 2>/dev/null
        exec setsid $BB getty -L -n -l /bin/sh 115200 ttyGS0 vt100
      fi
      $BB mdev -s 2>/dev/null
      $BB sleep 1
      i=$((i+1))
    done
  ) </dev/null >/dev/null 2>&1 &
fi

# ============================================================
# 3. 常驻喂狗
#    LK 跳内核前会把 HW WDT 武装上; mediatek_timer_init 已早停,
#    这里再常驻喂 /dev/watchdog 兜底 (防止任何驱动在 Alpine init 接管前
#    重新武装 WDT)。busybox watchdog 可用则用它常驻, 否则 echo V 循环。
#    switch_root 后若该循环还活着更好 (Alpine 自己不喂), 死了也无妨
#    (早停的 WDT 不会自己再武装)。
# ============================================================
if [ -e /dev/watchdog ]; then
  $BB echo "[init] feeding watchdog /dev/watchdog (persistent loop)"
  if $BB watchdog -t 5 -T 10 /dev/watchdog >/dev/null 2>&1; then
    :
  else
    (while true; do $BB echo V > /dev/watchdog 2>/dev/null; $BB sleep 5; done) &
  fi
fi

$BB echo "[init] Alpine switch-root init starting"
$BB echo "[init] kernel: $($BB cat /proc/version 2>/dev/null)"

# ============================================================
# 4. 等 /dev/mmcblk0p4 (mmc0 probe 实测 ~8s, 给 30s)
#    循环里每轮 mdev -s 兜底 (万一 /dev 被别处盖成非 devtmpfs)
# ============================================================
i=0
while [ $i -lt 30 ]; do
  if [ -b /dev/mmcblk0p4 ]; then
    break
  fi
  $BB echo "[init] waiting for /dev/mmcblk0p4 ($((i+1))s/30s)"
  $BB mdev -s 2>/dev/null
  $BB sleep 1
  i=$((i+1))
done

# ============================================================
# 5. 恢复 shell (失败兜底) — 与现 connsys init 的恢复路径一致:
#    ttyGS0 等待循环 + tty0 (framebuffer) + ttyS0 (串口, exec)
#    喂狗已在上文第 3 段常驻, 此处无需重复。
# ============================================================
recovery_shell() {
  $BB echo "[init] dropping to recovery shell (ttyS0 + ttyGS0 + tty0)"
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
  $BB setsid $BB sh -c 'exec $BB getty -n -l /bin/sh 0 tty0 vt100 2>/dev/null' </dev/tty0 >/dev/tty0 2>&1 &
  exec $BB getty -L -n -l /bin/sh 115200 ttyS0 vt100 2>/dev/null
  exit 1   # 仅当上面 exec getty 意外失败时走到: 别落回 switch 路径
}

if [ ! -b /dev/mmcblk0p4 ]; then
  $BB echo "[init] FATAL: /dev/mmcblk0p4 never appeared after 30s"
  recovery_shell
fi

# ============================================================
# 6. Alpine fstab 兼容: /dev/block/mmcblk0pN 软链
#    mainline devtmpfs 的块节点是 /dev/mmcblk0pN; Alpine /etc/fstab 用的是
#    /dev/block/mmcblk0pN。在 switch 前于 devtmpfs 上建软链, 因 /dev 会被
#    bind 进 Alpine, fstab 原样可用 → 免重刷 p4。
# ============================================================
$BB echo "[init] creating /dev/block symlinks for Alpine fstab"
$BB mkdir -p /dev/block 2>/dev/null
for p in 4 5 6; do
  $BB ln -sf ../mmcblk0p$p /dev/block/mmcblk0p$p 2>/dev/null || true
done

# ============================================================
# 7. 挂 p4 → /newroot
#    system_alpine.img 是 mke2fs 干净镜像, 直接 rw 挂安全。
#    (若某天 p4 是脏 ext4, 恢复 shell 里可手动 mount -o ro 抢救;
#     initramfs busybox 没有 fsck.ext4, 无法在 init 里 fsck。)
# ============================================================
$BB echo "[init] mounting /dev/mmcblk0p4 (ext4 rw) -> /newroot"
$BB mkdir -p /newroot 2>/dev/null
$BB mount -t ext4 -o rw /dev/mmcblk0p4 /newroot 2>/dev/null
if [ $? -ne 0 ]; then
  $BB echo "[init] FATAL: mount ext4 /dev/mmcblk0p4 failed (unformatted/dirty/wrong fstype?)"
  recovery_shell
fi

# 兜底校验: /newroot 里真有可执行的 /sbin/init (防 p4 里不是 Alpine)
if [ ! -x /newroot/sbin/init ]; then
  $BB echo "[init] FATAL: /newroot/sbin/init not executable (p4 image is not Alpine?)"
  recovery_shell
fi

# ============================================================
# 8. bind proc/sys/dev 进 Alpine
#    Alpine rootfs 自带的 /proc /sys /dev 目录会被 bind 盖掉 (dir 必须存在,
#    先 mkdir -p 兜底)。
# ============================================================
$BB mkdir -p /newroot/proc /newroot/sys /newroot/dev 2>/dev/null
$BB mount --bind /proc /newroot/proc 2>/dev/null
$BB mount --bind /sys  /newroot/sys  2>/dev/null
$BB mount --bind /dev  /newroot/dev  2>/dev/null

# ============================================================
# 9. switch_root
#    -c /dev/ttyS0: switch 后把 stdio 重定向到串口 (C3 桥), 保证 Alpine
#    init/OpenRC 的启动输出 + 登录提示直接可见。
#    /dev/ttyS0 在 bind 进 Alpine 的 devtmpfs 里存在 (串口驱动已 probe)。
# ============================================================
$BB echo "[init] switch_root /newroot /sbin/init ..."
exec $BB switch_root -c /dev/ttyS0 /newroot /sbin/init
