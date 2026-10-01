#!/bin/bash
# flash_a9_complete.sh — 完整刷写 Android 9 (boot.img + system.img) 到 Z1
#
# 功能:
#   - 刷 boot.img → bootimg 分区 (6MB)
#   - 刷 system.img → android 分区 (p4, 900MB)
#   - 带重试逻辑 (8 次, 指数退避)
#   - 刷写前校验镜像大小
#   - 刷写前备份当前分区
#   - 全程日志记录到 mainline_recon/flash_a9_log_TIMESTAMP.txt
#
# 用法: flash_a9_complete.sh <boot.img> <system.img>
#   例: tools/flash_a9_complete.sh out/boot_a9_pie.img out/system_a9_fixed.img
#
# 退出码:
#   0 = 全部成功
#   1 = 参数错误 / 镜像大小超限 / mtkclient 不可用
#   2 = boot 刷写失败
#   3 = system 刷写失败
#   4 = 备份失败

set -u

# ========== 配置 ==========
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROJECT_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
MTKCLIENT_DIR="$PROJECT_ROOT/mtkclient_v2141"
MTKCLIENT_PY="$PROJECT_ROOT/mtkclient_venv/bin/python"
FLASH_RETRY_SCRIPT="$SCRIPT_DIR/flash_with_retry.sh"
BACKUP_DIR="$PROJECT_ROOT/bridge_backups/a9_pre_flash_$(date +%Y%m%d_%H%M%S)"
LOG_DIR="$PROJECT_ROOT/mainline_recon"
LOG_FILE="$LOG_DIR/flash_a9_log_$(date +%Y%m%d_%H%M%S).txt"

# 分区限制 (字节)
MAX_BOOT_SIZE=6291456        # 6MB (bootimg 分区)
MAX_SYSTEM_SIZE=943718400    # 900MB (android 分区 p4)

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

validate_images() {
  local boot_img="$1"
  local system_img="$2"

  log "=== 镜像校验 ==="

  # 检查文件存在
  [ -f "$boot_img" ] || error "boot.img 不存在: $boot_img" 1
  [ -f "$system_img" ] || error "system.img 不存在: $system_img" 1

  # 检查大小
  local boot_size=$(stat -c%s "$boot_img" 2>/dev/null || echo 0)
  local system_size=$(stat -c%s "$system_img" 2>/dev/null || echo 0)

  log "boot.img:   $boot_size bytes ($(numfmt --to=iec-i --suffix=B $boot_size 2>/dev/null || echo $boot_size))"
  log "system.img: $system_size bytes ($(numfmt --to=iec-i --suffix=B $system_size 2>/dev/null || echo $system_size))"

  if [ "$boot_size" -gt "$MAX_BOOT_SIZE" ]; then
    error "boot.img 超出分区限制: $boot_size > $MAX_BOOT_SIZE (6MB)" 1
  fi

  if [ "$system_size" -gt "$MAX_SYSTEM_SIZE" ]; then
    error "system.img 超出分区限制: $system_size > $MAX_SYSTEM_SIZE (900MB)" 1
  fi

  log "✅ 镜像大小检查通过"
}

