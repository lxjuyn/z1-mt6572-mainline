#!/usr/bin/env bash
# z1_a9_flashpkg.sh —— 装配"可刷写的 A9 刷机包"（SP Flash Tool scatter + mtkclient 两条路都给）
#
# 用法:
#   tools/z1/z1_a9_flashpkg.sh --boot <boot.img> [--system <system.img>] [--dir <输出目录>]
#
# 为什么需要它（不是多余的封装）：
#  1) 原厂 1/MT6572_Android_scatter.txt 里 preloader / mbr / ebr1 / uboot(LK) / recovery /
#     sec_ro / logo / **usrdata** 的 is_download 都是 true。直接点 Download 会把引导链和
#     用户数据一起刷掉 —— 而 Z1 的 LK 是这台机器唯一的原厂引导，丢了它就没法回滚。
#  2) 分区容量是硬约束：bootimg=0x600000(6 MiB)、android=0x38400000(900 MiB)、
#     cache=0xE200000、usrdata=0x9DDC0000。镜像超容量时 SP Flash Tool 的报错很难懂，
#     这里提前判死。
#  3) 刷机包要自带指纹清单，避免"刷的是哪张"事后说不清。
set -uo pipefail
SELF=$(cd "$(dirname "$0")" && pwd)
WT=$(readlink -f "$SELF/../..")
ROOT=$(readlink -f "$WT/..")
SCATTER_SRC=${SCATTER_SRC:-$ROOT/1/MT6572_Android_scatter.txt}
ROLLBACK=${ROLLBACK_IMG:-$ROOT/1/boot.img}
BOOT="" SYS="" OUTDIR=""

while [ $# -gt 0 ]; do
  case "$1" in
    --boot) BOOT=${2:?}; shift 2;;
    --system) SYS=${2:?}; shift 2;;
    --dir) OUTDIR=${2:?}; shift 2;;
    *) echo "用法: $0 --boot <boot.img> [--system <system.img>] [--dir <目录>]"; exit 2;;
  esac
done
[ -n "$BOOT" ] || { echo "ERR: 必须给 --boot"; exit 2; }
[ -f "$BOOT" ] || { echo "ERR: 找不到 $BOOT"; exit 2; }
[ -f "$SCATTER_SRC" ] || { echo "ERR: 找不到原厂 scatter $SCATTER_SRC"; exit 2; }
OUTDIR=${OUTDIR:-$ROOT/out/A9_flashpkg_$(date +%Y%m%d_%H%M%S)}
mkdir -p "$OUTDIR" || exit 1

LIMIT_BOOT=$((0x600000)); LIMIT_SYS=$((0x38400000))
fail=0
die() { echo "PREFLIGHT-FAIL: $*" >&2; fail=$((fail+1)); }

echo "===== 预检 ====="
bs=$(stat -c%s "$BOOT")
echo "boot.img        $bs B / bootimg 分区 $LIMIT_BOOT B -> 余 $((LIMIT_BOOT-bs)) B"
[ "$bs" -le "$LIMIT_BOOT" ] || die "boot.img 超 bootimg 分区容量 $((bs-LIMIT_BOOT)) B（刷机时会被截断或拒绝）"
head -c 8 "$BOOT" | grep -q 'ANDROID!' || die "boot.img 头不是 ANDROID! magic"

SYS_NOTE=NONE
if [ -n "$SYS" ]; then
  [ -f "$SYS" ] || die "找不到 system: $SYS"
  ss=$(stat -c%s "$SYS")
  echo "system.img      $ss B / android 分区 $LIMIT_SYS B -> 余 $((LIMIT_SYS-ss)) B"
  [ "$ss" -le "$LIMIT_SYS" ] || die "system.img 超 android 分区容量 $((ss-LIMIT_SYS)) B"
  # 认两种合法形态：Android sparse(0x3AFF26AF) 与 ext4(superblock magic 0xEF53 @1024)
  python3 - "$SYS" <<'PY' || die "system.img 既不是 sparse 也不是 ext4"
