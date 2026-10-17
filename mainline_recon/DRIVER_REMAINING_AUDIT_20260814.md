# Z1 剩余外围驱动只读审计 (2026-08-14)

> 审计方式：纯只读。未改源码 / 未编译 / 未碰硬件。
> 依据：
> - 上板 log：`mainline_recon/z1_boot_mainline_20260814_021713.log`（Linux version `7.0.0-rc7-z1+` #91，boot 到 shell，末尾 CONNSYS insmod 段有 mtk_wmtd crash，详见 §9）
> - 源码：`fork_linux`（git HEAD `e3198bbc3`）+ `.config` + `arch/arm/boot/dts/mediatek/mt6572-z1.dts` / `mt6572.dtsi` / `mt6323.dtsi`
> - 交叉验证文档：`PERIPHERAL_VERIFY_CHECKLIST_20260814.md`（上一 boot 002746 的报告，电池/auxadc 部分已被本 log 推翻，见 §8）、`EMMC_REMAINING_OFFLINE_20260814.md`、`DRIVER_RESEARCH_SUMMARY_20260801.md`、`slim_clk_reg_mfd_lc.md`
> - 注意：log 是二进制混合文件，grep 必须加 `-a`。

## 结论速览

| # | 设备 | 结论 | 一句话 |
|---|---|---|---|
| 1 | ST7785M 显示 (DRM) | **缺失(源码就绪, 未使能)** | panel 驱动已写、DRM MEDIATEK 全 mt6572 支持在树内；CONFIG_DRM 全关 + dts `panel@0`/`&dsi` disabled，当前靠 simplefb |
| 2 | 音频 (MT6323+MT6572 AFE) | **缺失** | AFE/codec/machine 骨架已写；因占位 GIC_SPI 0 IRQ 挂死 boot，`CONFIG_SOUND` 整体关闭；另 MFD diag 模式不建 codec cell |
| 3 | RTC (mt6323-rtc) | **缺失** | 驱动已编 (`RTC_DRV_MT6397`)，MFD diag 模式不建 rtc cell + 驱动强制要 IRQ → 无 rtc0 |
| 4 | WATCHDOG (mtk-wdt) | **可用** | `MEDIATEK_WATCHDOG=y`，上板已启用 15s，内核 worker 喂狗，无复位 |
| 5 | GPIO/LED | **缺失** | LEDS_GPIO 驱动已编，dts 无 gpio-leds 节点、板载状态灯 GPIO 未知 |
| 6 | USB host 模式 | **不适用(待硬件确认)** | dts dr_mode=peripheral；Z1 听力机无 OTG/主机口（仅 micro-USB 充电/数据），g_serial device 模式工作 |
| 7 | UART 其它口 | **部分可用** | 仅 uart0→ttyS0(console) 启用；uart1(ttyS1) 在 dtsi disabled；无 uart2/3 节点；mainline 无 ttyMT* 名字 |
| 8 | eMMC 剩余 | **可用** | boot0/boot1/rpmb 已注册；pre_eol_info/life_time 已解码，上板经 sysfs 直接读 |

---

## 1. ST7785M 显示 panel（DRM 路径）

### 状态：面板驱动已在树内 ✅ / DRM 整体未使能 ❌

**Panel 驱动**：`drivers/gpu/drm/panel/panel-sitronix-st7785m.c` 已存在（项目自写，compatible `sitronix,st7785m`，**真实 init 表已从 LK 二进制提取** `mainline_recon/ST7785M_INIT_LK_20260810.h`，140MHz/1-lane/video mode/RGB565/240x320 时序已固化）。Kconfig `DRM_PANEL_SITRONIX_ST7785M`（tristate，depends OF，select DRM_MIPI_DSI）。probe 对 `vcc-supply` / `reset-gpios` 是 optional（无也能 probe 打日志），带 `drm_panel_of_backlight`。

