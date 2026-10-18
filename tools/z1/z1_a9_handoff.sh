#!/bin/busybox sh
# z1_a9_handoff.sh —— 把 PID 1 交棒给 android 分区里的 A9 userspace
#
# 用法:  z1_a9_handoff.sh probe     只读判定，exit 0 = 可以交棒
#        z1_a9_handoff.sh go        挂载 + exec /system/bin/init second_stage（成功不返回）
#
# 为什么要它：mainline 线本来要求"另一台机器把 A9 generic ramdisk(first-stage init) 交过来"，
# 但本机出网面拿不到那份原料（见 CORRECTION_ARM32_A9_EXISTS_20261018.md）。本脚本顶掉
# first-stage 的必要职责（认出 android 分区、挂 /system、补 by-name 符号链接），于是
# **只要手里有一份 arm32 A9 的 system.img，我们的 boot.img 就能直接把它拉起来**。
#
# 分区为什么不按名字找：GPT 分区名要靠 uevent 的 PARTNAME 或 udev 的 by-partlabel，
# 本树 + busybox mdev 不保证给。所以按**分区字节数**认（几何来自 1/MT6572_Android_scatter.txt），
# 找不到再退回读 uevent 里的 PARTNAME —— 两条都是实测可验证的，不靠猜设备号。
set -u
BB=${BB:-/bin/busybox}
SYSROOT=${Z1_SYSROOT:-/system}
BYNAME=${Z1_BYNAME_DIR:-/dev/block/by-name}
VENDROOT=${Z1_VENDROOT:-/vendor}
SELINUXFS=${Z1_SELINUXFS:-/sys/fs/selinux}
NODEPREFIX=${Z1_NODEPREFIX:-/dev}
SYSBLOCK=${Z1_SYSBLOCK:-/sys/class/block}
CON=${Z1_HANDOFF_CONSOLE:-/dev/console}
DISK=${Z1_DISK:-mmcblk0}

# label -> 字节数（scatter）；android=p4 是 system，usrdata=p6 绝不格式化
LBL_ANDROID=943718400        # 0x38400000
LBL_CACHE=236978176          # 0xE200000
LBL_BOOTIMG=6291456          # 0x600000
LBL_USRDATA=2648324096       # 0x9DDC0000

say() { echo "[a9hand] $*" > "$CON" 2>/dev/null || echo "[a9hand] $*"; }
bytes_of() { #$1=节点名  -> 字节数
  local s
  s=$(cat "$SYSBLOCK/$1/size" 2>/dev/null) || return 1
  [ -n "$s" ] || return 1
  echo $((s * 512))
}
partname_of() {
  $BB grep -m1 '^PARTNAME=' "$SYSBLOCK/$1/uevent" 2>/dev/null | $BB sed 's/^PARTNAME=//'
}

find_by_size() { # $1=目标字节数 -> 节点名（不含 /dev/）
  local d n b
  for d in $SYSBLOCK/${DISK}p*; do
    [ -e "$d" ] || continue
    n=$(basename "$d")
    b=$(bytes_of "$n") || continue
    [ "$b" = "$1" ] && { echo "$n"; return 0; }
  done
  return 1
}

find_by_label() { # $1=PARTNAME -> 节点名
  local d n
  for d in $SYSBLOCK/${DISK}p*; do
    [ -e "$d" ] || continue
    n=$(basename "$d")
    [ "$(partname_of "$n")" = "$1" ] && { echo "$n"; return 0; }
  done
  return 1
}

elf_is_arm32() { # $1=文件  -> 0 是 EM_ARM(0x28)
  local b
  [ -f "$1" ] || return 2
  b=$($BB od -An -tx1 -N2 -j18 "$1" 2>/dev/null | $BB tr -d ' \n')
  [ "$b" = "2800" ] && return 0
  [ "$b" = "b700" ] && return 3
  return 2
}

pick_android() {
  N_ANDROID=$(find_by_size "$LBL_ANDROID") || N_ANDROID=$(find_by_label android) || N_ANDROID=""
}

