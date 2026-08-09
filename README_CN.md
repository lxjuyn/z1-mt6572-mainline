# 佳伯听力机 Z1 —— mainline Linux 移植

> 把 MT6572 听力机 Z1 从原厂内核 3.4.67 迁移到 mainline Linux 7.0-rc7。
> 本仓库是编译树：mainline 7.0-rc7 源码 + Z1 板级适配驱动。

[English README](README.md)

## 设备状态

### 通用组件
| 组件 | JTY D101 | Lenovo A369i | Energy Phone Colors | Prestigio PAP5500 DUO |
|------|----------|--------------|---------------------|-----------------------|
| DRM | 🟢 可用，屏幕电源需改进 | 🟢 可用，屏幕需理顺 | 🟡 部分：屏幕需修复 | 🟢 可用，屏幕电源需改进 |
| 屏幕亮度 | 🔴 死 | 🔴 死 | 🔴 死 | 🟢 可用：pwm-backlight |
| 键盘灯: mediatek,mt6323-led | 🔴 待定 | 🔴 待定 | 🔴 待定 | 🟢 可用 |
| 手电筒: pwm-leds | 🔴 待定 | 🔴 待定 | 🔴 待定 | 🟢 可用: pwm-leds |
| 音量+/-键: mediatek,mt6779-keypad | 🟢 可用 | 🔴 待定 | 🔴 待定 | 🟢 可用 |
| 电源键: mediatek,mt6323-keys | 🟢 可用 | 🟢 可用 | 🔴 待定 | 🟢 可用 |
| 震动: regulator-haptic | 🟢 可用 | 🔴 待定 | 🔴 待定 | 🟢 可用 |
| 充电 | 🔴 死 | 🔴 死 | 🔴 死 | 🔴 死 |

### 各设备组件
| 组件 | JTY D101 | Lenovo A369i | Energy Phone Colors | Prestigio PAP5500 DUO |
|------|----------|--------------|---------------------|-----------------------|
| 触摸屏 | 🔴 死 | 🔴 死 | 🔴 死 | 🟢 可用: goodix,gt911 |
| 屏幕 | 🟡 部分：需电源时序 | 🟢 可用，需改进 | 🟡 部分：需修复 | 🟡 部分 |
| 加速度计 | 🔴 死 | 🔴 死 | 🔴 死 | 🟢 可用: bosch,bma222e (轮询) |
| 环境光/距离 | 🔴 死 | 🔴 死 | 🔴 死 | 🟢 可用: rohm,rpr0400 (轮询) |

## 平台状态
标 `needs upstreaming` 的表示上游不存在，为本 fork 自研。

### CPU
| 组件 | 驱动 | 状态 | 备注 |
|------|------|------|------|
| SMP | arch/arm/mach-mediatek/platsmp.c | 🟢 可用 | |
| cpufreq | drivers/cpufreq/mediatek-cpufreq.c | 🟢 可用 | |
| hotplug | arch/arm/mach-mediatek/platsmp.c | 🟢 可用 | needs upstreaming |
| cpuidle | | 🔴 死 | 可能需新驱动，wfi 或可用 |
| PMU | arm,cortex-a7-pmu | 🔴 死 | 低优先，移植应容易 |

### 定时器
| 组件 | 驱动 | 状态 | 备注 |
|------|------|------|------|
| APXGPT | mediatek,mt6577-timer | 🟢 可用 | |
| arch timer | arm,armv7-timer | 🟢 可用 | needs fix upstreaming |

### 时钟
以下均需 upstreaming

| 组件 | 驱动 | 状态 | 备注 |
|------|------|------|------|
| topckgen | mediatek,mt6572-topckgen | 🟢 可用 | |
| infracfg | mediatek,mt6572-infracfg | 🟢 可用 | |
| apmixed | mediatek,mt6572-apmixedsys | 🟢 可用 | |
| fhctl | apmixed 子集 | 🔴 死 | 不确定是否真需要 |
| mmsys | mediatek,mt6572-mmsys | 🟡 部分 | cg1 部分 dbi 时钟缺失 |
| mfgcfg | mediatek,mt6572-mfgcfg | 🟢 可用 | |
| audio | | 🔴 死 | 暂不需要，无法测试 |