**DRM 栈对 mt6572 的源码支持（全在）**：
- `mtk_drm_drv.c:336` `mediatek,mt6572-mmsys`（复用 `mt2701_mmsys_driver_data`，DRM master）
- `mtk_dsi.c:1283/1318` `mediatek,mt6572-dsi`（`mt6572_dsi_driver_data`）
- `mtk_disp_ovl.c:785` / `mtk_disp_rdma.c:469` mt6572 版本
- `drivers/soc/mediatek/mtk-mutex.c:1157` `mt6572-disp-mutex`（CONFIG_MTK_MMSYS=y 已编）
- `drivers/phy/mediatek/phy-mtk-mipi-dsi.c` 支持 `mt2701-mipi-tx`（dtsi `mipi_tx0` 用 `mt7623-mipi-tx`+`mt2701-mipi-tx`）
- `drivers/pmdomain/mediatek/mtk-pm-domains.c:1122` `mediatek,mt6572-power-controller`（DIS/MFG/CONN 域，CONFIG_MTK_SCPSYS_PM_DOMAINS=y）
- `drivers/iommu/mtk_iommu_v1.c:632` `mediatek,mt6572-m4u`

**当前 .config（缺口）**：`CONFIG_DRM` **未开**；`CONFIG_DRM_MEDIATEK` **未开**；`CONFIG_DRM_PANEL_SITRONIX_ST7785M` **未开**；`CONFIG_PHY_MTK_MIPI_DSI` **未开**；`CONFIG_MTK_IOMMU_V1` **未开**。已开且上板活跃：`CONFIG_MTK_MMSYS=y`（log：`probe of 14000000.syscon returned 0`）、`CONFIG_MTK_SCPSYS_PM_DOMAINS=y`（log：`probe of 10006000.power-controller:power-controller returned 0`，DIS 域已注册且 sync_state 挂 larb/iommu/gpu）。

**dts（缺口）**：`mt6572.dtsi` `&dsi` 节点 `status="disabled"`；`mt6572-z1.dts` `&dsi { panel@0 { status="disabled"; } }`（注释自述是占位）。当前显示靠 `chosen/framebuffer0` simplefb（log：`fb0: simplefb registered, 240x320x16`）。

### 启用 DRM 路径 checklist

**① .config 新增（`scripts/config --enable` 后 `make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- olddefconfig && make`，注意增量编要 `make clean` 后重建相关 .o）**：
- `CONFIG_DRM=y`
- `CONFIG_DRM_MEDIATEK=y`（自动 select DRM_MIPI_DSI / DRM_PANEL / DRM_GEM_DMA_HELPER / DRM_KMS_HELPER / DRM_DISPLAY_HELPER / DRM_BRIDGE_CONNECTOR / DRM_CLIENT_SELECTION；depends MTK_MMSYS / COMMON_CLK / HAVE_ARM_SMCCC / OF — 前两项已在）
- `CONFIG_DRM_PANEL_SITRONIX_ST7785M=y`
- `CONFIG_PHY_MTK_MIPI_DSI=y`（mipi_tx0 phy 必需，否则 dsi probe 拿不到 phy）
- `CONFIG_MTK_IOMMU_V1=y`（**建议**：ovl/rdma 节点带 `iommus=<&iommu ...>`，无 iommu driver 时 of_iommu_configure 可能把 probe 无限 defer；mt6572-m4u 在树内，开上最稳。若实测 DRM 不用 IOMMU 可再关）