backup_partitions() {
  log "=== 备份当前分区 ==="

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
    log "⚠️ bootimg 备份失败 (可能设备不在 preloader 模式, 继续刷写)"
  fi

  # 备份 android (system)
  log "备份 android 分区 (可能需要较长时间, 最多 300 秒)..."
  if timeout 300 "$MTKCLIENT_PY" mtk.py r android "$BACKUP_DIR/android_backup.img" >> "$LOG_FILE" 2>&1; then
    log "✅ android 分区备份完成: $BACKUP_DIR/android_backup.img"
  else
    log "⚠️ android 分区备份失败或超时 (继续刷写, 可用原厂镜像回滚)"
  fi

  cd "$PROJECT_ROOT" || exit 1
  log "✅ 备份阶段完成 (即使部分失败也继续刷写)"
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

flash_system() {
  local system_img="$1"
  local rollback_img="${2:-}"

  log "=========================================="
  log "=== 刷写 system.img → android 分区 ==="
  log "=========================================="
  log "镜像: $system_img"
  [ -n "$rollback_img" ] && log "回滚: $rollback_img"
  log ""
  log "⚠️ android 分区较大 (900MB), 刷写需要较长时间"
  log "   每次重试最多等待 600 秒,请耐心等待"
  log ""

  # 使用 flash_with_retry.sh (android 分区大, 单次超时可能需更长)
  if [ -n "$rollback_img" ]; then
    "$FLASH_RETRY_SCRIPT" android "$system_img" "$rollback_img" "$MAX_RETRIES" 2>&1 | tee -a "$LOG_FILE"
  else
    "$FLASH_RETRY_SCRIPT" android "$system_img" "" "$MAX_RETRIES" 2>&1 | tee -a "$LOG_FILE"
  fi

  local rc=${PIPESTATUS[0]}
  if [ "$rc" -eq 0 ]; then
    log "✅ system.img 刷写成功"
    return 0
  else
    error "system.img 刷写失败 (退出码: $rc)" 3
  fi
}

# ========== 主流程 ==========
main() {
  local boot_img="${1:-}"
  local system_img="${2:-}"

  # 参数检查
  if [ -z "$boot_img" ] || [ -z "$system_img" ]; then
    echo "用法: $0 <boot.img> <system.img>"
    echo "  例: $0 out/boot_a9_pie.img out/system_a9_fixed.img"
    echo ""
    echo "说明:"
    echo "  - 完整刷写 Android 9 到 Z1 (boot + system)"
    echo "  - 自动备份当前分区"
    echo "  - 带重试逻辑和回滚保护"
    echo "  - 日志保存到 mainline_recon/flash_a9_log_*.txt"
    exit 1
  fi

  # 转换为绝对路径
  boot_img=$(cd "$(dirname "$boot_img")" && echo "$(pwd)/$(basename "$boot_img")")
  system_img=$(cd "$(dirname "$system_img")" && echo "$(pwd)/$(basename "$system_img")")

  log "=========================================="
  log "=== Android 9 完整刷写 - Z1 ==="
  log "=========================================="
  log "boot.img:   $boot_img"
  log "system.img: $system_img"
  log ""

  # 环境检查
  check_prereq

  # 镜像校验
  validate_images "$boot_img" "$system_img"

  # 备份当前分区
  backup_partitions

  # 刷写 boot.img (使用备份作为回滚镜像)
  local boot_rollback=""
  if [ -f "$BACKUP_DIR/bootimg_backup.img" ]; then
    boot_rollback="$BACKUP_DIR/bootimg_backup.img"
  fi
  flash_boot "$boot_img" "$boot_rollback"

  log ""
  log "⏸️  boot.img 刷写完成, 等待 10 秒让设备复位..."
  sleep 10

  # 刷写 system.img (使用备份作为回滚镜像)
  local system_rollback=""
  if [ -f "$BACKUP_DIR/android_backup.img" ]; then
    system_rollback="$BACKUP_DIR/android_backup.img"
  fi
  flash_system "$system_img" "$system_rollback"

  log ""
  log "=========================================="
  log "✅✅✅ Android 9 刷写全部完成 ✅✅✅"
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
    log "  - 恢复 boot:   tools/flash_with_retry.sh bootimg $BACKUP_DIR/bootimg_backup.img \"\" 8"
  fi
  if [ -f "$BACKUP_DIR/android_backup.img" ]; then
    log "  - 恢复 system: tools/flash_with_retry.sh android $BACKUP_DIR/android_backup.img \"\" 8"
  fi
  log ""
  log "备份位置: $BACKUP_DIR"
  log "日志位置: $LOG_FILE"
  log "=========================================="
}

# 执行主流程
main "$@"
