# Z1 剩余驱动只读评估：ST7785M DRM / 音频 / RTC (2026-08-14)

> 评估方式：纯只读。未改任何源码 / 未编译 / 未碰硬件。
> 依据：`fork_linux` 当前树（含 `panel-sitronix-st7785m.c`、`sound/soc/mediatek/mt6572/*`、`sound/soc/codecs/mt6323.c`、`drivers/rtc/rtc-mt6397.c`、`drivers/mfd/mt6397-core.c`）、`.config`、`mt6572-z1.dts` / `mt6572.dtsi` / `mt6323.dtsi`、既有审计 `DRIVER_REMAINING_AUDIT_20260814.md`。
> 本评估对审计报告做了源码级复核，并补充了关键实现细节（MFD cell 的 IRQ resource 语义、platform_get_irq 优先级、mmsys→drm/clk 设备链、MM 时钟 ID 一致性、IOMMU 是否必需）。

## 结论速览

| # | 子系统 | 状态 | 最小启用改动量 | 下次上板建议 |
|---|---|---|---|---|
| 1 | ST7785M DRM | 源码全就绪，未使能 | config 5 项 + dts 3 处 + DSI 时序验证 | 单独一趟专门做（动 simplefb，有回归风险） |
| 2 | 音频（AFE+MT6323） | 骨架在树，整树关闭 | dts 删 1 行 IRQ + MFD 补 1 cell + config 3 项 | **与 RTC 一起做**（框架级验证，无声音） |
| 3 | RTC (mt6323-rtc) | 驱动已编，无设备 | MFD 补 1 cell + rtc 驱动改 1 处 | **与音频一起做**（最易、收益明确） |

---

## 1. ST7785M DRM 显示

### 状态：面板驱动在树（项目自写）✅ / DRM 栈源码级全链支持 ✅ / 当前未使能 ❌

**面板驱动** `fork_linux/drivers/gpu/drm/panel/panel-sitronix-st7785m.c`（445 行，`compatible="sitronix,st7785m"`）：
- 真实 init 表（22 条命令）已从 LK 二进制提取（注释引 `mainline_recon/ST7785M_INIT_LK_20260810.h`），非 ST7789V 默认；`sleep_delay`/`MADCTL`/`COLMOD=0x06`/gamma 等全部来自 LK 实测。
- 时序：240x320 / 1-lane / RGB565 / video+burst+LPM，V-timing 与 LK 实测一致（vtotal=337）；**H-timing 仍是 ST7789V 族默认占位**（源码 L312-317 TODO 注释）→ 上板最大不确定项。
- `vcc-supply` / `reset-gpios` 均为 optional（`devm_regulator_get_optional`/`devm_gpiod_get_optional`），无 DT 属性也能 probe；带 `drm_panel_of_backlight`。
- Kconfig `DRM_PANEL_SITRONIX_ST7785M`：tristate，`depends on OF`，`select DRM_MIPI_DSI`。

**DRM 栈对 mt6572 的源码支持（逐项复核，全在树且链完整）**：
- **主链**：`drivers/soc/mediatek/mtk-mmsys.c` `of_match_mtk_mmsys` 含 `mediatek,mt6572-mmsys`（数据 `mt6572_mmsys_driver_data`，`is_vppsys` 未置位 → **会**创建 `mediatek-drm` 平台设备）；`mtk_drm_drv.c:1281` `mtk_drm_platform_driver` 的 `.driver.name="mediatek-drm"`（无 of_match，靠平台设备名绑定）。`CONFIG_MTK_MMSYS=y` 已编且上板 log 显示 `14000000.syscon probe 0`。
- **MM 时钟**：`mtk_mmsys_probe` 同时创建 `clk-mt6572-mm` 设备；`drivers/clk/mediatek/clk-mt6572-mm.c` 注册 `CLK_MM_DSI_ENGINE(22)` / `CLK_MM_DSI_DIGITAL(23)`，与 dtsi `&dsi` 的 `clocks=<&mmsys CLK_MM_DSI_ENGINE/DIGITAL>` 及 `dt-bindings/clock/mediatek,mt6572-clk.h` 的 ID 一一对应 ✅。
- **DSI**：`mtk_dsi.c:839` `mediatek,mt6572-dsi`；probe 需 `devm_phy_get("dphy")`（→ `PHY_MTK_MIPI_DSI`，phy 驱动 `mt2701-mipi-tx` 在 `phy-mtk-mipi-dsi.c:183`）。dsi 用 `clk_set_rate(hs_clk, data_rate)` 把 `mipi_tx0_pll` 设到 140MHz。
- **OVL/RDMA/MUTEX**：`mtk_disp_ovl.c:785`、`mtk_disp_rdma.c:469` mt6572 版；`mtk-mutex.c:1157` `mt6572-disp-mutex`（`MTK_MMSYS=y` 已编）。
- **IOMMU/SMI（可选链）**：`mtk_iommu_v1.c:632` `mediatek,mt6572-m4u`；`mtk-smi.c:633` `mt6572-smi-larb`。dtsi `ovl/rdma` 节点带 `iommus=<&iommu ...>`。
- **电源域**：`mtk-pm-domains.c` `mediatek,mt6572-power-controller`（DIS/MFG/CONN），`CONFIG_MTK_SCPSYS_PM_DOMAINS=y`，DIS 域已注册。

