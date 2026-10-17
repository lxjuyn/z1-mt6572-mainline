# Z1 移植项目 — 2026-07-10 凌晨收工摘要

## 今晚达成的三个里程碑

### 1. C3 USB-UART1 桥烧成并跑通
- ESP32-C3 SuperMini 固件已烧好（IDF 6.2, 173KB）。桥透传: PC ↔ USB-CDC ↔ C3 ↔ UART1(921600) ↔ Z1 ttyMT0
- 双向交互通：能往 Z1 shell 发命令并收到回显
- 修复: watchdog(portMAX_DELAY), BUFSZ(64K→1K malloc失败), XMC flash brick(锁0xC0)
- 脚本: `/home/<user>/claude/6572/z1_auto_switch.sh`(自动切速率), `z1cmd.sh`(发命令)

### 2. kernel UART log 打开（Z1 从黑盒变可见）
- **根因**: LK 量产静默追加 `printk.disable_uart=1`, 无去重, boot.img cmdline 写=0 被覆盖
- **解决**: patch lk.bin 两字节(offset 211725+213760 各 `0x31→0x30`), 让 LK 追加 `=0`
- flash 现状: uboot(lk)=`out/lk_uart0patched.bin`, bootimg=`out/boot_v2_uart0.img`(v2原厂ramdisk+ttyMT0)
- kernel 完整 boot log 存档: `z1_lkpatch_test.log`（1.4MB，panic全文）, `z1_v2test.log`（进shell）

### 3. Z1 进 shell `~ #`（可交互 Linux 设备）
- v2 ramdisk 好的, 原厂 kernel 3.4.67 健康。eMMC SanDisk 4GB 识别, PMIC MT6323 全活, 双核SMP
- 崩溃真因(之前v6 boot_uart0 ramdisk坏的): `Initramfs unpacking failed → VFS: Unable to mount root fs`
- 交互: `picocom /dev/ttyACM0` 或 `z1cmd.sh "命令"` 往 Z1 发命令
- **注意**: Z1 USB 接电脑会干扰 boot（重启循环），必须拔掉 boot。刷写才插。两态分开

## 三条关键教训
1. **printk.disable_uart=1 来自 LK 硬编码**，改 boot.img 没用，要 patch lk.bin
2. **Z1 拔 USB 才能正常 boot**（MTK preloader 看 USB LineState 判模式）
3. **别自己 gzip 重压 cpio ramdisk**（kernel 解不开），用原厂字节原样

## mainline clone 进度
- mt6572-mainline/linux fork: ❌ 网络TLS错误无法clone
- torvalds Linux 7.0-rc7: ✅ 已clone到 `mainline_recon/torvalds/` (2GB)
- fork 多余驱动文件(clock/pinctrl/mt6323/dts): ✅ 已抓取到 `mainline_recon/fork_files/`
- 完整评估报告: `mainline_recon/REPORT.md`

## Z1 当前状态
- 电池大概率耗尽（16分钟+没电），**正在充电**
- flash 上好: uboot=patched(lk_uart0patched), bootimg=v2_uart0(boot_v2_uart0)
- 明天充好电 → 拔USB → 开机 → C3桥抓 → 进 shell

## 下一步（明天继续）
- 关掉 kernel battery/WDT 噪音（`echo 3 > /proc/sys/kernel/printk`）
- 进 shell 后挂 Alpine (`mmcblk0p4` → chroot)
- 配 USB RNDIS 让 Z1 进网
- mainline: fork 驱动 + torvalds 7.0-rc7 合成 → 写 Z1 DTS → 编译试 boot
