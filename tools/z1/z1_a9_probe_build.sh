#!/usr/bin/env bash
# 探针的可复现构建。本机 armhf 工具链没有静态 libc(缺 libc.a/crti.o)，
# 所以走 -nostdlib 裸 syscall 路线；入口必须是 main 且不能依赖 crti/crtend。
set -euo pipefail
SELF=$(cd "$(dirname "$0")" && pwd)
WT=$(readlink -f "$SELF/../..")
OUT=${1:-$SELF/z1_a9_probe}
# 探针用的是用户态 uapi 头，必须走 headers_install 产出的 usr/include
# （直接 -I include/uapi 会缺 asm/unistd-nr.h 这类合成头，当初 uapi 里少
#   Linux-syscall-note 会让 headers_install 失败，已在 a177b7b9f 修掉）
[ -d "$WT/usr/include/asm" ] || ( cd "$WT" && make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- headers_install >/dev/null )
arm-linux-gnueabihf-gcc -Os -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=softfp -mthumb-interwork \
  -ffreestanding -fno-stack-protector -fno-pic -nostdlib -nodefaultlibs \
  -Wl,-e,_start -Wl,--build-id=none -static \
  -isystem "$WT/usr/include/arm-linux-gnueabihf" -isystem "$WT/usr/include" \
  -o "$OUT" "$SELF/z1_a9_probe.c" -lgcc
# 入口自检：源码在 __arm__ 下入口是 _start（不是 main）。
# 传错 -e 时链接器只给一句警告然后把入口摆到段首地址 —— 二进制照样能 file 出
# "statically linked"，但一执行就跳进别的函数，表现为"探针毫无输出"，极容易被
# 误诊成内核问题。所以这里硬性核对入口 == _start。
entry=$(LC_ALL=C arm-linux-gnueabihf-readelf -h "$OUT" | grep -i entry | grep -oE '0x[0-9a-fA-F]+' | head -1)
start=$(LC_ALL=C arm-linux-gnueabihf-nm "$OUT" | awk '$3=="_start"{print $1}')
if [ -z "$start" ] || [ -z "$entry" ]; then echo "ERR: 取不到入口或 _start(entry=$entry start=$start)"; exit 4; fi
e=$((16#${entry#0x})); st=$((16#$start))
# Thumb 函数地址带 lsb=1，所以 entry==st 或 st+1 都算对
if [ "$e" -ne "$st" ] && [ "$e" -ne $((st + 1)) ]; then
  echo "ERR: 入口 $(printf 0x%x $e) != _start $(printf 0x%x $st)（-e 传错或被链接器忽略）"; exit 4
fi
echo "入口自检: entry=$entry _start=0x$start ✓"
file -b "$OUT"
echo "size=$(stat -c%s "$OUT") sha256=$(sha256sum "$OUT" | cut -d' ' -f1)"