### 缺口（当前 .config + dts）

- `.config` 未开：`CONFIG_DRM`、`CONFIG_DRM_MEDIATEK`、`CONFIG_DRM_PANEL_SITRONIX_ST7785M`、`CONFIG_PHY_MTK_MIPI_DSI`、`CONFIG_MTK_IOMMU_V1`。
- `.config` 已备（无需动）：`MTK_MMSYS=y`、`MTK_SCPSYS_PM_DOMAINS=y`、`COMMON_CLK=y`、`HAVE_ARM_SMCCC=y`、`I2C=y`、`VIDEO=y`、`HDMI=y`、`SYNC_FILE=y`、`DMA_SHARED_BUFFER=y`（DRM 的 select 项基本都齐）。
- dts：`mt6572.dtsi:620` `&dsi` `status="disabled"`；`mt6572-z1.dts` `&dsi{ panel@0 { status="disabled" } }`；`chosen/framebuffer0`（simplefb 240x320 r5g6b5）仍在 → 与 DRM 抢 fb0。

### 最小启用改动

① **.config**（`scripts/config --enable ...` 后 `make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- olddefconfig`，注意增量编需清理相关 .o）：
- `CONFIG_DRM=y`
- `CONFIG_DRM_MEDIATEK=y`（自动 select DRM_MIPI_DSI / DRM_PANEL / DRM_KMS_HELPER / DRM_DISPLAY_HELPER / DRM_GEM_DMA_HELPER / DRM_BRIDGE_CONNECTOR / DRM_CLIENT_SELECTION / VIDEOMODE_HELPERS）
- `CONFIG_DRM_PANEL_SITRONIX_ST7785M=y`
- `CONFIG_PHY_MTK_MIPI_DSI=y`（必需，否则 dsi `devm_phy_get` 失败）
- `CONFIG_MTK_IOMMU_V1=y`（**建议但非硬性**：dtsi 的 ovl/rdma 带 `iommus` 属性；本树 `of_dma_configure` 对无驱动的 iommu phandle 返回 -ENODEV 走 direct DMA，不会无限 defer，故不开也能跑；但 mt6572-m4u/smi-larb 全在树内，开上最稳。若实测 DRM 正常可再关以省镜像体积）

② **dts**：
- `mt6572.dtsi` `&dsi`：`status="disabled"` → `"okay"`
- `mt6572-z1.dts` `panel@0`：`status="disabled"` → `"okay"`
- **禁掉 simplefb**：`chosen/framebuffer0` 节点 `status="disabled"`（或删），避免 DRM 与 simplefb 双 fb0 打架；`framebuffer_reserved` 也需复核（DRM 接管后 LK 留在 0xbfd00000 的 surface 语义要确认）
- 面板 power/reset 可暂不补（probe 是 optional）；`reset-gpios`/`vcc-supply` 拿到板级证据后再加
- 复核项：`mipi_tx0` 的 `clocks = <&uart_clk>`（dtsi L374）——phy 驱动用该 refclk 做 PLL 分频，需确认 uart_clk 频率能让 PLL 出 140MHz（`mt2701_mipitx_data` 的 set_rate 按 ref_clk_rate 计算分频）

③ **上板验证**：
```sh
dmesg | grep -iE "mediatek-drm|mtk_dsi|st7785m|mipi_tx|disp-ovl|disp-rdma"
ls /dev/dri/                      # 期望 card0 / renderD128
ls /sys/class/drm/                # 期望 card0-DSI-1
cat /sys/class/drm/card0-DSI-1/modes   # 240x320@60（H-timing 仍是占位，若闪/花先调 st7785m_mode）
# 用 modetest 或 fbcon 确认画面，不再依赖 LK simplefb
```

