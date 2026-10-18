#!/usr/bin/env bash
# z1_a9_system_accept_selftest.sh —— 给 system 验收闸做正/负两路回归
#
# 为什么需要：闸只跑一次"能拒掉原厂 4.4"不足以证明可用 —— 误拒一份真 A9 镜像的代价
# 是让持源树的人白打包一次。所以固定三条判据：真 4.4 必须 REJECT、A9 arm32 形状必须
# ACCEPT、A9 但仍是 stock 门控 + arm64 必须 REJECT。夹具用 mke2fs -d 直接落目录，
# 比 debugfs 逐条 write 可靠。
set -uo pipefail
SELF=$(cd "$(dirname "$0")" && pwd)
WT=$(readlink -f "$SELF/../..")
ROOT=$(readlink -f "$WT/..")
ARMELF=${ARMELF:-$WT/tools/z1/z1_a9_probe}
STOCK=${STOCK_IMG:-$ROOT/1/system.img}
WORK=$(mktemp -d /tmp/a9acc_selftest.XXXXXX)
trap 'rm -rf "$WORK"' EXIT
[ -f "$ARMELF" ] || { echo "ERR: 找不到 arm32 ELF 夹具料 $ARMELF"; exit 1; }

mkimg() {  # $1=staging $2=out
  # 注意最后那个参数是**块数**不是字节数：写成 64*1024*1024 会造出 256 GiB 的镜像，
  # 直接撞上"超 android 分区"这条判据（本自测第一版就是这样假失败的）。
  rm -f "$2"
  mke2fs -q -F -t ext4 -b 4096 -O ^metadata_csum,^64bit -d "$1" "$2" 16384 >/dev/null 2>&1 \
    || { echo "ERR: mke2fs -d 失败（e2fsprogs 需 >=1.42）"; exit 1; }
}

stage_a9() {  # $1=dir $2=arm|arm64 $3=gated|clean
  local d=$1 arch=$2 gate=$3
  mkdir -p "$d/system/bin" "$d/system/etc/init" "$d/system/etc/vintf" \
           "$d/system/etc/selinux" "$d/vendor/etc/vintf"
  local sdk=28 abi=armeabi-v7a
  [ "$arch" = arm64 ] && abi=arm64-v8a
  {
    echo "ro.build.version.sdk=$sdk"
    echo "ro.build.version.release=9"
    echo "ro.product.cpu.abi=$abi"
    echo "ro.product.cpu.abilist=$abi"
    echo "ro.hardware=z1"
    echo "ro.product.device=z1"
    [ "$gate" = gated ] && echo "ro.z1.stock_kernel=1"
  } > "$d/system/build.prop"
  for b in init sh servicemanager hwservicemanager vndservicemanager lmkd netd ueventd app_process32; do
    cp "$ARMELF" "$d/system/bin/$b"
    # arm64 夹具：只改 ELF 头 e_machine(偏移 18, 小端) 为 0xB7，其余字节不动
    if [ "$arch" = arm64 ]; then
      printf '\xb7\x00' | dd of="$d/system/bin/$b" bs=1 seek=18 count=2 conv=notrunc status=none
    fi
  done
  {
    echo 'service hwservicemanager /system/bin/hwservicemanager'
    echo '    user root'
    echo '    group root'
    [ "$gate" = gated ] && echo '    disabled'
    echo '    oneshot'
  } > "$d/system/etc/init/hwservicemanager.rc"
  echo '<manifest version="1.0" type="framework"></manifest>' > "$d/system/etc/vintf/manifest.xml"
  echo '(genfscon proc "/selinux/$(security)")' > "$d/system/etc/selinux/plat_sepolicy.cil"
  [ "$gate" = gated ] && echo x > "$d/z1_stock_kernel"
}

