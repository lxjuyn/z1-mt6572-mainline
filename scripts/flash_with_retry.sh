#!/bin/bash
# flash_with_retry.sh — 无人值守安全刷写单个分区(反复重试 + 失败回滚兜底)
#
# 设计目标:
#   - mtkclient 对老 MT6572 写大分区偶发半写(usb bulk 丢包)。
#   - 此脚本:最多重试 N 次,每次趁 preloader/BROM 窗口抓一次写。
#   - 写失败不会停留在半写状态:立刻把"现役原版备份"写回该分区(回滚),
#     避免设备卡在引导循环。
#
# 用法: flash_with_retry.sh <partition> <image> [rollback_image] [maxtries]
#   partition       GPT 真名: uboot / recovery / bootimg
#   image           要刷入的镜像
#   rollback_image  失败后回滚用的镜像(默认 = image 本身,即不回滚)
#   maxtries        最大重试次数(默认 8)
#
# 退出码:
#   0 = 写成功
#   1 = 参数错
#   2 = 全部重试用尽
#   3 = 回滚写入也失败(危险:可能变砖,需人工)
#
# 重要: uboot(bootloader)半写=变砖风险。本脚本对 uboot 写失败会已回滚 live 是
# 已知原版备份——但 uboot 半写到一半时设备已可能起不来,无 adb 救。
# 因此 uboot 这种 bootloader 分区,默认 rollback_image 必须传(且是可靠的现役原版)。
set -u
# mtkclient 路径可用环境变量覆盖(默认认当前目录 ./mtkclient)。
DIR="${MTKCLIENT_DIR:-./mtkclient}"
PY="${MTK_PY:-python3}"
PART="${1:-}"
IMG_ARG="${2:-}"
ROLLBACK_ARG="${3:-}"
MAXTRIES="${4:-8}"

# set -u 下防 integer expression: maxtries 必须是纯数字
[[ "$MAXTRIES" =~ ^[0-9]+$ ]] || {
  echo "ERROR: maxtries 必须是正整数: '$MAXTRIES'" >&2
  exit 1
}

[ -n "$PART" ] && [ -n "$IMG_ARG" ] || {
  echo "usage: $0 <partition> <image> [rollback_image] [maxtries]"
  echo "  例: $0 uboot ./out/lk_Z1.bin           # 写 lk(无回滚)"
  echo "  例: $0 recovery ./out/twrp.img ./out/live_recovery.img"
  exit 1
}

# 镜像路径支持相对(相对当前 cwd)和绝对,统一转绝对,避免 cd mtkclient 后失效
abs_img=$(cd "$(dirname "$IMG_ARG")" 2>/dev/null && echo "$(pwd)/$(basename "$IMG_ARG")")
abs_rollback=""
if [ -n "$ROLLBACK_ARG" ]; then
  abs_rollback=$(cd "$(dirname "$ROLLBACK_ARG")" 2>/dev/null && echo "$(pwd)/$(basename "$ROLLBACK_ARG")")
fi
IMG="$abs_img"
ROLLBACK="$abs_rollback"
VERIFY_FILE=$(mktemp /tmp/z1_verify_part.XXXXXX.bin) || exit 1
trap 'rm -f "$VERIFY_FILE"' EXIT

cd "$DIR" || exit 1

# bootloader 类分区强制要求回滚镜像(变砖防护)
case "$PART" in
  uboot|preloader|lk)
    [ -n "$ROLLBACK" ] || {
      echo "ERROR: 写 $PART 是 bootloader,必须提供 rollback_image(现役原版备份)防变砖"
      exit 1
    }
    ;;
esac

[ -f "$IMG" ] || { echo "ERROR: $IMG 不存在"; exit 1; }
[ -z "$ROLLBACK" ] || [ -f "$ROLLBACK" ] || { echo "ERROR: rollback $ROLLBACK 不存在"; exit 1; }