import struct,sys
f=open(sys.argv[1],'rb'); m=f.read(4)
f.seek(1024+56); sb=f.read(2)
sparse = struct.unpack('<I', m)[0] == 0x3AFF26AF
ext4   = sb == b'\x53\xef'
print('system.img 形态: sparse=%s ext4=%s' % (sparse, ext4))
sys.exit(0 if (sparse or ext4) else 1)
PY
  cp "$SYS" "$OUTDIR/system.img"; SYS_NOTE=android
else
  echo "system.img      未提供 —— 本次只出 boot-only 包（刷 bootimg 一张）"
fi

[ -f "$ROLLBACK" ] && echo "回滚件         $ROLLBACK 在位" || die "回滚件 $ROLLBACK 不在位（刷失败就没法退回原厂）"

cp "$BOOT" "$OUTDIR/boot.img"
[ -n "$SYS" ] && cp "$SYS" "$OUTDIR/system.img"
echo "===== 生成 scatter（只放行 bootimg${SYS:+ 与 android}）====="
SC_ARGS=(--allow bootimg)
[ -n "$SYS" ] && SC_ARGS+=(--system)
python3 "$SELF/z1_a9_scatter.py" "$SCATTER_SRC" "$OUTDIR/MT6572_Z1_A9_scatter.txt" "${SC_ARGS[@]}"
[ $? -eq 0 ] || die "scatter 生成失败/结构被破坏，拒绝交付"

echo "===== 指纹清单 ====="
{
  echo "# A9 刷机包 $(date -Is)"
  echo "scatter_src=$(basename "$SCATTER_SRC") branch=$(git -C "$WT" rev-parse --abbrev-ref HEAD) HEAD=$(git -C "$WT" rev-parse HEAD)"
  echo "rollback=$(sha256sum "$ROLLBACK" | cut -d' ' -f1) <工作区根>/1/boot.img   # 不写绝对路径: 这份清单常被直接发出去"
  ( cd "$OUTDIR" && sha256sum boot.img MT6572_Z1_A9_scatter.txt $( [ -f system.img ] && echo system.img ) )
} | tee "$OUTDIR/HASHES.txt"

if [ "$fail" -ne 0 ]; then
  echo ">>> 预检未通过($fail 项)，包不可刷"; exit 3
fi

echo "===== 刷机步骤（你来执行；刷写才插下载线，正常开机拔掉）====="
cat <<EOF
【路线 A：SP Flash Tool（推荐一次刷 boot+system）】
  1. Scatter-loading 选 $OUTDIR/MT6572_Z1_A9_scatter.txt
  2. Download-Mode 必须选 **Download Only**（绝不是 Format All + Download —— 那会清 userdata/NVRAM）
  3. 核对右边表格：只有 bootimg$([ -n "$SYS" ] && echo " 与 android") 这几行是勾选状态，其余分区文件列应为 NONE
  4. 点 Download，然后按 Z1 开机键（先软件后按键，顺序反了连不上）

【路线 B：mtkclient 逐分区（可重试 + 失败自动回滚原厂）】
  cd $ROOT
  tools/flash_with_retry.sh bootimg $OUTDIR/boot.img <工作区根>/1/boot.img 8   # 第3参=失败自动回滚原厂件
$( [ -n "$SYS" ] && echo "  tools/flash_with_retry.sh android $OUTDIR/system.img '' 8   # android=p4 分区" )

刷完采集串口：Z1_CAPTURE_BAUD=921600 ./z1_capture_mainline.sh 600   # 或经 C3 桥 ./z1_capture_mainline.sh 180
读 RESULT_STEP_* / [a9cfg] / [a9dev] 三段；异常时先刷回 <工作区根>/1/boot.img（原厂零修改件）。
EOF
echo "PACKAGE=$OUTDIR"