**结论：缺失，但全链源码就绪。** 工作量 = config 5 项 + dts 3 处 + 上板调 DSI H-timing（唯一源码级 TODO）+ 确认 mipi_tx0 PLL。风险点：会动掉当前可用的 simplefb 显示，建议单独一趟专门调试，不要和低风险改动混刷。

---

## 2. 音频（MT6323 codec + MT6572 AFE）

### 状态：骨架在树，整棵 ALSA 关闭 ❌

源码（全部存在）：
- `sound/soc/mediatek/mt6572/mt6572-afe-pcm.c`：AFE 平台驱动，`compatible="mediatek,mt6572-audio"`，0x11140000/32-bit regmap，FE DAIs `PCM_DL1`/`PCM_VUL` + BE `ADDA`。**probe 对 IRQ 已是 warn+continue 兜底**（L508-520：`platform_get_irq` 负值 warn 继续；`request_irq` 失败也 warn 继续）。
- `mt6572-machine.c`：DPCM FE/BE 骨架，BE link 用 `COMP_CODEC(NULL,"mt6323-snd-codec-aif1")`，probe 要求 dts 有 `mediatek,platform` 与 `mediatek,audio-codec` 两个 phandle。
- `sound/soc/codecs/mt6323.c`：219 行 codec 骨架，DAPM 全 NOPM，无真实寄存器序列（`registers.h` 只有地址无 bit 定义——CLAUDE.md 已标 audio clock 标 DEAD）。
- Kconfig：`SND_SOC_MT6572`（depends ARCH_MEDIATEK, select SND_SOC_MEDIATEK）、`SND_SOC_MT6323`（depends MFD_MT6397，已 =y）。

当前 `.config`：`CONFIG_SOUND` 未开（整棵 ALSA 关）。上一轮因 audsys 占位 IRQ 在 AFE probe 挂死 kernel_init（155s backtrace）回退关闭。

### dts audsys 删占位 IRQ 后能否至少 probe AFE？—— **能**

- 当前 `mt6572-z1.dts:221`：`interrupts = <GIC_SPI 0 IRQ_TYPE_LEVEL_HIGH>`。`GIC_SPI` 宏就是 `0`（`include/dt-bindings/interrupt-controller/arm-gic.h:13`），所以等价 `<0 0 4>` = SPI#0，经 sysirq/GIC 域映射成一个非负 virq，走 `request_irq` 分支（成不成功当前代码都会继续）。这就是审计说的"非负 → 走进 request_irq"。
- **删掉 `interrupts` 属性后**：`platform_get_irq` → `of_irq_get` 返回负值（无属性 → -EINVAL）→ 走 warn+continue 分支 → AFE probe 继续：ioremap 0x11140000、建 regmap、`mt6572_afe_init_clock` 取 "audio"(CLK_TOP_AUDIO=77) / "audio_intbus_sel"(CLK_TOP_AUDIO_INTBUS_SEL=38)（**dts 已有这两个 clock 且 topckgen 都提供**，若缺失也只是 warn+NULL 继续）、注册 FE/BE DAI 组件。**AFE 平台驱动一定能 probe 成功。**
- **但 sound card 不会出现**：machine 的 BE link codec 节点（`&codec`）没有绑定的 codec 组件（diag 模式不建 codec cell）→ `snd_soc_bind_card` 返回 `-EPROBE_DEFER`（不是挂死，安全）。所以"AFE probe + 组件注册"可达成，"/proc/asound/cards 有卡"需再补 codec cell。

### MT6323 codec 需要什么？

1. **MFD diag 模式补 cell**：`drivers/mfd/mt6397-core.c:163-178` `mt6323_diagnostic_devs[]` 目前只有 `mt6323-regulator` + `mt6323-auxadc`。补一个：
   ```c
   {
       .name = "mt6323-sound",
       .of_compatible = "mediatek,mt6323-codec",
   },
   ```
   `mfd_add_device` 会按 `of_compatible` 把 `mt6323.dtsi` 的 `codec` 子节点（compatible 已是 `mediatek,mt6323-codec`）挂到该平台设备上，codec 驱动（`.name="mt6323-sound"` + of_match）即绑定。