IMG_SIZE=$(stat -c%s "$IMG" 2>/dev/null)
echo "=========================================="
echo " 刷写: $PART  ← $IMG ($IMG_SIZE B)"
[ -n "$ROLLBACK" ] && echo " 回滚: $ROLLBACK"
echo " 最大重试: $MAXTRIES 次"
echo "=========================================="

# 判断 mtkclient 输出是否确曾与设备建立握手/连接(写中途已进 preloader/BROM)。
# 用于区分"半写失败(需回滚)" 与 "干净没连上(只需重试, 不回滚)"。
mtk_output_connected() {
  local out="$1"
  # 显式失败/未连接短语优先, 排除 "No preloader detected"/"Nothing connected" 等假阳性
  if echo "$out" | grep -qiE '(nothing connected|no preloader|preloader not (found|detected)|no device|device not found|handshake failed|connection (error|failed)|timed out)'; then
    return 1
  fi
  # 确曾建立握手/连接的标记
  echo "$out" | grep -qiE '(preloader|brom|sync\.\.\.|sync done|connection|device detected|device found)' && return 0
  return 1
}

verify_partition_prefix() {
  local expected="$1"
  local label="$2"
  local verify_out verify_rc verify_size expected_size

  : > "$VERIFY_FILE"
  # timeout 60: 防止 mtk.py r 在设备退出 preloader(内核常驻)后永久轮询挂死
  verify_out=$(timeout 60 "$PY" mtk.py r "$PART" "$VERIFY_FILE" 2>&1)
  verify_rc=$?
  echo "$verify_out" | tail -8

  if [ "$verify_rc" -ne 0 ] || [ ! -f "$VERIFY_FILE" ]; then
    if ! mtk_output_connected "$verify_out"; then
      # 完全没连上(设备不在 preloader, 或 60s 超时无设备): 不是半写证据
      echo "ℹ️ $label 读回未连上设备(rc=$verify_rc), 不构成半写"
      return 2
    fi
    echo "❌ $label 读回失败(rc=$verify_rc)"
    return 1
  fi

  verify_size=$(stat -c%s "$VERIFY_FILE" 2>/dev/null || echo 0)
  expected_size=$(stat -c%s "$expected" 2>/dev/null || echo 0)
  if [ "$verify_size" -lt "$expected_size" ]; then
    echo "❌ $label 读回过短: $verify_size B < $expected_size B"
    return 1
  fi

  if ! cmp -s -n "$expected_size" "$VERIFY_FILE" "$expected"; then
    echo "❌ $label 读回前 $expected_size B 与期望镜像不一致"
    return 1
  fi

  echo "✅ $label 完整分区读回完成，前 $expected_size B 与期望镜像一致"
  return 0
}

rollback_and_verify() {
  local rollback_out rollback_rc vrc

  [ -n "$ROLLBACK" ] || return 1
  echo "=== 回滚:写回 $ROLLBACK → $PART ==="
  rollback_out=$($PY mtk.py w "$PART" "$ROLLBACK" 2>&1)
  rollback_rc=$?
  echo "$rollback_out" | tail -8
  if [ "$rollback_rc" -ne 0 ] || ! echo "$rollback_out" | grep -qiE 'write.*done|Done|finished|Successful'; then
    echo "❌ 回滚写入失败(rc=$rollback_rc)"
    return 1
  fi

  echo "=== 回滚校验:完整读回 $PART 并比较回滚镜像 ==="
  verify_partition_prefix "$ROLLBACK" "回滚"
  vrc=$?
  if [ "$vrc" -eq 2 ]; then
    # 回滚写命令已成功, 读回没连上设备(设备可能已启动回滚镜像): 视为回滚完成
    echo "ℹ️ 回滚写入成功(读回未连上设备, 视为回滚完成)"
    return 0
  fi
  return "$vrc"
}