**② dts 改动**：
- `mt6572.dtsi`：`&dsi` 去 `status="disabled"`（→ `"okay"`）
- `mt6572-z1.dts`：`&dsi { panel@0 { status="okay"; } }`
- **删/禁 `chosen/framebuffer0` simplefb 节点**（否则 DRM 与 simplefb 都注册 fb0，帧缓冲打架；DRM 接管后 LK 留下的 0xbfd00000 surface 语义要复核）
- 面板电源/复位：panel 驱动 optional `vcc-supply` + `reset-gpios`。DSI 引脚 GPIO79-84（WORKFLOW_FINDINGS_SUMMARY）已连 ST7785M，但 ST7785M 的 reset/AVDD 具体 GPIO 需板级证据；无证据可先不加（probe 仍会过，先看 DSI 能不能 up）
- 背光：`drm_panel_of_backlight` 需要 backlight 节点（`bls: pwm@1400a000` disp-pwm 在 dtsi 是 disabled，面板无背光可先不管，验证 DSI 画面优先）
- 复核 `power-controller@10006000` 节点 status（当前 dtsi 未写 disabled，正常）

**③ 上板验证**：
```sh
dmesg | grep -iE "mediatek-drm|mtk_dsi|st7785m|disp-ovl|disp-rdma|mipi_tx|panel"
ls /dev/dri/                  # 期望 card0 / renderD128
ls /sys/class/drm/            # 期望 card0-DSI-1 等
cat /sys/class/drm/card0-DSI-1/modes
# 闪屏/screenshot: cat /sys/class/graphics/fb0/...（或 DRM client）确认 240x320 内容
```

**结论：缺失（源码全就绪，未使能）。** 工作量 = config 5 项 + dts 3 处 + 上板调 DSI 时序（H-timing 仍是 ST7789V 族默认，注释标 TODO）。

---

## 2. 音频（MT6323 codec + MT6572 AFE）

### 状态：骨架驱动已写，整体关闭 ❌（无新进展，且被上一轮上板挂死回退）

**源码**（全部已存在）：
- `sound/soc/mediatek/mt6572/`：`mt6572-afe-pcm.c`（AFE 平台驱动，port 自 mt2701 基础，0x11140000/32-bit）+ `mt6572-afe-reg.h` + `mt6572-machine.c`（"Minimal ASoC machine skeleton"，DPCM FE/BE 注释 + COMP_CODEC(NULL,"mt6323-snd-codec-aif1")）
- `sound/soc/codecs/mt6323.c`（**219 行骨架**：DAPM 全是 NOPM，无真实寄存器编程序列；bit 语义需从 vendor `AudDrv_Kernel.c` 反推，MT6323 无 datasheet 是硬 blocker）
- Kconfig：`SND_SOC_MT6572`（depends ARCH_MEDIATEK，select SND_SOC_MEDIATEK）、`SND_SOC_MT6323`（depends MFD_MT6397）都在。

**当前 .config**：`CONFIG_SOUND` **未开**（整棵 ALSA 关）。原因（commit `0da8b21c4`）：`mt6572-afe-pcm` probe 对占位 IRQ `GIC_SPI 0` 调 `request_threaded_irq` 失败并**楔死 kernel_init**（实测 155s backtrace，shell 起不来）→ 回退关闭全部 SND/ASoC。

**dts**：`z1.dts` `audsys: audio@11140000` status="okay"，但 `interrupts = <GIC_SPI 0 IRQ_TYPE_LEVEL_HIGH>` 是 **无效占位**（GIC SPI 从 32 起，0 非法）；`sound: sound` machine 节点 status="okay"，`mediatek,audio-codec = <&codec>`（mt6323.dtsi 的 codec 子节点）。AFE probe 代码对 `platform_get_irq` 负值已做 warn+continue 兜底，但 dts 里塞了 `GIC_SPI 0` 会让它拿到 "0"（非负）→ 走进 `request_irq` → 挂死。

**其它缺口**：
- MFD diag 模式（`CONFIG_MFD_MT6323_NO_IRQ_DIAGNOSTIC=y`）的 `mt6323_diagnostic_devs[]` **只有 regulator + auxadc**（`drivers/mfd/mt6397-core.c:163-178`），`mt6323-sound`（"mediatek,mt6323-codec"）cell **不创建** → 即便开 CONFIG_SND，codec 无 platform device。
- pwrap 写禁止：`CONFIG_MTK_PMIC_WRAP_Z1_WRITE_DENY` **未开**（已解除）✅，codec 寄存器写通道理论上通。
- MT6323 audio 寄存器 bit 定义缺失（CLAUDE.md 已标 audio clock 标 DEAD，需 datasheet）。