# ---------------------------------------------------------------- probe
probe() {
  rc=0
  pick_android
  if [ -z "${N_ANDROID:-}" ]; then
    say "RESULT_STEP_handoff_probe=SKIP(没有尺寸=${LBL_ANDROID}B 且 PARTNAME 非 android 的分区)"
    return 1
  fi
  say "android 分区节点=$N_ANDROID ($(bytes_of "$N_ANDROID") B)"
  # 只读试探挂载，判据与 z1_a9_system_accept.py 同源
  # 已经挂好（或本机测试用目录夹具）就不重复挂；重复 mount 在板上是静默失败的主要来源
  if [ ! -e "$SYSROOT/build.prop" ] && [ ! -e "$SYSROOT/system/build.prop" ]; then
    mkdir -p "$SYSROOT" 2>/dev/null
    if ! $BB mount -t ext4 -o ro ${NODEPREFIX}/$N_ANDROID "$SYSROOT" 2>/dev/null; then
      say "RESULT_STEP_handoff_probe=FAIL(${NODEPREFIX}/$N_ANDROID 挂不上 ext4)"
      return 1
    fi
  else
    say "$SYSROOT 已有内容，跳过挂载"
  fi
  BP=$($BB find "$SYSROOT" -maxdepth 2 -name build.prop 2>/dev/null | $BB head -1)
  [ -n "$BP" ] || BP="$SYSROOT/build.prop"
  if [ ! -f "$BP" ]; then
    say "RESULT_STEP_handoff_probe=FAIL(没有 build.prop)"
    umount "$SYSROOT" 2>/dev/null
    return 1
  fi
  SDK=$($BB grep -m1 '^ro.build.version.sdk=' "$BP" 2>/dev/null | $BB sed 's/.*=//')
  ABI=$($BB grep -m1 '^ro.product.cpu.abi=' "$BP" 2>/dev/null | $BB sed 's/.*=//')
  say "build.prop: sdk=${SDK:-?} abi=${ABI:-?}"
  if [ "$SDK" != 28 ]; then
    say "RESULT_STEP_handoff_probe=FAIL(sdk=$SDK 不是 28，不交棒)"
    umount "$SYSROOT" 2>/dev/null
    return 1
  fi
  case "$ABI" in *arm64*)
    say "RESULT_STEP_handoff_probe=FAIL(abi=$ABI 是 arm64，本内核 32 位跑不了)"
    umount "$SYSROOT" 2>/dev/null; return 1;; esac
  BIN=""
  for c in "$SYSROOT/bin/init" "$SYSROOT/system/bin/init" "$SYSROOT/xbin/init"; do
    [ -f "$c" ] && { BIN=$c; break; }
  done
  if [ -z "$BIN" ]; then
    say "RESULT_STEP_handoff_probe=FAIL(找不到 init)"
    umount "$SYSROOT" 2>/dev/null; return 1
  fi
  elf_is_arm32 "$BIN"; EM=$?
  if [ $EM -ne 0 ]; then
    say "RESULT_STEP_handoff_probe=FAIL($BIN 的 e_machine 不是 EM_ARM(退 $EM)，交棒必 SIGILL)"
    umount "$SYSROOT" 2>/dev/null; return 1
  fi
  say "A9_INIT_BIN=$BIN (ELF EM_ARM 已验)"
  # 这一行决定板上第一眼的解释：r2 那份 init 是带 [Z1STOCK-DIAG] 的 stock 线定制件，
  # 它的代偿分支按 /z1_stock_kernel 标记门控（我们这边标记已删），但它还会多打一串
  # [Z1STOCK-DIAG] 诊断行；认出来就别把它们当 AOSP 原生日志读。
  if $BB grep -qa 'Z1STOCK-DIAG' "$BIN" 2>/dev/null; then
    say "RESULT_STEP_init_flavor=Z1STOCK-DIAG(stock 线定制件；其代偿分支由 /z1_stock_kernel 门控，本镜像已无该标记)"
  else
    say "RESULT_STEP_init_flavor=AOSP-pine"
  fi
  # 顺带把它的阶段入口也认出来：这份里没有 selinux_setup 字串，说明 first stage 是直接
  # reexec 到 second_stage 的 —— 交棒用 second_stage 是对的，别改成 selinux_setup。
  if $BB grep -qa 'selinux_setup' "$BIN" 2>/dev/null; then
    say "init 里有 selinux_setup 入口(标准 A9 三段式)"
  else
    say "init 里没有 selinux_setup 字串(reexec 直接到 second_stage)"
  fi
  say "RESULT_STEP_handoff_probe=GO"
  return 0
}

