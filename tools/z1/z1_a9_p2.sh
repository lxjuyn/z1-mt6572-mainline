#!/usr/bin/env bash
# z1_a9_p2.sh — A9 P2 出片：瘦身内核 + 可复现的 DT 变体（1-bit / 4-bit eMMC）+ 预算闸门
#
# 用法:
#   tools/z1/z1_a9_p2.sh pack <1b|4b> <输出镜像名>
#   tools/z1/z1_a9_p2.sh budget                 # 只报当前 kernel/dtb/ramdisk 的 boot 分区预算
#
# 为什么另起一个脚本而不是改 z1_a9_pack.sh：
#  - P1 的 A/B/C 是"手工逐步执行"产物，事后发现 C 其实等于 B+androidboot（4-bit 被顺带带进
#    了"C=A+androidboot"的差分件里），单变量差分被破坏。P2 把"变体"变成显式参数，
#    并在打包**之后**反解镜像内嵌 DTB 复核，杜绝"以为改了其实没改"（v13.1 截断事故同型）。
#  - boot 分区 6MB 是硬上限，A9 ramdisk 还没到位，先前的余量测算靠人肉减法。这里改成闸门。
set -uo pipefail

# 路径由脚本自身位置推导，避免把本机用户名写进公开仓库
SELF=$(cd "$(dirname "$0")" && pwd)
WT=$(readlink -f "$SELF/../..")
ROOT=$(readlink -f "$WT/..")
DTS=$WT/arch/arm/boot/dts/mediatek/mt6572-z1.dts
DTB=$WT/arch/arm/boot/dts/mediatek/mt6572-z1.dtb
ZIMG=$WT/arch/arm/boot/zImage
RD=${A9_RAMDISK:-$ROOT/out/ramdisk_a9p1.gz}
MKIMG=$ROOT/tools/z1_make_bootimg.sh
VAL=$ROOT/tools/validate_mtk_boot.py
CC=arm-linux-gnueabihf-
BOOT_LIMIT=6291456
RD_BASE=0x84100000

# 探针 ramdisk 的已知指纹；换用 A9 generic ramdisk 时由 recompute_initrd_end 现算，
# 这组常量只作为"是否仍是那颗探针 ramdisk"的交叉校验，不再是唯一真相。
EXPECT_BODY=1276023
EXPECT_END=0x84237877
CUR_END=$EXPECT_END

die() { echo "ERR: $*" >&2; exit 1; }

rd_body() { # body = 整文件大小 - 512B MTK ROOTFS 头；initrd-end 规则只算 body
	local n
	n=$(stat -c%s "$RD") || return 1
	[ "$n" -gt 512 ] || { echo "ramdisk 小于 512B，不可能带 MTK 头" >&2; return 1; }
	# 头必须是 88 16 88 58 magic + 0x08 处 ROOTFS 标签，否则这颗 ramdisk 不是本链格式
	local mg lb
	mg=$(dd if="$RD" bs=4 count=1 2>/dev/null | od -An -tx1 | tr -d ' \n')
	lb=$(dd if="$RD" bs=1 count=6 skip=8 2>/dev/null | tr -d '\0')
	[ "$mg" = "88168858" ] || { echo "头 magic=$mg 非 88168858" >&2; return 1; }
	[ "$lb" = "ROOTFS" ] || { echo "头 0x08 标签=$lb 非 ROOTFS" >&2; return 1; }
	echo $(( n - 512 ))
}

recompute_initrd_end() { # 按实际 ramdisk 现算 initrd-end，并（必要时）写回 dts + 重编 dtb
	local body end hdrf
	body=$(rd_body) || die "读不到 ramdisk $RD"
	end=$(printf '0x%x' $(( RD_BASE + body )))
	hdrf=$(dd if="$RD" bs=1 count=4 skip=4 2>/dev/null | od -An -tu4 | tr -d ' ')
	[ "$hdrf" = "$body" ] || die "ramdisk 512B 头 0x04 长度字段=$hdrf 与实际 body=$body 不一致（这颗 ramdisk 不可用）"
	echo "ramdisk=$(basename "$RD") body=$body 头字段=$hdrf -> initrd-end=$end"
	if [ "$body" = "$EXPECT_BODY" ]; then
		echo "NOTE: 仍是 P1 探针 ramdisk（期望 $EXPECT_END）"
	fi
	CUR_END=$end
	if ! grep -q "linux,initrd-end = <${end}>;" "$DTS"; then
		echo "--- dts initrd-end 与 ramdisk 不符，改写为 $end ---"
		cp -p "$DTS" "$ROOT/bridge_backups/mt6572-z1.dts.pre_p2_$(date +%Y%m%d_%H%M%S)"
		python3 - "$DTS" "$end" <<'PY'
import re, sys
p, e = sys.argv[1], sys.argv[2]
s = open(p, encoding='utf-8').read()
new, n = re.subn(r'linux,initrd-end = <[^>]*>;', 'linux,initrd-end = <%s>;' % e, s)
if n != 1:
    print('ERR: linux,initrd-end 行命中 %d 次（应为 1）' % n, file=sys.stderr); sys.exit(1)
open(p, 'w', encoding='utf-8').write(new)
print('dts 已改写 initrd-end ->', e)
PY
		[ $? -eq 0 ] || die "改写 dts initrd-end 失败"
		cd "$WT" && make ARCH=arm CROSS_COMPILE=$CC dtbs >/dev/null 2>&1 || die "dtbs 重编失败"
		grep -q "linux,initrd-end = <${end}>;" "$DTS" || die "改写后复核失败"
	fi
}