**修复方向**：
1. dts 修 IRQ：把 `audsys` 的 `interrupts` 删掉或改成真实 ASYS IRQ（未知则先**删掉 interrupts 属性**，让 `platform_get_irq` 返回 -ENXIO 走 warn+continue 路径，AFE 不挂）。
2. 开 `CONFIG_SOUND=y` + `CONFIG_SND_SOC_MT6572=y` + `CONFIG_SND_SOC_MT6323=y`。
3. `mt6397-core.c` diag devs 补 `mt6323-sound` cell。
4. 补 codec 真实寄存器（SPK_CON0@0x52、AUDTOP_CON0-9@0x700、ABB_AFE@0x4000）——工作量最大，数据手册缺失下只能逆向 vendor 源。
5. machine 卡 FE/BE 链路 + AFE memif/DL1/VUL 时钟。

**上板验证**（等修复后）：
```sh
cat /proc/asound/cards
aplay -l; arecord -l
speaker-test -t wav -l 1   # 期望耳机/扬声器出声（Z1 是听力机无喇叭则接耳机）
dmesg | grep -iE "asoc|snd_soc|mt6572-afe|mt6323"
```

**结论：缺失。** 骨架在树但整树被上一轮挂死回退；没有"新进展"，卡点=占位 IRQ + MFD cell + 无 datasheet。

---

## 3. RTC（mt6323-rtc）

### 状态：驱动已编，无设备，上板无 rtc0 ❌

**源码/config**：`CONFIG_RTC_DRV_MT6397=y`（`rtc-mt6397.c`，of_match 含 `mediatek,mt6323-rtc`）。log：`mtk_rtc_driver_init` initcall 执行（只注册 driver），**全 log 无 rtc probe、无 rtc0**。

**根因（双层）**：
1. MFD diag 模式 `mt6323_diagnostic_devs[]` 不含 `mt6323-rtc` cell → 内核**从未创建 rtc platform device**。
2. 即使补 cell：`rtc-mt6397.c mtk_rtc_probe` **强制 `platform_get_irq(pdev,0)`，负值直接 `return irq`**；diag 模式无 MT6323 IRQ domain，`DEFINE_RES_IRQ(MT6323_IRQ_STATUS_RTC)` 会解析成无效/无 irq → probe 必败。RTC 只有 alarm 需要 IRQ，读/写时间不需要，属可改的硬依赖。

**dts**：`mt6323.dtsi` `rtc { compatible = "mediatek,mt6323-rtc"; }`（pmic 子节点），z1.dts 无 override。

**修复方向**：
- 方案 A（快）：`mt6397-core.c` diag devs 补 `mt6323-rtc` cell + `rtc-mt6397.c` probe 把 IRQ 变 optional（无 IRQ 时跳过 request_threaded_irq，禁 alarm，只提供 read/set time）。regmap 走 pwrap（`mt6397_chip->regmap`），diag 模式可用。
- 方案 B（正路）：解除 diag 模式（做通 pwrap IRQ controller init）走完整 mt6323_devs，RTC 带 alarm。当前 diag 模式是为绕 pwrap IRQ ctl 初始化失败而设，z1.dts 注释（L297-301）说明 PMIC_EINT=GPIO7 但 IRQ ctl 没做通。

**上板验证**（修复后）：
```sh
ls -l /dev/rtc*
cat /proc/driver/rtc
date -s "2026-08-14 12:00:00"; hwclock -w; hwclock -r   # 掉电后重启 hwclock -r 应保持
```

**结论：缺失。** 双根因（MFD cell 不建 + 驱动硬依赖 IRQ）；修复方向明确、改动小，但需重刷验证。

