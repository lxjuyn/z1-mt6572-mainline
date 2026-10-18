#!/usr/bin/env bash
# z1_a9_mkramdisk.sh —— 可复现地重打"A9 bring-up ramdisk"（探针 + 交棒脚本 + /init）
#
# 为什么要脚本化：P1..P8 的 ramdisk 是手工攒出来的，出片档案里只留下 .gz，
# 无法证明"这张镜像里的 /init 就是仓里这份"。交棒上线后 /init 的行为直接决定板上能不能
# 进 A9，所以这条装配线必须能重跑、能自检。
#
# 覆盖变量：
#   Z1_RD_BASE  基底 ramdisk（默认 out/ramdisk_a9p1.gz，即已上板验过能起 shell 的那份）
#   Z1_RD_INIT  /init 源（默认 tools/z1/init_v13_9_a9handoff.src）
#   Z1_RD_TPL   取 512B MTK ROOTFS 头的模板（默认 out/ramdisk_v13_7_acm.gz）
#   Z1_RD_OUT   输出（默认 out/ramdisk_a9p9.gz）
set -uo pipefail
SELF=$(cd "$(dirname "$0")" && pwd)
WT=$(readlink -f "$SELF/../..")
ROOT=$(readlink -f "$WT/..")
BASE=${Z1_RD_BASE:-$ROOT/out/ramdisk_a9p1.gz}
INITSRC=${Z1_RD_INIT:-$SELF/init_v13_9_a9handoff.src}
TPL=${Z1_RD_TPL:-$ROOT/out/ramdisk_v13_7_acm.gz}
OUT=${Z1_RD_OUT:-$ROOT/out/ramdisk_a9p9.gz}
BB=/bin/busybox
[ -f "$BASE" ] || { echo "ERR: 基底 ramdisk 不存在 $BASE"; exit 1; }
[ -f "$INITSRC" ] || { echo "ERR: /init 源不存在 $INITSRC"; exit 1; }
[ -f "$TPL" ] || { echo "ERR: 头模板不存在 $TPL"; exit 1; }
[ -x "$BB" ] || { echo "ERR: 本机没有 busybox $BB（自检要用）"; exit 1; }

# 装配前静态门：语法不过就别浪费时间打包
$BB sh -n "$INITSRC"     || { echo "SELFCHK=init_syntax_FAIL"; exit 1; }
$BB sh -n "$SELF/z1_a9_handoff.sh" || { echo "SELFCHK=handoff_syntax_FAIL"; exit 1; }
echo "SELFCHK=init_syntax_OK handoff_syntax_OK"

STAGE=$(mktemp -d /tmp/a9rd.XXXXXX)
trap 'rm -rf "$STAGE"' EXIT
dd if="$BASE" bs=512 skip=1 2>/dev/null | zcat 2>/dev/null | \
  ( cd "$STAGE" && cpio -idm --quiet --no-absolute-filenames ) 2>/dev/null \
  || { echo "ERR: 基底 ramdisk 解包失败"; exit 1; }
[ -s "$STAGE/init" ] || { echo "ERR: 基底里没有 /init"; exit 1; }
mkdir -p "$STAGE/sbin"
install -m 0755 "$INITSRC" "$STAGE/init"
install -m 0755 "$SELF/z1_a9_handoff.sh" "$STAGE/sbin/z1_a9_handoff.sh"
# 探针优先用仓里的二进制；不在就现场交叉编译（本机有 arm-linux-gnueabihf-gcc）
if [ -f "$SELF/z1_a9_probe" ]; then
  install -m 0755 "$SELF/z1_a9_probe" "$STAGE/sbin/z1_a9_probe"
elif [ -x "$SELF/z1_a9_probe_build.sh" ]; then
  bash "$SELF/z1_a9_probe_build.sh" >/dev/null || { echo "ERR: 探针编译失败"; exit 1; }
  install -m 0755 "$SELF/z1_a9_probe" "$STAGE/sbin/z1_a9_probe"
else
  echo "ERR: 既无 $SELF/z1_a9_probe 也无编译脚本"; exit 1
fi

bash "$ROOT/tools/z1_repack_a9_ramdisk.sh" "$STAGE" "$TPL" "$OUT" || { echo "ERR: 重打失败"; exit 1; }

echo "===== 打包后回读自检（从成品 .gz 里解出来对账，不看 staging）====="
CHK=$(mktemp -d /tmp/a9rdchk.XXXXXX)
dd if="$OUT" bs=512 skip=1 2>/dev/null | zcat 2>/dev/null | \
  ( cd "$CHK" && cpio -idm --quiet --no-absolute-filenames ) 2>/dev/null
bad=0
grep -q 'a9hand' "$CHK/init"                  || { echo "CHK-FAIL /init 里没有 [a9hand] 段"; bad=$((bad+1)); }
grep -q 'RESULT_STEP_handoff_probe=GO' "$CHK/sbin/z1_a9_handoff.sh" \
                                             || { echo "CHK-FAIL /sbin/z1_a9_handoff.sh 不是判定版"; bad=$((bad+1)); }
[ -x "$CHK/sbin/z1_a9_handoff.sh" ]           || { echo "CHK-FAIL 交棒脚本丢了可执行位"; bad=$((bad+1)); }
[ -x "$CHK/sbin/z1_a9_probe" ]                || { echo "CHK-FAIL 探针缺失或不可执行"; bad=$((bad+1)); }
diff -q "$CHK/init" "$INITSRC" >/dev/null     || { echo "CHK-FAIL /init 与仓里源不一致"; bad=$((bad+1)); }
$BB sh -n "$CHK/init"                         || { echo "CHK-FAIL 成品 /init 语法不过"; bad=$((bad+1)); }
rm -rf "$CHK"
echo "CHK_FAIL=$bad"
[ $bad -eq 0 ] || { echo ">>> ramdisk 自检未过，$OUT 不可用于出片"; exit 3; }
echo "RAMDISK=$OUT sha256=$(sha256sum "$OUT" | cut -d' ' -f1)"
echo "RD_BUILD=OK"