try=1
while [ "$try" -le "$MAXTRIES" ]; do
  echo ""
  echo ">>> 第 $try/$MAXTRIES 次尝试写 $PART @ $(date '+%H:%M:%S' 2>/dev/null || echo '?')"
  # mtk w 自己会轮询等待 preloader/BROM 窗口,给 120s/次
  OUT=$($PY mtk.py w "$PART" "$IMG" 2>&1)
  RC=$?
  echo "$OUT" | tail -8

  # 成功判定: mtkclient 成功写完会打印类似 "Write done" / "Done" 且返回 0
  if [ "$RC" -eq 0 ] && echo "$OUT" | grep -qiE 'write.*done|Done|finished|Successful'; then
    echo ""
    echo "✅ 第 $try 次写命令完成: $PART ← $IMG"

    # Z1_VERIFY=0: 跳过严格读回 verify(只写 + 写成功即返回)。
    # emmc_full5 修好 boot 后内核常驻, 写后 mtk.py r 常连不上 preloader;
    # 此时若回滚会把成功镜像毁掉, 所以默认也要把"读回没连上"当非半写处理。
    if [ "${Z1_VERIFY:-1}" = "0" ]; then
      echo "ℹ️ Z1_VERIFY=0: 跳过严格读回 verify, 判定写入成功"
      exit 0
    fi

    echo "=== 严格校验:完整读回 $PART 并比较目标镜像长度 ==="
    verify_partition_prefix "$IMG" "目标"
    vrc=$?
    if [ "$vrc" -eq 0 ]; then
      echo "✅ 第 $try 次写入及严格读回校验均成功"
      exit 0
    fi

    if [ "$vrc" -eq 2 ]; then
      # 写命令已成功, 但读回连不上设备 —— 多半是设备已退出 preloader 引导新镜像,
      # 不是半写, 绝不能回滚(会毁掉刚写好的成功镜像)。
      echo "✅ 第 $try 次写入成功(读回未连上设备, 已跳过回滚; 若新镜像已开机即刷写成功)"
      exit 0
    fi

    echo "❌ 写命令返回成功，但严格读回校验失败；本次不得判定成功"
    if [ -n "$ROLLBACK" ]; then
      if rollback_and_verify; then
        echo "↩️ 已严格验证回滚，等待下一次目标写入"
      else
        echo "❌❌ 严格回滚失败；停止，禁止继续自动写入"
        exit 3
      fi
    else
      echo "❌ 无回滚镜像，停止"
      exit 3
    fi
    try=$((try+1))
    echo "--- 等 8s 让设备/USB 复位 ---"
    sleep 8
    continue
  fi

  echo "❌ 第 $try 次写失败(rc=$RC)。$([ -n "$ROLLBACK" ] && echo '将检查是否需回滚' || echo '无回滚镜像,继续重试')"

  # 写失败: 只有当"写中途确曾建立握手"(可能半写)时才回滚, 避免停在半写状态。
  # 干净没连上(设备不在 preloader, rc!=0)不算半写, 只等窗口重试, 不回滚。
  if [ -n "$ROLLBACK" ] && mtk_output_connected "$OUT"; then
    if rollback_and_verify; then
      echo "↩️ 回滚成功且读回一致，设备已恢复，可继续重试"
    else
      echo "❌❌ 回滚写入或读回校验失败；停止，禁止继续自动写入"
      exit 3
    fi
  elif [ -n "$ROLLBACK" ]; then
    echo "ℹ️ 本次写命令未与设备建立连接(干净失败), 不回滚, 等待窗口重试"
  fi

  try=$((try+1))
  # 如果设备已离线(写失败常伴随 USB 断),等设备重新暴露窗口
  echo "--- 等 8s 让设备/USB 复位 ---"
  sleep 8
done

echo ""
echo "❌❌ 全部 $MAXTRIES 次尝试用尽,$PART 仍未写成功"
if [ -n "$ROLLBACK" ]; then
  echo "已尝试回滚。建议:物理重启设备,检查 USB,换更大 retry 数再试。"
fi
exit 2