---

## 4. WATCHDOG（mtk-wdt）

### 状态：已开，上板启用且内核喂狗，无复位 ✅

**config**：`CONFIG_MEDIATEK_WATCHDOG=y`、`CONFIG_WATCHDOG=y`、`CONFIG_WATCHDOG_HANDLE_BOOT_ENABLED=y`、`CONFIG_WATCHDOG_OPEN_TIMEOUT=0`（infinity）。

**dts**：`mt6572.dtsi` `watchdog: watchdog@10007000 { compatible="mediatek,mt6572-wdt"; timeout-sec=15; #reset-cells=1; }`。

**上板行为**（log 原文）：
```
[7.130104] mtk-wdt 10007000.watchdog: Watchdog enabled (timeout=15 sec, nowayout=0)
[7.139298] probe of 10007000.watchdog returned 0 after 10044 usecs
```
WDT 已启用 15s、nowayout=0。`OPEN_TIMEOUT=0` + `HANDLE_BOOT_ENABLED` 下，`watchdog_need_worker()` 的 `(t && !watchdog_active && watchdog_hw_running)` 分支让**内核 worker 主动喂狗**（userspace 不 open 也一直喂），因此 shell 起来后无复位；整段 log 未再出现 preloader 重启 banner。

**上板验证**：
```sh
ls -l /dev/watchdog*                       # 期望 /dev/watchdog0
cat /sys/class/watchdog/watchdog0/identity
cat /sys/class/watchdog/watchdog0/timeout  # 15
wdctl /dev/watchdog
# 被动测试: 不 open watchdog，观察 dmesg worker 心跳即可; 不要轻易停狗测复位
```

**结论：可用。** WDT 在跑但被内核喂住；注意 `nowayout=0`，若未来 userspace 想接管需 watchdog daemon，否则保持内核喂狗即可。

---

## 5. GPIO/LED

### 状态：驱动已编，dts 无节点，板上状态灯未绑定 ❌

**config**：`CONFIG_LEDS_CLASS=y`、`CONFIG_LEDS_GPIO=y`（log：`gpio_led_driver_init` 注册成功），`CONFIG_LEDS_MT6323` 未开。LEDS 驱动全部只注册、无设备可绑。

**dts**：`mt6572-z1.dts` **无 `gpio-leds` 节点**（grep 0 命中）；`mt6572.dtsi` 无 leds。参考板 `mt6572-prestigio-pap5500-duo.dts` 有 `pwm-leds`/`&mt6323_leds`，但 Z1 不用。

**硬件**：Z1 有状态指示 LED（`slim_clk_reg_mfd_lc.md` 明言 `CONFIG_LEDS_GPIO=y - GPIO LEDs (Z1 status indicator)` 是保留项），但 **LED 具体 GPIO 未记录/未证实**。

**修复方向**：
- 需要板级证据确认 LED GPIO 号（万用表/原理图/原厂驱动 blib），然后 dts 加：
  ```dts
  leds {
      compatible = "gpio-leds";
      z1_status: led-0 {
          label = "z1:status";
          gpios = <&pio N 0>;   /* N = 实测 GPIO，active-low 则 <&pio N 1> */
          default-state = "off";
          linux,default-trigger = "heartbeat";   /* 或 timer */
      };
  };
  ```
- 若灯在 MT6323 上则走 `mt6323_leds`（需 diag devs 补 leds cell + `CONFIG_LEDS_MT6323=y`），但首选 GPIO 灯。

**上板验证**：
```sh
ls /sys/class/leds/
echo 1 > /sys/class/leds/z1:status/brightness
echo 0 > /sys/class/leds/z1:status/brightness
```

**结论：缺失。** 框架全在，缺硬件 GPIO 证据 + dts 节点。

---

## 6. USB host 模式

### 状态：device-only，物理上无 OTG/主机口 → host 模式不适用 ✅（设备模式工作）

