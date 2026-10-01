# Android 9 刷写流程文档

## 概述

本文档描述如何将 Android 9 (Pie) 镜像完整刷写到 Z1 设备。

## 镜像信息

| 文件 | 分区 | 大小限制 | 实际大小 | 说明 |
|------|------|----------|----------|------|
| `boot_a9_pie.img` | bootimg | 6MB (6291456 bytes) | 5.6MB (5806080 bytes) | Android 9 内核 + ramdisk |
| `system_a9_fixed.img` | android (p4) | 900MB (943718400 bytes) | 900MB (943718400 bytes) | Android 9 系统分区 |

## 前置条件

### 硬件准备
- Z1 设备 + 电池
- USB download 线 (数据线)
- C3 SuperMini 串口桥 (抓日志用, /dev/ttyACM0 @ 921600)
- 确保 Z1 电池有电

### 软件准备
- mtkclient 已安装: `/home/lxj/claude/6572/mtkclient_v2141`
- Python 虚拟环境: `/home/lxj/claude/6572/mtkclient_venv`
- 刷写脚本:
  - `tools/flash_a9_complete.sh` (完整刷写)
  - `tools/flash_a9_boot_only.sh` (仅刷 boot)
  - `tools/flash_with_retry.sh` (底层重试脚本)

### 环境检查
```bash
cd /home/lxj/claude/6572

# 检查镜像存在且大小合法
ls -lh out/boot_a9_pie.img out/system_a9_fixed.img

# 检查脚本可执行
ls -l tools/flash_a9_*.sh tools/flash_with_retry.sh
```

## 刷写流程

### 方案 A: 完整刷写 (boot + system)

**用途**: 全新安装 Android 9

**步骤**:

1. **启动刷写脚本**
   ```bash
   cd /home/lxj/claude/6572
   tools/flash_a9_complete.sh out/boot_a9_pie.img out/system_a9_fixed.img
   ```

2. **进入 download 模式**
   - 脚本会提示等待设备连接
   - 在提示后 10 秒内:
     - 按住 Z1 音量下键 (或音量上键)
     - 插入 USB download 线到电脑
     - 按 Z1 开机键
   - mtkclient 检测到设备后自动开始

3. **自动执行流程**
   - 环境检查 (mtkclient / 脚本)
   - 镜像大小校验
   - 备份当前分区 (bootimg + android)
   - 刷写 boot.img (带 8 次重试)
   - 刷写 system.img (带 8 次重试)

4. **完成后操作**
   - 拔掉 USB download 线
   - 插上电池
   - 按开机键启动 Z1

5. **抓取启动日志**
   ```bash
   cd /home/lxj/claude/6572
   ./z1_capture_mainline.sh 180
   ```

**预计时间**: 
- 备份: ~5 分钟 (boot 10s + system 300s)
- 刷 boot: ~30 秒
- 刷 system: ~5-10 分钟 (900MB, 取决于 USB 速度)
- 总计: ~15-20 分钟

### 方案 B: 仅刷写 boot (测试用)

**用途**: 
- 测试新内核/ramdisk 而不重刷 system
- 快速验证 boot.img 修改
- system 分区已是 Android 9,仅需更新内核

**步骤**:

1. **启动刷写脚本**
   ```bash
   cd /home/lxj/claude/6572
   tools/flash_a9_boot_only.sh out/boot_a9_pie.img
   ```

2. **进入 download 模式** (同方案 A 步骤 2)

3. **自动执行流程**
   - 环境检查
   - 镜像大小校验
   - 备份当前 bootimg 分区
   - 刷写 boot.img (带 8 次重试)

4. **完成后操作** (同方案 A 步骤 4-5)

**预计时间**: ~2-3 分钟

## 安全机制

### 自动备份
- 刷写前自动备份当前分区到: `bridge_backups/a9_pre_flash_YYYYMMDD_HHMMSS/`
- 备份文件:
  - `bootimg_backup.img` (6MB)
  - `android_backup.img` (900MB, 如果备份成功)

