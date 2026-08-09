#!/bin/bash
# 监听 Z1 原厂启动阶段；先明确把 C3 UART1 恢复到 115200。
# 检测到 "Jump to BL" 后，再将桥 UART1 切到 921600。
PORT="${C3_PORT:-/dev/ttyACM0}"
LOG="${Z1_AUTOSWITCH_LOG:-./z1_auto_switch.log}"
: > "$LOG"

[ -c "$PORT" ] || { echo "ERROR: $PORT 不存在"; exit 1; }

stty -F "$PORT" 115200 raw -echo -echoe -echok -echoctl -echoke 2>/dev/null
printf '[[BR:115200]]' > "$PORT" 2>/dev/null
sleep 0.4
timeout 1 cat "$PORT" > /dev/null 2>&1

echo "[auto] 监听就绪, 桥UART1=115200, 等Z1复位. pid=$$" | tee -a "$LOG"
( timeout 240 cat "$PORT" >> "$LOG" 2>/dev/null ) &
CAT=$!
echo "$CAT" > /tmp/z1_cap.pid

LAST_SWITCH=0
for i in $(seq 1 240); do
  sleep 1
  NOW=$(grep -a -c "Jump to BL" "$LOG" 2>/dev/null || true)
  NOW=${NOW:-0}
  if [ "$NOW" -gt "$LAST_SWITCH" ] 2>/dev/null; then
    printf '[[BR:921600]]' > "$PORT" 2>/dev/null
    echo "[auto] $(date +%T) 检测到 Jump to BL #${NOW}, 桥UART1已切921600" >> "$LOG"
    LAST_SWITCH=$NOW
  fi
  SZ=$(wc -c < "$LOG" 2>/dev/null || echo 0)
  echo "[auto] ${i}s log=${SZ}B JumpToBL=${NOW}" >> "$LOG"
done

kill "$CAT" 2>/dev/null || true
echo "[auto] 结束. 总字节=$(wc -c < "$LOG")" >> "$LOG"