**dts**：`&usb { dr_mode="peripheral"; }`（z1.dts L287-290）+ `&u2phy0` okay。config：`CONFIG_USB_MUSB_GADGET=y`（host 未开）、`CONFIG_USB_DWC2` 未开。

**硬件**：Z1 是"听力机"，`AUDIO_ADAPTATION_PLAN.md` 明确"**Z1 没有 USB OTG 或 USB host（需硬件确认）**"。USB 口是 micro-USB（充电/调试/数据），无 OTG 引脚识别/无 A/B 口 → host 模式在 Z1 无物理载体。上板 log：`musb-hdrc.3.auto` probe 0 + `g_serial gadget.0: g_serial ready`（device 模式工作正常，插线出 ttyGS0）。

**修复方向**：无需改。若未来发现 Z1 确有可做 host 的口（当前无证据），才需 `dr_mode="otg"`/`"host"` + `CONFIG_USB_MUSB_HOST=y` + USB 主机栈。

**上板验证**（当前 device 模式）：
```sh
# 内核起来后插 USB 线
ls -l /dev/ttyGS0
cat /dev/ttyGS0 &        # 主机侧 echo hello 应回显
```

**结论：不适用（待硬件最终确认）。** 无 OTG/主机口，dr_mode=peripheral 正确；设备端 g_serial 已就绪。

---

## 7. UART 其它口

### 状态：仅 uart0（ttyS0, console）启用；uart1 待启用；无 uart2/3；mainline 无 ttyMT* ✅/部分

**dts**：`mt6572.dtsi` 只有 `uart0: serial@11005000`（z1.dts 置 okay，console）和 `uart1: serial@11006000`（**disabled**，无 z1 override）；**没有 uart2/uart3 节点**。两节点 compatible `mediatek,mt6572-uart, mediatek,mt6577-uart`，clock-names "baud","bus"。

**上板**（log）：
```
Serial: <SERIAL> driver, 5 ports, IRQ sharing enabled
11005000.serial: <SERIAL> at MMIO 0x11005000 (irq = 56, base_baud = 1625000) is a ST16650V2
```
仅 ttyS0 有真实 MMIO 设备（console=`console=ttyS0,115200n8`，即 C3 桥接的那路）。另外 4 个 `serial8250:0-4` 是 8250 legacy 占位，无实设备。`CONFIG_SERIAL_8250_MT6577=y`、`CONFIG_SERIAL_8250_NR_UARTS=5`。

**说明**：vendor 内核叫 ttyMT0/1/2/3，mainline 用 8250 栈，设备名统一 ttyS*，不存在 ttyMT*。C3 桥物理接的是 Z1 uart0（0x11005000，现在 console 就跑这路）。

**修复方向**：若 Z1 有第二路物理 UART（BT/调试头，需板证），把 `&uart1 { status="okay"; }` 加进 z1.dts（compatible/时钟/中断都在 dtsi 已备好）。

**上板验证**：
```sh
ls -l /dev/ttyS*
cat /proc/tty/driver/serial
# 启用 uart1 后: stty -F /dev/ttyS1 115200; (对端短接 loopback 测回显)
```

**结论：部分可用。** ttyS0=console 在用；ttyS1 在 dtsi disabled 待启用；ttyS2/3 无节点；ttyMT 命名在 mainline 不存在。

---

## 8. eMMC 剩余（健康/寿命读取）

### 状态：全注册，健康数据已解码，上板经 sysfs 直接读 ✅

**上板**（log）：
```
mmc0: Host Software Queue enabled
mmc0: new MMC card at address 0001
mmcblk0: mmc0:0001 H4G1d 3.64 GiB
mmcblk0: p1 < p5 p6 > p2 p3 p4
mmcblk0boot0: ... 4.00 MiB
mmcblk0boot1: ... 4.00 MiB
mmcblk0rpmb: ... 4.00 MiB, chardev (241:0)
```
（`EMMC_REMAINING_OFFLINE_20260814.md` 已审计确认 boot 不自动碰 boot0/1/RPMB，只注册设备；boot 有标准 mainline EXT_CSD 配置写但不写用户区。）

