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
  for c in "$SYSROOT/bin/init" "$SYSROOT/system/bin/init"; do
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

  # 非 Treble 分区的 A9 把 /vendor 放在 system 里；LOS16 A-only 常见 /system/vendor
  [ -d "$SYSROOT/vendor" ] && { mkdir -p /vendor 2>/dev/null; $BB mount --bind "$SYSROOT/vendor" /vendor 2>/dev/null \
      && say "/vendor bind 自 $SYSROOT/vendor"; }
  # LOS16 的 system-as-root 形态：/system 在挂载点下一层
  if [ ! -e "$SYSROOT/bin/init" ] && [ -e "$SYSROOT/system/bin/init" ]; then
    $BB mount -o bind "$SYSROOT/system" "$SYSROOT" 2>/dev/null && say "/system bind 下移一层"
  fi

  INIT=$SYSROOT/bin/init
  [ -x "$INIT" ] || INIT=$SYSROOT/system/bin/init
  if [ ! -x "$INIT" ]; then
    say "RESULT_STEP_handoff=FAIL(可执行的 A9 init 不存在)"
    exit 1
  fi
  # 交棒后 A9 init 自己建 devpts/ashmem 绑定、cgroup 与 property 存储；
  # 我们只保证 /proc /sys /dev 在位、SELinux 由 dts 的 androidboot.selinux=permissive 决定。
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