# ---------------------------------------------------------------- go
byname_link() { # $1=label $2=字节数
  local n
  n=$(find_by_size "$2") || n=$(find_by_label "$1") || n=""
  [ -n "$n" ] || { say "by-name/$1 无从生成(找不到尺寸=$2 或 PARTNAME=$1 的分区)"; return 0; }
  mkdir -p "$BYNAME" 2>/dev/null
  ln -sf ${NODEPREFIX}/$n "$BYNAME/$1" 2>/dev/null && say "by-name/$1 -> /dev/$n"
}

go() {
  pick_android
  [ -n "${N_ANDROID:-}" ] || { say "RESULT_STEP_handoff=FAIL(无 android 分区)"; exit 1; }
  say "交棒开始：${NODEPREFIX}/$N_ANDROID -> $SYSROOT"

  # A9 的 fstab/ueventd 全靠 /dev/block/by-name/*，本树 mdev 不建，这里按几何补
  byname_link system    $LBL_ANDROID
  byname_link cache     $LBL_CACHE
  byname_link bootimg   $LBL_BOOTIMG
  byname_link userdata  $LBL_USRDATA

  if [ ! -e "$SYSROOT/build.prop" ] && [ ! -e "$SYSROOT/system/build.prop" ]; then
    $BB mount -t ext4 -o ro noatime ${NODEPREFIX}/$N_ANDROID "$SYSROOT" 2>/dev/null || \
      $BB mount -t ext4 -o ro ${NODEPREFIX}/$N_ANDROID "$SYSROOT" 2>/dev/null
    if ! $BB mount | $BB grep -q " on $SYSROOT "; then
      say "RESULT_STEP_handoff=FAIL(挂不上 ${NODEPREFIX}/$N_ANDROID)"
      exit 1
    fi
    say "已只读挂上 $SYSROOT（noatime）"
  else
    say "$SYSROOT 已有内容，跳过挂载"
  fi

  # LOS16 的 system-as-root 形态：/system 在挂载点下一层
  if [ ! -e "$SYSROOT/bin/init" ] && [ -e "$SYSROOT/system/bin/init" ]; then
    $BB mount -o bind "$SYSROOT/system" "$SYSROOT" 2>/dev/null && say "/system bind 下移一层"
  fi

  # ---- /vendor：A-only 的 LOS16 把 vendor 塞在 system 里，而它是只读的。
  # A9 second-stage 只在 /vendor|/odm|/product 下找 fstab.<ro.boot.hardware>，
  # 我们手里只有 device 层那份 fstab.z1（在 /system/etc），只读目录里放不进去，
  # 所以把 vendor 换成 tmpfs 再整份拷进来 —— 换来一个可写的 /vendor/etc。
  VSRC=""
  for c in "$SYSROOT/vendor" "$SYSROOT/system/vendor"; do
    [ -d "$c" ] && { VSRC=$c; break; }
  done
  if [ -n "$VSRC" ]; then
    mkdir -p "$VENDROOT" 2>/dev/null
    VKB=$($BB du -sk "$VSRC" 2>/dev/null | $BB awk '{print $1}')
    VSZ=$(( ${VKB:-20480} / 1024 + 48 ))m
    if $BB mount -t tmpfs -o "size=$VSZ" tmpfs /vendor 2>/dev/null && \
       $BB cp -a "$VSRC/." "$VENDROOT/" 2>/dev/null; then
      say "RESULT_STEP_vendor=TMPFS($VENDROOT 换成 tmpfs size=$VSZ，内容自 $VSRC)"
    elif $BB mount --bind "$VSRC" "$VENDROOT" 2>/dev/null; then
      say "RESULT_STEP_vendor=BIND($VENDROOT 只读自 $VSRC，fstab 多半投不进去)"
    else
      say "RESULT_STEP_vendor=UNMOUNTED($VSRC 没能挂到 $VENDROOT，下面仍按目录现状准备 fstab)"
    fi
  else
    say "RESULT_STEP_vendor=NONE(镜像里没有 vendor 目录)"
  fi

  # ---- fstab：从 cmdline 的 androidboot.hardware= 取机型名，与 A9 同源
  HW=$($BB cat /proc/cmdline 2>/dev/null | $BB tr ' ' '\n' | $BB sed -n 's/^androidboot.hardware=//p')
  [ -n "$HW" ] || HW=z1
  say "ro.boot.hardware 等价名=$HW(自 /proc/cmdline)"
  FFOUND=""
  for d in "$VENDROOT/etc" /odm/etc /product/etc; do
    for f in "$d"/fstab.*; do [ -f "$f" ] && { FFOUND=$f; break; }; done
    [ -n "$FFOUND" ] && break
  done
  if [ -n "$FFOUND" ]; then
    say "RESULT_STEP_fstab=OK(已有 $FFOUND)"
  else
    FSRC=""
    for c in "$SYSROOT/etc/fstab.$HW" "$SYSROOT/etc/fstab.z1" "$SYSROOT/system/etc/fstab.$HW"; do
      [ -f "$c" ] && { FSRC=$c; break; }
    done
    for m in "$SYSROOT/etc/z1_device_manifest.xml" "$SYSROOT/system/etc/z1_device_manifest.xml"; do
      if [ -f "$m" ] && [ ! -f "$VENDROOT/manifest.xml" ]; then
        $BB cp "$m" "$VENDROOT/manifest.xml" 2>/dev/null && say "RESULT_STEP_manifest=PLACED($VENDROOT/manifest.xml 自 $m)"
        break
      fi
    done
    if [ -n "$FSRC" ] && mkdir -p "$VENDROOT/etc" 2>/dev/null && $BB cp "$FSRC" "$VENDROOT/etc/fstab.$HW" 2>/dev/null; then
      say "RESULT_STEP_fstab=PLACED($VENDROOT/etc/fstab.$HW 自 $FSRC)"
    elif [ -n "$FSRC" ]; then
      say "RESULT_STEP_fstab=FAIL(找到 $FSRC 但 $VENDROOT 不可写；A9 没 fstab 就挂不了 /data 与 /cache)"
    else
      say "RESULT_STEP_fstab=MISS(镜像里没有 fstab.$HW 与 fstab.z1，device 层没投？)"
    fi
  fi

  # ---- SELinux：first-stage 该做的策略载入补上。second-stage 只调 SelinuxSetEnforcement，
  # 策略没载过就直接判死；cil 需要 secilc 在板上编不动，所以只认 precompiled_sepolicy。
  $BB mount -t selinuxfs selinux "$SELINUXFS" 2>/dev/null
  if [ -e "$SELINUXFS/load" ]; then
    POL=""
    for c in "$VENDROOT/etc/selinux/precompiled_sepolicy" /odm/etc/selinux/precompiled_sepolicy \
             /product/etc/selinux/precompiled_sepolicy $SYSROOT/etc/selinux/precompiled_sepolicy \
             $SYSROOT/vendor/etc/selinux/precompiled_sepolicy \
             $SYSROOT/system/vendor/etc/selinux/precompiled_sepolicy; do
      [ -f "$c" ] && { POL=$c; break; }
    done
    if [ -n "$POL" ]; then
      if $BB cat "$POL" > "$SELINUXFS/load" 2>/dev/null; then
        say "RESULT_STEP_selinux=LOADED($POL $($BB wc -c < "$POL" 2>/dev/null) B 字节已喂进 $SELINUXFS/load)"
      else
        say "RESULT_STEP_selinux=FAIL(load 被拒：策略版本高于内核 SELinux 上限，或镜像里那份是给 stock 3.4 编的)"
      fi
    else
      say "RESULT_STEP_selinux=NOPOLICY(没有 precompiled_sepolicy；permissive 下 A9 仍可能因无标签而挂)"
    fi
    if [ -e "$SELINUXFS/enforce" ]; then
      echo 0 > "$SELINUXFS/enforce" 2>/dev/null && say "enforce=0(兜 dts 的 androidboot.selinux=permissive)"
    fi
  else
    say "RESULT_STEP_selinux=SKIP($SELINUXFS/load 不在：内核未开 SECURITY_SELINUX 或挂不上)"
  fi

  # A-only 的 GSI（如 phhusson v123）刻意不带 /system/bin/init 与 /system/bin/ueventd：
  # 它假定设备的 boot ramdisk 提供 first-stage。因此允许从 ramdisk 里喂这两个二进制，
  # 但 A9 的 rc 里写的是绝对路径 /system/bin/init、/system/bin/ueventd，
  # 所以得把它们**种进 /system/bin**：这一步只在这两个路径本来不存在时做一次，
  # 用 rw 挂载 -> cp -> sync -> 立即回 ro，不改变镜像在分区上的长期形态。
  if [ ! -x "$SYSROOT/bin/init" ] && [ -f /sbin/a9_init ]; then
    $BB mount -o remount,rw "$SYSROOT" 2>/dev/null
    if $BB mount | $BB grep " on $SYSROOT " | $BB grep -q 'rw'; then
      cp /sbin/a9_init "$SYSROOT/bin/init" 2>/dev/null && chmod 755 "$SYSROOT/bin/init"
      [ -f /sbin/a9_ueventd ] && cp /sbin/a9_ueventd "$SYSROOT/bin/ueventd" 2>/dev/null && chmod 755 "$SYSROOT/bin/ueventd"
      $BB sync
      $BB mount -o remount,ro "$SYSROOT" 2>/dev/null
      say "RESULT_STEP_seed=$([ -x "$SYSROOT/bin/init" ] && echo OK || echo FAIL)(init/ueventd 自 ramdisk 种入 $SYSROOT/bin)"
    else
      say "RESULT_STEP_seed=FAIL(/system 无法临时转 rw)"
    fi
  fi

  INIT=""
  for c in "$SYSROOT/bin/init" "$SYSROOT/system/bin/init" "$SYSROOT/xbin/init" /sbin/a9_init; do
    [ -x "$c" ] && { INIT=$c; break; }
  done
  if [ -z "$INIT" ]; then
    # A-only 的 GSI（如 phhusson v123）刻意不带 /system/bin/init —— 它假定设备的 boot ramdisk
    # 提供 first-stage。此时需要外部把 arm32 的 A9 init 投进 ramdisk 的 /sbin/a9_init。
    say "RESULT_STEP_handoff=FAIL(没找到可执行的 A9 init：查过 $SYSROOT/bin/init、$SYSROOT/system/bin/init、$SYSROOT/xbin/init、/sbin/a9_init)"
    say "缺的是 A9 自己的 PID1(init/ueventd)，不是 system 内容；见 A9_GSI_20261018.md"
    exit 1
  fi
  say "RESULT_STEP_handoff=EXEC($INIT second_stage)"
  say ">>> 以下为 A9 init 自己的日志，串口上会接续出现 ===="
  exec "$INIT" second_stage
  say "RESULT_STEP_handoff=FAIL(exec 返回，A9 init 没跑住)"
  exit 1
}

case "${1:-probe}" in
  probe) probe; exit $?;;
  go)    go; exit 1;;
  *)     echo "用法: $0 probe|go"; exit 2;;
esac