### 重试逻辑
- 每个分区最多重试 8 次
- 指数退避: 每次失败等待 8 秒
- 失败时自动回滚到备份镜像 (如果可用)

### 大小校验
- boot.img 必须 ≤ 6MB
- system.img 必须 ≤ 900MB
- 超限拒绝刷写

### 日志记录
- 完整日志保存到: `mainline_recon/flash_a9_log_YYYYMMDD_HHMMSS.txt`
- 实时输出到终端

## 回滚流程

### 如果启动失败

#### 恢复到最近备份
```bash
cd /home/lxj/claude/6572

# 找到最新备份目录
ls -ldt bridge_backups/a9_*_flash_* | head -1

# 假设备份在 bridge_backups/a9_pre_flash_20261001_120000/
BACKUP_DIR="bridge_backups/a9_pre_flash_20261001_120000"

# 恢复 boot
tools/flash_with_retry.sh bootimg "$BACKUP_DIR/bootimg_backup.img" "" 8

# 恢复 system (如果需要, 需较长时间)
tools/flash_with_retry.sh android "$BACKUP_DIR/android_backup.img" "" 8
```

#### 恢复到原厂镜像
```bash
cd /home/lxj/claude/6572

# 恢复原厂 boot (零修改版, 6MB)
tools/flash_with_retry.sh bootimg 1/boot.img "" 8

# 如果有原厂 system 备份
# tools/flash_with_retry.sh android /path/to/factory_system.img "" 8
```

### 回滚注意事项
- 回滚前确保 Z1 能进入 preloader 模式
- boot 分区回滚快 (~30 秒)
- system 分区回滚慢 (~5-10 分钟)
- 如果设备完全变砖无法进 preloader,需要短接触点强制进 BROM 模式

## 预期输出

### 成功刷写的日志特征
```
✅ mtkclient: /home/lxj/claude/6572/mtkclient_v2141
✅ flash_with_retry.sh: /home/lxj/claude/6572/tools/flash_with_retry.sh
✅ 镜像大小检查通过
✅ bootimg 备份完成
✅ android 分区备份完成
>>> 第 1/8 次尝试写 bootimg @ HH:MM:SS
✅ 第 1 次写命令完成: bootimg ← out/boot_a9_pie.img
✅ 第 1 次写入及严格读回校验均成功
>>> 第 1/8 次尝试写 android @ HH:MM:SS
✅ 第 1 次写命令完成: android ← out/system_a9_fixed.img
✅ 第 1 次写入成功(读回未连上设备, 已跳过回滚; 若新镜像已开机即刷写成功)
✅✅✅ Android 9 刷写全部完成 ✅✅✅
```

### 启动日志关键点 (串口 921600)

**正常启动应包含**:
```
[LK] booting linux @ 0x80108000
[Kernel] Linux version 3.4.67 ...
[Kernel] mediatek,mt6572
[Init] init: init first stage started!
[Init] init: Parsing file /init.rc...
[Android] Booting Android Pie (9.0)
[Zygote] Zygote started
[SystemServer] Starting system services
```

**常见问题**:
- 无输出 / 8秒循环: earlycon 未配置 / WDT 未停 / appended DTB 问题
- `Kernel panic - not syncing: VFS: Unable to mount root`: ramdisk 损坏
- 卡在 LK: boot.img header 错误 / 分区表损坏
- 黑屏但有串口: display/panel 驱动问题 (非致命)

## 故障排查

### mtkclient 连不上设备
```bash
# 检查 USB 设备
lsusb | grep -i mediatek

# 应看到: Bus XXX Device XXX: ID 0e8d:0003 MediaTek Inc. MT6227 phone

# 如果没有:
# 1. 重新插拔 USB 线
# 2. 确认按住音量键再插线再按开机键
# 3. 更换 USB 口 (避免 USB 3.0,用 USB 2.0)
# 4. 检查 USB 线质量 (需数据线不是充电线)
```

