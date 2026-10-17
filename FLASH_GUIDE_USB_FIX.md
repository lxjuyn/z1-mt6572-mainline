# USB Gadget 修复刷写指南 (2026-08-16 19:57)

## 修复内容
- **根因**: `z1_adb_setup.sh` UDC 名称错误 (`musb-hdrc` → `11100000.usb`)
- **产物**: 
  - `out/boot_mainline_z1_stable.img` (5.8MB) — boot 分区
  - `out/system_alpine.img` (850MB) — p4 (android) 分区

## 刷写步骤

### 1. 准备
```bash
cd /home/<user>/claude/6572
# 启动 mtkclient 等待
./mtkclient_venv/bin/python mtkclient_v2141/mtk.py
```

### 2. 插下载线 + 按开机键
- 插 USB 下载线到 Z1
- 按住开机键进入 preloader/BROM 模式
- mtkclient 应该检测到设备

### 3. 刷 boot.img (新终端)
```bash
cd /home/<user>/claude/6572
tools/flash_with_retry.sh bootimg out/boot_mainline_z1_stable.img /home/<user>/claude/6572/1/boot.img 8
```

### 4. 刷 system_alpine.img (p4 分区)
```bash
tools/flash_with_retry.sh android out/system_alpine.img 2>/dev/null 8
```
注意: p4 分区 GPT 真名可能是 `android` 或 `system`，如果失败用:
```bash
# 查看分区表
./mtkclient_venv/bin/python mtkclient_v2141/mtk.py r gpt /tmp/gpt.bin
# 然后手动刷
./mtkclient_venv/bin/python mtkclient_v2141/mtk.py w android out/system_alpine.img
```

### 5. 拔下载线，正常开机
- 拔掉 USB 下载线 (避免 preloader LineState 干扰)
- 按开机键正常启动
- 等待 Alpine 启动完成 (约 30-60 秒)

### 6. 抓 log 验证
```bash
cd /home/<user>/claude/6572
./z1_capture_mainline.sh 180
```

### 7. 检查 USB gadget
进入 Alpine shell 后:
```bash
# 检查 OpenRC 服务状态
rc-service adbd status

# 手动运行 (如果服务没起)
sh /usr/local/sbin/z1_adb_setup.sh

# 检查 /dev/ttyGS0
ls -l /dev/ttyGS0

# 检查 UDC
ls /sys/class/udc/
cat /sys/kernel/config/usb_gadget/z1/UDC

# 诊断 (如果失败)
dmesg | grep -i 'udc\|gadget\|acm\|musb'
cat /var/log/adbd.log
```

### 8. 主机端测试
```bash
# 主机插 USB 线
adb devices
# 应该看到设备

# 串口测试
cat /dev/ttyGS0
```

## 回滚
如果刷写失败或启动失败:
```bash
# 回滚 boot.img
tools/flash_with_retry.sh bootimg /home/<user>/claude/6572/1/boot.img 2>/dev/null 8

# 回滚 system_alpine.img (如果有备份)
tools/flash_with_retry.sh android /home/<user>/claude/6572/out/system_alpine.img.bak.* 2>/dev/null 8
```

## 预期结果
- Alpine 启动成功: "Welcome to Alpine Linux 3.24, Kernel 7.0.0-rc7-z1+"
- `/dev/ttyGS0` 存在
- `adb devices` 可见设备
- `adb shell` 可以进入 Alpine shell
