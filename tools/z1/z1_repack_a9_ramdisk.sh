#!/usr/bin/env bash
# z1_repack_a9_ramdisk.sh — 从解包目录重打带 512B MTK ROOTFS 头的 initramfs
#
# 用法:
#   tools/z1_repack_a9_ramdisk.sh <staging_dir> <template.gz> <out.gz>
#
# 为什么单独成脚本（而不是手工命令串）：
#  - 本项目两次栽在"头长度字段与 dts linux,initrd-end 不同步"上（v13.1 截断、v13.4 覆盖）。
#    这里把 body 长度、头 0x04 小端字段、initrd-end 三者一次算清并打印，杜绝手算。
#  - 512B 头的 magic(88 16 88 58)、"ROOTFS" 标签(0x08)及其余保留字节必须与原厂/已验证件
#    逐字节一致，故用模板头做基底，只改长度字段。
#  - cpio 用 -R 0:0 强制 root 属主（解包时会被本地 uid 改掉），并保留可执行位。
#
# 输出（stdout, KEY=VALUE 便于机器读取）:
#   BODY_BYTES= N
#   HDR_LEN_FIELD= 0x......
#   INITRD_END= 0x84........
#   OUT_SHA256= ...
#   TEMPLATE_SHA256= ...
set -euo pipefail

STAGE=${1:?需要解包目录}
TPL=${2:?需要模板 ramdisk(.gz, 用于取 512B 头)}
OUT=${3:?需要输出文件}

[ -d "$STAGE" ] || { echo "ERR: 目录不存在 $STAGE" >&2; exit 1; }
[ -f "$TPL" ] || { echo "ERR: 模板不存在 $TPL" >&2; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# 1) body: cpio newc (root:root) -> gzip -9n（mtime=0，保证可复现）
( cd "$STAGE" && find . -print0 | LC_ALL=C sort -z | \
    cpio -o -H newc --null -R 0:0 --quiet ) > "$TMP/body.cpio" 2>/dev/null
gzip -9n -c "$TMP/body.cpio" > "$TMP/body.gz"

BODY=$(stat -c%s "$TMP/body.gz")

# 2) 头：取模板前 512 字节，仅重写 0x04 处小端 u32 = body 长度
head -c 512 "$TPL" > "$TMP/hdr.bin"
python3 - "$TMP/hdr.bin" "$BODY" <<'PY'
import struct, sys
p = sys.argv[1]; n = int(sys.argv[2])
b = bytearray(open(p, 'rb').read())
assert b[0:4] == bytes.fromhex('88168858'), "模板头 magic 不是 88 16 88 58"
assert b[8:14] == b'ROOTFS', "模板头 0x08 处不是 ROOTFS 标签"
b[4:8] = struct.pack('<I', n)
open(p, 'wb').write(bytes(b))
print("HDR magic/label 校验通过, 长度字段 ->", hex(n), file=sys.stderr)
PY

cat "$TMP/hdr.bin" "$TMP/body.gz" > "$OUT"

END=$(printf '0x%x' $((0x84100000 + BODY)))
echo "BODY_BYTES=$BODY"
echo "HDR_LEN_FIELD=$(printf '0x%x' "$BODY")"
echo "INITRD_END=$END"
echo "OUT_SHA256=$(sha256sum "$OUT" | cut -d' ' -f1)"
echo "TEMPLATE_SHA256=$(sha256sum "$TPL" | cut -d' ' -f1)"
echo "OUT_SIZE_TOTAL=$((512 + BODY))"

# 3) 自检：注意 gunzip 不能直接测整个文件 —— 前 512 字节是非 gzip 的 MTK 头。
#    GNU cpio -t 默认不列出 TRAILER!!! 条目，故不能用它做判据；改为
#    "解出实际文件清单并与 staging 目录逐项比对"，这才是有意义的完整性检查。
dd if="$OUT" bs=512 skip=1 2>/dev/null | gunzip -t 2>/dev/null \
  || { echo "SELFTEST=gunzip_FAIL(body 部分不是合法 gzip)" >&2; exit 1; }
mkdir -p "$TMP/verify"
# 注意必须 zcat：body 是 gzip 压缩过的 cpio，直接喂 cpio 会得到一堆"数字格式错误"
dd if="$OUT" bs=512 skip=1 2>/dev/null | zcat 2>/dev/null | \
  ( cd "$TMP/verify" && cpio -idm --quiet ) 2>/dev/null \
  || { echo "SELFTEST=cpio_extract_FAIL" >&2; exit 1; }
diff <(cd "$STAGE" && find . -printf '%y %p\n' 2>/dev/null | LC_ALL=C sort) \
     <(cd "$TMP/verify" && find . -printf '%y %p\n' 2>/dev/null | LC_ALL=C sort) \
  > "$TMP/listdiff" 2>&1
if [ -s "$TMP/listdiff" ]; then
	echo "SELFTEST=filelist_mismatch" >&2
	head -10 "$TMP/listdiff" >&2
	exit 1
fi
echo "SELFTEST=filelist_ok 条目=$(cd "$STAGE" && find . | wc -l)"

# 头长度字段与 body 实际字节必须互镜像一致（本项目两次栽在此处）
HDRN=$(dd if="$OUT" bs=1 count=4 skip=4 2>/dev/null | od -An -tu4 | tr -d ' ')
if [ "$HDRN" != "$BODY" ]; then echo "SELFTEST=len_field_mismatch hdr=$HDRN body=$BODY" >&2; exit 1; fi
echo "SELFTEST=len_field_ok hdr=$HDRN"
echo "INITRD_END_VERIFIED=$(printf '0x%x' $((0x84100000 + HDRN)))"
echo "条目数=$(dd if="$OUT" bs=1 skip=512 2>/dev/null | zcat 2>/dev/null | cpio -t 2>/dev/null | wc -l)"
