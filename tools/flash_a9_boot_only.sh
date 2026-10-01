#!/bin/bash
# flash_a9_boot_only.sh — 仅刷写 Android 9 boot.img 到 Z1 (测试用)
#
# 功能:
#   - 仅刷 boot.img → bootimg 分区 (6MB)
#   - 带重试逻辑 (8 次)
#   - 刷写前校验镜像大小
#   - 刷写前备份当前 boot 分区
#   - 日志记录到 mainline_recon/flash_a9_boot_log_TIMESTAMP.txt
#
# 用途: 在不动 system 分区的情况下测试新 boot.img
#
# 用法: flash_a9_boot_only.sh <boot.img>
#   例: tools/flash_a9_boot_only.sh out/boot_a9_pie.img
#
# 退出码:
#   0 = 成功
#   1 = 参数错误 / 镜像大小超限 / mtkclient 不可用
#   2 = 刷写失败
#   4 = 备份失败

set -u

# ========== 配置 ==========
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROJECT_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
MTKCLIENT_DIR="$PROJECT_ROOT/mtkclient_v2141"
MTKCLIENT_PY="$PROJECT_ROOT/mtkclient_venv/bin/python"
FLASH_RETRY_SCRIPT="$SCRIPT_DIR/flash_with_retry.sh"
BACKUP_DIR="$PROJECT_ROOT/bridge_backups/a9_boot_pre_flash_$(date +%Y%m%d_%H%M%S)"
LOG_DIR="$PROJECT_ROOT/mainline_recon"
LOG_FILE="$LOG_DIR/flash_a9_boot_log_$(date +%Y%m%d_%H%M%S).txt"

# 分区限制 (字节)
MAX_BOOT_SIZE=6291456        # 6MB (bootimg 分区)

# 重试参数
MAX_RETRIES=8

# ========== 函数 ==========
log() {
  local msg="[$(date '+%Y-%m-%d %H:%M:%S')] $*"
  echo "$msg" | tee -a "$LOG_FILE"
}

error() {
  log "❌ ERROR: $*"
  exit "${2:-1}"
}

check_prereq() {
  log "=== 环境检查 ==="

  # 检查 mtkclient
  if [ ! -d "$MTKCLIENT_DIR" ] || [ ! -f "$MTKCLIENT_PY" ]; then
    error "mtkclient 不存在: $MTKCLIENT_DIR" 1
  fi
  log "✅ mtkclient: $MTKCLIENT_DIR"

  # 检查 flash_with_retry.sh
  if [ ! -x "$FLASH_RETRY_SCRIPT" ]; then
    error "flash_with_retry.sh 不存在或不可执行: $FLASH_RETRY_SCRIPT" 1
  fi
  log "✅ flash_with_retry.sh: $FLASH_RETRY_SCRIPT"

  # 检查日志目录
  mkdir -p "$LOG_DIR" 2>/dev/null || error "无法创建日志目录: $LOG_DIR" 1
  log "✅ 日志: $LOG_FILE"
}

validate_image() {
  local boot_img="$1"

  log "=== 镜像校验 ==="

  # 检查文件存在
  [ -f "$boot_img" ] || error "boot.img 不存在: $boot_img" 1

  # 检查大小
  local boot_size=$(stat -c%s "$boot_img" 2>/dev/null || echo 0)

  log "boot.img: $boot_size bytes ($(numfmt --to=iec-i --suffix=B $boot_size 2>/dev/null || echo $boot_size))"

  if [ "$boot_size" -gt "$MAX_BOOT_SIZE" ]; then
    error "boot.img 超出分区限制: $boot_size > $MAX_BOOT_SIZE (6MB)" 1
  fi

  log "✅ 镜像大小检查通过"
}

backup_boot() {
  log "=== 备份当前 bootimg 分区 ==="

  mkdir -p "$BACKUP_DIR" 2>/dev/null || error "无法创建备份目录: $BACKUP_DIR" 4

  log "备份目录: $BACKUP_DIR"
  log "提示: 请在 10 秒内将 Z1 插入 download 线并按开机键进入 preloader..."
  sleep 10

  cd "$MTKCLIENT_DIR" || error "无法进入 mtkclient 目录" 4

  # 备份 bootimg
  log "备份 bootimg 分区..."
  if timeout 60 "$MTKCLIENT_PY" mtk.py r bootimg "$BACKUP_DIR/bootimg_backup.img" >> "$LOG_FILE" 2>&1; then
    log "✅ bootimg 备份完成: $BACKUP_DIR/bootimg_backup.img"
  else
    log "⚠️ bootimg 备份失败 (可能设备不在 preloader 模式)"
    log "   继续刷写, 但失败时无法自动回滚"
    log "   手动回滚可用原厂镜像: ~/claude/6572/1/boot.img"
  fi

  cd "$PROJECT_ROOT" || exit 1
}