2. **regmap**：codec probe 用 `dev_get_regmap(pdev->dev.parent)` 取不到（mt6397 MFD 设备自身不注册 regmap）→ 回退 `dev_get_drvdata(parent)=mt6397_chip->regmap`（pwrap regmap），**diag 模式可用**（与 auxadc 同通道）。
3. **config**：`CONFIG_SOUND=y`、`CONFIG_SND_SOC_MEDIATEK=y`、`CONFIG_SND_SOC_MT6572=y`、`CONFIG_SND_SOC_MT6323=y`。
4. **dts**：`audsys` 删 `interrupts`；`sound` 节点已 `status="okay"` 且两个 phandle 已备，无需改。
5. **pwrap 写门**：`CONFIG_MTK_PMIC_WRAP_Z1_WRITE_DENY` 当前**未开**（grep 0 命中），codec 寄存器写通道理论通（mt6323.c 注释里的 gate 描述已过时）。
6. **真实出声**（硬 blocker，本轮不做）：MT6323 audio 寄存器 bit 语义无 datasheet；DAPM 全 NOPM、machine card init/hw_params 空。即便框架全起，`speaker-test` 也不会有声。

### 上板验证（框架级）
```sh
dmesg | grep -iE "mt6572-afe|mt6572-audio|asoc|mt6323"   # AFE probe / codec bind / card 注册
cat /proc/asound/cards                                    # 期望出现 mt6572-mt6323 卡
aplay -l; arecord -l                                      # 列出 PCM_DL1 / PCM_VUL（不期望有声）
cat /sys/kernel/debug/asoc/components                      # 期望 afe/DAI/codec 组件在列
```

**结论：缺失。** 删占位 IRQ 后 AFE 必能 probe；补 MFD cell 后 sound card 能出现（框架级）；真实出声被 MT6323 datasheet 缺失卡死。本轮值得做的是"框架上板验证"，改动小且与 RTC 共用同一处 MFD 改动。

---

## 3. RTC（mt6323-rtc）

### 状态：驱动已编（`CONFIG_RTC_DRV_MT6397=y`），MFD diag 模式不建 cell → 无 rtc0 ❌

**驱动强制要 IRQ（复核确认，双处硬依赖）** `drivers/rtc/rtc-mt6397.c`：
- L265-267：`rtc->irq = platform_get_irq(pdev, 0); if (rtc->irq < 0) return rtc->irq;` —— 拿不到 IRQ 直接失败。
- L278-287：`devm_request_threaded_irq(rtc->irq, ...)` 失败直接 return —— 必须能装上。
- L258-261 还要 `platform_get_resource(pdev, IORESOURCE_MEM, 0)`（MFD cell 的 `DEFINE_RES_MEM(MT6323_RTC_BASE=0x8000, 0x40)` 提供，`addr_base=0x8000`）。

**MFD diag 模式双根因**：
1. `mt6323_diagnostic_devs[]`（mt6397-core.c:163-178）无 `mt6323-rtc` cell → 内核根本不建 rtc 平台设备。
2. 即使补 cell：cell 的资源含 `DEFINE_RES_IRQ(MT6323_IRQ_STATUS_RTC)`，其枚举值 = **20**（`include/linux/mfd/mt6323/core.h`：RTC 排在 SPKL_AB=0...PWRKEY=5...LDO=16,FCHRKEY=17,ACCDET=18,AUDIO=19,**RTC=20**）。`platform_get_irq` 逻辑（`drivers/base/platform.c` `platform_get_irq_affinity`）：有 of_node 时先 `of_irq_get`（rtc DT 节点无 interrupts → -EINVAL 不返回）→ 回落查 IORESOURCE_IRQ 资源 → diag 模式 `irq_base=0, domain=NULL`（mfd-core.c:222-232）→ 返回 **20**（一个 PMIC IRQ 索引，不是 Linux IRQ）→ `request_threaded_irq(20)` 失败 → probe 必败。**所以只补 cell 不够，IRQ resource 本身也是坑。**

**regmap**：`mtk_rtc_probe` 用 `dev_get_drvdata(pdev->dev.parent)=mt6397_chip->regmap`（pwrap regmap），diag 模式可用（与 auxadc/codec 同）。RTC 只有 alarm 需要 IRQ，读/写时间不需要 → 属可改的硬依赖。

### 最小启用改动（3 处源码，无需 dts）