check_initrd_end() {
	recompute_initrd_end
	local ddtb
	ddtb=$(dtc -I dtb -O dts "$DTB" 2>/dev/null | grep -m1 -oE 'linux,initrd-end = <0x[0-9a-fA-F]+>')
	echo "dtb 反解: ${ddtb:-未取到}  (期望 $CUR_END)"
	echo "OK: ramdisk 头字段 / body / dts / dtb 四者互证"
}

set_variant() { # $1 = 1b|4b  —— 幂等地把 &mmc0 块写成该变体（只在块内改，避免误伤 mmc1/mmc2）
	local v=$1
	[ "$v" = 1b ] || [ "$v" = 4b ] || die "变体只接受 1b 或 4b"
	python3 - "$DTS" "$v" <<'PY'
import re, sys
p, want = sys.argv[1], sys.argv[2]
s = open(p, encoding='utf-8').read()
# (1) bus-width 只在 &mmc0 块内改，避免误伤 mmc1/mmc2
i = s.index('&mmc0 {')
j = s.index('\n};', i) + 3
blk = s[i:j]
nb, nw = re.subn(r'bus-width = <\d+>;', 'bus-width = <%d>;' % (1 if want == '1b' else 4), blk)
if nw != 1:
    print('ERR: &mmc0 块里 bus-width 命中 %d 次（应为 1）' % nw, file=sys.stderr); sys.exit(1)
s2 = s[:i] + nb + s[j:]
# (2) DAT1-3 pinmux 在 &pio 下的 mmc0_pins: 组里，不在 &mmc0 块内（P1 的坑），故全文匹配
#     这组 MT6572_PIN_4x_MC0_DAT 只可能出现在 eMMC 引脚组，先断言唯一再改
four = (r'<MT6572_PIN_46_MC0_DAT0__FUNC_MC0_DAT0>\s*,\s*'
        r'<MT6572_PIN_47_MC0_DAT1__FUNC_MC0_DAT1>\s*,\s*'
        r'<MT6572_PIN_48_MC0_DAT2__FUNC_MC0_DAT2>\s*,\s*'
        r'<MT6572_PIN_49_MC0_DAT3__FUNC_MC0_DAT3>;')
n4 = len(re.findall(four, s2))
ndat = len(re.findall(r'MT6572_PIN_4[789]_MC0_DAT', s2))
if want == '1b':
    if ndat == 0 and n4 == 0:
        pass  # 已经是 1-bit，幂等跳过
    elif n4 != 1:
        print('ERR: 4-bit DAT 序列命中 %d 次（应为 1），DAT1-3 引用=%d' % (n4, ndat), file=sys.stderr); sys.exit(1)
    else:
        s2 = re.sub(four, '<MT6572_PIN_46_MC0_DAT0__FUNC_MC0_DAT0>;', s2)
elif ndat == 0:
    m = re.search(r'(\n[ \t]*)<MT6572_PIN_46_MC0_DAT0__FUNC_MC0_DAT0>;', s2)
    if not m:
        print('ERR: 找不到 DAT0 pinmux 行', file=sys.stderr); sys.exit(1)
    ind = m.group(1)
    repl = ('%s<MT6572_PIN_46_MC0_DAT0__FUNC_MC0_DAT0>,' + '%s<MT6572_PIN_47_MC0_DAT1__FUNC_MC0_DAT1>,'
            + '%s<MT6572_PIN_48_MC0_DAT2__FUNC_MC0_DAT2>,' + '%s<MT6572_PIN_49_MC0_DAT3__FUNC_MC0_DAT3>;') % (ind, ind, ind, ind)
    s2 = s2[:m.start()] + repl + s2[m.end():]
open(p, 'w', encoding='utf-8').write(s2)
print('mmc0 已设为 %s' % want)
PY
	[ $? -eq 0 ] || die "改写 mmc0 块失败"
	local bw dat
	bw=$(awk '/&mmc0 \{/{f=1} f&&/bus-width/{print;exit}' "$DTS")
	dat=$(grep -c 'MT6572_PIN_49_MC0_DAT3' "$DTS")
	echo "dts 现状: ${bw// /}  DAT1-3 pinmux 命中=$dat"
	case "$v" in
		1b) { echo "$bw" | grep -q '<1>'; [ "$dat" = 0 ]; } || die "切到 1b 失败" ;;
		4b) { echo "$bw" | grep -q '<4>'; [ "$dat" != 0 ]; } || die "切到 4b 失败" ;;
	esac
	grep -q 'androidboot.hardware=z1' "$DTS" || die "dts bootargs 缺 androidboot.hardware=z1"
	grep -q 'androidboot.selinux=permissive' "$DTS" || die "dts bootargs 缺 androidboot.selinux=permissive"
}