**健康读取**：`drivers/mmc/core/mmc.c:648-651` 在 boot 解码 `PRE_EOL_INFO`(EXT_CSD[267]) 与 `DEVICE_LIFE_TIME_EST_TYP_A/B`(EXT_CSD[268/269]) 存入 `card->ext_csd`；`mmc.c:805-808` 导出 sysfs 属性 `pre_eol_info` / `life_time`。

**上板验证命令**（只读，无硬件动作）：
```sh
cat /sys/block/mmcblk0/device/pre_eol_info
#   0x01 = Normal, 0x02 = WornOut 80%, 0x03 = Critical, 0x04 = EWR Over（0x00 = undefined）
cat /sys/block/mmcblk0/device/life_time
#   两字节 "typA typB"（0x00=undefined, 0x01=0-10%, ..., 0x0a=90-100%）
# 或 mmc-utils（若 rootfs 有）:
mmc extcsd read /dev/mmcblk0 | grep -iE "PRE_EOL|LIFE_TIME"
```
- 读 boot0/boot1：`dd if=/dev/mmcblk0boot0 of=/tmp/boot0.img bs=512 count=8192`（设备 force_ro，只读安全）。
- RPMB：char 设备 `241:0`，仅 `MMC_DRV_OP_IOCTL_RPMB` ioctl 可访问（需 RPMB key/auth）。

**结论：可用。** 分区/健康已注册解码，读法现成；注意当前 eMMC 健康 = 旧卡（H4G1d），上板读出 pre_eol_info 后按值判断是否需降级为只读归档。

---

## 9. 附：本 boot log 中的 CONNSYS 段 crash（非本次 8 项，仅记录）

log 末尾 `mtk_wmtd` 进程在 `hal_btif_dma_hw_init [mtk_btif_drv]` 处 NULL 崩（Call trace：`hal_btif_dma_hw_init ← _btif_init ← btif_open ← mtk_wcn_btif_open ← mtk_wcn_consys_stp_btif_open`）。本 log（02:17）早于 git HEAD `e3198bbc3`（"fix: btif ranges + dts initrd-end sync"，即 btif 节点补空 `ranges;` 修 EMI 翻译的修复，见 `BTIF_EMI_FIX_REVIEW_20260814.md`），**源码已修、未重刷验证**。下次刷机确认 BTIF 不再崩。

---

## 附：关键文件索引

| 文件 | 说明 |
|---|---|
| `fork_linux/drivers/gpu/drm/panel/panel-sitronix-st7785m.c` | ST7785M panel（自带 LK 真实 init 表） |
| `fork_linux/drivers/gpu/drm/mediatek/mtk_dsi.c` (L1283) | mt6572-dsi 支持 |
| `fork_linux/drivers/mfd/mt6397-core.c` (L163-178, L413) | MFD diag 模式只建 regulator+auxadc cell（RTC/keys/codec 全跳过） |
| `fork_linux/drivers/rtc/rtc-mt6397.c` (L250-270) | rtc probe 硬依赖 IRQ |
| `fork_linux/sound/soc/codecs/mt6323.c` | 219 行 codec 骨架（NOPM） |
| `fork_linux/sound/soc/mediatek/mt6572/` | AFE+AFE-reg+machine 骨架 |
| `fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts` | audsys GIC_SPI 0 占位 IRQ(L218) / panel@0 disabled / simplefb / goodix disabled |
| `fork_linux/arch/arm/boot/dts/mediatek/mt6572.dtsi` | &dsi disabled(L610) / uart1 disabled / m4u(larb0 iommus) |
| `fork_linux/arch/arm/boot/dts/mediatek/mt6323.dtsi` | rtc/codec/keys 子节点（无 cell 则不实例化） |