echo "===== 1) 真实原厂 4.4 镜像（必须 REJECT，判据要落到 sdk 与 bins）====="
python3 "$SELF/z1_a9_system_accept.py" "$STOCK" --json "$WORK/stock.json" > "$WORK/stock.log" 2>&1
rc=$?
grep -E '^(FAIL|SYS_ACCEPT|CHECKS)' "$WORK/stock.log"
[ $rc -eq 3 ] && grep -q 'SYS_ACCEPT=REJECT' "$WORK/stock.log" \
  && echo "CASE1=PASS" || { echo "CASE1=FAIL(rc=$rc)"; cat "$WORK/stock.log"; exit 1; }

echo "===== 2) A9 arm32 干净形状（必须 ACCEPT）====="
stage_a9 "$WORK/s_clean" arm clean
mkimg "$WORK/s_clean" "$WORK/a9_clean.img"
python3 "$SELF/z1_a9_system_accept.py" "$WORK/a9_clean.img" --json "$WORK/clean.json" > "$WORK/clean.log" 2>&1
rc=$?
grep -E '^(FAIL|WARN|SYS_ACCEPT|CHECKS)' "$WORK/clean.log"
[ $rc -eq 0 ] && grep -q 'SYS_ACCEPT=ACCEPT' "$WORK/clean.log" \
  && echo "CASE2=PASS" || { echo "CASE2=FAIL(rc=$rc) —— 误拒真 A9 形状"; cat "$WORK/clean.log"; exit 1; }

echo "===== 3) A9 但仍是 stock 门控 + arm64 二进制（必须 REJECT，且逐条点名）====="
stage_a9 "$WORK/s_gated" arm64 gated
mkimg "$WORK/s_gated" "$WORK/a9_gated.img"
python3 "$SELF/z1_a9_system_accept.py" "$WORK/a9_gated.img" --json "$WORK/gated.json" > "$WORK/gated.log" 2>&1
rc=$?
grep -E '^(FAIL|SYS_ACCEPT|CHECKS)' "$WORK/gated.log"
[ $rc -eq 3 ] && grep -q 'SYS_ACCEPT=REJECT' "$WORK/gated.log" \
  && grep -q 'FAIL sdk\|FAIL abi_prop\|FAIL stock_marker\|FAIL hidl_rc\|FAIL elf_init' "$WORK/gated.log" \
  && echo "CASE3=PASS" || { echo "CASE3=FAIL(rc=$rc)"; cat "$WORK/gated.log"; exit 1; }

echo "===== 4) --expect-adapted 语义：没投 device 层的 A9 也必须拒 ====="
python3 "$SELF/z1_a9_system_accept.py" "$WORK/a9_clean.img" --expect-adapted > "$WORK/adapt.log" 2>&1
rc=$?
grep -E '^(FAIL dev_layer|SYS_ACCEPT)' "$WORK/adapt.log"
[ $rc -eq 3 ] && echo "CASE4=PASS（未投递 device 层时拒绝，符合 --expect-adapted 语义）" \
  || { echo "CASE4=FAIL"; cat "$WORK/adapt.log"; exit 1; }

echo "===== 5) 关键件丢可执行位（本轮真出过的错：/bin/init 0644）-> 必须 REJECT ====="
stage_a9 "$WORK/s_noexec" arm clean
mkimg "$WORK/s_noexec" "$WORK/a9_noexec.img"
chmod 644 "$WORK/s_noexec/system/bin/init" 2>/dev/null
# 直接在镜像里改权限，模拟 adapter 投错 mode
debugfs -w -R 'set_inode_field /system/bin/init mode 0644' "$WORK/a9_noexec.img" 2>/dev/null
python3 "$SELF/z1_a9_system_accept.py" "$WORK/a9_noexec.img" > "$WORK/noexec.log" 2>&1
rc=$?
grep -E 'FAIL exec_bit|SYS_ACCEPT' "$WORK/noexec.log"
[ $rc -eq 3 ] && grep -q 'FAIL exec_bit' "$WORK/noexec.log" \
  && echo "CASE5=PASS(丢可执行位被点名)" || { echo "CASE5=FAIL"; tail -5 "$WORK/noexec.log"; exit 1; }

echo "SELFTEST=ALL_PASS"
