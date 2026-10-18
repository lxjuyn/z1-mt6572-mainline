#!/usr/bin/env bash
# z1_a9_sparse2raw_selftest.sh —— 真布局回归：洞位偏移 + 28B/12B 头 + CRC32
#
# 为什么必须有：本轮两个真错都出在"sparse 分支只在夹具上跑过、而夹具根本不是 sparse"。
# 这里自己造一份带 DONT_CARE 洞与 FILL 的 sparse 镜像（内含可识别的 ext4 标记文件），
# 转换后用 debugfs 从输出 ext4 里把标记读回来 —— 洞没推进偏移的话，这步必然失败。
# 若本机留有真 GSI（out/gsi_v123/system.img），额外再跑一次真实件校验。
set -uo pipefail
SELF=$(cd "$(dirname "$0")" && pwd)
ROOT=$(readlink -f "$SELF/../../..")
W=$(mktemp -d /tmp/a9sparse.XXXXXX)
trap 'rm -rf "$W"' EXIT
B=/bin/busybox

python3 - "$W" <<'PY' || { echo "SELFTEST=build_FAIL"; exit 1; }
import os, struct, subprocess, sys, zlib
w = sys.argv[1]
st = os.path.join(w, 'st')
os.makedirs(os.path.join(st, 'marker'))
open(os.path.join(st, 'marker', 'hello.txt'), 'w').write('SPARSE-HOLE-OFFSET-MARKER\n')
subprocess.run(['mke2fs', '-q', '-F', '-t', 'ext4', '-b', '4096',
                '-O', '^metadata_csum,^64bit', '-d', st,
                os.path.join(w, 'fs.img'), '16384'], check=True,
               capture_output=True)
raw = open(os.path.join(w, 'fs.img'), 'rb').read()
BLK = 4096
# sparse 是**交错格式**：块头紧跟该块数据，再来下一个块头。分两段写就等于造了个假镜像
# （第一版就这样："未知类型 0x0000"）。
# 洞与填充必须落在**实际全零**的块区间上，否则语义不等价（DONT_CARE/FILL 都表达"这里就是 0"）。
# 先扫出两个全零窗口放在中间，前后各留一段真数据 —— 这样"洞不推进偏移"的 bug 一定会把尾段
# 数据写错位，debugfs 读标记必然失败；否则测不出东西。
zb = b'\0' * BLK
zwin = [i for i in range(len(raw) // BLK) if raw[i * BLK:(i + 1) * BLK] == zb]
def find(n, lo):
    for s0 in zwin:
        if s0 >= lo and all((s0 + k) in zwin for k in range(n)):
            return s0
    return None
h1 = find(100, 200)
h2 = find(50, h1 + 100 + 1)
if h1 is None or h2 is None:
    raise SystemExit('夹具里找不到两个全零窗口，测不了（换大点的 ext4）')
a = raw[:h1 * BLK]
mid = raw[(h1 + 100) * BLK:h2 * BLK]
tail = raw[(h2 + 50) * BLK:]
parts, meta = [], []
def put(ctype, blocks, data=b''):
    total = 12 + len(data)
    parts.append(struct.pack('<HHII', ctype, 0, blocks, total) + data)
    meta.append((ctype, blocks, total))
put(0xCAC1, h1, a)
put(0xCAC3, 100)
put(0xCAC1, len(mid) // BLK, mid)
put(0xCAC2, 50, struct.pack('<I', 0))
put(0xCAC1, len(tail) // BLK, tail)
total_blks = len(raw) // BLK
crc = zlib.crc32(raw) & 0xFFFFFFFF
parts.append(struct.pack('<HHII', 0xCAC4, 0, 1, 16) + struct.pack('<I', crc))
hdr = struct.pack('<IHHHHIIII', 0xED26FF3A, 1, 0, 28, 12, BLK, total_blks, len(parts), 0)
open(os.path.join(w, 'fake.sparse.img'), 'wb').write(hdr + b''.join(parts))
print('造的 sparse: %d 块 声明=%d B' % (total_blks, total_blks * BLK))
PY

timeout 300 python3 "$SELF/z1_a9_sparse2raw.py" "$W/fake.sparse.img" "$W/out.img" > "$W/conv.log" 2>&1
rc=$?
echo "转换 rc=$rc  输出=$(stat -c%s "$W/out.img" 2>/dev/null) B"
[ $rc -eq 0 ] || { tail -5 "$W/conv.log"; echo "SELFTEST=convert_FAIL"; exit 1; }
grep -E 'CRC32|WARN' "$W/conv.log" | sed 's/^/  /'
# 关键断言：洞之后的数据落对了位置 -> 输出 ext4 里标记文件读得回来，且整镜像与原 fs 逐字节一致
got=$(debugfs -R 'cat /marker/hello.txt' "$W/out.img" 2>/dev/null)
[ "$got" = "SPARSE-HOLE-OFFSET-MARKER" ] && echo "CASE1=PASS(DONT_CARE 之后数据落点正确)" \
  || { echo "CASE1=***FAIL*** 读回=[$got]"; exit 1; }
cmp -s "$W/fs.img" "$W/out.img" && echo "CASE2=PASS(往返逐字节一致)" \
  || { echo "CASE2=***FAIL*** $(ls -l "$W/fs.img" "$W/out.img" | awk '{print $5,$9}' | tr '\n' ' ')"; exit 1; }
timeout 200 e2fsck -fn "$W/out.img" >/dev/null 2>&1; [ $? -eq 0 ] && echo "CASE3=PASS(e2fsck rc=0)" \
  || { echo "CASE3=***FAIL***"; exit 1; }

REAL="$ROOT/out/gsi_v123/system.img"
if [ -f "$REAL" ]; then
  echo "== 真件复测（GSI v123 sparse）=="
  timeout 900 python3 "$SELF/z1_a9_sparse2raw.py" "$REAL" "$W/real.img" > "$W/real.log" 2>&1
  [ $? -eq 0 ] || { tail -3 "$W/real.log"; echo "CASE4=***FAIL***"; exit 1; }
  sz=$(stat -c%s "$W/real.img")
  [ "$sz" = 1313583104 ] && echo "CASE4=PASS(展开 1,313,583,104 B 与声明一致)" || { echo "CASE4=***FAIL*** $sz"; exit 1; }
  timeout 300 e2fsck -fn "$W/real.img" > "$W/reck.log" 2>&1
  grep -q '文件系统状态' "$W/reck.log"; d2=$(dumpe2fs -h "$W/real.img" 2>/dev/null | grep -m1 'Filesystem volume name' | cut -d: -f2 | tr -d ' \t')
  [ "$d2" = system ] && echo "CASE5=PASS(真 A9 GSI 展开后 dumpe2fs 认卷名 system)" || { echo "CASE5=***FAIL*** 卷名=[$d2]"; exit 1; }
else
  echo "CASE4/5=SKIP(本机未留真 GSI $REAL)"
fi
echo "SPARSE_SELFTEST=ALL_PASS"