1. `drivers/mfd/mt6397-core.c`：给 `mt6323_diagnostic_devs[]` 补 rtc cell，**用不带 IRQ 的专用资源数组**：
   ```c
   static const struct resource mt6323_rtc_diag_resources[] = {
       DEFINE_RES_MEM(MT6323_RTC_BASE, MT6323_RTC_SIZE),   /* 0x8000, 0x40，去掉 DEFINE_RES_IRQ */
   };
   /* 在 mt6323_diagnostic_devs[] 内： */
   {
       .name = "mt6323-rtc",
       .num_resources = ARRAY_SIZE(mt6323_rtc_diag_resources),
       .resources = mt6323_rtc_diag_resources,
       .of_compatible = "mediatek,mt6323-rtc",
   },
   ```
   （保留现有 `mt6323_rtc_resources[]` 供非 diag 模式用。）
2. `drivers/rtc/rtc-mt6397.c` 把 IRQ 改 optional：
   - L265-267：`rtc->irq = platform_get_irq(pdev, 0);` 后改成 `if (rtc->irq < 0) rtc->irq = -1;`（不再 `return rtc->irq`；无 IRQ 资源 + rtc 节点无 interrupts → -EINVAL → -1）。
   - L278-287：`devm_request_threaded_irq` 包进 `if (rtc->irq >= 0)`，无 IRQ 时跳过。
   - L240-245 `mtk_rtc_ops`：无 IRQ 时 `.read_alarm`/`.set_alarm` 置 NULL（rtc 核心对 NULL ops 的 alarm ioctl 返回 -EINVAL，hwclock 只走 read/set time 不受影响）。
   - L300-320 `mt6397_rtc_suspend/resume`：`enable_irq_wake/disable_irq_wake` 用 `if (rtc->irq >= 0)` 保护。
3. `.config`：`CONFIG_RTC_DRV_MT6397=y`、`CONFIG_RTC_CLASS=y`、`CONFIG_RTC_HCTOSYS=y`、`CONFIG_RTC_SYSTOHC=y` 已全开，无需动。

**dts 无需改**：`mt6323.dtsi` 的 `rtc { compatible = "mediatek,mt6323-rtc" }` 已存在，cell 的 `of_compatible` 会自动挂上。

### 上板验证
```sh
ls -l /dev/rtc*                     # 期望 rtc0（rtc-mt6397 注册）
cat /proc/driver/rtc                # 期望读到时间（PMIC 电池供电 RTC）
date -s "2026-08-14 12:00:00"; hwclock -w; hwclock -r   # 写读验证（走 pwrap + WRTGR 触发）
# 掉电重启后再 hwclock -r，应保持（MT6323 RTC 有独立 32K + VBAT 供电）
```

**结论：缺失，但改动最小、风险最低、收益明确（持久时钟）。** 双根因（diag 不建 cell + 驱动硬依赖 IRQ，且 DEFINE_RES_IRQ 的 20 号资源本身就是个坑）都有明确的 3 处源码解法。

---

## 下次上板一起做的建议

**优先级排序：RTC ＞ 音频框架 ＞ DRM。**

- **RTC（首推，与音频同趟做）**：改动 3 处源码全在同一文件对（`mt6397-core.c` 的 diag devs + `rtc-mt6397.c` 的 IRQ optional），与音频补 codec cell 是**同一数组、同一趟编译**；上板验证一条命令 `ls /dev/rtc0 && cat /proc/driver/rtc`，零风险、不碰现有可用功能。
- **音频框架（同趟）**：dts `audsys` 删占位 IRQ 一行 + MFD 补 `mt6323-sound` cell + config 3 项。验证目标是 `cat /proc/asound/cards` 出卡、`aplay -l` 见 PCM_DL1，**明确无声音**（datasheet blocker）。注意：本次刷机也要确认 BTIF crash 修复（git HEAD `e3198bbc3`）一起验证。
- **DRM/ST7785M（单独一趟）**：要动当前正在用的 simplefb 显示、要调 DSI H-timing、要确认 mipi_tx0 PLL 从 uart_clk 出 140MHz——属于显示 bring-up 迭代，且若失败影响看 log。建议单独排一趟专门刷，不要与 RTC/音频的低风险改动混在一起（混刷时出错难定位）。

所有改动前按项目规则先 `cp` 备份带时间戳到 `bridge_backups/`；每步修复验证通过后立即 git add/commit/push（仓库公开，禁密钥）。
