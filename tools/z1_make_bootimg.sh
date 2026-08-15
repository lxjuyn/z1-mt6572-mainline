#!/bin/bash
# z1_make_bootimg.sh — Z1 (MT6572) mainline boot.img 一键打包
#
# 把历史手工流程脚本化 (CLAUDE.md §4a):
#   1. cat zImage mt6572-z1.dtb > /tmp/zImage_appended_z1ver
#      (mainline CONFIG_ARM_APPENDED_DTB 在 zImage 文件末尾 _edata 找 d00dfeed magic,
#       所以 dtb 必须 append 到 zImage 末尾, 不能用 mkbootimg --dt 独立 dt 区)
#   2. mkbootimg --kernel <appended> --ramdisk <...> --mtk 1 打包
#   3. 校验: 大小 ≤ 6MB boot 分区 / ANDROID! magic / sha256
#
# 用法 (全部位置参数可省, 有默认值; 也可用环境变量 Z1_ZIMAGE/Z1_DTB/Z1_RAMDISK/Z1_OUT 覆盖):
#   tools/z1_make_bootimg.sh [zImage] [dtb] [ramdisk] [output]
#
# V2 (2026-08-14) 新增:
#   * ramdisk 缺失或 Z1_REPACK=1 → 自动调 repack_ramdisk_connsys.sh 重建
#     (固件 WIFI_RAM_CODE/WMT_SOC.cfg/ROMv1_patch + NVRAM + /usr/bin/iw + launcher + 验证脚本 一并打包)
#   * 打包前校验 ramdisk 内含固件/NVRAM/iw/launcher/脚本, 缺关键件给出 WARN
#
# 默认值对应 CONNSYS WiFi/BT 上板验证链路:
#   zImage  = fork_linux/arch/arm/boot/zImage
#   dtb     = fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dtb
#   ramdisk = out/ramdisk_mainline_connsys.gz  (MTK ROOTFS blob, 内嵌 connsys 模块/固件)
#   output  = out/boot_mainline_connsys.img
#
# 参考手工命令 (历史):
#   cat fork_linux/arch/arm/boot/zImage fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dtb > /tmp/zImage_appended
#   ./build/mtkbootimg/mkbootimg --kernel /tmp/zImage_appended --ramdisk out/forensic/linux/ramdisk.gz \
#     --base 0x10000000 --kernel_offset 0x8000 --pagesize 2048 --mtk 1 \
#     --cmdline "..." -o out/boot_mainline_appdtb.img
#
# ⚠️ 板子关机时在 PC 上跑, 不碰硬件. 本脚本只打包不刷写; 刷写由用户开机后另执行.

set -euo pipefail

ROOT=/home/lxj/claude/6572
MKBOOTIMG=$ROOT/build/mtkbootimg/mkbootimg

ZIMAGE=${1:-${Z1_ZIMAGE:-$ROOT/fork_linux/arch/arm/boot/zImage}}
DTB=${2:-${Z1_DTB:-$ROOT/fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dtb}}
RAMDISK=${3:-${Z1_RAMDISK:-$ROOT/out/ramdisk_mainline_connsys.gz}}
OUT=${4:-${Z1_OUT:-$ROOT/out/boot_mainline_connsys.img}}

CMDLINE=${Z1_CMDLINE:-"console=tty0 console=ttyS0,115200n8 root=/dev/ram panic=0 ignore_loglevel clk_ignore_unused rdinit=/init"}
APPENDED=/tmp/zImage_appended_z1ver
MAX_BOOT=6291456   # Z1 bootimg 分区 6MB

echo "=== Z1 boot.img 打包 ==="
echo "  zImage  : $ZIMAGE"
echo "  dtb     : $DTB"
echo "  ramdisk : $RAMDISK"
echo "  output  : $OUT"
echo ""

# --- 自动重建 ramdisk (若缺失或 Z1_REPACK=1) ---
# repack_ramdisk_connsys.sh 负责把固件/NVRAM/iw/launcher/验证脚本一并塞进 ramdisk
DEFAULT_RAMDISK=$ROOT/out/ramdisk_mainline_connsys.gz
if [ "${Z1_REPACK:-0}" = "1" ] || [ ! -f "$RAMDISK" ]; then
    if [ "$RAMDISK" != "$DEFAULT_RAMDISK" ]; then
        echo "WARN: ramdisk 缺失且非默认路径 — 跳过自动 repack (手工跑 tools/repack_ramdisk_connsys.sh)"
    else
        echo "[*] 重建 ramdisk (固件/NVRAM/iw/launcher/验证脚本): tools/repack_ramdisk_connsys.sh"
        bash "$ROOT/tools/repack_ramdisk_connsys.sh" || { echo "ERROR: repack 失败"; exit 1; }
    fi
fi

[ -x "$MKBOOTIMG" ] || { echo "ERROR: 找不到 mkbootimg: $MKBOOTIMG"; exit 1; }
for f in "$ZIMAGE" "$DTB" "$RAMDISK"; do
    [ -f "$f" ] || { echo "ERROR: 缺少输入文件 $f"; exit 1; }