verify_embedded() { # 打完从镜像里反解 DTB 复核变体，别只信源文件
	local img=$1 want=$2 end=$3
	python3 - "$img" "$want" "$end" <<'PY'
import re,subprocess,sys
img,want,end=sys.argv[1],sys.argv[2],sys.argv[3].lower().replace('0x','')
d=open(img,'rb').read()
cands=[]
for m in re.finditer(bytes.fromhex('d00dfeed'), d):
    off=m.start(); size=int.from_bytes(d[off+4:off+8],'big')
    if 15000<size<40000 and off+size<=len(d): cands.append((off,size))
if not cands: print("VERIFY=FAIL 未在镜像中找到内嵌 DTB"); sys.exit(1)
off,size=cands[-1]
open('/tmp/_p2.dtb','wb').write(d[off:off+size])
s=subprocess.run(['dtc','-I','dtb','-O','dts','/tmp/_p2.dtb'],capture_output=True,text=True).stdout
bw=re.findall(r'bus-width = <0x([0-9a-fA-F]+)>', s)
ab=re.findall(r'androidboot\.[a-z]+=', s)
ie=re.findall(r'linux,initrd-end = <0x([0-9a-fA-F]+)>', s)
exp = 4 if want == '4b' else 1
# dtc 以十六进制打印（<0x01>），必须按数值比，不能按字符串比
ok = (bw and int(bw[-1], 16) == exp) and ('hardware' in ' '.join(ab)) and (ie and ie[-1].lower() == end)
print(f"VERIFY={'OK' if ok else 'FAIL'} 变体期望={want} initrd-end期望=0x{end} 内嵌dtb: bus-width={bw} initrd-end={ie} androidboot={ab}")
sys.exit(0 if ok else 1)
PY
}

budget() {
	local k d r total
	k=$(stat -c%s "$ZIMG") || die "缺 zImage（先编译）"
	d=$(stat -c%s "$DTB") || die "缺 dtb"
	r=$(stat -c%s "$RD")
	total=$(( k + d + r + 4096 ))
	cat <<EOF
===== boot 分区预算 (上限 $BOOT_LIMIT) =====
zImage            = $k
内嵌 dtb          = $d
ramdisk(带512B头) = $r
头+对齐(估)       = 4096
合计              = $total
余量              = $(( BOOT_LIMIT - total ))  (即 A9 ramdisk 最大可用约 $(( BOOT_LIMIT - k - d - 4096 )) B)
EOF
}

cmd=${1:-budget}
case "$cmd" in
  pack)
	v=${2:?需要变体 1b|4b}; out=${3:?需要输出镜像路径}
	[ -f "$ZIMG" ] || die "缺 zImage"
	cd "$WT" || exit 1
	check_initrd_end
	set_variant "$v"
	touch "$DTS"
	make ARCH=arm CROSS_COMPILE=$CC dtbs >/dev/null 2>&1 || die "dtbs 编译失败"
	budget
	rm -f "$out"
	Z1_ZIMAGE="$ZIMG" Z1_DTB="$DTB" Z1_RAMDISK="$RD" Z1_OUT="$out" \
		Z1_CMDLINE='console=tty0 console=ttyS0,921600n8 root=/dev/ram panic=10 loglevel=7 clk_ignore_unused regulator_ignore_unused oops=panic sysrq_always_enabled rdinit=/init earlycon=mtk8250,0x11005000 androidboot.hardware=z1 androidboot.selinux=permissive' \
		bash "$MKIMG" "$ZIMG" "$DTB" "$RD" "$out" 2>&1 | tail -8
	[ -f "$out" ] || die "打包失败"
	sz=$(stat -c%s "$out")
	[ "$sz" -le "$BOOT_LIMIT" ] || die "镜像 $sz B 超过 boot 分区上限 $BOOT_LIMIT"
	python3 "$VAL" "$out" --dtb "$DTB" --require-init-root 2>&1 | tail -6
	verify_embedded "$out" "$v" "$CUR_END" || die "内嵌 DTB 复核未通过，镜像不可交付"
	echo "OUT=$out SIZE=$sz HEADROOM=$(( BOOT_LIMIT - sz )) SHA256=$(sha256sum "$out" | cut -d' ' -f1)"
	;;
  budget) budget ;;
  check) check_initrd_end ;;
  *) echo "用法: $0 pack <1b|4b> <镜像输出路径> | budget | check"; exit 2 ;;
esac
