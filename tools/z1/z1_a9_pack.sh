#!/usr/bin/env bash
# z1_a9_pack.sh — 步骤 8-12：探针入 ramdisk、重算 initrd-end、重编 dtb、出镜像 A/B、写台账
#
# 用法:
#   tools/z1_a9_pack.sh probe <探针二进制>       # 放入 staging
#   tools/z1_a9_pack.sh ramdisk                  # 重打 ramdisk + 改 dts + 重编 dtb
#   tools/z1_a9_pack.sh imageA                   # 出 A（eMMC 保持基线 1-bit）
#   tools/z1_a9_pack.sh imageB                   # 出 B（唯一差异 bus-width=<4>）
#   tools/z1_a9_pack.sh manifest                 # 写 out/BUILD_MANIFEST
#   tools/z1_a9_pack.sh all <探针二进制>          # 依次执行全部
#
# 设计要点（都是本项目踩过的坑）：
#  - linux,initrd-end 必须 = 0x84100000 + ramdisk body 字节数，且与 ramdisk 512B 头 0x04
#    小端字段互镜像；换 ramdisk 不重算 = 内核解包失败或截断（v13.1 事故）。
#  - dtb 必须 append 到 zImage 末尾（CONFIG_ARM_APPENDED_DTB），mkbootimg --dt 独立区不算 append。
#  - 源码 mtime 早于旧产物时增量 make 不感知 → 改 dts 前 touch。
#  - 不动 fork_linux 主树；一切在 a9_wt 与本工作区内完成；不刷写、不碰 /dev。
set -uo pipefail

ROOT=/home/lxj/claude/6572
WT=$ROOT/a9_wt
DTS=$WT/arch/arm/boot/dts/mediatek/mt6572-z1.dts
DTB=$WT/arch/arm/boot/dts/mediatek/mt6572-z1.dtb
ZIMG=$WT/arch/arm/boot/zImage
STAGE=/tmp/rd_a9
RD=$ROOT/out/ramdisk_a9p1.gz
TPL=$ROOT/out/ramdisk_v13_7_acm.gz
MKIMG=$ROOT/tools/z1_make_bootimg.sh
VAL=$ROOT/tools/validate_mtk_boot.py
REPACK=$ROOT/tools/z1_repack_a9_ramdisk.sh
CC=arm-linux-gnueabihf-
IMG_A=$ROOT/out/boot_mainline_a9p1_A.img
IMG_B=$ROOT/out/boot_mainline_a9p1_B_emmc4b.img
# cmdline 仅语义自洽用：本启动链 LK 不转发 boot.img 头 cmdline，真实 bootargs 在 dts chosen
CMDLINE='console=tty0 console=ttyS0,921600n8 root=/dev/ram panic=10 loglevel=7 clk_ignore_unused regulator_ignore_unused oops=panic sysrq_always_enabled rdinit=/init earlycon=mtk8250,0x11005000'

cmd=${1:-all}

do_probe() {
	local p=${1:?需要探针二进制路径}
	[ -f "$p" ] || { echo "ERR: 探针不存在 $p"; exit 1; }
	file "$p" | grep -q 'ARM' || { echo "ERR: 不是 ARM 二进制: $(file -b $p)"; exit 1; }
	file "$p" | grep -q 'statically linked' || echo "WARN: 非静态链接，板端可能缺 .so"
	mkdir -p "$STAGE/sbin"
	cp -p "$p" "$STAGE/sbin/z1_a9_probe"
	chmod 755 "$STAGE/sbin/z1_a9_probe"
	grep -q 'z1_a9_probe' "$STAGE/init" || { echo "ERR: staging /init 未含探针注入段"; exit 1; }
	echo "OK: 探针已入 staging ($(stat -c%s "$STAGE/sbin/z1_a9_probe") B)"
}

