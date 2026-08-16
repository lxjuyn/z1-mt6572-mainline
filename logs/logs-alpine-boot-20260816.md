# Alpine Linux 3.24 armv7 启动成功 (2026-08-16)

## 里程碑
Z1 MT6572 成功启动 Alpine Linux 3.24 armv7 发行版，从原厂 Android 系统切换到完整 Linux 发行版。

## 技术细节

### 启动方式
- **分区**: p4 (android, offset 0x04340000, 900MB)
- **rootfs**: `out/system_alpine.img` (40MB ext4)
- **boot.img**: `out/boot_mainline_z1_stable.img` (5.8MB)
- **initramfs**: 包含 `z1_alpine_switch_init.sh` 脚本，自动 switch_root 到 p4

### 内核配置
- Kernel 7.0.0-rc7-z1+
- vermagic: `7.0.0-rc7-z1+` (CONFIG_LOCALVERSION="-z1")
- Console: `ttyS0,115200n8` (CH340 串口适配器)

### USB 子系统状态
所有 USB 驱动 probe 成功：
- USB PHY: 0.097s probe 成功 (曾有 -517 EPROBE_DEFER，后在 9.2s 解决)
- MUSB: 6.516s probe 成功
- g_serial ACM: 6.610s probe 成功，内核日志显示 "g_serial ready"

### 已知问题
**USB gadget `/dev/ttyGS0` 不存在**
- 内核日志显示 g_serial ready
- 但 `/dev/ttyGS0` 设备节点未创建
- 可能原因：
  1. USB PHY slew-rate 校准超时 (非致命警告)
  2. VBUS dummy supply 警告
  3. configfs 挂载时序问题
- 需要进一步调查

### 关键脚本
1. **z1_alpine_switch_init.sh**: initramfs `/init`，负责 switch_root 到 p4
2. **z1_deploy_alpine_rootfs.sh**: Alpine rootfs 部署到 p4 分区
3. **z1_adb_setup.sh**: USB gadget configfs + adbd 启动 (f_fs 顺序修复)

### 产物归档
`bridge_backups/products/products_alpine_boot_20260816_153821/`
- boot_mainline_z1_stable.img
- system_alpine.img
- .config
- z1_adb_setup.sh
- z1_alpine_switch_init.sh
- boot log

## 下一步
1. 修复 USB gadget `/dev/ttyGS0` 问题
2. 验证 ADB 交互
3. 测试发行版启动后的非重要驱动
4. 集成更多用户空间工具

## 参考
- CLAUDE.md §路D — Alpine Linux 发行版启动
- Git commit: b0f9d1ae0 (alpine: add ADB setup + rootfs deployment scripts)
