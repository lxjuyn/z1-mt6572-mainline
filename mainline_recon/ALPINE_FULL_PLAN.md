# Alpine Linux 完整实施计划

> 2026-08-16 创建，Z1 MT6572 Alpine 发行版启动完整路线图

## 当前状态 (2026-08-16 晚间)

### ✅ 已完成
1. **Alpine 3.24 armv7 启动成功** (2026-08-16)
   - p4 分区 (android, 0x04340000, 900MB) 作为 rootfs
   - initramfs switch_root 逻辑工作正常
   - 启动成功显示 "Welcome to Alpine Linux 3.24, Kernel 7.0.0-rc7-z1+"

2. **关键脚本已实现并提交**
   - `scripts/z1_alpine_switch_init.sh` (7638 bytes) - initramfs /init，switch_root 到 p4
   - `scripts/z1_deploy_alpine_rootfs.sh` (3555 bytes) - Alpine rootfs 部署到 p4
   - `scripts/z1_adb_setup.sh` (3015 bytes) - USB gadget configfs + adbd 启动

3. **产物归档**
   - `bridge_backups/products/products_alpine_boot_20260816_153821/`
   - boot_mainline_z1_stable.img (5.8MB)
   - system_alpine.img (40MB ext4)

### ❌ 当前卡点
**USB gadget `/dev/ttyGS0` 不存在**
- 内核日志显示 `g_serial ready`
- 但 `/dev/ttyGS0` 设备节点未创建
- ADB 无法使用
- 可能原因：
  1. USB PHY slew-rate 校准超时（非致命警告）
  2. VBUS dummy supply 警告
  3. configfs 挂载时序问题
  4. UDC 绑定失败

## 实施计划

### Phase 1: USB Gadget 修复（优先级：高）
**目标**: 让 `/dev/ttyGS0` 正常工作

**步骤**:
1. **调查 configfs 挂载时序**
   - 检查 `z1_adb_setup.sh` 中 configfs 挂载点
   - 确认 `/sys/kernel/config/usb_gadget/` 存在
   - 验证 UDC 绑定逻辑

2. **添加 USB PHY 调试**
   - 在 PHY probe 添加详细日志
   - 检查 slew-rate 校准失败原因
   - 验证 VBUS supply 状态

3. **测试 UDC 手动绑定**
   - 手动 `echo "11100000.usb" > UDC` 测试
   - 检查 `dmesg` 错误信息
   - 验证 `/dev/ttyGS0` 是否创建

4. **修复脚本**
   - 根据调查结果修复 `z1_adb_setup.sh`
   - 添加重试逻辑和错误处理
   - 确保 configfs 正确挂载

**产物**:
- 修复后的 `z1_adb_setup.sh`
- USB gadget 调试日志
- 上板验证结果

### Phase 2: ADB 集成验证（优先级：高）
**目标**: ADB 可以正常连接和交互

**步骤**:
1. **ADB 连接测试**
   - `adb devices` 检查设备列表
   - `adb shell` 进入 Alpine shell
   - 验证基本命令执行

2. **ADB 文件传输**
   - `adb push/pull` 测试
   - 验证大文件传输稳定性

3. **ADB over USB vs TCP**
   - 测试 USB ADB 连接
   - 可选：配置 TCP ADB（`adb tcpip 5555`）

**产物**:
- ADB 测试报告
- ADB 连接脚本（如需要）

### Phase 3: 驱动部署（优先级：中）
**目标**: 在 Alpine 中加载 CONNSYS 驱动

**步骤**:
1. **驱动模块部署**
   - 将 5 个 CONNSYS .ko 复制到 Alpine rootfs
   - 创建 `/lib/modules/7.0.0-rc7-z1+/` 目录
   - 设置 `modprobe` 配置

2. **驱动加载脚本**
   - 创建 `scripts/z1_modprobe_connsys.sh`
   - 按依赖顺序加载：
     - mtk_hif_sdio.ko
     - mtk_stp_wmt.ko
     - mtk_wmt_wifi.ko
     - wlan_mt6620.ko
     - mtk_stp_bt.ko / mtk_stp_uart.ko / mtk_stp_gps.ko

3. **WiFi/BT 测试**
   - `ifconfig -a` 检查 wlan0
   - `iwconfig` 检查无线接口
   - `hciconfig` 检查蓝牙接口

**产物**:
- 驱动部署脚本
- WiFi/BT 测试报告

### Phase 4: 用户空间工具（优先级：低）
**目标**: 完善 Alpine 用户空间

**步骤**:
1. **网络配置**
   - 配置 `/etc/network/interfaces`
   - 设置 DNS (`/etc/resolv.conf`)
   - 测试 `ping` 和 `wget`

2. **包管理器**
   - `apk update` 测试
   - 安装常用工具：`vim`, `htop`, `wireless-tools`

3. **系统服务**
   - 配置 OpenRC 服务
   - 设置开机自启动

**产物**:
- 网络配置脚本
- 系统服务配置

## 关键文件清单

### 脚本
- `scripts/z1_alpine_switch_init.sh` ✅
- `scripts/z1_deploy_alpine_rootfs.sh` ✅
- `scripts/z1_adb_setup.sh` ✅ (待修复 USB gadget)
- `scripts/z1_modprobe_connsys.sh` ⏳ (待创建)

### 产物
- `out/boot_mainline_z1_stable.img` ✅
- `out/system_alpine.img` ✅
- `out/boot_mainline_z1_adb.img` ⏳ (待创建，含 USB gadget 修复)

### 文档
- `CLAUDE.md` §路D ✅
- `logs/logs-alpine-boot-20260816.md` ✅
- `mainline_recon/ALPINE_FULL_PLAN.md` ✅ (本文档)

## 下一步行动

1. **立即**: 调查 USB gadget `/dev/ttyGS0` 问题
   - 派 agent 分析 configfs 挂载和 UDC 绑定逻辑
   - 检查 PHY slew-rate 校准失败原因
   - 修复 `z1_adb_setup.sh`

2. **明天早上**: 上板验证
   - 刷 `boot_mainline_z1_adb.img`（含 USB gadget 修复）
   - 测试 ADB 连接
   - 验证 Alpine 启动

3. **后续**: 驱动部署 + 用户空间完善

## 参考
- CLAUDE.md §路D — Alpine Linux 发行版启动
- Git commits: 5e1472cbc, b0f9d1ae0, 0e190b339
- 产物归档: `bridge_backups/products/products_alpine_boot_20260816_153821/`