### Pinctrl
缺 emmc r1r0 引脚，需 upstreaming

### 总线
| 组件 | 驱动 | 状态 | 备注 |
|------|------|------|------|
| UART | mediatek,mt6577-uart | 🟢 可用 | |
| I2C | mediatek,mt6572-i2c | 🟢 可用 | needs upstreaming |
| SPI | | 🔴 死 | |
| USB | mediatek,mtk-musb | 🟢 可用 | |
| USB PHY | mediatek,generic-tphy-v1 | 🟢 可用 | |

### 电源
### SoC
| 组件 | 驱动 | 状态 | 备注 |
|------|------|------|------|
| pwrap | mediatek,mt6572-pwrap | 🟢 可用 | needs upstreaming |
| power domain | mediatek,mt6572-power-controller | 🟡 部分 | 仅 disp 和 mfg 两个 pd 可用 |

#### PMIC
| 组件 | 驱动 | 状态 | 备注 |
|------|------|------|------|
| regulators | mediatek,mt6323-regulator | 🟢 可用 | |
| efuse | mediatek,mt6323-efuse | 🟢 可用 | needs upstreaming |
| thermal | mediatek,mt6323-thermal | 🟢 可用 | needs upstreaming, 已在 mt8163 测过 |
| ADC | mediatek,mt6323-auxadc | 🟢 可用 | needs upstreaming, 需清理 |
| fuel gauge | | 🔴 死 | 需新驱动 |

### 存储
用上游驱动

| 组件 | 驱动 | 状态 | 备注 |
|------|------|------|------|
| eMMC | mediatek,mt2701-mmc | 🟢 可用 | 无 PMT 解析器 |
| microSD | mediatek,mt2701-mmc | 🟢 可用 | |
| NAND | | 🔴 死 | 无已知带 NAND 设备 |

### SoC 杂项
| 组件 | 驱动 | 状态 | 备注 |
|------|------|------|------|
| 中断父级 | mediatek,mt6577-sysirq | 🟢 可用 | |
| reset 控制器 | mediatek,mt6572-wdt | 🟢 可用 | needs upstreaming |
| cpu core control | mediatek,mt6572-mcusys | | dummy compatible, 用于 hotplug |
| efuse | mediatek,mt8173-efuse | 🟢 可用 | needs upstreaming |
| ADC | mediatek,mt8173-auxadc | 🟢 可用 | |
| thermal | mediatek,mt6572-thermal | 🟡 部分 | 温度偏高(?), needs upstreaming |

### 显示
| 组件 | 驱动 | 状态 | 备注 |
|------|------|------|------|
| MMSYS | drivers/soc/mediatek/mtk-mmsys.c: mediatek,mt6572-mmsys | 🟡 部分 | 无 cmdq, 需 upstreaming 和路由清理 |
| DRM | drivers/gpu/drm/mediatek/mtk_drm_drm.c: mediatek,mt6572-mmsys | 🟡 部分 | 暂用 mt2701 plat data, 因只有 1 个 rdma 需自建 |
| IOMMU | mediatek,mt6572-m4u | 🟢 可用 | needs upstreaming |
| SMI | mediatek,mt6572-smi-common | 🟢 可用 | needs upstreaming |
| LARB | mediatek,mt6572-smi-larb | 🟢 可用 | needs upstreaming |
| overlay | mediatek,mt6572-disp-ovl | 🟢 可用 | needs upstreaming |
| read DMA | mediatek,mt6572-disp-rdma | 🟢 可用 | needs upstreaming |
| write DMA | | 🔴 死 | 无驱动 |
| BLS | mediatek,mt2701-disp-pwm | 🟡 部分 | 亮度不工作, 需修复和可能的新 compatible |
| color correction | mediatek,mt2701-disp-color | 🟢 可用 | |
| DSI | mediatek,mt6572-dsi | 🟡 部分 | needs upstreaming, drm 初始化前有 vblank 超时 |
| DSI PHY | mediatek,mt2701-mipi-tx | 🟢 可用 | |
| DBI | | 🔴 死 | 无已知 DBI 屏设备, 无法测试/移植 |
| DPI | | 🔴 死 | 无已知 DPI 屏设备, 无法测试/移植 |
| hw mutex | mediatek,mt6572-disp-mutex | 🟡 部分 | needs upstreaming, 缺 mdp ids |
| CMDQ | | 🔴 死 | cmdq 与 gce 差异大, 需大量 drm 改动 |
| GPU | arm,mali-400 | 🟢 可用 | |