flash_boot() {
  local boot_img="$1"
  local rollback_img="${2:-}"

  log "=========================================="
  log "=== 刷写 boot.img → bootimg 分区 ==="
  log "=========================================="
  log "镜像: $boot_img"
  [ -n "$rollback_img" ] && log "回滚: $rollback_img"
  log ""
  log "⚠️ 请确保 Z1 已连接 download 线并处于 preloader 模式"
  log "   (按住音量键 + 插 USB,或运行 mtkclient 后再按开机键)"
  log ""

  # 使用 flash_with_retry.sh
  if [ -n "$rollback_img" ]; then
    "$FLASH_RETRY_SCRIPT" bootimg "$boot_img" "$rollback_img" "$MAX_RETRIES" 2>&1 | tee -a "$LOG_FILE"
  else
    "$FLASH_RETRY_SCRIPT" bootimg "$boot_img" "" "$MAX_RETRIES" 2>&1 | tee -a "$LOG_FILE"
  fi

  local rc=${PIPESTATUS[0]}
  if [ "$rc" -eq 0 ]; then
    log "✅ boot.img 刷写成功"
    return 0
  else
    error "boot.img 刷写失败 (退出码: $rc)" 2
  fi
}

# ========== 主流程 ==========
main() {
  local boot_img="${1:-}"

  # 参数检查
  if [ -z "$boot_img" ]; then
    echo "用法: $0 <boot.img>"
    echo "  例: $0 out/boot_a9_pie.img"
    echo ""
    echo "说明:"
    echo "  - 仅刷写 Android 9 boot.img 到 Z1"
    echo "  - 不修改 system 分区"
    echo "  - 自动备份当前 boot 分区"
    echo "  - 带重试逻辑和回滚保护"
    echo "  - 日志保存到 mainline_recon/flash_a9_boot_log_*.txt"
    echo ""
    echo "用途:"
    echo "  - 测试新 boot.img 而不重刷整个 system"
    echo "  - 快速验证内核/initramfs 修改"
    exit 1
  fi

  # 转换为绝对路径
  boot_img=$(cd "$(dirname "$boot_img")" && echo "$(pwd)/$(basename "$boot_img")")

  log "=========================================="
  log "=== Android 9 Boot 刷写 - Z1 ==="
  log "=========================================="
  log "boot.img: $boot_img"
  log ""

  # 环境检查
  check_prereq

  # 镜像校验
  validate_image "$boot_img"

  # 备份当前 boot 分区
  backup_boot

  # 刷写 boot.img (使用备份作为回滚镜像)
  local boot_rollback=""
  if [ -f "$BACKUP_DIR/bootimg_backup.img" ]; then
    boot_rollback="$BACKUP_DIR/bootimg_backup.img"
  fi
  flash_boot "$boot_img" "$boot_rollback"

  log ""
  log "=========================================="
  log "✅✅✅ Boot 刷写完成 ✅✅✅"
  log "=========================================="
  log ""
  log "下一步:"
  log "  1. 拔掉 download 线"
  log "  2. 插上电池,按开机键启动 Z1"
  log "  3. 通过串口 (C3 桥 /dev/ttyACM0 921600) 抓取启动日志:"
  log "     cd $PROJECT_ROOT"
  log "     ./z1_capture_mainline.sh 180"
  log ""
  log "回滚 (如果启动失败):"
  if [ -f "$BACKUP_DIR/bootimg_backup.img" ]; then
    log "  - 恢复 boot: tools/flash_with_retry.sh bootimg $BACKUP_DIR/bootimg_backup.img \"\" 8"
  else
    log "  - 恢复 boot: tools/flash_with_retry.sh bootimg ~/claude/6572/1/boot.img \"\" 8"
  fi
  log ""
  log "备份位置: $BACKUP_DIR"
  log "日志位置: $LOG_FILE"
  log "=========================================="
}

# 执行主流程
main "$@"
