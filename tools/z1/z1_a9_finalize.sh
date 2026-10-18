#!/usr/bin/env bash
# z1_a9_finalize.sh —— 把 A9 交付物的最后一段串成一次调用：
#   ramdisk 验收 -> (必要时重打成带 512B MTK 头的封装) -> 出 boot.img -> system 镜像级改造 -> 装配可刷包
#
# 用法:
#   tools/z1/z1_a9_finalize.sh --ramdisk <A9 generic ramdisk> \
#        [--system <A9 system.img>] [--variant 1b|4b] [--dir <输出目录>]
#
# 为什么要有这个脚本，而不是让四条命令各跑：这四步之间有**真实的先后依赖**，
# 跳步的代价是刷一次板子：
#   - ramdisk 必须先验收（形状不对/残留 stock 门控，出片也白出）；
#   - 裸 gzip 的 ramdisk 必须先重打成带头封装，否则 pack 会拒收，而 body 一变
#     linux,initrd-end 必须跟着重算（P2 起由 pack 现算，不能再手填）；
#   - system 若还是 stock 那套门控形态，必须先镜像级撤代偿，否则 hwservicemanager
#     仍是 disabled，A9 的 HIDL 服务起不来；
#   - 装配阶段才做容量/magic/回滚件预检。
# 任何一步非零退出就停，并打印停在哪一步 —— 不留"半条链跑完了"的模糊状态。
set -uo pipefail
SELF=$(cd "$(dirname "$0")" && pwd)
WT=$(readlink -f "$SELF/../..")
ROOT=$(readlink -f "$WT/..")
RD="" SYS="" VARIANT=1b DIR=""
while [ $# -gt 0 ]; do
  case "$1" in
    --ramdisk) RD=${2:?}; shift 2;;
    --system)  SYS=${2:?}; shift 2;;
    --variant) VARIANT=${2:?}; shift 2;;
    --dir)     DIR=${2:?}; shift 2;;
    *) echo "用法: $0 --ramdisk <rd> [--system <img>] [--variant 1b|4b] [--dir <目录>]"; exit 2;;
  esac
done
[ -n "$RD" ] || { echo "ERR: 必须给 --ramdisk"; exit 2; }
[ -f "$RD" ] || { echo "ERR: 找不到 $RD"; exit 2; }
DIR=${DIR:-$ROOT/out/A9_final_$(date +%Y%m%d_%H%M%S)}
mkdir -p "$DIR"
step() { echo; echo "########## $* ##########"; }
abort() { echo; echo ">>> 停在第 $1 步: $2"; exit 3; }

step "1/4 ramdisk 内容级验收"
python3 "$SELF/z1_a9_ramdisk_accept.py" "$RD" 2>&1 | tee "$DIR/step1_accept.log"
grep -q "RAMDISK_ACCEPT=ACCEPT" "$DIR/step1_accept.log" || abort 1 "ramdisk 验收未过，见 $DIR/step1_accept.log"

RD_FINAL=$RD
if grep -q "需先重打封装\|没有 512B MTK" "$DIR/step1_accept.log"; then
  step "1b/4 重打成带 512B MTK ROOTFS 头的封装"
  STAGE=$(mktemp -d /tmp/rd_a9fin.XXXXXX)
  zcat "$RD" | ( cd "$STAGE" && cpio -idm --quiet --no-absolute-filenames ) \
    || { rm -rf "$STAGE"; abort 1 "cpio 解包失败"; }
  bash "$ROOT/tools/z1_repack_a9_ramdisk.sh" "$STAGE" "$ROOT/out/ramdisk_v13_7_acm.gz" "$DIR/ramdisk_a9_final.gz" \
    | tee "$DIR/step1b_repack.log" || { rm -rf "$STAGE"; abort 1 "重打封装失败"; }
  rm -rf "$STAGE"
  RD_FINAL=$DIR/ramdisk_a9_final.gz
fi

step "2/4 出 boot.img（initrd-end 按实际 body 现算 + 6MB 预算闸门 + 载荷逐字节核验）"
IMG=$DIR/boot_mainline_a9_final_${VARIANT}.img
( cd "$WT" && A9_RAMDISK="$RD_FINAL" ./tools/z1/z1_a9_p2.sh pack "$VARIANT" "$IMG" ) \
  2>&1 | tee "$DIR/step2_pack.log"
grep -q "RESULT: VALID" "$DIR/step2_pack.log" || abort 2 "boot.img 未过 validate"
grep -q "VERIFY=OK"      "$DIR/step2_pack.log" || abort 2 "内嵌 DTB 复核未过"
grep -q "PAYLOAD-OK ramdisk" "$DIR/step2_pack.log" || abort 2 "镜像内 ramdisk 载荷不完整"
[ -f "$IMG" ] || abort 2 "boot.img 没落盘"

SYS_FINAL=""
if [ -n "$SYS" ]; then
  step "3/4 system 镜像级改造（撤 stock 门控 + 投递 mainline device 层）"
  python3 "$SELF/z1_a9_mainline_system.py" --system "$SYS" --out "$DIR/sysadapt" \
    2>&1 | tee "$DIR/step3_system.log" || abort 3 "system 改造失败"
  SYS_FINAL=$DIR/sysadapt/system_mainline.img
  [ -f "$SYS_FINAL" ] || abort 3 "改造后的 system 没落盘"
else
  step "3/4 跳过（未给 --system，只出 boot-only 包）"
fi

step "4/4 装配可刷包（scatter 只放行 bootimg${SYS_FINAL:+,android} + 容量/magic/回滚件预检）"
ARGS=(--boot "$IMG" --dir "$DIR/pkg")
[ -n "$SYS_FINAL" ] && ARGS+=(--system "$SYS_FINAL")
bash "$SELF/z1_a9_flashpkg.sh" "${ARGS[@]}" 2>&1 | tee "$DIR/step4_pkg.log" \
  || abort 4 "装配预检未过"
grep -q "PACKAGE=" "$DIR/step4_pkg.log" || abort 4 "装配没产出包"

echo
echo "================= 全链通过 ================="
echo "boot.img : $IMG"
[ -n "$SYS_FINAL" ] && echo "system   : $SYS_FINAL"
echo "刷机包   : $(grep -o 'PACKAGE=.*' "$DIR/step4_pkg.log" | tail -1 | cut -d= -f2)"
echo "逐步日志 : $DIR/step*.log"
sha256sum "$IMG" $( [ -n "$SYS_FINAL" ] && echo "$SYS_FINAL" ) 2>/dev/null | sed 's/^/  /'
echo "FINALIZE=OK"
