#!/usr/bin/env bash
# z1_a9_handoff_selftest.sh —— 交棒脚本的判据回归（本机免 root 可跑）
#
# 思路：交棒的"找分区"用假的 sysfs 目录树（Z1_SYSBLOCK），"判 A9 形状"用目录夹具
# （Z1_SYSROOT 指过去就跳过挂载），最后一发 exec 用内部子 shell 承接 —— 板上 exec
# 顶替 PID 1，这里只验证"确实走到了 exec 且目标是 EM_ARM 的那份 init"。
set -uo pipefail
SELF=$(cd "$(dirname "$0")" && pwd)
WT=$(readlink -f "$SELF/../..")
BB=${BB:-/bin/busybox}
ARMELF=${ARMELF:-$WT/tools/z1/z1_a9_probe}
H=$SELF/z1_a9_handoff.sh
W=$(mktemp -d /tmp/a9hand.XXXXXX)
trap 'rm -rf "$W"' EXIT
[ -x "$BB" ] || { echo "ERR: 本机没有 busybox ($BB)"; exit 1; }
[ -f "$ARMELF" ] || { echo "ERR: 缺 arm32 ELF 夹具料 $ARMELF"; exit 1; }

DISK=zt.disk
mk_sysfs() { # $1=sysfs 目录  造 4 个分区：bootimg/android/cache/usrdata 按 scatter 字节数
  local d=$1 i
  mkdir -p "$d/${DISK}p1" "$d/${DISK}p2" "$d/${DISK}p3" "$d/${DISK}p4"
  echo 12288   > "$d/${DISK}p1/size"          # 6291456 B  bootimg
  echo 1843200 > "$d/${DISK}p2/size"          # 943718400 B android
  echo 462848  > "$d/${DISK}p3/size"          # 236978176 B cache
  echo 5172508 > "$d/${DISK}p4/size"          # 2648324096 B usrdata
  printf 'PARTNAME=android\n'  > "$d/${DISK}p2/uevent"
}
mk_fixture() { # $1=目录 $2=sdk $3=arm|arm64
  local d=$1 sdk=$2 arch=$3 b
  mkdir -p "$d/bin" "$d/etc"
  local abi=armeabi-v7a; [ "$arch" = arm64 ] && abi=arm64-v8a
  printf 'ro.build.version.sdk=%s\nro.build.version.release=%s\nro.product.cpu.abi=%s\nro.hardware=z1\n' \
    "$sdk" "$( [ "$sdk" = 28 ] && echo 9 || echo 4.4.2 )" "$abi" > "$d/build.prop"
  for b in init sh servicemanager hwservicemanager lmkd netd app_process32; do
    cp "$ARMELF" "$d/bin/$b"
    [ "$arch" = arm64 ] && printf '\xb7\x00' | dd of="$d/bin/$b" bs=1 seek=18 count=2 conv=notrunc status=none
  done
}
run_probe() { # $1=sysfs $2=sysroot  -> 打印 + 返回码
  BB=$BB Z1_SYSBLOCK="$1" Z1_SYSROOT="$2" Z1_DISK="$DISK" Z1_NODEPREFIX="$W/dev" \
  Z1_BYNAME_DIR="$W/by-name" Z1_HANDOFF_CONSOLE=/dev/stdout $BB sh "$H" probe 2>&1
}

fail=0
expect() { # $1=期望 PASS/FAIL $2=用例名 $3=返回码 $4=输出
  if [ "$1" = PASS ] && [ "$3" -eq 0 ]; then echo "  CASE=$2 PASS(判定符合)"
  elif [ "$1" = FAIL ] && [ "$3" -ne 0 ]; then echo "  CASE=$2 PASS(按预期拒绝)"
  else echo "  CASE=$2 ***UNEXPECTED(rc=$3)***"; fail=$((fail+1)); echo "$4" | sed 's/^/      /'; fi
}

mkdir -p "$W/dev"; mk_sysfs "$W/sys"