done

# 非致命提示: ramdisk 应为 MTK ROOTFS blob (头 4 字节 88 16 88 58), 别用标准 gzip-cpio
if [ "$(head -c 4 "$RAMDISK" | od -An -tx1 | tr -d ' \n')" != "88168858" ]; then
    echo "WARN: ramdisk 头不是 MTK ROOTFS (期望 8816 8858 ...) — 确认用的是 out/ramdisk_mainline_connsys.gz 或 out/forensic/linux/ramdisk.gz"
fi

# ramdisk 内容校验 (固件/NVRAM/iw/模块/脚本) — 缺关键件则提示重打包 (非致命)
TMP=$(mktemp -d /tmp/z1mk_XXXXXX)
trap 'rm -rf "$TMP"' EXIT
dd if="$RAMDISK" bs=1 skip=512 2>/dev/null > "$TMP/body"
rd_list=""
case "$(head -c 2 "$TMP/body" | od -An -tx1 | tr -d ' \n')" in
    fd37*) rd_list=$(xz -dc "$TMP/body" 2>/dev/null | cpio -it 2>/dev/null) ;;
    1f8b*) rd_list=$(gzip -dc "$TMP/body" 2>/dev/null | cpio -it 2>/dev/null) ;;
    *) echo "WARN: 无法识别 ramdisk body 压缩 (须为 MTK ROOTFS blob), 跳过内容校验" ;;
esac
echo ""
echo "=== ramdisk 内容校验: $(basename "$RAMDISK") ==="
if [ -z "$rd_list" ]; then
    echo "  (未做内容校验 — 请确认 ramdisk 是 MTK ROOTFS blob)"
else
    rd_has() { echo "$rd_list" | grep -a "$1" >/dev/null; }
    MISS=0
    for f in \
        "lib/firmware/WIFI_RAM_CODE" \
        "system/etc/firmware/WMT_SOC.cfg" \
        "system/etc/firmware/ROMv1_patch" \
        "etc/firmware/nvram/WIFI" \
        "usr/bin/iw" \
        "root/connsys/mtk_stp_launcher" \
        "root/connsys/z1_modprobe_connsys.sh" \
        "root/connsys/z1_connsys_onboard_test.sh"; do
        if rd_has "$f"; then
            echo "  [OK]   $f"
        else
            echo "  [WARN] $f 缺失 — 该项功能不可用"
            MISS=$((MISS + 1))
        fi
    done
    KO=$(echo "$rd_list" | grep -ac '\.ko$')
    echo "  [INFO] root/connsys/*.ko 数量: $KO (期望 ≥6: btif/wmt_soc/bt_soc/wmt_wifi_soc/cfg80211/wlan_gen2)"
    if [ "$MISS" -gt 0 ]; then
        echo "  WARN: 缺 $MISS 项 — 设 Z1_REPACK=1 重新打包可自动补齐"
    else
        echo "  [OK]   固件/NVRAM/iw/launcher/验证脚本齐全"
    fi
fi

# 1) 拼 appended dtb
cat "$ZIMAGE" "$DTB" > "$APPENDED"
echo "[*] appended zImage+dtb: $APPENDED ($(stat -c%s "$APPENDED") B)"

# 2) mkbootimg — 不带 --dt (dtb 已 append 进 kernel 段), --mtk 1 = MTK 头
"$MKBOOTIMG" --kernel "$APPENDED" \
    --ramdisk "$RAMDISK" \
    --base 0x10000000 --kernel_offset 0x8000 --pagesize 2048 --mtk 1 \
    --cmdline "$CMDLINE" \
    -o "$OUT"

# 3) 校验
SIZE=$(stat -c%s "$OUT")
echo ""
echo "=== 产物校验: $OUT ==="
echo "大小: $SIZE B (≈ $(echo "$SIZE" | awk '{printf "%.2f", $1/1048576}') MiB)"
if [ "$SIZE" -le "$MAX_BOOT" ]; then
    echo "[OK] ≤ 6MB (bootimg 分区可容纳)"
else
    echo "[FAIL] 超 6MB 分区限制 $((SIZE - MAX_BOOT)) B — 需精简 zImage/ramdisk"
    exit 1
fi
MAGIC=$(head -c 8 "$OUT")
if [ "$MAGIC" = "ANDROID!" ]; then
    echo "[OK] ANDROID! magic 正确"
else
    echo "[FAIL] magic 错误: $(printf '%s' "$MAGIC" | od -An -tx1 | tr -d ' \n') (期望 ANDROID! = 414e44524f494421)"
    exit 1
fi
echo "sha256: $(sha256sum "$OUT" | cut -d' ' -f1)"
echo ""
echo "=== 打包完成, 未刷写. ==="
echo "刷写 (用户开机时, 另执行): tools/flash_with_retry.sh bootimg $OUT $ROOT/1/boot.img 8"
