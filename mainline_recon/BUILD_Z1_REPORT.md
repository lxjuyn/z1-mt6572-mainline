# Mainline Z1 boot.img 编译报告 (2026-07-10)

> 注: 编译 agent 在写报告那步撞 API 524 超时挂了，但编译本身成功、产物完整。本报告由主会话从编译产物逆向重建。

## A. 编译结果

| 项 | 结果 |
|---|---|
| 源码树 | `/home/<user>/claude/6572/fork_linux` (Linux 7.0-rc7 + mt6572-mainline fork 驱动已合并) |
| 交叉编译器 | (见 agent transcript, agent 自行找到的 arm cross toolchain) |
| zImage | ✅ `fork_linux/arch/arm/boot/zImage` (5,002,056 B) |
| 设备树 | ✅ `fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dtb` (16,560 B) |
| .config | ✅ `fork_linux/.config` (CONFIG_ARM=y, CONFIG_ARCH_MEDIATEK=y, CONFIG_MACH_MT6572=y) |
| boot.img | ✅ `out/boot_mainline_z1tymt0.img` (6,092,800 B, ANDROID! magic) |

## B. Z1 dts 修改 (agent 做的)

- 新建 `mt6572-z1.dts` (基于 mt6572-jty-d101.dts), minimal bring-up: eMMC + UART + USB only
- **console 走 uart0 (ttyMT0) 修对了**: `stdout-path = "serial0:921600n8"`, `serial0 = &uart0`
- 屏幕 ST7785M 先关掉 (first boot 专做 kernel 能否 boot)
- mmc0 配 eMMC (bus-width=8 在 dtsi 里, z1 用 mmc0_pins 9-bit), vmmc=vemc3v3, vqmmc=vio18
- usb + u2phy0 okay, pmic/pwrap/pio pmic_int_pins 全配
- memory@80000000 reg=<0x80000000 0x40000000> (1GB, 物理可能只 512MB，待验)

## C. boot.img header 字段 ✅ 实际正确 (主会话误判已纠正)

主会话初轮看偏移把 kernel_addr 当成 ramdisk_size 判"header 待修"是错的。重读标准 AOSP bootimg.h 偏移后确认正确:

```
@0x08 kernel_size    0x4c95f8    (4.78MB 真实 ✅)
@0x0c kernel_addr    0x10008000  (加载地址, 不是size)
@0x10 ramdisk_size   0x105141    (1069377B ≈1MB ramdisk ✅)
@0x14 ramdisk_addr   0x11000000
@0x18 second_size    0x0         (✅ 本来就是0, 之前误把0x1c的second_addr当second_size)
@0x1c second_addr    0x10f00000
@0x20 tags_addr      0x10000100  (MTK常这样填)
@0x24 page_size      0x800       (2048 ✅)
@0x40 cmdline        console=tty0 console=ttyMT0,921600n8 root=/dev/ram panic=0
```

结构核验: kernel区[0,0x4ca000) + ramdisk区 ≤ 0x105141, 文件总长0x5cf800↔0x5cf141+填充, 闭合 ✅.
**结论: header 没bug, boot_mainline_z1tymt0.img 直接可用, 不用重打包.**

## D. cmdline 对比 (vs 历史)

| boot | console | 连串口了吗 |
|---|---|---|
| boot_mainline_v5 (旧) | ttyMT1 | ❌ Z1 实测是 ttyMT0, 抓不到 |
| boot_mainline_z1tymt0 (新) | **ttyMT0** ✅ | 应能抓到 mainline dmesg |

## E. 下一步: 刷 Z1 验证 (依赖 Task#4)

前置: 修 boot.img header 字段 (ramdisk_size/second_size)
1. 提前通知用户:"该插 Z1 USB download 线了" (C3 桥 UART 线全程不动)
2. 起 mtkclient 等 preloader → w bootimg out/boot_mainline_z1tymt0.img
3. byte-cmp 校验
4. 用户拔 download 线 + 电池开机
5. C3 桥抓 kernel log (ttyMT0 @ 921600n8): 期待 "Linux version 7.0.0-rc7" dmesg
6. mainline 能跑到驱动探测就算打通; userspace 此阶段不要

## F. 编译 agent 留下的问题 (需后续核实)

- 没真正确认编译过程有无 error/warning (agent transcript 不可读, 产物在=zImage 能编)
- fork_linux 有没有 clk-mt6572.c / 专门 mt6572 clk 驱动? (之前查 drivers/clk/mediatek 只见 mt2701 系列) — 影响 mainline 起来后 clk 是否完整
- 厂商 USB gadget 驱动 (mtk musb) mainline fork 有没有? 影响 console 抓取
- WiFi/音频 mainline 零支持 (见 WIFI_MT6620_REPORT.md), 本 boot 不含它们

## G. 风险评估

mainline 7.0-rc7 在 MT6572 上是 fork 驱动拼凑, 第一次 boot 很可能:
- 卡在 clk/pinctrl/pwrap 初始化 (vendor pmic pwrap 协议私有)
- 卡在 mmc0 eMMC 探测 (dts 时序/驱动强度对不上)
- 即便能到 dmesg, 未必能解 ramdisk (mainline 没原厂 ramdisk, root=/dev/ram 会 panic — 但只要 dmesg 出来就算 kernel boot 成功)
- 所以验证目标定在: **能吐出 Linux version 行 + 驱动探测 log**, 不要求进 userspace