echo "===== 1) arm32 A9(sdk=28) 齐备 -> probe 必须 GO ====="
mk_fixture "$W/sys_a9_arm" 28 arm
out=$(run_probe "$W/sys" "$W/sys_a9_arm"); rc=$?; echo "$out" | grep -E 'RESULT_STEP|android 分区|build.prop:' | sed 's/^/  /'
[ $rc -eq 0 ] && expect PASS 1 "$rc" "$out" || { echo "  CASE=1 ***FAIL***"; fail=$((fail+1)); }

echo "===== 2) 只有几何没有 A9（sdk=19 的原厂形态）-> 必须拒 ====="
mk_fixture "$W/sys_44" 19 arm
out=$(run_probe "$W/sys" "$W/sys_44"); rc=$?; echo "$out" | grep -E 'RESULT_STEP' | sed 's/^/  /'
expect FAIL 2 "$rc" "$out"

echo "===== 3) A9 但 arm64 userspace -> 必须拒（本内核 32 位）====="
mk_fixture "$W/sys_a9_arm64" 28 arm64
out=$(run_probe "$W/sys" "$W/sys_a9_arm64"); rc=$?; echo "$out" | grep -E 'RESULT_STEP' | sed 's/^/  /'
expect FAIL 3 "$rc" "$out"

echo "===== 4) 属性写 arm32 但 ELF 是 arm64（属性骗人那一类）-> 必须按 ELF 拒 ====="
mk_fixture "$W/sys_liar" 28 arm
printf '\xb7\x00' | dd of="$W/sys_liar/bin/init" bs=1 seek=18 count=2 conv=notrunc status=none
out=$(run_probe "$W/sys" "$W/sys_liar"); rc=$?; echo "$out" | grep -E 'RESULT_STEP|e_machine' | sed 's/^/  /'
expect FAIL 4 "$rc" "$out"

echo "===== 5) 分区几何对不上（没有 android 尺寸）-> SKIP，不许乱挂 ====="
mkdir -p "$W/sys_bad/${DISK}p9"; echo 4096 > "$W/sys_bad/${DISK}p9/size"
out=$(run_probe "$W/sys_bad" "$W/sys_a9_arm"); rc=$?; echo "$out" | grep -E 'RESULT_STEP' | sed 's/^/  /'
expect FAIL 5 "$rc" "$out"

echo "===== 6) 尺寸文件缺失、只有 PARTNAME=android -> 走 uevent 兜底 ====="
mkdir -p "$W/sys_ue/${DISK}p2"; printf 'PARTNAME=android\n' > "$W/sys_ue/${DISK}p2/uevent"
out=$(run_probe "$W/sys_ue" "$W/sys_a9_arm"); rc=$?; echo "$out" | grep -E 'RESULT_STEP|节点' | sed 's/^/  /'
[ $rc -eq 0 ] && expect PASS 6 "$rc" "$out" || { echo "  CASE=6 ***FAIL***"; fail=$((fail+1)); }

echo "===== 7) go 的接线：by-name 符号链接 + 一路打到 exec ====="
mkdir -p "$W/dev" "$W/by-name"
out=$(BB=$BB Z1_SYSBLOCK="$W/sys" Z1_SYSROOT="$W/sys_a9_arm" Z1_DISK="$DISK" Z1_NODEPREFIX="$W/dev" \
       Z1_BYNAME_DIR="$W/by-name" Z1_HANDOFF_CONSOLE=/dev/stdout $BB sh -c "$BB sh $H go" 2>&1); rc=$?
echo "$out" | grep -E 'by-name|RESULT_STEP' | sed 's/^/  /'
echo "  (go 的 exec 目标在 x86 主机上必不可执行，rc=$rc 属预期；这里只断言判据链打通)"
echo "$out" | grep -q 'RESULT_STEP_handoff=EXEC' && [ -L "$W/by-name/system" ] \
  && echo "  CASE=7 PASS(到 exec 且 by-name/system 已建)" \
  || { echo "  CASE=7 ***FAIL***"; echo "$out" | sed 's/^/      /'; fail=$((fail=fail+1)); }
ls -l "$W/by-name" | sed 's/^/    /'

echo "====="
[ $fail -eq 0 ] && echo "HANDOFF_SELFTEST=ALL_PASS" || { echo "HANDOFF_SELFTEST=FAIL($fail)"; exit 1; }
