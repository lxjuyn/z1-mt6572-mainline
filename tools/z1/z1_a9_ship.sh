#!/usr/bin/env bash
# z1_a9_ship.sh —— 大镜像的分片交付/验收（本机↔持 A9 system.img 那台机器）
#
# 用法:
#   tools/z1/z1_a9_ship.sh split <镜像> <输出目录> [分片大小 MiB，默认 200]
#   tools/z1/z1_a9_ship.sh verify <分片目录> <输出镜像>
#   tools/z1/z1_a9_ship.sh pull   <owner/repo> <tag> <输出目录>   # 只拉 .a9part 资产
#
# 为什么要它：本机唯一被证明可下大件的通路是 GitHub Release 资产（实测 api.github.com 的
# asset 接口 + release-assets.githubusercontent.com 302 都通，19,744 B 的 dtb 原样下回并解得开）。
# 但单件上限 GitHub 文档没写死，而 A9 的 system.img 是 943,718,400 B —— 与其赌上限，
# 不如固定切片 + 逐片 sha256 + 整件 sha256，任何一段坏了都在拼装阶段就停，不会拿一份
# "看起来完整"的镜像去刷板子。pull 只做只读列举与下载，不碰仓库。
set -uo pipefail
cmd=${1:?用法: split|verify|pull}

form_of() { # 报镜像形态，便于 verify 阶段人一眼看出给的是不是 ext4/sparse
  python3 - "$1" <<'PY'
import struct, sys
b = open(sys.argv[1], 'rb').read(1082)
sp = struct.unpack('<I', b[:4])[0] == 0x3AFF26AF
# ext4 超级块在偏移 1024，但 s_magic 在超级块内 +56，也就是文件偏移 1080。
# 只看 1024 读到的是 s_log_block_size，会把真 ext4 判成 UNKNOWN（本轮实测踩到）。
ex = b[1080:1082] == b'\x53\xef'
print('FORM=%s' % ('sparse' if sp else ('ext4' if ex else 'UNKNOWN')))
PY
}

case "$cmd" in
split)
  img=${2:?需要镜像}; dir=${3:?需要输出目录}; mb=${4:-200}
  [ -f "$img" ] || { echo "ERR: 找不到 $img"; exit 1; }
  mkdir -p "$dir" || exit 1
  total=$(stat -c%s "$img"); sha=$(sha256sum "$img" | cut -d' ' -f1)
  base=$(basename "$img")
  rm -f "$dir"/*.a9part "$dir"/SHIPPING.txt
  split -b "${mb}m" -d --additional-suffix=.a9part "$img" "$dir/${base}."
  {
    echo "# A9 分片交付清单 $(date -Is)"
    echo "NAME=$base"
    echo "TOTAL_BYTES=$total"
    echo "CHUNK_MIB=$mb"
    echo "WHOLE_SHA256=$sha"
    echo "FORM=$(form_of "$img" | cut -d= -f2)"
    ( cd "$dir" && sha256sum *.a9part )
  } > "$dir/SHIPPING.txt"
  echo "SPLIT_PARTS=$(ls "$dir"/*.a9part | wc -l) DIR=$dir"
  echo "WHOLE_SHA256=$sha"
  grep -c '.a9part' "$dir/SHIPPING.txt" >/dev/null || { echo "ERR: 清单没写进分片哈希"; exit 3; }
  ;;
verify)
  dir=${2:?需要分片目录}; out=${3:?需要输出镜像}
  [ -f "$dir/SHIPPING.txt" ] || { echo "ERR: $dir 缺 SHIPPING.txt"; exit 1; }
  base=$(grep -m1 '^NAME=' "$dir/SHIPPING.txt" | cut -d= -f2)
  want=$(grep -m1 '^TOTAL_BYTES=' "$dir/SHIPPING.txt" | cut -d= -f2)
  wsha=$(grep -m1 '^WHOLE_SHA256=' "$dir/SHIPPING.txt" | cut -d= -f2)
  # 不解析 sha256sum -c 的输出：本机 locale 是 zh_CN，它打的是"…: 成功"而不是"…: OK"，
  # 按 ': OK' 数会把全过判成 0/5（安全方向错，但仍是要修的错）。逐片自己算哈希比对，
  # 并核对"清单列的片数"与"目录里的片数"一致，防漏片。
  listed=0; bad=0
  while read -r h f _rest; do
    listed=$((listed+1))
    if [ ! -f "$dir/$f" ]; then echo "  缺片 $f"; bad=$((bad+1)); continue; fi
    g=$(sha256sum "$dir/$f" | cut -d' ' -f1)
    if [ "$g" = "$h" ]; then :; else echo "  哈希不符 $f"; bad=$((bad+1)); fi
  done < <(grep -E '^[0-9a-f]{64}  ' "$dir/SHIPPING.txt")
  onfile=$(ls "$dir"/*.a9part 2>/dev/null | wc -l)
  if [ "$listed" = 0 ] || [ "$listed" != "$onfile" ] || [ "$bad" != 0 ]; then
    echo "VERIFY=FAIL 清单列 $listed 片，目录里 $onfile 片，不符 $bad 片"
    exit 3
  fi
  rm -f "$out"
  cat "$dir/${base}."*.a9part > "$out" || { echo "ERR: 拼装失败"; exit 3; }
  got=$(stat -c%s "$out"); gsha=$(sha256sum "$out" | cut -d' ' -f1)
  [ "$got" = "$want" ] || { echo "VERIFY=FAIL 尺寸 $got != 清单 $want"; exit 3; }
  [ "$gsha" = "$wsha" ] || { echo "VERIFY=FAIL 整件 sha $gsha != 清单 $wsha"; exit 3; }
  echo "VERIFY=OK parts=$(ls "$dir"/*.a9part | wc -l) bytes=$got sha256=$gsha"
  form_of "$out"
  echo "下一步: python3 tools/z1/z1_a9_system_accept.py $out"
  ;;
pull)
  repo=${2:?需要 owner/repo}; tag=${3:?需要 tag}; dir=${4:-/tmp/a9ship}
  mkdir -p "$dir" || exit 1
  rel=$(curl -fsS --max-time 60 "https://api.github.com/repos/$repo/releases/tags/$tag") \
    || { echo "ERR: 取不到 release $repo/$tag"; exit 1; }
  ids=$(printf '%s' "$rel" | python3 -c '
import json,sys
d=json.load(sys.stdin)
for a in d.get("assets",[]):
    if a["name"].endswith(".a9part") or a["name"]=="SHIPPING.txt":
        print(a["id"], a["name"], sep="\t")
')
  [ -n "$ids" ] || { echo "ERR: $repo/$tag 里没有 .a9part 资产"; exit 1; }
  printf '%s\n' "$ids" | sed 's/^/  待拉: /'
  printf '%s\n' "$ids" | while IFS=$'\t' read -r id name; do
    curl -fsSL --max-time 900 -H "Accept: application/octet-stream" \
      "https://api.github.com/repos/$repo/releases/assets/$id" -o "$dir/$name" \
      || { echo "ERR: 下载失败 $name"; exit 1; }
    echo "  已取 $name $(stat -c%s "$dir/$name") B"
  done
  echo "PULLED_DIR=$dir"
  echo "下一步: tools/z1/z1_a9_ship.sh verify $dir out/system_a9.img"
  ;;
*) echo "用法: $0 split|verify|pull ..."; exit 2;;
esac
