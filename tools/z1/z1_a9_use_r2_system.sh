#!/usr/bin/env bash
# z1_a9_use_r2_system.sh —— 把自家私有仓 z1-android9 的 Linux7-20261006-r2 system 换成正式 payload
#
# 为什么单独成脚本：这份 system 与我已经取到的 init/sepolicy **同源**（都是那条 release 的产物），
# 比 GSI + 外来投件更该做成品；但它有 943,718,400 B，任何一步手敲错都浪费一次上板。
# 所以一条命令走完：sha 校验 -> 验收 -> 撤 stock 门控 + 投我们这份 device 层 -> 装包 -> 独立复核。
set -uo pipefail
SELF=$(cd "$(dirname "$0")" && pwd)
WT=$(readlink -f "$SELF/../..")
ROOT=$(readlink -f "$WT/..")
IMG=${1:-$ROOT/out/z1a9_r2/system.raw.img}
BOOT=${BOOT:-$ROOT/out/boot_mainline_a9p9b_D1_1b.img}
DIR=${DIR:-$ROOT/out/A9_flashpkg_p12_r2}
WANT_SHA=b6daba2417dedc339cedeccf5b2eddd929dab892f170b4c2807cb588861a22e6
TOTAL=943718400
[ -f "$IMG" ] || { echo "ERR: 还没有 $IMG（下载未完成？）"; exit 1; }
sz=$(stat -c%s "$IMG")
[ "$sz" = "$TOTAL" ] || { echo "FAIL: 尺寸 $sz != $TOTAL，件没下全"; exit 3; }
got=$(sha256sum "$IMG" | cut -d' ' -f1)
if [ "$got" != "$WANT_SHA" ]; then
  echo "FAIL: sha256=$got 与 release 的 SHA256SUMS 不符 —— 绝不用这份件出片"
  exit 3
fi
echo "SHA256=OK $got"
[ -f "$BOOT" ] || { echo "ERR: 找不到 boot $BOOT"; exit 1; }

step() { echo; echo "########## $* ##########"; }
step "1/3 内容级验收（未改造）"
python3 "$SELF/z1_a9_system_accept.py" "$IMG" --json "$DIR.accept.json" 2>&1 | tail -6
step "2/3 撤 stock 门控 + 投本仓 device 层（含 init/ueventd 投件，若这份 system 自己带就跳过）"
rm -rf "$ROOT/out/a9sys_r2"
python3 "$SELF/z1_a9_mainline_system.py" --system "$IMG" --out "$ROOT/out/a9sys_r2" 2>&1 | tail -5
SYS="$ROOT/out/a9sys_r2/system_mainline.img"
[ -f "$SYS" ] || { echo "FAIL: 改造产物没落盘"; exit 3; }
step "3/3 装包 + 独立复核"
bash "$SELF/z1_a9_flashpkg.sh" --boot "$BOOT" --system "$SYS" --dir "$DIR" 2>&1 | grep -E 'SYS_ACCEPT|system 验收|放行|PACKAGE='
python3 - "$DIR" "$IMG" <<'PY'
import hashlib, subprocess, re, sys, os
d = sys.argv[1]
def sh(p): return hashlib.sha256(open(p,'rb').read()).hexdigest()
for n, src in (('boot.img', os.environ.get('BOOT','')), ('system.img', os.path.join(d,'system.img'))):
    p = os.path.join(d, n)
    if os.path.exists(p): print('  %-10s %d B sha=%s' % (n, os.path.getsize(p), sh(p)[:16]))
img = os.path.join(d, 'system.img')
out = subprocess.run(['debugfs','-R','ls -d /bin',img],capture_output=True,text=True).stdout
names = [m[1] for m in re.findall(r'\((\d+)\)\s+(\S+)', out)]
need = ['init','ueventd','servicemanager','hwservicemanager','app_process32','surfaceflinger','lmkd','netd','sh']
print('  /bin 关键件:', sorted(set(names) & set(need)))
print('  包:', d, '文件:', sorted(os.listdir(d)))
PY
echo "USE_R2=OK 包在 $DIR"
