#!/usr/bin/env bash
# z1_a9_check_seed.sh —— 检查那两个"缺就起不了安卓"的输入件（A9 arm32 的 init 与 ueventd）
#
# 只判三件事，都是板上会不会 SIGILL/起不来的硬条件：
#   1) ELF 机器码必须是 EM_ARM(0x28) —— 本树是 ARCH=arm 32 位内核；
#   2) init 里必须能找到 second_stage 这个 arg 串（A9 的 main() 用它选阶段，
#      拿到 8.x/10.x 的 init 会因为参数集不同而静默起不来）；
#   3) 两者的 PT_INTERP 必须是 /system/bin/linker（不是 linker64）。
set -uo pipefail
D=${1:-$(cd "$(dirname "$0")" && pwd)/../../android9/mainline/rootdir/bin}
D=$(readlink -f "$D")
fail=0
for n in init ueventd; do
  f="$D/$n"
  if [ ! -f "$f" ]; then echo "MISS $n  期望 $f"; fail=$((fail+1)); continue; fi
  em=$(od -An -tx1 -N2 -j18 "$f" | tr -d ' \n')
  sz=$(stat -c%s "$f")
  case "$em" in
    2800) echo "OK   $n  EM_ARM $sz B";;
    b700) echo "BAD  $n  EM_AARCH64（64 位，本内核跑不了）"; fail=$((fail+1));;
    *)    echo "BAD  $n  e_machine=0x$em 认不出"; fail=$((fail+1));;
  esac
  interp=$(readelf -l "$f" 2>/dev/null | tr -s ' ' | sed -n 's/.*\[Requesting program interpreter: \(.*\)\]/\1/p' | tr -d '[]')
  [ -n "$interp" ] && echo "     interp=$interp" || echo "     (静态/无 PT_INTERP)"
  case "${interp:-}" in *linker64*) echo "BAD  $n 用了 linker64"; fail=$((fail+1));; esac
  if [ "$n" = init ]; then
    if grep -qa second_stage "$f"; then echo "     含 second_stage 参数串"; else
      echo "BAD  init 里没有 second_stage —— 不是 A9/A10 那一族的 init"; fail=$((fail+1)); fi
  fi
done
echo "SEED_FAIL=$fail"
[ $fail -eq 0 ] || echo ">>> 补齐后重跑: python3 tools/z1/z1_a9_mainline_system.py --system <raw> --out out/a9sys_gsi"
[ $fail -eq 0 ]