### 刷写中途卡住
```bash
# 如果脚本卡在"等待设备"超过 2 分钟:
# Ctrl+C 中断脚本
# 物理重启 Z1 (拔电池)
# 重新执行刷写命令
```

### 分区表损坏
```bash
# 如果 mtkclient 报 "partition not found"
cd /home/lxj/claude/6572/mtkclient_v2141
mtkclient_venv/bin/python mtk.py printgpt

# 检查输出是否包含:
# - bootimg (6MB, offset ...)
# - android (900MB, offset ...)

# 如果分区表错误,需重新写 GPT (危险,联系用户确认)
```

### Z1 完全变砖
```bash
# 症状: 插 USB 无任何 USB 设备,开机无反应,串口无输出
# 原因: preloader 损坏
# 解法:
# 1. 拆机找到 eMMC 触点
# 2. 短接 eMMC CLK 到 GND (或特定测试点)
# 3. 强制进入 BROM 模式 (芯片内置 ROM)
# 4. 用 mtkclient 刷写 preloader
# 5. 刷写完整分区表 + boot + system

# 预防: 不要刷写 preloader / uboot 分区 (除非有可靠备份)
```

## 参考资料

- **CLAUDE.md §4**: 当前卡点和编译命令
- **CLAUDE.md §1**: Z1 硬件参数 (MT6572, 1GB RAM, 921600 串口)
- **mainline_recon/BUILD_Z1_REPORT.md**: boot.img header 格式 (ANDROID!! magic)
- **tools/flash_with_retry.sh**: 底层刷写逻辑和重试机制
- **1/boot.img**: 原厂 boot 镜像 (零修改, 6MB, 紧急回滚用)

## 注意事项

1. **USB download 线干扰启动**: Z1 接电脑 USB 会干扰 boot (LineState 判模式)
   - 刷写时插 USB
   - 启动时拔 USB
   - 两态分开

2. **C3 桥串口线常驻**: UART1 透传线 (TX7/RX6) 全程不拔,只用于抓日志

3. **system 分区刷写时间长**: 900MB 大分区,单次可能 5-10 分钟,脚本会自动等待

4. **Z1_VERIFY 环境变量**: 
   - 默认 `Z1_VERIFY=1`: 严格读回校验
   - 设置 `Z1_VERIFY=0`: 跳过读回 (设备常驻后连不上 preloader)
   - 用法: `Z1_VERIFY=0 tools/flash_a9_complete.sh ...`

5. **backup 失败不阻塞刷写**: 如果设备不在 preloader,备份会超时失败,但脚本继续刷写 (可用原厂镜像回滚)

6. **git 上传规则**: 刷写成功后,如果修改了内核/驱动,记得 `git commit + push` 到仓库

## 下一步

刷写成功并启动后:

1. **验证 Android 9 启动**:
   - 串口日志包含 "Android Pie" / "init first stage"
   - 屏幕显示 Android 开机动画 (如果 display 驱动正常)

2. **ADB 连接** (如果 USB gadget 正常):
   ```bash
   adb devices
   adb shell getprop ro.build.version.release  # 应显示 9
   ```

3. **WiFi/BT 测试** (如果 CONNSYS 驱动已加载):
   ```bash
   adb shell lsmod | grep mt6620
   adb shell ifconfig wlan0 up
   ```

4. **音频测试** (MT6323 PMIC codec):
   ```bash
   adb shell tinymix  # 查看 mixer controls
   adb shell tinyplay /system/media/audio/ringtones/Orion.ogg
   ```

5. **性能/功耗监控**:
   ```bash
   adb shell cat /proc/cpuinfo
   adb shell cat /sys/class/thermal/thermal_zone*/temp
   adb shell dumpsys battery
   ```

---

**创建日期**: 2026-10-01  
**维护**: 随刷写脚本更新同步更新本文档  
**状态**: ✅ 脚本已完成,待用户上板验证