## 外部贡献
用于追踪对 kernel fork 的贡献者

- [CustomFirmwareDev](https://github.com/gabin8) — i2c dma 修复, Prestigio PAP5500 DUO 支持

## 已死子系统
无上游支持或需较大工作量

### MDP
| 组件 | 相似驱动 | 备注 |
|------|----------|------|
| read DMA | mediatek,mt8183-mdp3-rdma | |
| resize | mediatek,mt8183-mdp3-rsz | |
| write DMA | mediatek,mt8183-mdp3-wdma | |
| sharpness | mediatek,mt8195-mdp3-tdshp | |

### Camera
上游不存在

### HW 视频编解码
基本无用, 只是流程一部分而非完整硬件引擎, 不值得做

### 连接性 (WiFi/BT)
上游不存在

### 音频
需 afe/i2s 等驱动

### Pericfg
似乎是 NAND 和 USB 的 clock + reset 控制器

### EMI mfd
EMI 有性能监控 + 带宽限制

### HACC
可能不值得做, 相比软件较慢

### devapc
总线违规监控? 不确定是否真需要, 但移植应较容易

### APARM (?)
下游称 APARM_BASE, 映射为 infrasys? 用于 watchpoint 和 breakpoint

---

## Z1 (佳伯听力机) 移植进展

### 硬件
| 项 | 值 |
|---|---|
| SoC | MediaTek MT6572 (Cortex-A7 双核, 1GB RAM) |
| 原厂内核 | 3.4.67 (`#1 SMP hongyao@R630`, gcc 4.7, 2022-08) |
| 存储 | eMMC 4GB (H4G1d 3.64GiB, boot0/boot1/rpmb + p1..p6) |
| 屏幕 | Sitronix ST7785M 240x320 DSI |
| 按键 | 8 键矩阵 + PMIC 电源键 + 3 电容触摸 (返回/主页/多任务) |
| 桥 | ESP32-C3 SuperMini USB-UART ↔ Z1 UART1 (ttyMT0, 115200) |

### 已完成 (上板验证)

1. **CMD1 stuck-busy 根因** — LK 交接 eMMC 时卡处于 busy 态, CMD1 无响应 (resp=0)。
   修法: `mtk-sd.c` probe 里 `msdc_z1_emmc_hw_reset()` 脉冲 BOOTRST 释放卡。
2. **NULL-supply oops** — Z1 dts `&mmc0` 无 `vmmc-supply`, `mmc->supply.vmmc = NULL`,
   但 `msdc_ops_set_ios` 用 `!IS_ERR()` 挡不住 NULL → 崩进 `regulator_set_voltage(NULL)`。
   修法: 5 处 supply guard 改 `IS_ERR_OR_NULL`, `msdc_init_hw` 提出 guard 外。提交 `8bea81fa5`。
3. 实测枚举完整成功: `mmc0: new MMC H4G1d 3.64GiB` + boot0/boot1/rpmb + p1..p6 全注册。

周边驱动 (历史轮):
- WiFi / WMT / BT 外编 .ko 全编通 (wlan_mt6620, mtk_stp_wmt, mtk_hif_sdio 等 7 个)
- 13 键全编进 zImage (矩阵 + PMIC 电源键 + goodix 3 键)
- USB CDC-ACM gadget (musb mediatek + f_acm)
- GPIO/EINT/RTC no-op 桩头 (`stub_includes/`)

### 当前卡点: 块读 R1B busy

枚举成功, 但 `cat /dev/mmcblk0p2` 卡死:

```
mmc0: Card stuck being busy! __mmc_poll_for_busy
mtk-msdc 11120000.mmc: msdc_cmd_done: cmd=6 arg=03B34801; cmd_error=-110
```

- `cmd=6 arg=03B34801` = CMD6 SWITCH 写 `EXT_CSD_PART_CONFIG`(179)=0x48 (每次 I/O 前切 user 分区)
- R1B 响应正常 (rsp 00000900 = TRAN/READY_FOR_DATA), 但响应后 **DAT0 持续为低**
- `msdc_card_busy` 读 `MSDC_PS bit16` = DAT0 电平判断 busy, 等不到释放 → -110
- 已排除: VEMC 掉电 (pwrap 挡写切不掉)、PB1 BUSY_CHECK_SEL、pinctrl default (应用后破坏 CMD1)、
  数据超时 (5s watchdog 未触发)、DMA/clock (枚举已通)

> 排查方向: `MSDC_PS bit16` 是否真是 DAT0 引脚电平 / 卡端真 busy / 电气 pinmux。

### 编译方法

```bash
# zImage + dtb
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- zImage dtbs
# 产物: arch/arm/boot/zImage + arch/arm/boot/dts/mediatek/mt6572-z1.dtb
```

打包 boot.img (appdtb 模式: cat 拼 dtb + mkbootimg 带 --dt):

```bash
cat arch/arm/boot/zImage arch/arm/boot/dts/mediatek/mt6572-z1.dtb > /tmp/zImage_appended
./build/mtkbootimg/mkbootimg --kernel /tmp/zImage_appended \
  --ramdisk <MTK ROOTFS ramdisk.gz> \
  --dt arch/arm/boot/dts/mediatek/mt6572-z1.dtb \
  --base 0x10000000 --kernel_offset 0x8000 --pagesize 2048 --mtk 1 \
  --cmdline "console=tty0 console=ttyS0,115200n8 root=/dev/ram panic=0 ignore_loglevel clk_ignore_unused rdinit=/init" \
  -o boot.img
```

刷写用 mtkclient: `tools/flash_with_retry.sh bootimg <img> <rollback_img> 8`。
> 注意: Z1 插 USB 会干扰 boot (preloader 看 LineState), 刷写才插 download 线, 正常开机拔掉。

### 关键文件

| 文件 | 作用 |
|---|---|
| `drivers/mmc/host/mtk-sd.c` | MSDC eMMC 驱动 (Z1: BOOTRST / IS_ERR_OR_NULL / z1_direct_cid) |
| `arch/arm/boot/dts/mediatek/mt6572-z1.dts` | Z1 板级 dts (mmc0/uart/usb/按键/pmic) |
| `arch/arm/boot/dts/mediatek/mt6572.dtsi` | MT6572 SoC dtsi |
| `drivers/pinctrl/mediatek/pinctrl-mt6572.c` | MT6572 pinmux (R1R0 上拉支持) |
| `stub_includes/` | 桩头 (wakelock/earlysuspend/aee/xlog/mach) |

### 历史提交

```
629d81675 dts: revert pinctrl default for mmc0 - breaks CMD1
d7d5597aa fix: keep eMMC VCC (VEMC) powered - regulator-always-on
d7fc80d98 revert: eMMC PB1 restore under z1_direct_cid - breaks CMD1
7a74f3153 fix: eMMC R1B busy misdetect - restore vendor PB1 under z1_direct_cid
8bea81fa5 fix: eMMC probe NULL-supply oops - gate vmmc/vqmmc with IS_ERR_OR_NULL
7f926bfac fix: full-enumeration eMMC - drop CID-only gates (BOOTRST kept)
37818733b import: mainline 7.0-rc7 + Z1 MT6572 fork 驱动基线
```