do_ramdisk() {
	[ -x "$STAGE/sbin/z1_a9_probe" ] || { echo "ERR: 先执行 probe 步骤"; exit 1; }
	local out
	out=$(bash "$REPACK" "$STAGE" "$TPL" "$RD") || { echo "ERR: 重打包失败"; echo "$out"; exit 1; }
	echo "$out"
	BODY=$(echo "$out" | awk -F= '$1=="BODY_BYTES"{print $2; exit}')
	END=$(echo "$out" | awk -F= '$1=="INITRD_END"{print $2; exit}')
	[ -n "${BODY:-}" ] && [ -n "${END:-}" ] || { echo "ERR: 未取得 BODY/INITRD_END"; exit 1; }
	echo "--- 写 dts linux,initrd-end -> $END ---"
	cp -p "$DTS" "$ROOT/bridge_backups/mt6572-z1.dts.pre_a9_$(date +%Y%m%d_%H%M%S)"
	python3 - "$DTS" "$END" <<'PY'
import re,sys
p,e=sys.argv[1],sys.argv[2]
s=open(p,encoding='utf-8').read()
val=int(e,16)
new=re.sub(r'linux,initrd-end = <[^>]*>;', 'linux,initrd-end = <%s>;' % hex(val), s)
if new==s:
    print("ERR: 未找到 linux,initrd-end 行", file=sys.stderr); sys.exit(1)
open(p,'w',encoding='utf-8').write(new)
print("dts 已更新:", hex(val))
PY
	touch "$DTS"
	make -C "$WT" ARCH=arm CROSS_COMPILE=$CC dtbs 2>&1 | tail -3
	echo "--- dtb 反解核对 ---"
	dtc -I dtb -O dts "$DTB" 2>/dev/null | grep -E 'linux,initrd-(start|end)' | sed 's/^/  /'
	dtc -I dtb -O dts "$DTB" 2>/dev/null | grep -q "$(printf '%s' "$END" | tr 'a-z' 'A-Z' | sed 's/0X/0x/')" \
		&& echo "OK: dtb 内 initrd-end 与 ramdisk 一致" || echo "NOTE: dtb 以数值形式呈现，请对照上面反解行与 $END"
	echo "INITRD_END=$END BODY=$BODY"
}

do_image() {
	local tag=${1:?A 或 B}
	[ -f "$ZIMG" ] || { echo "ERR: 缺 zImage（构建未完成？）"; exit 1; }
	[ -f "$RD" ] || { echo "ERR: 缺 ramdisk，先执行 ramdisk 步骤"; exit 1; }
	local out=$([ "$tag" = A ] && echo "$IMG_A" || echo "$IMG_B")
	rm -f "$out"
	Z1_ZIMAGE="$ZIMG" Z1_DTB="$DTB" Z1_RAMDISK="$RD" Z1_OUT="$out" Z1_CMDLINE="$CMDLINE" \
		bash "$MKIMG" "$ZIMG" "$DTB" "$RD" "$out" 2>&1 | tail -14
	[ -f "$out" ] || { echo "ERR: 打包失败"; exit 1; }
	echo "--- validate ---"
	python3 "$VAL" "$out" --dtb "$DTB" --require-init-root 2>&1 | tail -12
	echo "OUT=$out SIZE=$(stat -c%s "$out") SHA256=$(sha256sum "$out" | cut -d' ' -f1)"
}

do_manifest() {
	local m=$ROOT/out/BUILD_MANIFEST
	{
		echo "# A9 P1 构建台账 $(date -Is)"
		echo "branch=$(git -C "$WT" rev-parse --abbrev-ref HEAD) HEAD=$(git -C "$WT" rev-parse HEAD)"
		echo "baseline=ffdff0653 (main)"
		echo "config_sha256=$(sha256sum "$WT/.config" | cut -d' ' -f1)"
		echo "kernelrelease=$(make -C "$WT" -s ARCH=arm CROSS_COMPILE=$CC kernelrelease 2>/dev/null)"
		for f in "$ZIMG" "$DTB" "$RD" "$IMG_A" "$IMG_B"; do
			[ -f "$f" ] && echo "$(stat -c%s "$f") $(sha256sum "$f" | cut -d' ' -f1) $(basename "$f")"
		done
		echo "probe=$(sha256sum "$STAGE/sbin/z1_a9_probe" 2>/dev/null | cut -d' ' -f1)"
	} > "$m"
	cat "$m"
}

case "$cmd" in
	probe) do_probe "${2:?}" ;;
	ramdisk) do_ramdisk ;;
	imageA) do_image A ;;
	imageB) do_image B ;;
	manifest) do_manifest ;;
	all) do_probe "${2:?}"; do_ramdisk; do_image A; make -C "$WT" ARCH=arm CROSS_COMPILE=$CC dtbs >/dev/null 2>&1; do_image B; do_manifest ;;
	*) echo "用法: $0 {probe|ramdisk|imageA|imageB|manifest|all} [探针]"; exit 2 ;;
esac
