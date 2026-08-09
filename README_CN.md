# 佳伯听力机 Z1 —— mainline Linux 移植

> 把 MT6572 听力机 Z1 从原厂内核 3.4.67 迁移到 mainline Linux 7.0-rc7。
> 本仓库是编译树：mainline 7.0-rc7 源码 + Z1 板级适配驱动。

## 硬件

| 项 | 值 |
|---|---|
| SoC | MediaTek MT6572 (Cortex-A7 双核, 1GB RAM) |
| 原厂内核 | 3.4.67 (`#1 SMP hongyao@R630`, gcc 4.7, 2022-08) |
| 存储 | eMMC 4GB (H4G1d 3.64GiB, boot0/boot1/rpmb + p1..p6) |
| 屏幕 | Sitronix ST7785M 240x320 DSI |
| 按键 | 8 键矩阵 + PMIC 电源键 + 3 电容触摸 (返回/主页/多任务) |
| 桥 | ESP32-C3 SuperMini USB-UART ↔ Z1 UART1 (ttyMT0, 115200) |

## 已完成

### eMMC 枚举修复（已上板验证）

1. **CMD1 stuck-busy 根因** — LK 把 eMMC 交接给内核时卡处于 busy 态，CMD1 无响应（resp=0）。
   修法：`mtk-sd.c` 在 probe 里 `msdc_z1_emmc_hw_reset()` 脉冲 BOOTRST 释放卡。
2. **NULL-supply oops** — Z1 dts `&mmc0` 无 `vmmc-supply`，`mmc->supply.vmmc = NULL`，
   但 `msdc_ops_set_ios` 用 `!IS_ERR()` 挡不住 NULL → 崩进 `regulator_set_voltage(NULL)`。
   修法：5 处 supply guard 改 `IS_ERR_OR_NULL`，`msdc_init_hw` 提出 guard 外。提交 `8bea81fa5`。
3. 实测枚举完整成功：`mmc0: new MMC H4G1d 3.64GiB` + boot0/boot1/rpmb + p1..p6 全注册。

### 周边驱动（历史轮）

- WiFi / WMT / BT 外编 .ko 全部编通（wlan_mt6620, mtk_stp_wmt, mtk_hif_sdio 等 7 个）
- 13 键全编进 zImage（矩阵 + PMIC 电源键 + goodix 触摸 3 键）
- USB CDC-ACM gadget（musb mediatek + f_acm）
- GPIO/EINT/RTC no-op 桩头（`stub_includes/`）

## 当前卡点：块读 R1B busy

枚举成功，但 `cat /dev/mmcblk0p2` 卡死：

```
mmc0: Card stuck being busy! __mmc_poll_for_busy
mtk-msdc 11120000.mmc: msdc_cmd_done: cmd=6 arg=03B34801; cmd_error=-110
```

- `cmd=6 arg=03B34801` = CMD6 SWITCH 写 `EXT_CSD_PART_CONFIG`(179)=0x48（每次 I/O 前切 user 分区）
- R1B 响应正常（rsp 00000900 = TRAN/READY_FOR_DATA），但响应后 **DAT0 持续为低**
- `msdc_card_busy` 读 `MSDC_PS bit16`=DAT0 电平判断 busy，等不到释放 → -110
- 已排除：VEMC 掉电（pwrap 挡写切不掉）、PB1 BUSY_CHECK_SEL、pinctrl default（应用后破坏 CMD1）、
  数据超时（5s watchdog 未触发）、DMA/clock（枚举已通）

> 排查方向：`MSDC_PS bit16` 是否真是 DAT0 引脚电平 / 卡端真 busy / 电气 pinmux。

## 编译方法

```bash
# zImage + dtb
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- zImage dtbs
# 产物: arch/arm/boot/zImage + arch/arm/boot/dts/mediatek/mt6572-z1.dtb
```

打包 boot.img（appdtb 模式：cat 拼 dtb + mkbootimg 带 --dt）：

```bash
cat arch/arm/boot/zImage arch/arm/boot/dts/mediatek/mt6572-z1.dtb > /tmp/zImage_appended
./build/mtkbootimg/mkbootimg --kernel /tmp/zImage_appended \
  --ramdisk <MTK ROOTFS ramdisk.gz> \
  --dt arch/arm/boot/dts/mediatek/mt6572-z1.dtb \
  --base 0x10000000 --kernel_offset 0x8000 --pagesize 2048 --mtk 1 \
  --cmdline "console=tty0 console=ttyS0,115200n8 root=/dev/ram panic=0 ignore_loglevel clk_ignore_unused rdinit=/init" \
  -o boot.img
```

刷写用 mtkclient：`tools/flash_with_retry.sh bootimg <img> <rollback_img> 8`。
> 注意：Z1 插 USB 会干扰 boot（preloader 看 LineState），刷写才插 download 线，正常开机拔掉。

## 关键文件

| 文件 | 作用 |
|---|---|
| `drivers/mmc/host/mtk-sd.c` | MSDC eMMC 驱动（Z1 适配：BOOTRST / IS_ERR_OR_NULL / z1_direct_cid） |
| `arch/arm/boot/dts/mediatek/mt6572-z1.dts` | Z1 板级 dts（mmc0/uart/usb/按键/pmic） |
| `arch/arm/boot/dts/mediatek/mt6572.dtsi` | MT6572 SoC dtsi |
| `drivers/pinctrl/mediatek/pinctrl-mt6572.c` | MT6572 pinmux（R1R0 上拉支持） |
| `stub_includes/` | 桩头（wakelock/earlysuspend/aee/xlog/mach） |

## 历史提交

```
629d81675 dts: revert pinctrl default for mmc0 - breaks CMD1
d7d5597aa fix: keep eMMC VCC (VEMC) powered - regulator-always-on
d7fc80d98 revert: eMMC PB1 restore under z1_direct_cid - breaks CMD1
7a74f3153 fix: eMMC R1B busy misdetect - restore vendor PB1 under z1_direct_cid
8bea81fa5 fix: eMMC probe NULL-supply oops - gate vmmc/vqmmc with IS_ERR_OR_NULL
7f926bfac fix: full-enumeration eMMC - drop CID-only gates (BOOTRST kept)
37818733b import: mainline 7.0-rc7 + Z1 MT6572 fork 驱动基线
```
